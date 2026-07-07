#include "domains/session/live_transcriber.hpp"

#include "core/event_bus.hpp"
#include "domains/audio/recording_manager.hpp"
#include "domains/session/events.hpp"
#include "domains/session/session_manager.hpp"
#include "domains/speech/model.hpp"
#include "domains/speech/speech_backend.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using yoru::audio::RecordingManager;
using yoru::config::Configuration;
using yoru::core::EventBus;
using yoru::core::SessionId;
using yoru::session::LiveTranscriber;
using yoru::session::LiveTranscriberConfig;
using yoru::session::SessionError;
using yoru::session::SessionManager;
using yoru::session::TranscriptionPartial;
using yoru::speech::Model;
using yoru::speech::SpeechBackend;
using yoru::speech::SpeechError;
using yoru::speech::Transcript;
using yoru::speech::TranscriptionRequest;
using yoru::speech::TranscriptionResult;

namespace {

// A SpeechBackend that never touches whisper.cpp or the audio it's given:
// transcribe_partial() ignores `window` entirely and returns a
// pre-scripted sequence of texts, one per call (repeating the last one
// once the script runs out). This is what makes LocalAgreement's
// word-by-word behavior deterministically testable — a real WhisperBackend
// re-decoding real audio would not reliably reproduce the exact same
// wording across two consecutive calls, which is the exact instability
// LocalAgreement exists to guard against (see live_transcriber.hpp).
class ScriptedSpeechBackend : public SpeechBackend {
public:
    std::optional<SpeechError> load_model(const Model&) override {
        return std::nullopt;
    }

    void unload_model() override {}

    bool has_model_loaded() const override {
        return true;
    }

    TranscriptionResult transcribe(SessionId /*session_id*/, const std::vector<float>& /*samples*/,
                                   const TranscriptionRequest& request) override {
        return Transcript{
            .text = "unused",
            .detected_language = "en",
            .requested_language = request.language,
            .audio_duration = {},
            .processing_time = {},
        };
    }

    TranscriptionResult transcribe_partial(const std::vector<float>& /*window*/,
                                           const TranscriptionRequest& request) override {
        ++call_count;
        last_request = request;
        const std::string text =
            scripted_texts.empty()
                ? ""
                : scripted_texts[std::min(next_index, scripted_texts.size() - 1)];
        if (next_index + 1 < scripted_texts.size()) {
            ++next_index;
        }
        return Transcript{
            .text = text,
            .detected_language = "en",
            .requested_language = request.language,
            .audio_duration = {},
            .processing_time = {},
        };
    }

    std::vector<std::string> scripted_texts;
    std::size_t next_index = 0;
    int call_count = 0;
    std::optional<TranscriptionRequest> last_request;
};

// Same role as SessionManager's own tests: every test here that records
// needs a real capture device. Skip gracefully rather than failing on a
// host without one.
bool skip_if_no_device(const std::optional<SessionError>& error) {
    if (error.has_value()) {
        MESSAGE("skipping: no capture device available (", error->message, ")");
        return true;
    }
    return false;
}

// A config that never force-commits on its own within a short test's
// lifetime: a very long segment cap, and a VAD trailing window far longer
// than any test's captured audio, so has_trailing_silence()'s "not enough
// audio yet to judge" guard always applies. Tests that specifically want
// a force-commit override max_segment_duration explicitly.
LiveTranscriberConfig no_force_commit_config() {
    LiveTranscriberConfig config;
    config.min_interval = std::chrono::milliseconds{0};
    config.max_segment_duration = std::chrono::minutes{5};
    config.vad_trailing_duration = std::chrono::minutes{5};
    return config;
}

} // namespace

TEST_CASE("tick() passes the configured language and prompt to the Speech Backend") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    ScriptedSpeechBackend speech_backend;
    speech_backend.scripted_texts = {"hello"};
    Configuration configuration;
    configuration.default_language = "pt";
    configuration.transcription_prompt = "mixing English terms into Portuguese speech";
    SessionManager session_manager(bus, recording_manager, speech_backend, configuration);
    LiveTranscriber live_transcriber(bus, session_manager, recording_manager, speech_backend,
                                     configuration, no_force_commit_config());

    if (skip_if_no_device(session_manager.start_session())) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    live_transcriber.tick();

    REQUIRE(speech_backend.last_request.has_value());
    CHECK(speech_backend.last_request->language == "pt");
    CHECK(speech_backend.last_request->initial_prompt ==
         "mixing English terms into Portuguese speech");

    session_manager.cancel_session();
}

TEST_CASE("tick() is a no-op while the service is Idle") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    ScriptedSpeechBackend speech_backend;
    SessionManager session_manager(bus, recording_manager, speech_backend, Configuration{});
    LiveTranscriber live_transcriber(bus, session_manager, recording_manager, speech_backend,
                                     Configuration{});

    live_transcriber.tick();

    CHECK(speech_backend.call_count == 0);
}

TEST_CASE("tick() commits words only once they agree across two consecutive ticks") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    ScriptedSpeechBackend speech_backend;
    speech_backend.scripted_texts = {"hello", "hello world", "hello world today"};
    SessionManager session_manager(bus, recording_manager, speech_backend, Configuration{});
    LiveTranscriber live_transcriber(bus, session_manager, recording_manager, speech_backend,
                                     Configuration{},
no_force_commit_config());

    std::vector<TranscriptionPartial> events;
    bus.subscribe<TranscriptionPartial>([&](const TranscriptionPartial& e) { events.push_back(e); });

    if (skip_if_no_device(session_manager.start_session())) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    // Tick 1: nothing to agree with yet (no previous tick), so nothing is
    // committed regardless of wording — the whole first recognition is
    // tentative tail text.
    live_transcriber.tick();
    REQUIRE(events.size() == 1);
    CHECK(events[0].committed_text.empty());
    CHECK(events[0].tail_text == "hello");

    // Tick 2: "hello" agrees with tick 1's "hello", so it's promoted to
    // committed_text. "world" is the new last word and stays tentative
    // (LocalAgreement never commits the freshest word by agreement alone).
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    live_transcriber.tick();
    REQUIRE(events.size() == 2);
    CHECK(events[1].committed_text == "hello");
    CHECK(events[1].tail_text == "world");

    // Tick 3: "hello world" now agrees across ticks 2 and 3, so "world"
    // gets promoted too; "today" is the new tentative last word.
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    live_transcriber.tick();
    REQUIRE(events.size() == 3);
    CHECK(events[2].committed_text == "hello world");
    CHECK(events[2].tail_text == "today");

    session_manager.cancel_session();
}

TEST_CASE("tick() force-commits the whole segment once max_segment_duration elapses") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    ScriptedSpeechBackend speech_backend;
    speech_backend.scripted_texts = {"hello"};
    SessionManager session_manager(bus, recording_manager, speech_backend, Configuration{});

    LiveTranscriberConfig config;
    config.min_interval = std::chrono::milliseconds{0};
    config.max_segment_duration = std::chrono::milliseconds{100};
    config.vad_trailing_duration = std::chrono::minutes{5}; // isolate from VAD
    config.keep_duration = std::chrono::milliseconds{0};
    LiveTranscriber live_transcriber(bus, session_manager, recording_manager, speech_backend,
                                     Configuration{},
config);

    std::vector<TranscriptionPartial> events;
    bus.subscribe<TranscriptionPartial>([&](const TranscriptionPartial& e) { events.push_back(e); });

    if (skip_if_no_device(session_manager.start_session())) {
        return;
    }
    // 200ms of real capture exceeds the 100ms segment cap, so the very
    // first tick must force-commit "hello" whole — no second tick needed
    // to "agree" with it first, unlike the LocalAgreement test above.
    std::this_thread::sleep_for(std::chrono::milliseconds{200});

    live_transcriber.tick();

    REQUIRE(events.size() == 1);
    CHECK(events[0].committed_text == "hello");
    CHECK(events[0].tail_text.empty());

    session_manager.cancel_session();
}

TEST_CASE("a force-commit boundary does not duplicate the word carried over via keep_duration") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    ScriptedSpeechBackend speech_backend;
    // The second segment's script ("world today") simulates whisper
    // re-recognizing "world" from the small kept-audio overlap carried
    // across the force-commit boundary (see LiveTranscriberConfig::
    // keep_duration), followed by genuinely new speech ("today"). Without
    // the overlap check at the top of tick(), "world" would be committed
    // a second time on the second segment's first tick.
    speech_backend.scripted_texts = {"hello world", "world today"};
    SessionManager session_manager(bus, recording_manager, speech_backend, Configuration{});

    LiveTranscriberConfig config;
    config.min_interval = std::chrono::milliseconds{0};
    config.max_segment_duration = std::chrono::milliseconds{100};
    config.vad_trailing_duration = std::chrono::minutes{5}; // isolate from VAD
    config.keep_duration = std::chrono::milliseconds{200};
    LiveTranscriber live_transcriber(bus, session_manager, recording_manager, speech_backend,
                                     Configuration{},
config);

    std::vector<TranscriptionPartial> events;
    bus.subscribe<TranscriptionPartial>([&](const TranscriptionPartial& e) { events.push_back(e); });

    if (skip_if_no_device(session_manager.start_session())) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    live_transcriber.tick();
    REQUIRE(events.size() == 1);
    CHECK(events[0].committed_text == "hello world");

    // Second segment, also force-committed (still well past
    // max_segment_duration): "world" must be recognized as a duplicate of
    // the previous segment's last committed word and skipped, so only
    // "today" is newly appended.
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    live_transcriber.tick();
    REQUIRE(events.size() == 2);
    CHECK(events[1].committed_text == "hello world today");

    session_manager.cancel_session();
}

TEST_CASE("overlap dedup ignores trailing punctuation churn across a force-commit boundary") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    ScriptedSpeechBackend speech_backend;
    // Reproduces a real duplication found in manual testing: whisper.cpp
    // marks a word at the very edge of a growing window with a trailing
    // "..." (possibly-truncated marker), then drops that mark once the
    // next segment gives it more audio context. "hello world..." and
    // "world today" are the same underlying word ("world"/"world...")
    // straddling the boundary, just with different trailing punctuation —
    // overlap_prefix_length() must still recognize them as the same word.
    speech_backend.scripted_texts = {"hello world...", "world today"};
    SessionManager session_manager(bus, recording_manager, speech_backend, Configuration{});

    LiveTranscriberConfig config;
    config.min_interval = std::chrono::milliseconds{0};
    config.max_segment_duration = std::chrono::milliseconds{100};
    config.vad_trailing_duration = std::chrono::minutes{5}; // isolate from VAD
    config.keep_duration = std::chrono::milliseconds{200};
    LiveTranscriber live_transcriber(bus, session_manager, recording_manager, speech_backend,
                                     Configuration{},
config);

    std::vector<TranscriptionPartial> events;
    bus.subscribe<TranscriptionPartial>([&](const TranscriptionPartial& e) { events.push_back(e); });

    if (skip_if_no_device(session_manager.start_session())) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    live_transcriber.tick();
    REQUIRE(events.size() == 1);
    CHECK(events[0].committed_text == "hello world...");

    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    live_transcriber.tick();
    REQUIRE(events.size() == 2);
    CHECK(events[1].committed_text == "hello world... today");

    session_manager.cancel_session();
}

TEST_CASE("tick() respects min_interval pacing between transcriptions") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    ScriptedSpeechBackend speech_backend;
    speech_backend.scripted_texts = {"hello"};
    SessionManager session_manager(bus, recording_manager, speech_backend, Configuration{});

    LiveTranscriberConfig config = no_force_commit_config();
    config.min_interval = std::chrono::milliseconds{200};
    LiveTranscriber live_transcriber(bus, session_manager, recording_manager, speech_backend,
                                     Configuration{},
config);

    if (skip_if_no_device(session_manager.start_session())) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{500});

    live_transcriber.tick();
    CHECK(speech_backend.call_count == 1);

    // Called again immediately: min_interval hasn't elapsed yet, so this
    // must be a silent no-op, not a second transcription.
    live_transcriber.tick();
    CHECK(speech_backend.call_count == 1);

    std::this_thread::sleep_for(std::chrono::milliseconds{250});
    live_transcriber.tick();
    CHECK(speech_backend.call_count == 2);

    session_manager.cancel_session();
}

TEST_CASE("a new recording resets pacing and committed text so its first tick starts fresh") {
    EventBus bus;
    RecordingManager recording_manager(bus);
    ScriptedSpeechBackend speech_backend;
    speech_backend.scripted_texts = {"hello"};
    SessionManager session_manager(bus, recording_manager, speech_backend, Configuration{});

    LiveTranscriberConfig config;
    config.min_interval = std::chrono::milliseconds{500};
    config.max_segment_duration = std::chrono::milliseconds{100};
    config.vad_trailing_duration = std::chrono::minutes{5};
    config.keep_duration = std::chrono::milliseconds{0};
    LiveTranscriber live_transcriber(bus, session_manager, recording_manager, speech_backend,
                                     Configuration{},
config);

    std::vector<TranscriptionPartial> events;
    bus.subscribe<TranscriptionPartial>([&](const TranscriptionPartial& e) { events.push_back(e); });

    if (skip_if_no_device(session_manager.start_session())) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    live_transcriber.tick();
    REQUIRE(events.size() == 1);
    CHECK(events[0].committed_text == "hello"); // force-committed (200ms > 100ms cap)
    session_manager.cancel_session();

    // A brand new recording: RecordingStarted fires, which resets both
    // the Live Transcriber's pacing AND its committed text. Without that
    // reset, this tick would either be silently skipped (leftover
    // min_interval) or would report a committed_text carried over from
    // the previous, already-finished session.
    if (skip_if_no_device(session_manager.start_session())) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    live_transcriber.tick();
    REQUIRE(events.size() == 2);
    CHECK(events[1].committed_text == "hello");

    session_manager.cancel_session();
}
