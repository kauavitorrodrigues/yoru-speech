#pragma once

#include "domains/session/session.hpp"
#include "domains/speech/model.hpp"
#include "domains/speech/transcript.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace yoru::speech {

// An error loading a model or transcribing audio, with a message suitable
// for logging. Never thrown: reported through the backend's return values.
struct SpeechError {
    std::string message;
};

// Per-call parameters for a single transcription. Distinct from
// config::Configuration, which is persisted user intent, not an argument
// to one specific call.
struct TranscriptionRequest {
    // Requested language code, or "auto" to let the backend detect it.
    std::string language = "auto";
};

// The outcome of transcribe(): exactly one of a Transcript or the reason
// it could not be produced.
using TranscriptionResult = std::variant<Transcript, SpeechError>;

// Abstraction over any mechanism capable of turning audio into text. The
// rest of the system depends only on this interface, never on a specific
// recognition engine (see WhisperBackend), so the backend can be replaced
// without touching callers.
class SpeechBackend {
public:
    virtual ~SpeechBackend() = default;

    // Loads `model` into memory, replacing any model already loaded.
    // Returns an error, without side effects, if the file at model.path
    // does not exist, is corrupted, or otherwise fails to load.
    virtual std::optional<SpeechError> load_model(const Model& model) = 0;

    // Releases the currently loaded model, if any. Safe to call when none
    // is loaded (no-op).
    virtual void unload_model() = 0;

    virtual bool has_model_loaded() const = 0;

    // Transcribes `samples`, mono PCM float32 at the sample rate the
    // Recording Manager captures in, into a Transcript for `session_id`.
    // Fails if `samples` is empty or no model is loaded.
    virtual TranscriptionResult transcribe(session::SessionId session_id,
                                            const std::vector<float>& samples,
                                            const TranscriptionRequest& request) = 0;
};

} // namespace yoru::speech
