#include "domains/ipc/message_codec.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <vector>

using yoru::audio::Recording;
using yoru::audio::RecordingFinished;
using yoru::audio::RecordingStarted;
using yoru::config::Configuration;
using yoru::config::ConfigurationChanged;
using yoru::config::ModelLoadPolicy;
using yoru::core::ErrorOccurred;
using yoru::core::SessionId;
using yoru::ipc::decode_set_config;
using yoru::ipc::encode_ack;
using yoru::ipc::encode_config;
using yoru::ipc::encode_error;
using yoru::ipc::encode_event;
using yoru::ipc::encode_models;
using yoru::ipc::encode_state;
using yoru::ipc::encode_transcript;
using yoru::ipc::parse_message;
using yoru::session::ServiceState;
using yoru::session::SessionCancelled;
using yoru::session::TranscriptionPartial;
using yoru::speech::Model;
using yoru::speech::ModelLoaded;
using yoru::speech::ModelSize;
using yoru::speech::Transcript;
using yoru::speech::TranscriptionCompleted;
using yoru::speech::TranscriptionStarted;

TEST_CASE("parse_message() extracts the type field from a well-formed message") {
    const auto message = parse_message(R"({"type":"start_recording"})");

    REQUIRE(message.type.has_value());
    CHECK(message.type.value() == "start_recording");
    CHECK_FALSE(message.error.has_value());
}

TEST_CASE("parse_message() reports an error for malformed JSON") {
    const auto message = parse_message("not json at all");

    CHECK_FALSE(message.type.has_value());
    REQUIRE(message.error.has_value());
}

TEST_CASE("parse_message() reports an error when \"type\" is missing") {
    const auto message = parse_message(R"({"foo":"bar"})");

    CHECK_FALSE(message.type.has_value());
    REQUIRE(message.error.has_value());
}

TEST_CASE("parse_message() reports an error when \"type\" is not a string") {
    const auto message = parse_message(R"({"type":42})");

    CHECK_FALSE(message.type.has_value());
    REQUIRE(message.error.has_value());
}

TEST_CASE("parse_message() reports an error for a JSON value that isn't an object") {
    const auto message = parse_message(R"(["start_recording"])");

    CHECK_FALSE(message.type.has_value());
    REQUIRE(message.error.has_value());
}

TEST_CASE("decode_set_config() decodes a fully populated request") {
    const auto decoded = decode_set_config(R"({
        "type": "set_config",
        "default_language": "pt",
        "selected_model": "small",
        "auto_clipboard": false,
        "model_load_policy": "always_loaded",
        "transcription_prompt": "mixing English terms into Portuguese speech"
    })",
                                           Configuration{});

    REQUIRE_FALSE(decoded.error.has_value());
    CHECK(decoded.configuration.default_language == "pt");
    CHECK(decoded.configuration.selected_model == "small");
    CHECK_FALSE(decoded.configuration.auto_clipboard);
    CHECK(decoded.configuration.model_load_policy == ModelLoadPolicy::AlwaysLoaded);
    CHECK(decoded.configuration.transcription_prompt ==
         "mixing English terms into Portuguese speech");
}

TEST_CASE("decode_set_config() rejects transcription_prompt with the wrong JSON type") {
    const auto decoded =
        decode_set_config(R"({"type": "set_config", "transcription_prompt": 42})", Configuration{});

    REQUIRE(decoded.error.has_value());
}

TEST_CASE(
    "decode_set_config() leaves unspecified fields at the given base, not at hardcoded defaults") {
    Configuration base;
    base.default_language = "pt";
    base.selected_model = "small";
    base.model_load_policy = ModelLoadPolicy::AlwaysLoaded;

    const auto decoded =
        decode_set_config(R"({"type": "set_config", "auto_clipboard": false})", base);

    REQUIRE_FALSE(decoded.error.has_value());
    CHECK(decoded.configuration.default_language == "pt");
    CHECK(decoded.configuration.selected_model == "small");
    CHECK(decoded.configuration.model_load_policy == ModelLoadPolicy::AlwaysLoaded);
    CHECK_FALSE(decoded.configuration.auto_clipboard);
}

TEST_CASE("decode_set_config() rejects an unrecognized model_load_policy string") {
    const auto decoded = decode_set_config(
        R"({"type": "set_config", "model_load_policy": "sometimes"})", Configuration{});

    REQUIRE(decoded.error.has_value());
}

TEST_CASE("decode_set_config() rejects a field with the wrong JSON type") {
    const auto decoded =
        decode_set_config(R"({"type": "set_config", "auto_clipboard": "yes"})", Configuration{});

    REQUIRE(decoded.error.has_value());
}

TEST_CASE("decode_set_config() rejects malformed JSON") {
    const auto decoded = decode_set_config("not json", Configuration{});

    REQUIRE(decoded.error.has_value());
}

TEST_CASE("encode_ack() echoes the type and reports success") {
    const auto encoded = encode_ack("start_recording");

    const auto reparsed = parse_message(encoded);
    REQUIRE(reparsed.type.has_value());
    CHECK(reparsed.type.value() == "start_recording");
    CHECK(encoded.find(R"("ok":true)") != std::string::npos);
}

TEST_CASE("encode_error() echoes the type and carries the error message") {
    const auto encoded = encode_error("start_recording", "a session is already active");

    const auto reparsed = parse_message(encoded);
    REQUIRE(reparsed.type.has_value());
    CHECK(reparsed.type.value() == "start_recording");
    CHECK(encoded.find(R"("ok":false)") != std::string::npos);
    CHECK(encoded.find("a session is already active") != std::string::npos);
}

TEST_CASE("encode_transcript() carries every Transcript field") {
    const Transcript transcript{
        .text = "hello world",
        .detected_language = "en",
        .requested_language = "auto",
        .audio_duration = std::chrono::milliseconds{1500},
        .processing_time = std::chrono::milliseconds{80},
    };

    const auto encoded = encode_transcript("stop_recording", transcript);

    CHECK(encoded.find("hello world") != std::string::npos);
    CHECK(encoded.find(R"("detected_language":"en")") != std::string::npos);
    CHECK(encoded.find(R"("audio_duration_ms":1500)") != std::string::npos);
    CHECK(encoded.find(R"("processing_time_ms":80)") != std::string::npos);
}

TEST_CASE("encode_state() renders each ServiceState as a lowercase string") {
    CHECK(encode_state("get_state", ServiceState::Idle).find(R"("state":"idle")") !=
          std::string::npos);
    CHECK(encode_state("get_state", ServiceState::Recording).find(R"("state":"recording")") !=
          std::string::npos);
    CHECK(encode_state("get_state", ServiceState::Processing).find(R"("state":"processing")") !=
          std::string::npos);
    CHECK(encode_state("get_state", ServiceState::Error).find(R"("state":"error")") !=
          std::string::npos);
}

TEST_CASE("encode_config() carries every Configuration field") {
    Configuration configuration;
    configuration.default_language = "pt";
    configuration.selected_model = "small";
    configuration.auto_clipboard = false;
    configuration.model_load_policy = ModelLoadPolicy::AlwaysLoaded;
    configuration.transcription_prompt = "mixing English terms into Portuguese speech";

    const auto encoded = encode_config("get_config", configuration);

    CHECK(encoded.find(R"("default_language":"pt")") != std::string::npos);
    CHECK(encoded.find(R"("selected_model":"small")") != std::string::npos);
    CHECK(encoded.find(R"("auto_clipboard":false)") != std::string::npos);
    CHECK(encoded.find(R"("model_load_policy":"always_loaded")") != std::string::npos);
    CHECK(encoded.find("mixing English terms into Portuguese speech") != std::string::npos);
}

TEST_CASE("encode_models() carries every Model field for every model in the list") {
    const std::vector<Model> models{
        Model{
            .name = "ggml-tiny",
            .size = ModelSize::Tiny,
            .supported_language = "multi",
            .path = "/models/ggml-tiny.bin",
            .backend = "whisper.cpp",
        },
    };

    const auto encoded = encode_models("list_models", models);

    CHECK(encoded.find(R"("name":"ggml-tiny")") != std::string::npos);
    CHECK(encoded.find(R"("size":"tiny")") != std::string::npos);
    CHECK(encoded.find(R"("path":"/models/ggml-tiny.bin")") != std::string::npos);
    CHECK(encoded.find(R"("backend":"whisper.cpp")") != std::string::npos);
}

TEST_CASE("encode_models() with an empty list still produces a valid response") {
    const auto encoded = encode_models("list_models", {});

    const auto reparsed = parse_message(encoded);
    REQUIRE(reparsed.type.has_value());
    CHECK(encoded.find(R"("models":[])") != std::string::npos);
}

TEST_CASE("encode_event() envelopes every event with type \"event\" and its own event name") {
    const auto encoded = encode_event(RecordingStarted{.session_id = SessionId{7}});

    const auto reparsed = parse_message(encoded);
    REQUIRE(reparsed.type.has_value());
    CHECK(reparsed.type.value() == "event");
    CHECK(encoded.find(R"("event":"recording_started")") != std::string::npos);
    CHECK(encoded.find(R"("session_id":7)") != std::string::npos);
}

TEST_CASE("encode_event() for RecordingFinished carries duration and format, not raw samples") {
    const Recording recording{
        .samples = {0.1F, 0.2F, 0.3F, 0.4F},
        .sample_rate = 4,
        .channels = 1,
    };

    const auto encoded =
        encode_event(RecordingFinished{.session_id = SessionId{3}, .recording = recording});

    CHECK(encoded.find(R"("event":"recording_finished")") != std::string::npos);
    CHECK(encoded.find(R"("duration_ms":1000)") != std::string::npos);
    CHECK(encoded.find(R"("sample_rate":4)") != std::string::npos);
    CHECK(encoded.find("samples") == std::string::npos);
}

TEST_CASE("encode_event() for TranscriptionStarted carries the session id") {
    const auto encoded = encode_event(TranscriptionStarted{.session_id = SessionId{9}});

    CHECK(encoded.find(R"("event":"transcription_started")") != std::string::npos);
    CHECK(encoded.find(R"("session_id":9)") != std::string::npos);
}

TEST_CASE("encode_event() for TranscriptionCompleted carries the Transcript") {
    const Transcript transcript{
        .text = "olá mundo",
        .detected_language = "pt",
        .requested_language = "auto",
        .audio_duration = std::chrono::milliseconds{2000},
        .processing_time = std::chrono::milliseconds{90},
    };

    const auto encoded =
        encode_event(TranscriptionCompleted{.session_id = SessionId{1}, .transcript = transcript});

    CHECK(encoded.find(R"("event":"transcription_completed")") != std::string::npos);
    CHECK(encoded.find("mundo") != std::string::npos);
    CHECK(encoded.find(R"("audio_duration_ms":2000)") != std::string::npos);
}

TEST_CASE("encode_event() for TranscriptionPartial carries the session id, committed and tail text") {
    const auto encoded = encode_event(TranscriptionPartial{
        .session_id = SessionId{5}, .committed_text = "olá mundo", .tail_text = "como vai"});

    CHECK(encoded.find(R"("event":"transcription_partial")") != std::string::npos);
    CHECK(encoded.find(R"("session_id":5)") != std::string::npos);
    CHECK(encoded.find("mundo") != std::string::npos);
    CHECK(encoded.find("como vai") != std::string::npos);
}

TEST_CASE("encode_event() for ModelLoaded carries the Model") {
    const auto encoded = encode_event(ModelLoaded{
        .model =
            Model{
                .name = "ggml-base",
                .size = ModelSize::Base,
                .supported_language = "multi",
                .path = "/models/ggml-base.bin",
                .backend = "whisper.cpp",
            },
    });

    CHECK(encoded.find(R"("event":"model_loaded")") != std::string::npos);
    CHECK(encoded.find(R"("name":"ggml-base")") != std::string::npos);
}

TEST_CASE("encode_event() for ConfigurationChanged carries the new Configuration") {
    Configuration configuration;
    configuration.default_language = "pt";
    configuration.transcription_prompt = "mixing English terms into Portuguese speech";

    const auto encoded = encode_event(ConfigurationChanged{.configuration = configuration});

    CHECK(encoded.find(R"("event":"configuration_changed")") != std::string::npos);
    CHECK(encoded.find(R"("default_language":"pt")") != std::string::npos);
    CHECK(encoded.find("mixing English terms into Portuguese speech") != std::string::npos);
}

TEST_CASE("encode_event() for ErrorOccurred omits session_id when absent") {
    const auto encoded = encode_event(ErrorOccurred{
        .session_id = std::nullopt,
        .component = "config",
        .message = "failed to load config.toml",
    });

    CHECK(encoded.find(R"("event":"error_occurred")") != std::string::npos);
    CHECK(encoded.find("session_id") == std::string::npos);
    CHECK(encoded.find("failed to load config.toml") != std::string::npos);
}

TEST_CASE("encode_event() for ErrorOccurred includes session_id when present") {
    const auto encoded = encode_event(ErrorOccurred{
        .session_id = SessionId{4},
        .component = "speech",
        .message = "no model loaded",
    });

    CHECK(encoded.find(R"("session_id":4)") != std::string::npos);
}

TEST_CASE("encode_event() for SessionCancelled carries the session id") {
    const auto encoded = encode_event(SessionCancelled{.session_id = SessionId{2}});

    CHECK(encoded.find(R"("event":"session_cancelled")") != std::string::npos);
    CHECK(encoded.find(R"("session_id":2)") != std::string::npos);
}
