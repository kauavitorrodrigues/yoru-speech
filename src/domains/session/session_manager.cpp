#include "domains/session/session_manager.hpp"

#include "core/error_event.hpp"
#include "domains/config/events.hpp"
#include "domains/session/events.hpp"
#include "domains/speech/events.hpp"

#include <chrono>
#include <variant>

namespace yoru::session {

SessionManager::SessionManager(core::EventBus& event_bus,
                               audio::RecordingManager& recording_manager,
                               speech::SpeechBackend& speech_backend,
                               const config::Configuration& initial_configuration,
                               std::size_t queue_capacity)
    : event_bus_(event_bus), recording_manager_(recording_manager),
      default_language_(initial_configuration.default_language),
      transcription_prompt_(initial_configuration.transcription_prompt),
      queue_(speech_backend, queue_capacity) {
    event_bus_.subscribe<config::ConfigurationChanged>(
        [this](const config::ConfigurationChanged& event) {
            default_language_ = event.configuration.default_language;
            transcription_prompt_ = event.configuration.transcription_prompt;
        });
}

SessionManager::~SessionManager() = default;

void SessionManager::fail(const std::string& component, const std::string& message) {
    active_session_->try_transition(SessionState::Failed, std::chrono::system_clock::now());
    const core::SessionId session_id = active_session_->id();

    state_ = ServiceState::Error;
    event_bus_.publish(core::ErrorOccurred{
        .session_id = session_id,
        .component = component,
        .message = message,
    });
}

std::optional<SessionError> SessionManager::start_session() {
    if (state_ != ServiceState::Idle) {
        return SessionError{"a session is already active"};
    }
    if (!queue_.has_capacity()) {
        const std::string message = "transcription queue is full";
        event_bus_.publish(core::ErrorOccurred{
            .session_id = std::nullopt,
            .component = "queue",
            .message = message,
        });
        return SessionError{message};
    }

    const auto session_id = core::SessionId{next_session_id_++};

    if (const auto error = recording_manager_.start(session_id)) {
        // Not fail(): nothing was actually started (no active_session_, no
        // RecordingStarted published), and Idle -> Error has no place in
        // ServiceState's transition rules. Report the failure without
        // moving the service out of Idle; session_id is simply discarded.
        event_bus_.publish(core::ErrorOccurred{
            .session_id = std::nullopt,
            .component = "audio",
            .message = error->message,
        });
        return SessionError{error->message};
    }

    active_session_.emplace(session_id, std::chrono::system_clock::now());
    active_session_->try_transition(SessionState::Recording, std::chrono::system_clock::now());
    state_ = ServiceState::Recording;
    return std::nullopt;
}

StopSessionResult SessionManager::stop_session() {
    if (state_ != ServiceState::Recording) {
        return SessionError{"no session is recording"};
    }

    const core::SessionId session_id = active_session_->id();

    auto recording_result = recording_manager_.stop();
    if (const auto* error = std::get_if<audio::RecordingError>(&recording_result)) {
        fail("audio", error->message);
        return SessionError{error->message};
    }

    active_session_->try_transition(SessionState::Processing, std::chrono::system_clock::now());

    auto& recording = std::get<audio::Recording>(recording_result);
    const auto position = queue_.try_enqueue(TranscriptionJob{
        .session_id = session_id,
        .samples = std::move(recording.samples),
        .request =
            speech::TranscriptionRequest{
                .language = default_language_,
                .initial_prompt = transcription_prompt_,
            },
    });
    if (!position.has_value()) {
        // start_session() already checked has_capacity() and this thread is
        // the queue's only producer, so a refusal here means the queue was
        // shut down in between: effectively a shutdown-only path. The
        // recording is lost, which is reported as a failure.
        fail("queue", "transcription queue is full or shutting down");
        return SessionError{"transcription queue is full or shutting down"};
    }

    // The Session object covers the recording only; from here the job is
    // tracked by the queue, and the next session can start immediately.
    active_session_.reset();
    state_ = ServiceState::Idle;
    event_bus_.publish(TranscriptionQueued{session_id, *position});
    return QueuedJob{.session_id = session_id, .position = *position};
}

std::size_t SessionManager::poll_completed() {
    // Completed before started: a job's "started" notice is recorded
    // before its result, so reading in this order guarantees every
    // completed job's start is also in `started`, and TranscriptionStarted
    // is always published ahead of its TranscriptionCompleted.
    auto results = queue_.drain_completed();
    for (const core::SessionId id : queue_.drain_started()) {
        event_bus_.publish(speech::TranscriptionStarted{id});
    }
    for (auto& job : results) {
        if (const auto* error = std::get_if<speech::SpeechError>(&job.result)) {
            last_failed_ = job.session_id;
            event_bus_.publish(core::ErrorOccurred{
                .session_id = job.session_id,
                .component = "speech",
                .message = error->message,
            });
            continue;
        }
        last_completed_ = job.session_id;
        event_bus_.publish(speech::TranscriptionCompleted{
            job.session_id, std::get<speech::Transcript>(std::move(job.result))});
    }
    return results.size();
}

std::optional<SessionError> SessionManager::cancel_session() {
    if (state_ == ServiceState::Idle) {
        return SessionError{"no session to cancel"};
    }

    if (state_ == ServiceState::Recording) {
        recording_manager_.stop();
    }

    std::optional<core::SessionId> session_id;
    if (active_session_.has_value()) {
        session_id = active_session_->id();
        active_session_->try_transition(SessionState::Failed, std::chrono::system_clock::now());
        active_session_.reset();
    }

    state_ = ServiceState::Idle;

    if (session_id.has_value()) {
        event_bus_.publish(SessionCancelled{session_id.value()});
    }
    return std::nullopt;
}

bool SessionManager::cancel_job(core::SessionId session_id) {
    if (!queue_.cancel_pending(session_id)) {
        return false;
    }
    event_bus_.publish(SessionCancelled{session_id});
    return true;
}

void SessionManager::set_activity_callback(std::function<void()> callback) {
    queue_.set_activity_callback(std::move(callback));
}

std::vector<core::SessionId> SessionManager::shutdown() {
    auto dropped = queue_.shutdown();
    for (const core::SessionId id : dropped) {
        event_bus_.publish(core::ErrorOccurred{
            .session_id = id,
            .component = "queue",
            .message = "dropped at shutdown before it could be transcribed",
        });
    }
    return dropped;
}

namespace {

ServiceState derive_state(ServiceState capture_state, const QueueSnapshot& queue) {
    if (capture_state != ServiceState::Idle) {
        return capture_state;
    }
    return queue.busy || queue.pending > 0 ? ServiceState::Processing : ServiceState::Idle;
}

} // namespace

ServiceState SessionManager::state() const {
    return derive_state(state_, queue_.snapshot());
}

SessionStatus SessionManager::status() const {
    // One snapshot feeds every field, so the reply cannot contradict itself
    // (e.g. "processing" with nothing queued or running).
    const QueueSnapshot queue = queue_.snapshot();
    return SessionStatus{
        .state = derive_state(state_, queue),
        .queued = queue.pending,
        .processing = queue.busy,
        .last_completed = last_completed_,
        .last_failed = last_failed_,
    };
}

bool SessionManager::transcription_busy() const {
    return queue_.outstanding_count() > 0;
}

std::optional<core::SessionId> SessionManager::active_session_id() const {
    if (!active_session_.has_value()) {
        return std::nullopt;
    }
    return active_session_->id();
}

} // namespace yoru::session
