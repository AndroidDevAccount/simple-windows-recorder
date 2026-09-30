# Take One 0.8 recording-workflow pass

## Requested behavior

- Add a nondestructive **Peak tamer** effect for isolated transients such as the
  screenshot's raw -10.8 dBFS peak. The raw take is healthy; +12 dB track Gain
  produces the reported +1.2 dBFS pre-FX overload. Peak tamer must reduce such
  peaks without rewriting the source WAV.
- New tracks start with FX **off**. `Dry / no effects` is not shown in the preset
  menu. With no chosen preset the menu says `Choose preset…`; choosing one turns
  FX on. Existing projects retain their saved preset/bypass state.
- Give Gain a wider dB slider and explicit -/+ buttons that nudge by 0.1 dB.
  Put the live input meter beside REC/M/S rather than consuming the Gain row.
- Clip editing is locked by default. Clicking still seeks/selects and the
  playhead remains draggable, but clips cannot be moved, trimmed, split or
  deleted until **Edit clips** is enabled.
- Splitting is a mode, not a generic toolbar action: enable Edit clips, select
  **Razor**, hover a clip to get a razor cursor, and click the exact split point.
  Razor turns itself off after a successful split. Delete is also gated by Edit
  clips. Raw source files remain on disk and Undo restores edits.
- The everyday toolbar contains Track, Undo/Redo and edit-mode controls. New,
  Open, Save now, Import audio, Export WAV and Audio setup move under a File
  popup. Sessions continue to autosave; Save now is an explicit flush/status.
- Compact the note monitor to `Tuner`: input selector, note name, In tune/Flat/
  Sharp and cents, with its graphic immediately beside the text. Do not show Hz,
  `A4 = 440 Hz`, or `Play or sing a note`.

## Safety and verification

- Peak tamer combines fast compression with a -1 dBFS zero-lookahead safety
  ceiling after track Gain and reverb, so raising Gain cannot reintroduce a
  per-track overload after the limiter. It is designed for occasional spikes, not restoration of an input
  that was already clipped by the interface. Playback/export must match across
  block sizes and the original buffer must remain bit-identical.
- Editing state is intentionally not persisted: every launch/project begins
  locked. UI regression checks cover locked drag, razor split location, source
  offsets and Undo.
- File-menu actions retain existing autosave and missing-media protections.
