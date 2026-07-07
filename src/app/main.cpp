#include "core/error_event.hpp"
#include "core/event_bus.hpp"
#include "core/logger.hpp"
#include "core/session_id.hpp"
#include "domains/audio/events.hpp"
#include "domains/audio/recording_manager.hpp"
#include "domains/clipboard/auto_clipboard.hpp"
#include "domains/config/configuration_manager.hpp"
#include "domains/config/events.hpp"
#include "domains/ipc/ipc_service.hpp"
#include "domains/session/events.hpp"
#include "domains/session/live_transcriber.hpp"
#include "domains/session/session_manager.hpp"
#include "domains/speech/events.hpp"
#include "domains/speech/model_repository.hpp"
#include "domains/speech/whisper_backend.hpp"

#include <csignal>
#include <cstdint>
#include <string>

namespace {

// Set by the signal handler, read by the run loop. sig_atomic_t is the
// only type the C standard guarantees safe to touch from a signal
// handler; a plain bool or std::atomic<bool> is not guaranteed async-
// signal-safe to write to in this context.
volatile std::sig_atomic_t g_should_stop = 0;

void handle_stop_signal(int /*signal*/) {
    g_should_stop = 1;
}

std::string session_id_str(yoru::core::SessionId session_id) {
    return std::to_string(static_cast<std::uint64_t>(session_id));
}

// Wires up logging for every operationally significant domain event, so
// the journal tells a complete story of what the service is doing without
// needing an IPC client subscribed to events. Each domain publishes facts
// about itself (recording, transcription, model, configuration, session
// lifecycle); this is the composition root's job of making them visible,
// not any individual domain's.
void log_domain_events(yoru::core::EventBus& event_bus, const yoru::core::Logger& logger) {
    event_bus.subscribe<yoru::audio::RecordingStarted>(
        [&logger](const yoru::audio::RecordingStarted& event) {
            logger.info("recording started (session " + session_id_str(event.session_id) + ")");
        });
    event_bus.subscribe<yoru::audio::RecordingFinished>(
        [&logger](const yoru::audio::RecordingFinished& event) {
            logger.info("recording finished (session " + session_id_str(event.session_id) + ", " +
                        std::to_string(event.recording.duration().count()) + "ms)");
        });
    event_bus.subscribe<yoru::speech::TranscriptionStarted>(
        [&logger](const yoru::speech::TranscriptionStarted& event) {
            logger.info("transcription started (session " + session_id_str(event.session_id) + ")");
        });
    event_bus.subscribe<yoru::speech::TranscriptionCompleted>(
        [&logger](const yoru::speech::TranscriptionCompleted& event) {
            logger.info("transcription completed (session " + session_id_str(event.session_id) +
                        ", language " + event.transcript.detected_language + ", " +
                        std::to_string(event.transcript.processing_time.count()) + "ms)");
        });
    // debug, not info: published on every Live Transcriber tick while a
    // session is Recording (roughly once per second), which would
    // otherwise flood the journal during ordinary dictation.
    event_bus.subscribe<yoru::session::TranscriptionPartial>(
        [&logger](const yoru::session::TranscriptionPartial& event) {
            logger.debug("transcription partial (session " + session_id_str(event.session_id) +
                        "): committed=\"" + event.committed_text + "\" tail=\"" +
                        event.tail_text + "\"");
        });
    event_bus.subscribe<yoru::speech::ModelLoaded>(
        [&logger](const yoru::speech::ModelLoaded& event) {
            logger.info("model loaded: " + event.model.name);
        });
    event_bus.subscribe<yoru::session::SessionCancelled>(
        [&logger](const yoru::session::SessionCancelled& event) {
            logger.info("session cancelled (session " + session_id_str(event.session_id) + ")");
        });
    event_bus.subscribe<yoru::config::ConfigurationChanged>(
        [&logger](const yoru::config::ConfigurationChanged& event) {
            logger.info("configuration updated (language " + event.configuration.default_language +
                        ", model " + event.configuration.selected_model + ", auto_clipboard " +
                        (event.configuration.auto_clipboard ? "on" : "off") + ")");
        });
}

// Loads the model named by `configuration.selected_model`, if one by
// that name exists in `models_directory`. A missing model or a load
// failure are both logged and left as-is: transcription will simply
// fail with a clear error until the user places a model and/or fixes
// the configuration, rather than refusing to start the whole service
// over it. Runtime model selection (Fase 10) will replace this fixed,
// startup-only attempt. Success is not logged here: load_model()
// publishes ModelLoaded, which log_domain_events() already logs.
void load_configured_model(yoru::speech::WhisperBackend& backend,
                           const yoru::config::Configuration& configuration,
                           const std::filesystem::path& models_directory,
                           const yoru::core::Logger& logger) {
    const auto available = yoru::speech::list_available_models(models_directory);
    const auto model = yoru::speech::find_model(available, configuration.selected_model);
    if (!model.has_value()) {
        logger.warn("configured model \"" + configuration.selected_model + "\" not found in " +
                    models_directory.string() + "; transcription will fail until one is available");
        return;
    }

    if (const auto error = backend.load_model(model.value())) {
        logger.warn("failed to load model \"" + configuration.selected_model +
                    "\": " + error->message);
    }
}

} // namespace

int main() {
    const yoru::core::Logger logger("main");
    logger.info("Yoru Speech started");

    yoru::core::EventBus event_bus;

    // Logs any fact the rest of the system reports as an error, so a
    // failure is visible even when no IPC client happens to be
    // subscribed to events. A full audit of what logs what is Fase 9's
    // job; this is the minimal wiring that makes today's ErrorOccurred
    // publishers (WhisperBackend, AutoClipboard) actually observable.
    event_bus.subscribe<yoru::core::ErrorOccurred>(
        [&logger](const yoru::core::ErrorOccurred& error) {
            logger.warn(error.component + ": " + error.message);
        });
    log_domain_events(event_bus, logger);

    yoru::config::ConfigurationManager configuration_manager(yoru::config::default_config_path(),
                                                             event_bus);
    for (const auto& error : configuration_manager.load()) {
        logger.warn("config: " + error.field + ": " + error.message);
    }

    yoru::audio::RecordingManager recording_manager(event_bus);
    yoru::speech::WhisperBackend speech_backend(event_bus);
    load_configured_model(speech_backend, configuration_manager.current(),
                          yoru::speech::default_models_path(), logger);

    yoru::session::SessionManager session_manager(event_bus, recording_manager, speech_backend,
                                                  configuration_manager.current());
    yoru::session::LiveTranscriber live_transcriber(event_bus, session_manager, recording_manager,
                                                     speech_backend, configuration_manager.current());
    yoru::clipboard::AutoClipboard auto_clipboard(event_bus, configuration_manager.current());

    yoru::ipc::IpcService ipc_service(event_bus, session_manager, configuration_manager);
    if (const auto error = ipc_service.start()) {
        logger.error("failed to start IPC server: " + error->message);
        return 1;
    }
    logger.info("listening on " + yoru::ipc::default_socket_path().string());

    std::signal(SIGINT, handle_stop_signal);
    std::signal(SIGTERM, handle_stop_signal);
    // Writing to a subprocess's stdin pipe (WlClipboardAdapter) after it
    // has closed its read end (e.g. it exited before reading) raises
    // SIGPIPE, whose default disposition kills the process. A local
    // subprocess's failure must never be able to take the whole daemon
    // down with it; the write() call already treats a failed write as an
    // ordinary error value once the signal itself can't terminate us.
    std::signal(SIGPIPE, SIG_IGN);

    while (g_should_stop == 0) {
        ipc_service.poll_once(200);
        live_transcriber.tick();
    }

    logger.info("shutting down");
    if (session_manager.state() != yoru::session::ServiceState::Idle) {
        session_manager.cancel_session();
    }
    ipc_service.stop();

    return 0;
}
