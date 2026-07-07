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
    // The error_code overload is required, not just preferred: the
    // throwing overload of exists() raises filesystem_error for a stat()
    // failure other than "doesn't exist" (e.g. permission denied on the
    // model directory), which would otherwise be an uncaught exception
    // that crashes the daemon over a permissions issue.
    std::error_code exists_error;
    const bool model_exists = std::filesystem::exists(model.path, exists_error);
    if (exists_error) {
        return SpeechError{"failed to check model file: " + exists_error.message()};
    }
    if (!model_exists) {
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

TranscriptionResult WhisperBackend::run_whisper_full(const std::vector<float>& samples,
                                                      const TranscriptionRequest& request) {
    if (samples.empty()) {
        return SpeechError{"empty audio buffer"};
    }
    if (impl_->ctx == nullptr) {
        return SpeechError{"no model loaded"};
    }

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.print_special = false;

    // "auto" as the language string means "detect, then transcribe":
    // whisper.cpp still runs full decoding after detecting. params.
    // detect_language is a different, narrower knob ("stop right after
    // detecting, don't transcribe" — mirroring whisper-cli's separate
    // --detect-language flag) and must stay false here, or transcribe()
    // silently returns zero segments no matter how clear the audio is.
    const bool auto_detect = request.language.empty() || request.language == "auto";
    params.language = auto_detect ? "auto" : request.language.c_str();
    params.detect_language = false;

    // Conditions the decoder with an example of the vocabulary/style to
    // expect (e.g. Portuguese speech with embedded English technical
    // terms), helping it avoid collapsing mixed-language audio into a
    // single language. carry_initial_prompt re-applies it to every
    // internal decode window, not just the first, in case a single call
    // spans more than whisper.cpp's own ~30s window. See
    // config::Configuration::transcription_prompt.
    const bool has_prompt = !request.initial_prompt.empty();
    params.initial_prompt = has_prompt ? request.initial_prompt.c_str() : nullptr;
    params.carry_initial_prompt = has_prompt;

    const auto started_at = std::chrono::steady_clock::now();
    const int status =
        whisper_full(impl_->ctx, params, samples.data(), static_cast<int>(samples.size()));
    const auto processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at);

    if (status != 0) {
        return SpeechError{"whisper_full failed internally"};
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

    return Transcript{
        .text = std::move(text),
        .detected_language = detected_language,
        .requested_language = request.language,
        .audio_duration = std::chrono::milliseconds{static_cast<std::int64_t>(samples.size()) *
                                                    1000 / WHISPER_SAMPLE_RATE},
        .processing_time = processing_time,
    };
}

TranscriptionResult WhisperBackend::transcribe(core::SessionId session_id,
                                               const std::vector<float>& samples,
                                               const TranscriptionRequest& request) {
    // Rejected before TranscriptionStarted is published: nothing was
    // actually attempted, so there is nothing to report as "started".
    // run_whisper_full() re-checks both conditions below; duplicated here
    // only to gate the TranscriptionStarted publish — do not remove this
    // pair to "simplify", or an empty/model-less call starts reporting a
    // spurious TranscriptionStarted.
    if (samples.empty()) {
        return SpeechError{"empty audio buffer"};
    }
    if (impl_->ctx == nullptr) {
        return SpeechError{"no model loaded"};
    }

    event_bus_.publish(TranscriptionStarted{session_id});

    const TranscriptionResult result = run_whisper_full(samples, request);

    if (const auto* error = std::get_if<SpeechError>(&result)) {
        event_bus_.publish(core::ErrorOccurred{
            .session_id = session_id,
            .component = "speech",
            .message = error->message,
        });
        return *error;
    }

    const auto& transcript = std::get<Transcript>(result);
    event_bus_.publish(TranscriptionCompleted{session_id, transcript});
    return transcript;
}

TranscriptionResult WhisperBackend::transcribe_partial(const std::vector<float>& window,
                                                        const TranscriptionRequest& request) {
    return run_whisper_full(window, request);
}

} // namespace yoru::speech
