# Yoru Speech

<div align="center">

### An offline voice dictation daemon for Linux

Part of the [Yoru](https://github.com/kauavitorrodrigues/yoru) ecosystem

[![License](https://img.shields.io/badge/license-MIT-9ccbfb?style=for-the-badge&labelColor=101418&color=FFFFFF)](LICENSE)

</div>

Yoru Speech is a background service that turns speech into text entirely on your machine, no cloud, no network calls. It records from your microphone on demand, transcribes with [whisper.cpp](https://github.com/ggerganov/whisper.cpp), and copies the result straight to your clipboard. A small Unix-socket IPC protocol lets any client (a keybind, a script, a shell widget) start, stop, and configure it.

## How it works

1. A client sends `start_recording` over the daemon's Unix socket.
2. The daemon captures microphone audio until it receives `stop_recording` (or `cancel_session` to discard it). While recording, it also periodically re-transcribes what's been captured so far and pushes `transcription_partial` events to subscribed clients, so text can be shown live as the user speaks.
3. The recording is transcribed offline by a local Whisper model.
4. The transcript is returned to the client and, if `auto_clipboard` is enabled, copied to the clipboard automatically.

## Repository Structure

```
yoru-speech/
├── src/
│   ├── app/              # Composition root (main.cpp)
│   ├── core/              # Cross-cutting infrastructure (logger, event bus, error events)
│   └── domains/           # Business domains
│       ├── audio/         # Microphone capture and recording lifecycle
│       ├── speech/        # Whisper backend, model repository, transcripts
│       ├── session/       # Recording/transcription session state machine
│       ├── ipc/           # Unix-socket server, command dispatch, event bridge
│       ├── config/        # TOML-backed persisted configuration
│       └── clipboard/     # Automatic clipboard integration (wl-clipboard)
├── tests/                 # Unit and end-to-end tests, mirroring src/ layout
├── packaging/
│   ├── systemd/           # systemd user service unit
│   └── hyprland/          # yoru-dictate helper for toggle / push-to-talk binds
└── install.sh              # Idempotent install script (Arch Linux)
```

## Features

**Fully offline**
Speech recognition runs locally via whisper.cpp. No audio or text ever leaves the machine.

**IPC-driven, client-agnostic**
Any process that can open a Unix socket can drive the daemon: `start_recording`, `stop_recording`, `cancel_session`, `get_config`, `set_config`. The bundled `yoru-dictate` script is one such client, built for Hyprland keybinds.

**Automatic clipboard**
Transcripts are copied to the clipboard as soon as they're ready, so dictation fits into any text field without extra steps.

**Configurable model loading**
Choose between loading the recognition model on demand (lower idle memory) or keeping it always loaded (lower per-session latency) via configuration.

**Live transcription preview**
Partial results are streamed as `transcription_partial` events while recording is still in progress, instead of only once at the end.

**Mixed-language dictation**
An optional `transcription_prompt` conditions the model with an example of the vocabulary you actually dictate (e.g. mostly-Portuguese speech with embedded English technical terms), helping it avoid collapsing mixed-language audio into a single language.

**Resilient by design**
Handles microphone disconnects, socket takeover, and filesystem errors without taking the whole service down.

## Installation

> **Arch Linux only** for now.

Clone the repo and run the install script:

```bash
git clone https://github.com/kauavitorrodrigues/yoru-speech
cd yoru-speech
./install.sh
```

The script will:

1. Install build dependencies via `pacman` (and a starter recognition model via `yay`, if available)
2. Build the daemon in Release mode
3. Install it as a systemd user service
4. Set up the `yoru-dictate` helper for dictation keybinds

Then enable the service:

```bash
systemctl --user enable --now yoru-speech
```

See [`packaging/systemd/README.md`](packaging/systemd/README.md) for manual service setup and [`packaging/hyprland/README.md`](packaging/hyprland/README.md) for wiring up toggle or push-to-talk keybinds.

## Dependencies

The install script handles all of these automatically on Arch.

| Package | Source | Purpose |
|---------|--------|---------|
| `whisper-cpp-vulkan` | pacman | Offline speech recognition backend |
| `miniaudio` | pacman | Microphone capture |
| `tomlplusplus` | pacman | Configuration file parsing |
| `nlohmann-json` | pacman | IPC message encoding |
| `wl-clipboard` | pacman | Clipboard integration (Wayland) |
| `whisper.cpp-model-large-v3-turbo-q5_0` | AUR (`yay`) | Starter recognition model |

**Also required:**
- CMake 3.20+ and a C++20 compiler
- A Wayland compositor with `wl-clipboard` support (e.g. [Hyprland](https://hyprland.org/))

## Testing

The project has unit and end-to-end test coverage per domain under `tests/`, using [doctest](https://github.com/doctest/doctest):

```bash
cmake -B build -DYORU_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

## License

MIT. See [LICENSE](LICENSE) for the full text.
