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
// Every method must be called from a single controlling thread: the
// underlying whisper_context is stateful and not safe for concurrent use,
// and publishing onto EventBus is itself not thread-safe.
class WhisperBackend : public SpeechBackend {
public:
    // `event_bus` must outlive this backend. Publishes ModelLoaded on a
    // successful load_model(), and TranscriptionStarted/
    // TranscriptionCompleted/ErrorOccurred around each transcribe() call.
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

    TranscriptionResult transcribe(session::SessionId session_id, const std::vector<float>& samples,
                                    const TranscriptionRequest& request) override;

private:
    core::EventBus& event_bus_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace yoru::speech
