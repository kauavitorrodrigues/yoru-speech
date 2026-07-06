# Dictation in Hyprland

`yoru-dictate` sends `start_recording`/`stop_recording` to the running
daemon over its IPC socket (the transcript is copied to the clipboard
automatically by the daemon's `auto_clipboard` setting).

## Toggle (recommended)

One press starts recording; press the same bind again to stop and
transcribe. This only needs a plain `bind` (key press), not a release bind:

```conf
bind = $mainMod, D, exec, yoru-dictate toggle
```

Then reload Hyprland: `hyprctl reload`.

`toggle` tracks state itself via a marker file, so it works even when the
compositor's release event isn't delivered reliably for a given key or
combo — which is why this is the default over push-to-talk below.

## Push-to-talk (hold/release)

If your setup delivers key-release events reliably, you can instead bind a
press and a release so holding the key records and releasing it stops and
transcribes:

```conf
bind = , Control_L, exec, yoru-dictate start
bindr = , Control_L, exec, yoru-dictate stop
```

`bind` fires on key press, `bindr` on release, with no minimum hold
duration of their own — `yoru-dictate` cancels the session itself instead
of transcribing if the key was held for less than ~150ms, so an accidental
tap doesn't produce a stray transcript or overwrite the clipboard.

Binding directly to `Control_L` means every other shortcut that uses Ctrl
(copy, paste, etc.) also triggers a very short start/stop cycle, which the
150ms guard absorbs. If you'd rather avoid that, bind a less contested key
instead.

Known pitfall: on some Hyprland versions/setups, `bindr` simply doesn't
fire on release — for any key, not just modifier/lock keys like `Shift_L`
or `Caps_Lock` — leaving the daemon stuck recording until you run
`yoru-dictate stop` manually. If that happens, use `toggle` instead.

If `notify-send` is installed, `yoru-dictate` also shows a notification
with the transcribed text (or the error, if something failed).
