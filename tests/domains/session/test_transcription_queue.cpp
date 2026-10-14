#include "domains/session/transcription_queue.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
using yoru::core::SessionId;
using yoru::session::TranscriptionJob;
using yoru::session::TranscriptionJobResult;
using yoru::session::TranscriptionQueue;
using yoru::speech::Model;
using yoru::speech::SpeechError;
using yoru::speech::Transcript;
using yoru::speech::TranscriptionRequest;
using yoru::speech::TranscriptionResult;

// A backend whose transcribe() blocks until the test releases it, so the
// tests can observe the queue while a job is "being transcribed".
class GatedSpeechBackend : public yoru::speech::SpeechBackend {
public:
    std::optional<SpeechError> load_model(const Model&) override { return std::nullopt; }
    void unload_model() override {}
    bool has_model_loaded() const override { return true; }

    TranscriptionResult transcribe(SessionId id, const std::vector<float>&,
                                   const TranscriptionRequest&) override {
        std::unique_lock lock(mutex_);
        started_.push_back(id);
        started_cv_.notify_all();
        gate_cv_.wait(lock, [this] { return open_ || aborted_; });
        if (aborted_) {
            return SpeechError{"aborted"};
        }
        if (open_once_) {
            open_ = false;
            open_once_ = false;
        }
        if (fail_) {
            return SpeechError{"boom"};
        }
        Transcript t;
        t.text = "session " + std::to_string(static_cast<std::uint64_t>(id));
        return t;
    }

    TranscriptionResult transcribe_partial(const std::vector<float>&,
                                           const TranscriptionRequest&) override {
        return SpeechError{"unused"};
    }

    void request_abort() override {
        std::lock_guard lock(mutex_);
        aborted_ = true;
        gate_cv_.notify_all();
    }

    void clear_abort() override {
        std::lock_guard lock(mutex_);
        aborted_ = false;
        ++clear_abort_calls_;
    }

    std::size_t clear_abort_calls() {
        std::lock_guard lock(mutex_);
        return clear_abort_calls_;
    }

    // Lets every current and future transcribe() finish.
    void open() {
        std::lock_guard lock(mutex_);
        open_ = true;
        gate_cv_.notify_all();
    }

    // Lets exactly the transcribe() currently waiting finish, then closes.
    void release_one() {
        std::lock_guard lock(mutex_);
        open_ = true;
        open_once_ = true;
        gate_cv_.notify_all();
    }

    void fail_jobs() {
        std::lock_guard lock(mutex_);
        fail_ = true;
    }

    // Blocks until `count` transcribe() calls have started.
    bool wait_started(std::size_t count) {
        std::unique_lock lock(mutex_);
        return started_cv_.wait_for(lock, 5s, [&] { return started_.size() >= count; });
    }

    std::vector<SessionId> started() {
        std::lock_guard lock(mutex_);
        return started_;
    }

private:
    std::mutex mutex_;
    std::condition_variable gate_cv_;
    std::condition_variable started_cv_;
    std::vector<SessionId> started_;
    bool open_ = false;
    bool open_once_ = false;
    bool aborted_ = false;
    bool fail_ = false;
    std::size_t clear_abort_calls_ = 0;
};

TranscriptionJob make_job(std::uint64_t id) {
    TranscriptionJob job;
    job.session_id = static_cast<SessionId>(id);
    job.samples = {0.0F, 0.1F};
    return job;
}

// Polls drain_completed() until `count` results arrived or time runs out.
std::vector<TranscriptionJobResult> collect(TranscriptionQueue& queue, std::size_t count) {
    std::vector<TranscriptionJobResult> all;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (all.size() < count && std::chrono::steady_clock::now() < deadline) {
        for (auto& r : queue.drain_completed()) {
            all.push_back(std::move(r));
        }
        std::this_thread::sleep_for(1ms);
    }
    return all;
}

} // namespace

TEST_CASE("a job enqueued on an idle queue is transcribed and its result drained") {
    GatedSpeechBackend backend;
    backend.open();
    TranscriptionQueue queue(backend);

    const auto position = queue.try_enqueue(make_job(1));
    REQUIRE(position.has_value());
    CHECK(*position == 0);

    const auto results = collect(queue, 1);
    REQUIRE(results.size() == 1);
    CHECK(results[0].session_id == static_cast<SessionId>(1));
    const auto* transcript = std::get_if<Transcript>(&results[0].result);
    REQUIRE(transcript != nullptr);
    CHECK(transcript->text == "session 1");
}

TEST_CASE("a job can be enqueued while another is being transcribed") {
    GatedSpeechBackend backend;
    TranscriptionQueue queue(backend);

    REQUIRE(queue.try_enqueue(make_job(1)).has_value());
    REQUIRE(backend.wait_started(1));
    CHECK(queue.snapshot().busy);

    const auto position = queue.try_enqueue(make_job(2));
    REQUIRE(position.has_value());
    CHECK(*position == 1);
    CHECK(queue.snapshot().pending == 1);
    CHECK(queue.outstanding_count() == 2);

    backend.open();
    CHECK(collect(queue, 2).size() == 2);
}

TEST_CASE("jobs are transcribed in FIFO order") {
    GatedSpeechBackend backend;
    TranscriptionQueue queue(backend);

    for (std::uint64_t id = 1; id <= 4; ++id) {
        REQUIRE(queue.try_enqueue(make_job(id)).has_value());
    }
    backend.open();

    const auto results = collect(queue, 4);
    REQUIRE(results.size() == 4);
    for (std::size_t i = 0; i < results.size(); ++i) {
        CHECK(results[i].session_id == static_cast<SessionId>(i + 1));
    }
    const auto started = backend.started();
    REQUIRE(started.size() == 4);
    for (std::size_t i = 0; i < started.size(); ++i) {
        CHECK(started[i] == static_cast<SessionId>(i + 1));
    }
}

TEST_CASE("a full queue rejects new jobs without losing the accepted ones") {
    GatedSpeechBackend backend;
    TranscriptionQueue queue(backend, 2);

    REQUIRE(queue.try_enqueue(make_job(1)).has_value());
    REQUIRE(backend.wait_started(1)); // job 1 left the queue, so 2 slots remain
    REQUIRE(queue.try_enqueue(make_job(2)).has_value());
    REQUIRE(queue.try_enqueue(make_job(3)).has_value());

    CHECK_FALSE(queue.has_capacity());
    CHECK_FALSE(queue.try_enqueue(make_job(4)).has_value());

    backend.open();
    const auto results = collect(queue, 3);
    REQUIRE(results.size() == 3);
    CHECK(results[2].session_id == static_cast<SessionId>(3));
    CHECK(queue.has_capacity());
}

TEST_CASE("cancel_pending removes a waiting job but not the one in progress") {
    GatedSpeechBackend backend;
    TranscriptionQueue queue(backend);

    REQUIRE(queue.try_enqueue(make_job(1)).has_value());
    REQUIRE(backend.wait_started(1));
    REQUIRE(queue.try_enqueue(make_job(2)).has_value());
    REQUIRE(queue.try_enqueue(make_job(3)).has_value());

    CHECK(queue.cancel_pending(static_cast<SessionId>(2)));
    CHECK_FALSE(queue.cancel_pending(static_cast<SessionId>(2)));
    CHECK_FALSE(queue.cancel_pending(static_cast<SessionId>(1)));

    backend.open();
    const auto results = collect(queue, 2);
    REQUIRE(results.size() == 2);
    CHECK(results[0].session_id == static_cast<SessionId>(1));
    CHECK(results[1].session_id == static_cast<SessionId>(3));
}

TEST_CASE("a backend failure is reported for that job and the next one still runs") {
    GatedSpeechBackend backend;
    backend.fail_jobs();
    backend.open();
    TranscriptionQueue queue(backend);

    REQUIRE(queue.try_enqueue(make_job(1)).has_value());
    REQUIRE(queue.try_enqueue(make_job(2)).has_value());

    const auto results = collect(queue, 2);
    REQUIRE(results.size() == 2);
    for (const auto& r : results) {
        const auto* error = std::get_if<SpeechError>(&r.result);
        REQUIRE(error != nullptr);
        CHECK(error->message == "boom");
    }
}

TEST_CASE("shutdown drops waiting jobs, aborts the one in progress and joins the worker") {
    GatedSpeechBackend backend;
    TranscriptionQueue queue(backend);

    REQUIRE(queue.try_enqueue(make_job(1)).has_value());
    REQUIRE(backend.wait_started(1));
    REQUIRE(queue.try_enqueue(make_job(2)).has_value());
    REQUIRE(queue.try_enqueue(make_job(3)).has_value());

    const auto dropped = queue.shutdown();
    REQUIRE(dropped.size() == 2);
    CHECK(dropped[0] == static_cast<SessionId>(2));
    CHECK(dropped[1] == static_cast<SessionId>(3));

    CHECK_FALSE(queue.snapshot().busy);
    CHECK(queue.snapshot().pending == 0);
    // Only the aborted job produced a result; the dropped ones never ran.
    const auto results = queue.drain_completed();
    REQUIRE(results.size() == 1);
    CHECK(results[0].session_id == static_cast<SessionId>(1));
    CHECK(std::holds_alternative<SpeechError>(results[0].result));
    CHECK(backend.started().size() == 1);
}

TEST_CASE("after shutdown the queue accepts no jobs and shutdown is idempotent") {
    GatedSpeechBackend backend;
    TranscriptionQueue queue(backend);

    queue.shutdown();

    CHECK_FALSE(queue.has_capacity());
    CHECK_FALSE(queue.try_enqueue(make_job(1)).has_value());
    CHECK(queue.shutdown().empty());
}

TEST_CASE("destroying a queue with work in flight does not hang") {
    GatedSpeechBackend backend;
    {
        TranscriptionQueue queue(backend);
        REQUIRE(queue.try_enqueue(make_job(1)).has_value());
        REQUIRE(backend.wait_started(1));
        REQUIRE(queue.try_enqueue(make_job(2)).has_value());
    }
    CHECK(backend.started().size() == 1);
}

TEST_CASE("release_one lets jobs finish one at a time") {
    GatedSpeechBackend backend;
    TranscriptionQueue queue(backend);

    REQUIRE(queue.try_enqueue(make_job(1)).has_value());
    REQUIRE(queue.try_enqueue(make_job(2)).has_value());
    REQUIRE(backend.wait_started(1));

    backend.release_one();
    REQUIRE(collect(queue, 1).size() == 1);
    REQUIRE(backend.wait_started(2));
    CHECK(queue.snapshot().busy);

    backend.open();
    CHECK(collect(queue, 1).size() == 1);
}

TEST_CASE("the queue clears a stale abort before each job, never transcribe() itself") {
    GatedSpeechBackend backend;
    backend.request_abort(); // left over from some earlier shutdown request
    backend.open();
    TranscriptionQueue queue(backend);

    REQUIRE(queue.try_enqueue(make_job(1)).has_value());
    REQUIRE(queue.try_enqueue(make_job(2)).has_value());

    const auto results = collect(queue, 2);
    REQUIRE(results.size() == 2);
    // Neither job was poisoned by the stale abort, and the queue (not the
    // backend) did the clearing, once per job.
    for (const auto& r : results) {
        CHECK(std::holds_alternative<Transcript>(r.result));
    }
    CHECK(backend.clear_abort_calls() == 2);
}

TEST_CASE("an abort requested right after a job is enqueued is never lost") {
    // The window being guarded: the worker has popped the job but has not
    // yet entered transcribe() when shutdown() asks for the abort. The gate
    // only opens on abort, so a lost request would hang this test.
    for (int i = 0; i < 200; ++i) {
        GatedSpeechBackend backend;
        {
            TranscriptionQueue queue(backend);
            REQUIRE(queue.try_enqueue(make_job(1)).has_value());
            queue.shutdown();
        }
    }
    CHECK(true); // reaching here means every shutdown() returned
}

TEST_CASE("drain_started reports jobs in the order the worker began them") {
    GatedSpeechBackend backend;
    TranscriptionQueue queue(backend);

    REQUIRE(queue.try_enqueue(make_job(1)).has_value());
    REQUIRE(queue.try_enqueue(make_job(2)).has_value());
    REQUIRE(backend.wait_started(1));

    auto started = queue.drain_started();
    REQUIRE(started.size() == 1);
    CHECK(started[0] == static_cast<SessionId>(1));
    CHECK(queue.drain_started().empty());

    backend.open();
    REQUIRE(collect(queue, 2).size() == 2);
    started = queue.drain_started();
    REQUIRE(started.size() == 1);
    CHECK(started[0] == static_cast<SessionId>(2));
}

TEST_CASE("snapshot reads pending and busy together") {
    GatedSpeechBackend backend;
    TranscriptionQueue queue(backend);

    CHECK(queue.snapshot().pending == 0);
    CHECK_FALSE(queue.snapshot().busy);

    REQUIRE(queue.try_enqueue(make_job(1)).has_value());
    REQUIRE(backend.wait_started(1));
    REQUIRE(queue.try_enqueue(make_job(2)).has_value());

    const auto snapshot = queue.snapshot();
    CHECK(snapshot.pending == 1);
    CHECK(snapshot.busy);
    backend.open();
}
