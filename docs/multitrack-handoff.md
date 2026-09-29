# Multitrack 0.2 handoff — 2026-09-29

This supersedes the original calibration-only handoff. C++20, pinned JUCE 9,
CMake; AGPL-3.0-or-later. Architecture.md remains a roadmap, not shipped scope.

## Implemented

- StudioComponent: stacked tracks/horizontal timeline, arm/input selection,
  mute/solo/volume, transport, tempo/metronome, one-bar count-in, editable clock,
  clip move/trim/split/delete, optional Auto level, 30-step session undo/redo.
- SessionEngine: immutable playback snapshot, preallocated recording FIFO,
  background WAV writer, mono file per armed physical input, multi-arm capture.
  No software input monitoring: use Scarlett direct monitoring.
- Punch starts at the playhead, never the previous clip end. Overlapping old
  clips are split nondestructively. Undo restores the previous arrangement.
  Compensation is a source offset, not destructive WAV editing. Stop captures
  the remaining delay tail silently before finalizing. Device loss preserves
  the recoverable part instead of claiming a missing tail was captured.
- ProjectStore: relative Media paths, atomic JSON project replacement, import,
  background stereo 24-bit WAV export respecting source offsets, mute/solo,
  levels, and short edge fades.
- AudioPreferences/CalibrationPanel: saved backend/devices, input meter,
  speaker test, seven-click acoustic calibration and aligned waveforms.
  New calibration profiles bind rate, buffer and active channel masks too.

## User audio setup

Focusrite USB ASIO for both input/output. User measured approximately 33.8 ms
acoustically versus approximately 284 ms in Windows Audio. Legacy preferences
are imported provisionally for matching devices; verify a real overdub before
treating the correction as proven. Acoustic delay includes output/input and
speaker-to-microphone travel, not input latency alone. Compensation aligns
recorded clips; it cannot remove live monitoring delay. Do not subtract the
driver latency a second time.

Settings: %APPDATA%/SimpleWindowsRecorder/SimpleRecorder.settings.
Last project: sibling Workspace.settings. Sessions default to the Windows
Documents folder under Simple Recorder Sessions, each with Song.srproject
and Media. Autosave runs idle and after completed takes. Undo/delete do not
delete source WAVs. Keep project and Media together when moving/backing up.

## Using punch-in

Arm REC on the intended track and select its input. On Scarlett Solo, Input 1
is the XLR microphone and Input 2 the instrument jack. Click the timeline or
double-click the clock and enter 00:30.00. Record (R) starts there after the
optional four-beat count-in. Stop (Space) finishes; Return (Enter) goes back
to the punch start. Undo restores overlapping material. Unarmed tracks play
underneath. The count-in currently has no backing-track pre-roll.

## Verification

Release build and direct --self-test passed locally; report is test-results.txt
in the process working directory. Close the normal app first (single instance).

- Six engine groups: 30-second replacement and retained sides; simultaneous
  inputs, compensation tail and disk output; zero-position compensation;
  cancelled count-in; exact count-in boundary; device-loss truncation.
- Fourteen preference tests: backend restoration and calibration identity.
- Three project groups: round trip; exported sample positions/mute/solo;
  missing-media load leaves the current session untouched.

--preview --snapshot creates a synthetic screenshot without opening devices.
This checks layout, not physical recording. Real overdub and long-session
stress tests remain. CTest stalled once here; direct process execution worked.

## Limits / next work

- Full clip PCM is decoded into RAM; waveform drawing scans PCM. Add streaming
  playback and cached multiresolution peaks before large projects.
- Capture caps at 30 minutes / 256 MiB across armed channels, whichever comes
  first; maximum 32 armed tracks. Import also has a memory limit.
- Track controls lock during transport. No live faders, take lanes, looping,
  effects, MIDI, generated drums, Genesis composer or backing pre-roll yet.
- Auto level is an explicit clip action, not automatic analog gain control.
- Autosave is not crash recovery. Add capture journal, orphan-take recovery,
  missing-media relinking and disk-full/device-unplug stress tests.
- Calibration still needs synthetic noise/echo/drift tests and wired loopback
  validation. Consistent clicks alone do not prove actual overdub alignment.
- Check keyboard focus and display scaling. Nonfatal JUCE deprecation/shadow
  warnings remain. Legacy src/Main.cpp is uncompiled historical prototype.

## Build

Use the README CMake commands. This machine builds in build/local. Executable:
build/local/SimpleRecorder_artefacts/Release/Simple Recorder.exe.
Run --self-test through Start-Process -PassThru, wait for exit, inspect ExitCode
and test-results.txt. Close the app before rebuilding.
