#pragma once

namespace yoru::session {

// Mutually exclusive states of the service, as defined by the domain model.
// Every behavior of the system derives from this state.
enum class ServiceState {
    Idle,
    Recording,
    Processing,
    Error,
};

// Validates whether a transition between two service states is allowed.
//
// Allowed transitions. Note that SessionManager stores only the capture
// side (Idle, Recording, Error) and derives Processing from its
// transcription queue in state(): after a recording is stopped the stored
// state goes Recording -> Idle, and the reported state is Processing while
// jobs are queued or running, even when a new Recording starts (the
// reported state is then Recording again). The table below is the domain
// model's view of one session's progress.
//
//   Idle       -> Recording  (session started)
//   Recording  -> Processing (recording finished, transcription starts)
//   Recording  -> Idle       (session cancelled)
//   Recording  -> Error      (capture failure)
//   Processing -> Idle       (transcription completed or cancelled)
//   Processing -> Error      (transcription failure)
//   Error      -> Idle       (recovery)
//
// A transition to the same state is not considered valid: it does not
// represent a state change and callers should not attempt it.
bool can_transition(ServiceState from, ServiceState to);

} // namespace yoru::session
