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

## Version 0.2: try a punch-in

Arm the intended track with REC and choose its input. Click the timeline or
double-click the clock and enter `00:30.00`. Press Record (R), then Stop
(Space). Return (Enter) takes you back to the punch start. Recording replaces
only the recorded span, never appends; Undo restores the old arrangement.
Use Scarlett direct monitoring to hear yourself. Sessions autosave in
Documents/Simple Recorder Sessions; keep each project's Media folder with it.

See [current features, tests and limitations](docs/multitrack-handoff.md).
Loop recording, crash recovery, MIDI, drums and Genesis composition
are not implemented yet. Auto level is currently an optional clip action.

## Version 0.3: simple track effects

Each track now has a preset selector: Lead vocal, Warm vocal, Acoustic guitar,
Bass, Gentle cleanup, or Dry. Choose one while stopped; press **?** beside it
to see the exact filters, EQ and compression settings with an explanation.
Uncheck **FX** to compare without losing your chosen preset.

Effects are nondestructive and included in playback and WAV export. They do
not change your input recording, calibration or Scarlett direct monitoring.
Presets/bypass save with the project and support undo. Existing tracks load
dry by default. There is no lookahead or added block-buffering latency.
See [effects implementation and test notes](docs/track-effects.md).

## Version 0.4: independent reverb

Enable **Verb** on a track, choose **Spring**, **Hall** or **Flerb**, and adjust
**Mix** (0% dry, 100% wet). The second **?** explains the sound and controls.
This is a Holy Grail-inspired effect, not an exact EHX emulation. It follows
the EQ/compressor but has its own on/off switch. Everything is nondestructive,
saved with the project and included in export, including the decaying tail.
Stop first before changing effects. See [reverb notes](docs/reverb.md).

## Planned scope (not all implemented)

- Windows 10/11
- Focusrite Scarlett and built-in audio devices
- Mono/stereo audio tracks, unlimited in principle
- Metronome, count-in, loop recording, mute/solo, trim, split, move, undo
- Guided latency calibration and automatic placement compensation
- Guided input-level check and automatic non-destructive playback leveling
- WAV import/export and project autosave/recovery

The Genesis composer is planned as a separate executable sharing a small
sequencer/audio-core library; see the architecture record for why.
