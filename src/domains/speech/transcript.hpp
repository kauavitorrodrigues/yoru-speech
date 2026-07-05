#pragma once

#include <chrono>
#include <string>

namespace yoru::speech {

// The main product of the system: the result produced by speech recognition.
// Always refer to this concept as "Transcript", never "Result", "Output",
// or "Text", to keep the domain language consistent.
struct Transcript {
    std::string text;
    std::string detected_language;
    std::string requested_language;
    std::chrono::milliseconds audio_duration{};
    std::chrono::milliseconds processing_time{};
};

} // namespace yoru::speech
