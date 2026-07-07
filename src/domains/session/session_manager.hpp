#pragma once

#include "core/event_bus.hpp"
#include "domains/audio/recording_manager.hpp"
#include "domains/config/configuration.hpp"
#include "domains/session/service_state.hpp"
#include "domains/session/session.hpp"
#include "domains/speech/speech_backend.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace yoru::session {

// An error starting, stopping, or cancelling a session, with a message
// suitable for logging. Never thrown: reported through the corresponding
// method's return value.
struct SessionError {
    std::string message;
};

// The outcome of stop_session(): exactly one of the produced Transcript or
// the reason it could not be produced.
using StopSessionResult = std::variant<speech::Transcript, SessionError>;

// Coordinates the full dictation cycle: start -> record -> stop ->
// transcribe -> Transcript. The only component authorized to start, stop,
// or cancel a session, and the only one that mutates ServiceState; no
// other component may change it directly.
//
// Owns no audio or speech resources itself. `recording_manager` and
// `speech_backend` are injected and must outlive this manager, same as
// `event_bus`. This keeps the Session Manager coordinating, not
// implementing, capture or recognition.
//
// Every method must be called from a single controlling thread: neither
// the service state nor the active Session are guarded by a lock,
// matching the single-thread contracts already documented on
// RecordingManager and WhisperBackend, which this manager calls into
// synchronously.
class SessionManager {
public:
    // `initial_configuration` seeds the language/prompt passed to the
    // Speech Backend on stop_session(); later changes are tracked via
    // ConfigurationChanged, not by re-reading configuration (same pattern
    // as clipboard::AutoClipboard).
    SessionManager(core::EventBus& event_bus, audio::RecordingManager& recording_manager,
                   speech::SpeechBackend& speech_backend,
                   const config::Configuration& initial_configuration);

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    SessionManager(SessionManager&&) = delete;
    SessionManager& operator=(SessionManager&&) = delete;

    // Starts a new session: requires the service to be Idle. Generates a
    // new SessionId and delegates to the Recording Manager to open the
    // microphone; on success transitions to Recording. Returns an error,
    // without side effects, if a session is already active (including a
    // session left in Error; see cancel_session()) or the device can't be
    // opened. The latter publishes ErrorOccurred but, per ServiceState's
    // transition rules (Idle -> Error is not a legal transition; nothing
    // was actually started), leaves the service Idle rather than moving it
    // to Error.
    std::optional<SessionError> start_session();

    // Stops the active recording and transcribes it. Requires the service
    // to be Recording. On success, transitions back to Idle and returns
    // the Transcript directly (also published via TranscriptionCompleted,
    // by whichever SpeechBackend is in use). Returns an error if no
    // session is recording, or if the recording or the transcription
    // fails, in which case the service transitions to Error instead of
    // Idle.
    StopSessionResult stop_session();

    // Cancels the active session from Recording, Processing, or Error,
    // discarding whatever recording or transcription was in progress (or
    // acknowledging a prior failure, for Error), and returns to Idle.
    // Publishes SessionCancelled. Returns an error if the service is
    // already Idle: there is nothing to cancel.
    std::optional<SessionError> cancel_session();

    ServiceState state() const;

    // The active session's id, or nullopt if the service is Idle. Exists
    // for components that need to tag their own output with the right
    // session (e.g. the Live Transcriber) without becoming a second
    // authority over the session lifecycle themselves.
    std::optional<core::SessionId> active_session_id() const;

private:
    // Transitions a Recording/Processing session to Error and publishes
    // ErrorOccurred for it. Only valid to call while active_session_ has a
    // value (i.e. from stop_session(), never from start_session(), which
    // has no legal path into Error; see start_session()'s doc comment).
    void fail(const std::string& component, const std::string& message);

    core::EventBus& event_bus_;
    audio::RecordingManager& recording_manager_;
    speech::SpeechBackend& speech_backend_;
    ServiceState state_ = ServiceState::Idle;
    std::optional<Session> active_session_;
    std::uint64_t next_session_id_ = 1;
    std::string default_language_;
    std::string transcription_prompt_;
};

} // namespace yoru::session
