# Push-to-talk in Hyprland

`yoru-dictate` sends `start_recording`/`stop_recording` to the running
daemon over its IPC socket. Bind it to a key press and release so holding
the key records and releasing it stops and transcribes (the transcript is
copied to the clipboard automatically by the daemon's `auto_clipboard`
setting).

Add to `~/.config/hypr/hyprland.conf`:

```conf
bind = , Control_L, exec, yoru-dictate start
bindr = , Control_L, exec, yoru-dictate stop
```

Then reload Hyprland: `hyprctl reload`.

`bind` fires on key press, `bindr` on release, with no minimum hold
duration of their own — `yoru-dictate` cancels the session itself instead
of transcribing if the key was held for less than ~150ms, so an
accidental tap doesn't produce a stray transcript or overwrite the
clipboard.

Binding directly to `Control_L` means every other shortcut that uses Ctrl
(copy, paste, etc.) also triggers a very short start/stop cycle, which the
150ms guard absorbs. If you'd rather avoid that entirely, bind a less
contested key instead, e.g.:

```conf
bind = , Caps_Lock, exec, yoru-dictate start
bindr = , Caps_Lock, exec, yoru-dictate stop
```

If `notify-send` is installed, `yoru-dictate` also shows a notification
with the transcribed text (or the error, if something failed).
