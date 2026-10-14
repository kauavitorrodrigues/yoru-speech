#pragma once

#include "core/event_bus.hpp"
#include "domains/audio/recording_manager.hpp"
#include "domains/config/configuration.hpp"
#include "domains/session/service_state.hpp"
#include "domains/session/session.hpp"
#include "domains/session/session_status.hpp"
#include "domains/session/transcription_queue.hpp"
#include "domains/speech/speech_backend.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace yoru::session {

// An error starting, stopping, or cancelling a session, with a message
// suitable for logging. Never thrown: reported through the corresponding
// method's return value.
struct SessionError {
    std::string message;
};

// The outcome of stop_session(): exactly one of the queued job or the
// reason the recording could not be accepted.
using StopSessionResult = std::variant<QueuedJob, SessionError>;

// Coordinates the full dictation cycle: start -> record -> stop ->
// (queued) -> transcribe -> Transcript. The only component authorized to
// start, stop, or cancel a session, and the only one that mutates
// ServiceState; no other component may change it directly.
//
// Capture and transcription are decoupled: stop_session() hands the
// recording to a TranscriptionQueue and returns at once, so the next
// session can start while earlier ones are still being transcribed. The
// queue's worker thread never publishes events; poll_completed() drains
// its results from the controlling thread and publishes them.
//
// Owns no audio or speech resources itself. `recording_manager` and
// `speech_backend` are injected and must outlive this manager, same as
// `event_bus`. This keeps the Session Manager coordinating, not
// implementing, capture or recognition.
//
// Every method must be called from a single controlling thread (the
// worker inside the queue is an implementation detail): neither the
// service state nor the active Session are guarded by a lock, matching
// the single-thread contracts already documented on RecordingManager and
// EventBus.
class SessionManager {
public:
    // `initial_configuration` seeds the language/prompt passed to the
    // Speech Backend on stop_session(); later changes are tracked via
    // ConfigurationChanged, not by re-reading configuration (same pattern
    // as clipboard::AutoClipboard).
    SessionManager(core::EventBus& event_bus, audio::RecordingManager& recording_manager,
                   speech::SpeechBackend& speech_backend,
                   const config::Configuration& initial_configuration,
                   std::size_t queue_capacity = TranscriptionQueue::default_capacity);
    ~SessionManager();

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    SessionManager(SessionManager&&) = delete;
    SessionManager& operator=(SessionManager&&) = delete;

    // Starts a new session: requires no session to be recording (earlier
    // sessions may still be transcribing). Generates a new SessionId and
    // delegates to the Recording Manager to open the microphone; on
    // success transitions to Recording. Returns an error, without side
    // effects, if a session is already recording (including one left in
    // Error; see cancel_session()), the transcription queue is full
    // (recording now would only produce audio that cannot be accepted), or
    // the device can't be opened. The latter two publish ErrorOccurred
    // but, per ServiceState's transition rules (Idle -> Error is not a
    // legal transition; nothing was actually started), leave the service
    // Idle rather than moving it to Error.
    std::optional<SessionError> start_session();

    // Stops the active recording and queues it for transcription.
    // Requires the service to be Recording. Returns as soon as the audio
    // is safely queued, publishing TranscriptionQueued; the Transcript is
    // published later by poll_completed(). Returns an error
    // if no session is recording, or if the recording cannot be stopped or
    // queued, in which case the service transitions to Error instead of
    // Idle.
    StopSessionResult stop_session();

    // Publishes what the transcription worker did since the last call, in
    // order: TranscriptionStarted for each job it began, then for each job
    // it finished TranscriptionCompleted on success or ErrorOccurred on
    // failure. A failed transcription does
    // not put the service in Error (it concerns one job, not capture).
    // Returns how many results (not counting "started" notices) were
    // published. Call regularly from the controlling thread.
    std::size_t poll_completed();

    // Cancels the active session from Recording or Error, discarding the
    // recording in progress (or acknowledging a prior failure, for Error),
    // and returns to Idle. Publishes SessionCancelled. Returns an error if
    // there is no recording or failure to cancel. Jobs already queued are
    // not affected: use cancel_job() for those.
    std::optional<SessionError> cancel_session();

    // Removes a queued job that has not started transcribing. Publishes
    // SessionCancelled. Returns false if `session_id` is not waiting in
    // the queue (unknown, running, or finished): a transcription already
    // in progress cannot be interrupted, only shutdown() aborts it.
    bool cancel_job(core::SessionId session_id);

    // Stops accepting work, drops queued jobs, aborts the transcription in
    // progress and joins the worker. Publishes an ErrorOccurred
    // (component "queue") for every dropped job so clients waiting on it get
    // a definite outcome, and returns their ids. Must run before the speech
    // backend is destroyed. The aborted job's result can still be published
    // with poll_completed() afterwards.
    std::vector<core::SessionId> shutdown();

    // Derived from capture and queue activity: Recording or Error while
    // capture says so, otherwise Processing if any job is queued or being
    // transcribed, otherwise Idle.
    // Registers a callback the transcription worker invokes, from its own
    // thread, whenever a job starts or finishes: the cue to call
    // poll_completed() soon. Typically wakes the owner's event loop. Must be
    // fast and thread-safe. Pass an empty function to unregister before
    // destroying whatever the callback refers to.
    void set_activity_callback(std::function<void()> callback);

    ServiceState state() const;

    SessionStatus status() const;

    // True while the transcription worker owns the speech backend (a job
    // is queued or running). Anything else that would call into the
    // backend, like the Live Transcriber's preview, must stay out of it.
    bool transcription_busy() const;

    // The active session's id, or nullopt if the service is Idle. Exists
    // for components that need to tag their own output with the right
    // session (e.g. the Live Transcriber) without becoming a second
    // authority over the session lifecycle themselves.
    std::optional<core::SessionId> active_session_id() const;

private:
    // Transitions a Recording session to Error and publishes ErrorOccurred
    // for it. Only valid to call while active_session_ has a value (i.e.
    // from stop_session(), never from start_session(), which has no legal
    // path into Error; see start_session()'s doc comment).
    void fail(const std::string& component, const std::string& message);

    core::EventBus& event_bus_;
    audio::RecordingManager& recording_manager_;
    // Capture state only: Idle, Recording or Error. state() folds in the
    // queue to report Processing.
    ServiceState state_ = ServiceState::Idle;
    std::optional<Session> active_session_;
    std::uint64_t next_session_id_ = 1;
    std::string default_language_;
    std::string transcription_prompt_;
    std::optional<core::SessionId> last_completed_;
    std::optional<core::SessionId> last_failed_;
    // Last member: its worker thread must stop before anything above is
    // destroyed.
    TranscriptionQueue queue_;
};

} // namespace yoru::session
