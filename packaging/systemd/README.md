# Running Yoru Speech as a systemd user service

1. Build the project (see the top-level build instructions) and copy the
   resulting binary somewhere on your `PATH`, matching the path this unit
   file expects:

   ```sh
   mkdir -p ~/.local/bin
   cp build/yoru-speech ~/.local/bin/yoru-speech
   ```

2. Install the unit file:

   ```sh
   mkdir -p ~/.config/systemd/user
   cp packaging/systemd/yoru-speech.service ~/.config/systemd/user/
   systemctl --user daemon-reload
   ```

3. Enable and start it:

   ```sh
   systemctl --user enable --now yoru-speech
   ```

4. Check status and logs:

   ```sh
   systemctl --user status yoru-speech
   journalctl --user -u yoru-speech -f
   ```

After editing `~/.local/bin/yoru-speech` (a new build), restart the service
with `systemctl --user restart yoru-speech`.

## Clipboard not working after a fresh boot

`systemd --user` starts this service as soon as `default.target` is reached at
login, which on most setups happens *before* the compositor has exported
`WAYLAND_DISPLAY` into the systemd user manager's environment. The daemon then
runs without that variable for its whole lifetime (services don't pick up
later env changes), so `wl-copy` fails every time with exit status 1 — logged
as `clipboard: wl-copy exited with status 1` — even though `WAYLAND_DISPLAY`
looks present when you check manually afterwards.

This unit's `After=graphical-session.target` ordering does not fix this on
its own: `graphical-session.target` is typically reached at login
independently of the compositor (e.g. via logind), so the ordering is
satisfied before the compositor imports anything. Don't "fix" this by adding
`Wants=graphical-session.target` here — pulling the target as a dependency
from this unit caused a Hyprland black-screen on start on at least one setup,
likely by making systemd activate/reach the target out of the sequence the
compositor and session manager expect.

The fix has to happen on the compositor side: import the relevant variables
into the systemd user manager, then (re)start this service, as part of your
compositor's own startup (e.g. `exec-once` in `hyprland.conf`):

```sh
systemctl --user import-environment WAYLAND_DISPLAY XDG_CURRENT_DESKTOP HYPRLAND_INSTANCE_SIGNATURE
systemctl --user restart yoru-speech.service
```

We intentionally don't ship this as part of installation: it edits the
compositor's own config, and doing so silently caused a black-screen issue on
at least one Hyprland setup. Add it to your compositor's autostart yourself,
adjusting the variable list for your compositor (sway, etc. export different
ones), and verify it doesn't interfere with your session before relying on
it.

## Crash-looping after a `whisper-cpp`/`ggml` package update

Arch's `ggml` package moved the CPU/Vulkan compute backends out of versioned
libraries in `/usr/lib` and into runtime-loaded plugins under `/usr/lib/ggml/`
(e.g. `libggml-cpu-*.so`, `libggml-vulkan.so`). Two symptoms follow from a
`ggml`/`whisper-cpp` update, in order:

- A binary built before this change fails to even start:
  `error while loading shared libraries: libggml-cpu.so.0: cannot open shared
  object file: No such file or directory` (`systemctl --user status` shows
  `status=127`). Fix: rebuild (`./install.sh` or `cmake --build build --target
  yoru-speech`) against the current packages — a fresh build no longer links
  those libraries directly.
- After that rebuild, the daemon instead aborts while loading the model:
  `/build/ggml/src/ggml/src/ggml-backend.cpp:606: GGML_ASSERT(device) failed`,
  with `devices = 0` / `backends = 0` logged just above it. This is because
  `whisper.cpp` no longer loads its own backend plugins — callers must call
  `ggml_backend_load_all()` themselves before initializing a model. This is
  handled in `WhisperBackend`'s constructor (`src/domains/speech/
  whisper_backend.cpp`); if you see this assert, you're running a binary
  older than that fix and need to rebuild.
