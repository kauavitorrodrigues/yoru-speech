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
