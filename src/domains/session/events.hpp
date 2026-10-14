#pragma once

#include "core/session_id.hpp"

#include <cstddef>
#include <string>

namespace yoru::session {

// Domain events published by the Session Manager and the Live
// Transcriber. Each represents a fact that already happened, never
// something expected to happen.
//
// There is no SessionStarted/SessionStopped counterpart: RecordingStarted
// (domains/audio) and TranscriptionCompleted (domains/speech), already
// published as the Session Manager delegates to those domains, serve that
// role. Cancellation has no such stand-in, since neither the Recording
// Manager nor the Speech Engine knows a session was cancelled rather than
// simply stopped, hence this dedicated event.
struct SessionCancelled {
    core::SessionId session_id;
};

// Published by the Session Manager when stop_session() has accepted a
// recording into the transcription queue. `position` counts the jobs ahead
// of it, including the one being transcribed. speech::TranscriptionStarted
// follows later, when the worker actually begins decoding it.
struct TranscriptionQueued {
    core::SessionId session_id;
    std::size_t position = 0;
};

// Published repeatedly by the Live Transcriber while a session is
// Recording. `committed_text` is stable and monotonically growing: once a
// span of words appears here, it is never revised or repeated in a later
// event, so a client may simply display it once. `tail_text` is the
// opposite: the still-unstable end of the utterance, replaced wholesale on
// every event (never appended to), since it may still be rewritten as
// more audio arrives. Distinct from speech::TranscriptionCompleted, which
// is the session's one authoritative final Transcript, produced only once
// at the end — these are a live, continuously revised preview of it.
struct TranscriptionPartial {
    core::SessionId session_id;
    std::string committed_text;
    std::string tail_text;
};

} // namespace yoru::session
