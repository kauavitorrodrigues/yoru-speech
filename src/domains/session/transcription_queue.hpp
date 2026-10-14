#pragma once

#include "core/session_id.hpp"
#include "domains/speech/speech_backend.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace yoru::session {

// A finished recording waiting to be transcribed.
struct TranscriptionJob {
    core::SessionId session_id{};
    std::vector<float> samples;
    speech::TranscriptionRequest request;
    std::chrono::steady_clock::time_point enqueued_at = std::chrono::steady_clock::now();
};

// The outcome of one TranscriptionJob, in the order jobs were processed.
struct TranscriptionJobResult {
    core::SessionId session_id{};
    speech::TranscriptionResult result;
};

// Bounded FIFO of TranscriptionJobs processed one at a time by a dedicated
// worker thread, so a slow transcription never blocks recording or IPC.
//
// Threading contract: the worker thread is the only caller of
// SpeechBackend::transcribe() (the Whisper context is not thread-safe) and
// it never publishes events. It hands results back through a completion
// list that the owner drains from its own thread with drain_completed(),
// which keeps the EventBus, IPC and clipboard single-threaded. Every public
// method may be called from the owner thread; none is meant for
// concurrent use by several owners.
class TranscriptionQueue {
public:
    static constexpr std::size_t default_capacity = 8;

    // Starts the worker thread. `backend` must outlive this object.
    explicit TranscriptionQueue(speech::SpeechBackend& backend,
                                std::size_t capacity = default_capacity);
    ~TranscriptionQueue();

    TranscriptionQueue(const TranscriptionQueue&) = delete;
    TranscriptionQueue& operator=(const TranscriptionQueue&) = delete;

    // Appends `job` unless the queue is full or shut down. On success
    // returns the job's position: 0 when nothing else is queued or being
    // processed, 1 when one job is ahead of it, and so on. Returns
    // nullopt when the job was not accepted.
    std::optional<std::size_t> try_enqueue(TranscriptionJob job);

    // True if a job can currently be accepted.
    bool has_capacity() const;

    // Removes a job that has not started processing yet. Returns false if
    // `session_id` is not waiting in the queue (unknown, already running,
    // or already finished): an in-progress transcription is not
    // interruptible, only shutdown() aborts it.
    bool cancel_pending(core::SessionId session_id);

    // Takes all results completed since the last call, in processing order.
    std::vector<TranscriptionJobResult> drain_completed();

    // Jobs waiting, not counting the one being processed.
    std::size_t pending_count() const;

    // True while the worker is transcribing a job.
    bool is_busy() const;

    // Jobs waiting plus the one being processed.
    std::size_t outstanding_count() const;

    // Stops accepting jobs, drops the ones still waiting, asks the backend
    // to abort the transcription in progress, and joins the worker.
    // Returns how many waiting jobs were dropped. Idempotent.
    std::size_t shutdown();

private:
    void run();

    speech::SpeechBackend& backend_;
    const std::size_t capacity_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<TranscriptionJob> pending_;
    std::vector<TranscriptionJobResult> completed_;
    bool busy_ = false;
    bool stopping_ = false;

    std::thread worker_;
};

} // namespace yoru::session
