# Track effects — version 0.3

Six choices: Dry, Lead vocal, Warm vocal, Acoustic guitar, Bass, Gentle cleanup.
Every processed preset uses a 12 dB/octave high-pass, two broad Q=0.8 bell EQ
bands, then a stereo-linked peak-envelope compressor. The track fader is after
effects; clip gain/Auto level is before them. There is no automatic makeup gain,
gate, reverb, lookahead or software monitoring. These are starting points, not
claims to repair clipped or noisy recordings or match every voice/instrument.

The per-track ? dialog describes the actual numerical settings directly from
the same preset table used by DSP. FX toggles bypass without losing the preset.
Changing a preset enables it. Controls lock during transport, consistent with
the existing model snapshot design. Original source audio is never altered.

## Implementation

- TrackEffects.h/.cpp owns stable preset IDs, explanations, filter/compressor
  state and shared dry clip rendering. Coefficients use JUCE's IIR formulas.
- Track has effectPresetId and effectsBypassed. Saved as optional version-1
  JSON fields; missing/unrecognized IDs load Dry. Session snapshots naturally
  preserve them for undo/redo. Old application builds ignore these fields, so
  saving a project in an older build loses the preset selections.
- SessionEngine prepares per-track processors before publishing playback state.
  Audio callback uses fixed 1024-sample stereo scratch arrays, chunking larger
  device callbacks. No allocation, locks or disk work added in processing.
- Export uses the same renderer and processors, reset at time zero. Transport
  start resets state at the chosen playhead (no historical preroll); starting
  midway through audio can have a brief filter/compressor settling difference
  versus uninterrupted playback from the beginning.
- No buffering/algorithmic delay is introduced. IIR filters still alter phase
  as expected from ordinary minimum-phase EQ. Calibration is not changed.
- Armed tracks remain excluded from backing playback during punch-in. Captured
  WAVs stay dry; the track preset applies when replaying the completed take.

## Regression coverage

EffectTests runs under --self-test alongside existing engine/project tests:
all presets process finite audio, dry/bypass exact, block-size independence,
stereo link preserves channel balance, impulse has no added buffering delay,
44.1/48/96 kHz stability, silence/reset, DC filtering, preset/bypass persistence,
legacy project default, sample-level engine/export agreement, unmodified source
and unchanged duration. Existing compensated punch-in tests still apply.
Human listening evaluation with the user's mic/guitar remains outstanding.

Verified locally: Release build, all existing regressions and all five effects
test groups passed; synthetic preview screenshot inspected. No physical mic
recording or speaker playback was performed for this feature.

The user was away during implementation. Their existing recorder was left
alone. The new executable is built separately at:
`build/effects/SimpleRecorder_artefacts/Release/Simple Recorder.exe`.
Close the older app before opening this one. Test/preview modes now allow a
second instance and do not open hardware audio devices.
