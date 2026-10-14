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

// Queue occupancy read under a single lock, so the fields are consistent
// with each other (separate accessor calls can straddle a job finishing).
struct QueueSnapshot {
    std::size_t pending = 0; // waiting, not counting the one in progress
    bool busy = false;       // a job is being transcribed
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
// which keeps the EventBus, IPC and clipboard single-threaded.
//
// There is exactly one producer: the owner thread. Callers rely on that to
// do check-then-act without a race, e.g. start_session() checks
// has_capacity() and stop_session() enqueues later (capacity can only grow
// in between, since only the worker removes jobs), and the Live Transcriber
// checks "no work outstanding" before calling the backend itself (nothing
// can be enqueued while the owner thread is inside that call). A second
// producer thread would silently break both.
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

    // Takes the ids of jobs the worker began processing since the last
    // call, in processing order. Lets the owner report "started" from its
    // own thread; a job can appear here and in drain_completed() in the
    // same round.
    std::vector<core::SessionId> drain_started();

    // Takes all results completed since the last call, in processing order.
    std::vector<TranscriptionJobResult> drain_completed();

    // Jobs waiting and whether one is being processed, read atomically.
    QueueSnapshot snapshot() const;

    // Jobs waiting plus the one being processed.
    std::size_t outstanding_count() const;

    // Stops accepting jobs, drops the ones still waiting, asks the backend
    // to abort the transcription in progress, and joins the worker.
    // Returns the ids of the waiting jobs that were dropped (they never
    // run and produce no result). Idempotent.
    std::vector<core::SessionId> shutdown();

private:
    void run();

    speech::SpeechBackend& backend_;
    const std::size_t capacity_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<TranscriptionJob> pending_;
    std::vector<core::SessionId> started_;
    std::vector<TranscriptionJobResult> completed_;
    bool busy_ = false;
    bool stopping_ = false;

    std::thread worker_;
};

} // namespace yoru::session
