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
// Allowed transitions:
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
