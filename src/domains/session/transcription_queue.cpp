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

std::vector<TranscriptionJobResult> TranscriptionQueue::drain_completed() {
    std::lock_guard lock(mutex_);
    return std::exchange(completed_, {});
}

std::size_t TranscriptionQueue::pending_count() const {
    std::lock_guard lock(mutex_);
    return pending_.size();
}

bool TranscriptionQueue::is_busy() const {
    std::lock_guard lock(mutex_);
    return busy_;
}

std::size_t TranscriptionQueue::outstanding_count() const {
    std::lock_guard lock(mutex_);
    return pending_.size() + (busy_ ? 1 : 0);
}

std::size_t TranscriptionQueue::shutdown() {
    std::size_t dropped = 0;
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        dropped = pending_.size();
        pending_.clear();
    }
    wake_.notify_all();
    // Outside the lock: the worker may be inside transcribe(), which does
    // not hold mutex_, and the abort only needs to reach the backend.
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
            job = std::move(pending_.front());
            pending_.pop_front();
            busy_ = true;
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
