#pragma once

#include "core/event_bus.hpp"
#include "domains/audio/recording_manager.hpp"
#include "domains/config/configuration.hpp"
#include "domains/session/session_manager.hpp"
#include "domains/speech/speech_backend.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace yoru::session {

// Tuning for LiveTranscriber. Grouped into a struct (rather than five
// positional constructor parameters) because every field trades off
// against the others and callers should be able to name the ones they
// override.
struct LiveTranscriberConfig {
    // Minimum wall-clock time between two ticks that actually run a
    // transcription; a tick() called sooner than that is a silent no-op.
    std::chrono::milliseconds min_interval = std::chrono::milliseconds{1000};

    // A segment (the audio between two commits) is force-committed once
    // it reaches this duration, even without agreement or detected
    // silence — the same role as whisper.cpp's own `stream` example's
    // `--length` cap: it bounds how long a user can talk uninterrupted
    // before something is committed, so the tail preview doesn't grow
    // forever. Kept short (rather than whisper.cpp stream's own 10s
    // default) because LocalAgreement-2 can stall on a single wobbly word
    // — whisper.cpp's own re-decoding occasionally revises a word's
    // spelling or punctuation from tick to tick — and the prefix-based
    // agreement check (see tick()) blocks every later word from being
    // recognized as agreed until that one word settles. A shorter cap
    // bounds how long the tail can visibly "stall" before this segment's
    // whole remaining tail is committed outright, at the cost of forcing
    // a commit (and losing the chance for agreement to catch a mid-word
    // correction) somewhat more often.
    std::chrono::milliseconds max_segment_duration = std::chrono::milliseconds{5000};

    // How much trailing raw audio is kept (not discarded) across a
    // commit, carried into the next segment's window. Mirrors
    // whisper.cpp stream's `--keep`: without this, the word straddling
    // the commit boundary would lose the audio context on one side and
    // could be mis-recognized or clipped in the next segment.
    std::chrono::milliseconds keep_duration = std::chrono::milliseconds{200};

    // How much trailing audio the simple VAD (voice activity detector)
    // inspects to decide whether the user just paused. Below this much
    // captured audio, VAD makes no judgement (same guard as whisper.cpp's
    // own vad_simple()).
    std::chrono::milliseconds vad_trailing_duration = std::chrono::milliseconds{1000};

    // VAD threshold: the trailing audio is considered silence when its
    // average energy is at or below this fraction of the whole segment's
    // average energy. Same default and meaning as whisper.cpp stream's
    // `-vth`.
    float vad_threshold = 0.6F;
};

// Keeps a live text preview available while a session is still Recording,
// by periodically re-transcribing the audio captured since the last
// commit and publishing session::TranscriptionPartial as it goes. Exists
// so an external client (e.g. a shell UI) can show recognized text as the
// user speaks, not only once the full Transcript is produced at the end
// of the session.
//
// Naively re-transcribing a fixed trailing window from scratch on every
// tick (this class's first implementation) makes recognized text
// reappear and get reworded on every tick, because whisper.cpp
// re-decodes the same audio independently each time. This implementation
// instead follows the two techniques whisper.cpp's own real-time example
// (examples/stream) and the LocalAgreement policy from the
// "Turning Whisper into Real-Time Transcription System" paper (the
// whisper_streaming project) use to avoid that:
//
//   - The window given to the Speech Backend only ever GROWS within a
//     segment (never slides and drops its start), so consecutive
//     re-transcriptions of it are extensions of each other rather than
//     unrelated re-decodes of different audio spans.
//   - A word only becomes "committed" (permanent, never shown differently
//     again) once it agrees, word for word, across two consecutive ticks
//     (LocalAgreement-2) — except the very last recognized word, which is
//     never committed by agreement alone, since it's the one most likely
//     to still change as more audio arrives.
//   - A segment is force-committed in full (bypassing agreement) once it
//     reaches `max_segment_duration`, or once the simple VAD in
//     has_trailing_silence() (live_transcriber.cpp) detects the user just
//     paused — matching whisper.cpp stream's own two commit strategies
//     (a length cap, and its VAD sliding-window mode). A force-commit
//     starts the next segment's window with `keep_duration` of trailing
//     raw audio carried over, so the word straddling the boundary isn't
//     cut without context — but that carried-over audio has already been
//     committed once as text, and would otherwise be recognized again as
//     "new" words by the next segment's first tick. The words just
//     committed are remembered (`carried_words_`) specifically so that
//     first tick can recognize and discard a leading duplicate of them
//     (see the overlap check at the top of tick()), instead of
//     re-committing the same word(s) a second time.
//
// Deliberately reuses the existing batch-style, single-thread
// architecture instead of introducing a real streaming engine or a
// dedicated thread: tick() is meant to be called repeatedly from the same
// controlling thread that drives the rest of the service (see the
// composition root's run loop), matching the single-thread contract
// already documented on EventBus, WhisperBackend, and SessionManager. A
// tick that actually runs a transcription blocks that thread for its
// duration; `min_interval` exists specifically to bound how often that
// can happen.
class LiveTranscriber {
public:
    // `event_bus`, `session_manager`, `recording_manager`, and
    // `speech_backend` must outlive this object. Subscribes to
    // audio::RecordingStarted on `event_bus` to call reset() automatically
    // at the start of every new recording, so no state (committed text,
    // pacing, agreement bookkeeping) carries over from a previous,
    // already-finished session. Like every EventBus subscription, this one
    // outlives this object with no way to unsubscribe (see EventBus's own
    // docs): `event_bus` must not publish after this object is destroyed.
    // `initial_configuration` seeds the language/prompt passed to the
    // Speech Backend on every tick(); later changes are tracked via
    // ConfigurationChanged, not by re-reading configuration (same pattern
    // as SessionManager and clipboard::AutoClipboard).
    LiveTranscriber(core::EventBus& event_bus, SessionManager& session_manager,
                    audio::RecordingManager& recording_manager,
                    speech::SpeechBackend& speech_backend,
                    const config::Configuration& initial_configuration,
                    LiveTranscriberConfig config = {});

    LiveTranscriber(const LiveTranscriber&) = delete;
    LiveTranscriber& operator=(const LiveTranscriber&) = delete;
    LiveTranscriber(LiveTranscriber&&) = delete;
    LiveTranscriber& operator=(LiveTranscriber&&) = delete;

    // A no-op unless the service currently has an active Recording session,
    // at least `min_interval` has passed since the last tick that actually
    // ran, and there is new audio since the last commit. Otherwise,
    // transcribes the audio captured since the last commit, updates the
    // agreement/commit bookkeeping described on the class, and publishes
    // session::TranscriptionPartial via `event_bus`. Never throws; a
    // transcription failure is silently swallowed here (unlike
    // stop_session()'s transcribe(), a failed partial doesn't need to fail
    // the session — the next tick just tries again).
    void tick();

    // Forgets all bookkeeping (pacing, committed text, agreement state),
    // so the next tick() starts a fresh segment from scratch. Called
    // automatically on every audio::RecordingStarted (see the
    // constructor); public mainly so tests can reset deterministically
    // without publishing a real event.
    void reset();

private:
    void commit(const std::vector<std::string>& words, std::size_t from_index,
               std::size_t to_index);

    core::EventBus& event_bus_;
    SessionManager& session_manager_;
    audio::RecordingManager& recording_manager_;
    speech::SpeechBackend& speech_backend_;
    LiveTranscriberConfig config_;

    std::optional<std::chrono::steady_clock::time_point> last_tick_at_;

    // Index into the recording's full sample buffer where the current,
    // not-yet-committed segment begins. Advances only on a force-commit.
    std::size_t committed_sample_count_ = 0;
    // How many of the current segment's recognized words are already part
    // of `committed_text_`. Reset to 0 whenever a segment is force-
    // committed and a new one begins.
    std::size_t committed_word_count_ = 0;
    // Stable, monotonically growing text already committed this session.
    std::string committed_text_;
    // The current segment's not-yet-committed tail, replaced wholesale on
    // every tick.
    std::string tail_text_;
    // This segment's recognized words as of the previous tick, compared
    // against the current tick's words to find the LocalAgreement-2
    // prefix.
    std::vector<std::string> previous_words_;
    // The words committed by the most recent force-commit, kept only
    // until the new segment's first tick consumes them (see the class
    // doc comment). Empty otherwise, including for the session's very
    // first segment, which has nothing to carry.
    std::vector<std::string> carried_words_;
    std::string default_language_;
    std::string transcription_prompt_;
};

} // namespace yoru::session
