#include "domains/session/session_manager.hpp"

#include "core/error_event.hpp"
#include "domains/config/events.hpp"
#include "domains/session/events.hpp"

#include <chrono>
#include <variant>

namespace yoru::session {

SessionManager::SessionManager(core::EventBus& event_bus,
                               audio::RecordingManager& recording_manager,
                               speech::SpeechBackend& speech_backend,
                               const config::Configuration& initial_configuration)
    : event_bus_(event_bus), recording_manager_(recording_manager),
      speech_backend_(speech_backend),
      default_language_(initial_configuration.default_language),
      transcription_prompt_(initial_configuration.transcription_prompt) {
    event_bus_.subscribe<config::ConfigurationChanged>(
        [this](const config::ConfigurationChanged& event) {
            default_language_ = event.configuration.default_language;
            transcription_prompt_ = event.configuration.transcription_prompt;
        });
}

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
    state_ = ServiceState::Processing;

    const auto& recording = std::get<audio::Recording>(recording_result);
    auto transcription_result = speech_backend_.transcribe(
        session_id, recording.samples,
        speech::TranscriptionRequest{
            .language = default_language_,
            .initial_prompt = transcription_prompt_,
        });
    if (const auto* error = std::get_if<speech::SpeechError>(&transcription_result)) {
        fail("speech", error->message);
        return SessionError{error->message};
    }

    active_session_->try_transition(SessionState::Completed, std::chrono::system_clock::now());
    active_session_.reset();
    state_ = ServiceState::Idle;
    return std::get<speech::Transcript>(std::move(transcription_result));
}

std::optional<SessionError> SessionManager::cancel_session() {
    if (state_ == ServiceState::Idle) {
        return SessionError{"no session to cancel"};
    }

    // Processing only ever exists transiently within stop_session()'s own
    // call, since transcribe() is synchronous: an external caller can never
    // actually observe and cancel it mid-flight in this architecture. This
    // branch exists for the state machine's sake, not because it's
    // reachable today; revisit if transcription ever becomes asynchronous.
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

ServiceState SessionManager::state() const {
    return state_;
}

std::optional<core::SessionId> SessionManager::active_session_id() const {
    if (!active_session_.has_value()) {
        return std::nullopt;
    }
    return active_session_->id();
}

} // namespace yoru::session
