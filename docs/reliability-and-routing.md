# Take One 0.6: routing and diagnostics

Simple Recorder is now **Take One**, with a teal waveform/red record-dot Windows
icon. Existing project formats, Media folders and preference locations remain
unchanged; there is no migration or destructive rewrite of audio.

## Recording inputs

The transport's Default input selector saves the physical recording channel in
Workspace.settings. New tracks follow it. Track menus identify the channel and
device; Default follows the global setting, while explicit selections override
it. Existing projects retain their explicit routing. **Tracks to default**
switches all existing tracks to the default and is undoable. Track meters and
recording use the same resolved channel. The note monitor remains independent.

The old Mix slider was reverb wet/dry, not an input mix. It is now labelled
Reverb and disabled when reverb is off. No input-summing mode is needed.

## Device recovery

Startup enumerates drivers without first opening their default device, then
requests the exact saved backend/input/output. A missing saved device or failed
open is reported; another driver is not silently substituted. A mono retry uses
the same endpoints. In particular, a saved Focusrite USB ASIO configuration must
not first open Focusrite Thunderbolt ASIO. Fake-driver regression tests cover
this distinction without accessing hardware.

Audio offline is shown in the main window, with transport disabled. Open Audio
setup to select devices or use Retry audio to reopen the saved profile. Close
other recorder instances before retrying: ASIO drivers may reject competing
clients. The screenshot's Thunderbolt error is evidence of a wrong driver
selection, not proof that Windows requires a reboot. Physical USB recovery still
needs testing with the user's Scarlett.

## Developer reports and failure boundaries

Diagnostics opens a live, selectable log with Copy report and Open log folder.
Logs include actual device-open errors, enumerated devices, requested setup,
active sample rate/buffer/channel masks, status and driver-error callbacks.
Persistent file: `%APPDATA%/SimpleWindowsRecorder/Logs/take-one.log`.
The file is bounded to approximately 2 MiB on startup; the viewer displays its
last 120,000 characters. Reports can contain local file and device names.

Recoverable audio failures stop transport and preserve the error for inspection.
JUCE-dispatched C++ exceptions disable transport and prompt a save/restart.
A Windows unhandled-exception/terminate handler attempts to append a code/address
marker to sibling `crash.txt` and show a fatal-error message. This is not a stack
trace, minidump, crash-recovery system or guarantee against driver/heap corruption.
Fatal failures still terminate the process and may lose an in-flight take.
Existing source WAVs are never intentionally rewritten by these handlers.

Continuous audio callbacks do not write log files; the UI poll consumes a bounded
driver-error message buffer. Preview and self-tests never open physical devices.

## Verification and handoff (2026-09-30)

Release build and `--self-test` pass: seven engine regression groups, 17 audio
preference checks, project/export, effects, reverb, pitch/metronome and UI checks.
New coverage records two default-routed tracks alongside an explicit override
and checks their actual captured samples; a simulated 0x54f error remains
available and blocks playback. The preview screenshot was inspected for layout.
The native icon is embedded by JUCE's generated Windows resource script.

The user had Take One running, so this verified build was linked separately at
`build/verified/Take One.exe`, without terminating their process. Close the old
instance before opening it. Normal clean builds still output
`build/effects/SimpleRecorder_artefacts/Release/Take One.exe`.
Hardware reconnect, fatal-handler invocation and taskbar appearance were not
tested by interrupting the user's active app. Next check: launch the verified
build alone, confirm Focusrite USB ASIO, choose the mic's default channel, use
Tracks to default if desired, and record a short take. If audio is offline,
Retry audio then share Diagnostics > Copy report.

Version 0.6.1 holds the tuner's last valid note for 1.8 seconds. Low-confidence
or silent analysis frames no longer erase a good reading immediately; the text
and cents marker fade during the final 0.6 seconds, then clear. Changing inputs
or losing the audio device still clears the reading immediately.

Version 0.6.2 fixes the saved waveform's obsolete 24-pixel vertical scale and
uses most of each track lane. Every track shows red +1/-1 (0 dBFS) reference
lines and the visible raw/pre-FX peak in dBFS. Its labelled nondestructive Gain
control is -60 to +12 dB; waveform height follows the same clip and track gain
used for playback. Samples beyond the references turn red. These references are
per-track and pre-FX: presets, reverb and summing multiple tracks can still raise
the final output, so they are not a replacement for a future master-output meter.

Version 0.8.1 replaces that pre-FX approximation with a cached nondestructive
preview of the actual preset, reverb, Gain and independent Peak tamer chain.
The raw-input and audible-output peaks are labelled separately, Gain extends to
+24 dB, and the source recording remains unchanged. File is upper-left; Hand,
Split and Delete are persistent tools; Ctrl+Z/Ctrl+Y provide multi-level history.
