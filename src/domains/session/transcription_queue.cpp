#include "domains/session/transcription_queue.hpp"

#include <utility>

namespace yoru::session {

TranscriptionQueue::TranscriptionQueue(speech::SpeechBackend& backend, std::size_t capacity)
    : backend_(backend), capacity_(capacity), worker_([this] { run(); }) {}

TranscriptionQueue::~TranscriptionQueue() {
    shutdown();
}

std::optional<std::size_t> TranscriptionQueue::try_enqueue(TranscriptionJob job) {
    std::size_t position = 0;
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || pending_.size() >= capacity_) {
            return std::nullopt;
        }
        position = pending_.size() + (busy_ ? 1 : 0);
        pending_.push_back(std::move(job));
    }
    wake_.notify_one();
    return position;
}

bool TranscriptionQueue::has_capacity() const {
    std::lock_guard lock(mutex_);
    return !stopping_ && pending_.size() < capacity_;
}

bool TranscriptionQueue::cancel_pending(core::SessionId session_id) {
    std::lock_guard lock(mutex_);
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->session_id == session_id) {
            pending_.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<core::SessionId> TranscriptionQueue::drain_started() {
    std::lock_guard lock(mutex_);
    return std::exchange(started_, {});
}

std::vector<TranscriptionJobResult> TranscriptionQueue::drain_completed() {
    std::lock_guard lock(mutex_);
    return std::exchange(completed_, {});
}

QueueSnapshot TranscriptionQueue::snapshot() const {
    std::lock_guard lock(mutex_);
    return QueueSnapshot{.pending = pending_.size(), .busy = busy_};
}

std::size_t TranscriptionQueue::outstanding_count() const {
    std::lock_guard lock(mutex_);
    return pending_.size() + (busy_ ? 1 : 0);
}

std::vector<core::SessionId> TranscriptionQueue::shutdown() {
    std::vector<core::SessionId> dropped;
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        for (const auto& job : pending_) {
            dropped.push_back(job.session_id);
        }
        pending_.clear();
    }
    wake_.notify_all();
    // After stopping_ is set (under the lock) the worker takes no new job,
    // so it can no longer clear the abort flag; this request therefore
    // cannot be lost. Outside the lock: the worker may be inside
    // transcribe(), which does not hold mutex_.
    backend_.request_abort();
    if (worker_.joinable()) {
        worker_.join();
    }
    return dropped;
}

void TranscriptionQueue::run() {
    for (;;) {
        TranscriptionJob job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
            if (stopping_) {
                return;
            }
            // Cleared here, under the lock and before the job starts, not
            // inside transcribe(): shutdown() sets stopping_ under this same
            // lock before requesting the abort, so a request can never be
            // erased by a job that was already on its way in.
            backend_.clear_abort();
            job = std::move(pending_.front());
            pending_.pop_front();
            busy_ = true;
            started_.push_back(job.session_id);
        }

        auto result = backend_.transcribe(job.session_id, job.samples, job.request);

        {
            std::lock_guard lock(mutex_);
            busy_ = false;
            completed_.push_back({job.session_id, std::move(result)});
        }
    }
}

} // namespace yoru::session
