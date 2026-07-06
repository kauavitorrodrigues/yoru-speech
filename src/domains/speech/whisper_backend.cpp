#include "domains/speech/whisper_backend.hpp"

#include "core/error_event.hpp"
#include "domains/speech/events.hpp"

#include <whisper.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>

namespace yoru::speech {

struct WhisperBackend::Impl {
    whisper_context* ctx = nullptr;
    std::optional<Model> current_model;
};

WhisperBackend::WhisperBackend(core::EventBus& event_bus)
    : event_bus_(event_bus), impl_(std::make_unique<Impl>()) {}

WhisperBackend::~WhisperBackend() {
    unload_model();
}

std::optional<SpeechError> WhisperBackend::load_model(const Model& model) {
    if (!std::filesystem::exists(model.path)) {
        return SpeechError{"model file not found: " + model.path.string()};
    }

    const whisper_context_params params = whisper_context_default_params();
    whisper_context* ctx = whisper_init_from_file_with_params(model.path.string().c_str(), params);
    if (ctx == nullptr) {
        return SpeechError{"failed to load model: " + model.path.string()};
    }

    unload_model();
    impl_->ctx = ctx;
    impl_->current_model = model;
    event_bus_.publish(ModelLoaded{model});
    return std::nullopt;
}

void WhisperBackend::unload_model() {
    if (impl_->ctx == nullptr) {
        return;
    }
    whisper_free(impl_->ctx);
    impl_->ctx = nullptr;
    impl_->current_model.reset();
}

bool WhisperBackend::has_model_loaded() const {
    return impl_->ctx != nullptr;
}

TranscriptionResult WhisperBackend::transcribe(core::SessionId session_id,
                                               const std::vector<float>& samples,
                                               const TranscriptionRequest& request) {
    if (samples.empty()) {
        return SpeechError{"empty audio buffer"};
    }
    if (impl_->ctx == nullptr) {
        return SpeechError{"no model loaded"};
    }

    event_bus_.publish(TranscriptionStarted{session_id});

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.print_special = false;

    const bool auto_detect = request.language.empty() || request.language == "auto";
    params.language = auto_detect ? nullptr : request.language.c_str();
    params.detect_language = auto_detect;

    const auto started_at = std::chrono::steady_clock::now();
    const int status =
        whisper_full(impl_->ctx, params, samples.data(), static_cast<int>(samples.size()));
    const auto processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at);

    if (status != 0) {
        const SpeechError error{"whisper_full failed internally"};
        event_bus_.publish(core::ErrorOccurred{
            .session_id = session_id,
            .component = "speech",
            .message = error.message,
        });
        return error;
    }

    std::string text;
    const int segment_count = whisper_full_n_segments(impl_->ctx);
    for (int i = 0; i < segment_count; ++i) {
        text += whisper_full_get_segment_text(impl_->ctx, i);
    }

    const int lang_id = whisper_full_lang_id(impl_->ctx);
    // whisper_lang_str() returns nullptr for an id it doesn't recognize;
    // std::string(nullptr) is undefined behavior, so guard against it in
    // addition to the negative "detection disabled or failed" case.
    const char* lang_str = lang_id >= 0 ? whisper_lang_str(lang_id) : nullptr;
    const std::string detected_language = lang_str != nullptr ? lang_str : "";

    Transcript transcript{
        .text = std::move(text),
        .detected_language = detected_language,
        .requested_language = request.language,
        .audio_duration = std::chrono::milliseconds{static_cast<std::int64_t>(samples.size()) *
                                                    1000 / WHISPER_SAMPLE_RATE},
        .processing_time = processing_time,
    };

    event_bus_.publish(TranscriptionCompleted{session_id, transcript});
    return transcript;
}

} // namespace yoru::speech
