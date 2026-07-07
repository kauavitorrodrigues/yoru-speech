#include "domains/session/live_transcriber.hpp"

#include "domains/audio/capture_device.hpp"
#include "domains/audio/events.hpp"
#include "domains/config/events.hpp"
#include "domains/session/events.hpp"
#include "domains/session/service_state.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace yoru::session {

namespace {

std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> words;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) {
        words.push_back(word);
    }
    return words;
}

// Joins words[from_index, to_index) with single spaces. Empty if the
// range is empty or out of bounds.
std::string join_words(const std::vector<std::string>& words, std::size_t from_index,
                       std::size_t to_index) {
    std::string joined;
    for (std::size_t i = from_index; i < to_index && i < words.size(); ++i) {
        if (!joined.empty()) {
            joined += ' ';
        }
        joined += words[i];
    }
    return joined;
}

// Joins words[from_index, words.size()) with single spaces.
std::string join_words(const std::vector<std::string>& words, std::size_t from_index) {
    return join_words(words, from_index, words.size());
}

// Strips trailing punctuation (periods, commas, "...", etc.) from `word`.
// whisper.cpp frequently revises trailing punctuation for the same
// underlying word between re-decodes of a growing window — most visibly,
// it appends "..." to a word right at the edge of the audio it was given,
// to mark it as possibly truncated, and drops that mark once more audio
// arrives. Comparing words raw would treat "de..." and "de" as two
// different words and block agreement/overlap detection on nothing more
// than that punctuation churn. Only used to decide whether two words
// "match"; the original, unstripped word is still what actually gets
// committed and displayed.
std::string strip_trailing_punctuation(const std::string& word) {
    std::size_t end = word.size();
    while (end > 0 && std::ispunct(static_cast<unsigned char>(word[end - 1])) != 0) {
        --end;
    }
    return word.substr(0, end);
}

bool words_match(const std::string& a, const std::string& b) {
    return strip_trailing_punctuation(a) == strip_trailing_punctuation(b);
}

// How many leading words `a` and `b` agree on, word for word.
std::size_t common_prefix_length(const std::vector<std::string>& a,
                                 const std::vector<std::string>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::size_t i = 0;
    while (i < n && words_match(a[i], b[i])) {
        ++i;
    }
    return i;
}

// How many trailing words of `previous_segment_words` reappear, in the
// same order, as the leading words of `new_segment_words`. Used to detect
// when a force-commit's small kept-audio overlap (see
// LiveTranscriberConfig::keep_duration) makes the new segment's first
// tick re-recognize a word that was already committed at the end of the
// previous segment, so it can be skipped instead of committed a second
// time. Tries the longest possible overlap first, since a short match
// (e.g. a single common word) could occur by coincidence rather than
// because of the kept audio.
std::size_t overlap_prefix_length(const std::vector<std::string>& previous_segment_words,
                                  const std::vector<std::string>& new_segment_words) {
    const std::size_t max_k = std::min(previous_segment_words.size(), new_segment_words.size());
    for (std::size_t k = max_k; k > 0; --k) {
        bool matches = true;
        for (std::size_t i = 0; i < k; ++i) {
            if (!words_match(previous_segment_words[previous_segment_words.size() - k + i],
                             new_segment_words[i])) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return k;
        }
    }
    return 0;
}

// A simple energy-ratio voice activity detector: true when the trailing
// `trailing_duration` of `window` is much quieter than the window as a
// whole, i.e. the user just paused. Same algorithm as whisper.cpp's own
// examples/common.cpp vad_simple() (minus its optional high-pass filter,
// omitted here to keep this a direct port of the essential technique
// rather than a full reimplementation). Never signals silence before
// `trailing_duration` worth of audio has actually been captured — with
// too little audio to compare against, there's nothing to judge yet.
bool has_trailing_silence(const std::vector<float>& window,
                          std::chrono::milliseconds trailing_duration, float threshold) {
    const auto trailing_samples =
        static_cast<std::size_t>(trailing_duration.count()) * audio::kSampleRate / 1000;
    if (window.size() <= trailing_samples || trailing_samples == 0) {
        return false;
    }

    float energy_all = 0.0F;
    float energy_trailing = 0.0F;
    for (std::size_t i = 0; i < window.size(); ++i) {
        const float amplitude = std::fabs(window[i]);
        energy_all += amplitude;
        if (i >= window.size() - trailing_samples) {
            energy_trailing += amplitude;
        }
    }
    energy_all /= static_cast<float>(window.size());
    energy_trailing /= static_cast<float>(trailing_samples);

    return energy_trailing <= threshold * energy_all;
}

} // namespace

LiveTranscriber::LiveTranscriber(core::EventBus& event_bus, SessionManager& session_manager,
                                 audio::RecordingManager& recording_manager,
                                 speech::SpeechBackend& speech_backend,
                                 const config::Configuration& initial_configuration,
                                 LiveTranscriberConfig config)
    : event_bus_(event_bus), session_manager_(session_manager),
      recording_manager_(recording_manager), speech_backend_(speech_backend),
      config_(config), default_language_(initial_configuration.default_language),
      transcription_prompt_(initial_configuration.transcription_prompt) {
    event_bus_.subscribe<audio::RecordingStarted>(
        [this](const audio::RecordingStarted&) { reset(); });
    event_bus_.subscribe<config::ConfigurationChanged>(
        [this](const config::ConfigurationChanged& event) {
            default_language_ = event.configuration.default_language;
            transcription_prompt_ = event.configuration.transcription_prompt;
        });
}

void LiveTranscriber::commit(const std::vector<std::string>& words, std::size_t from_index,
                             std::size_t to_index) {
    const std::string new_text = join_words(words, from_index, to_index);
    if (new_text.empty()) {
        return;
    }
    if (!committed_text_.empty()) {
        committed_text_ += ' ';
    }
    committed_text_ += new_text;
}

void LiveTranscriber::tick() {
    if (session_manager_.state() != ServiceState::Recording) {
        return;
    }

    const auto session_id = session_manager_.active_session_id();
    if (!session_id.has_value()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (last_tick_at_.has_value() && now - *last_tick_at_ < config_.min_interval) {
        return;
    }

    const std::vector<float> window = recording_manager_.samples_snapshot(committed_sample_count_);
    if (window.empty()) {
        return;
    }
    last_tick_at_ = now;

    const speech::TranscriptionResult result = speech_backend_.transcribe_partial(
        window, speech::TranscriptionRequest{
                    .language = default_language_,
                    .initial_prompt = transcription_prompt_,
                });
    const auto* transcript = std::get_if<speech::Transcript>(&result);
    if (transcript == nullptr) {
        return;
    }

    const std::vector<std::string> current_words = split_words(transcript->text);

    // Snapshot before this tick's mutations, so a tick that recognizes
    // nothing new (identical committed/tail text) can be skipped below
    // instead of re-publishing a redundant event.
    const std::string previous_committed_text = committed_text_;
    const std::string previous_tail_text = tail_text_;

    // First tick of a new segment (right after a force-commit; empty for
    // the session's very first segment, which has nothing carried over):
    // discard any leading words that are just a re-recognition of the
    // small kept-audio overlap from the previous segment's boundary,
    // before they can be committed a second time.
    if (committed_word_count_ == 0 && previous_words_.empty() && !carried_words_.empty()) {
        committed_word_count_ = overlap_prefix_length(carried_words_, current_words);
        carried_words_.clear();
    }

    const auto window_duration = std::chrono::milliseconds{
        static_cast<std::int64_t>(window.size()) * 1000 / audio::kSampleRate};
    const bool force_commit =
        window_duration >= config_.max_segment_duration ||
        has_trailing_silence(window, config_.vad_trailing_duration, config_.vad_threshold);

    if (force_commit) {
        commit(current_words, committed_word_count_, current_words.size());
        tail_text_.clear();
        // Remembered so the next segment's first tick can recognize and
        // discard a leading re-recognition of the kept-audio overlap
        // below, instead of committing it a second time.
        carried_words_ = current_words;

        const auto keep_samples =
            static_cast<std::size_t>(config_.keep_duration.count()) * audio::kSampleRate / 1000;
        committed_sample_count_ +=
            window.size() > keep_samples ? window.size() - keep_samples : window.size();
        committed_word_count_ = 0;
        previous_words_.clear();
    } else {
        const std::size_t agreement = common_prefix_length(previous_words_, current_words);
        // Never commit the very last recognized word by agreement alone:
        // it's the one most likely to still change as more audio arrives.
        const std::size_t safe_upper =
            current_words.empty() ? 0 : std::min(agreement, current_words.size() - 1);

        if (safe_upper > committed_word_count_) {
            commit(current_words, committed_word_count_, safe_upper);
            committed_word_count_ = safe_upper;
        }

        tail_text_ = join_words(current_words, committed_word_count_);
        previous_words_ = current_words;
    }

    if (committed_text_ == previous_committed_text && tail_text_ == previous_tail_text) {
        return;
    }

    event_bus_.publish(TranscriptionPartial{
        .session_id = *session_id,
        .committed_text = committed_text_,
        .tail_text = tail_text_,
    });
}

void LiveTranscriber::reset() {
    last_tick_at_.reset();
    committed_sample_count_ = 0;
    committed_word_count_ = 0;
    committed_text_.clear();
    tail_text_.clear();
    previous_words_.clear();
    carried_words_.clear();
}

} // namespace yoru::session
