#pragma once

#include "core/event_bus.hpp"
#include "domains/speech/speech_backend.hpp"

#include <memory>

namespace yoru::speech {

// Implements SpeechBackend using whisper.cpp. This is the only translation
// unit in the project that includes <whisper.h>; nothing else knows
// whisper.cpp exists, so the backend can be replaced later without
// touching callers of SpeechBackend.
//
// The underlying whisper_context is stateful and not safe for concurrent
// use, so load_model(), unload_model(), transcribe() and
// transcribe_partial() must never overlap: at any moment only one thread
// may be inside them (session::TranscriptionQueue guarantees this by
// running transcribe() on its single worker and pausing the live preview
// meanwhile). request_abort() is the one method safe from any thread.
// Publishing onto EventBus is not thread-safe, so only load_model()
// publishes (ModelLoaded); transcribe() deliberately publishes nothing.
class WhisperBackend : public SpeechBackend {
public:
    // `event_bus` must outlive this backend. Publishes ModelLoaded on a
    // successful load_model().
    explicit WhisperBackend(core::EventBus& event_bus);
    ~WhisperBackend() override;

    // Non-copyable and non-movable: owns a live whisper.cpp context by
    // pointer, with no use case for relocating it.
    WhisperBackend(const WhisperBackend&) = delete;
    WhisperBackend& operator=(const WhisperBackend&) = delete;
    WhisperBackend(WhisperBackend&&) = delete;
    WhisperBackend& operator=(WhisperBackend&&) = delete;

    std::optional<SpeechError> load_model(const Model& model) override;
    void unload_model() override;
    bool has_model_loaded() const override;

    TranscriptionResult transcribe(core::SessionId session_id, const std::vector<float>& samples,
                                   const TranscriptionRequest& request) override;

    TranscriptionResult transcribe_partial(const std::vector<float>& window,
                                           const TranscriptionRequest& request) override;

    void request_abort() override;

private:
    // Runs whisper_full over `samples` and builds the resulting Transcript,
    // without publishing any event: the shared core of transcribe() and
    // transcribe_partial(), so neither duplicates whisper_full's setup and
    // segment collection.
    TranscriptionResult run_whisper_full(const std::vector<float>& samples,
                                         const TranscriptionRequest& request);

    core::EventBus& event_bus_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace yoru::speech
