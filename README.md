# Simple Windows Recorder

A musician-first multitrack recorder for Windows: arm a track, count in, record,
and have the new take land in time. The product deliberately hides traditional
DAW complexity until it is needed.

The first engineering decision and proposed delivery plan are in
[`docs/architecture.md`](docs/architecture.md).

The first real acoustic test is recorded in
[`docs/calibration-report-2026-09-29.md`](docs/calibration-report-2026-09-29.md).

## Product promises

1. Recorded takes line up with what the musician heard.
2. The common path needs no mixer or audio-engine knowledge.
3. Recordings are never destructively altered.
4. Audio continues safely if drawing, saving, or analysis gets slow.
5. Advanced controls are available, but not placed in the recording path.

## Initial scope

- Windows 10/11
- Focusrite Scarlett and built-in audio devices
- Mono/stereo audio tracks, unlimited in principle
- Metronome, count-in, loop recording, mute/solo, trim, split, move, undo
- Guided latency calibration and automatic placement compensation
- Guided input-level check and automatic non-destructive playback leveling
- WAV import/export and project autosave/recovery

The Genesis composer is planned as a separate executable sharing a small
sequencer/audio-core library; see the architecture record for why.
