# Take One 0.8 recording-workflow pass

## Requested behavior

- Add a nondestructive **Peak tamer** effect for isolated transients such as the
  screenshot's raw -10.8 dBFS peak. The raw take is healthy; adding 12 dB of
  track Gain produces the reported +1.2 dBFS overload. Peak tamer must reduce
  such peaks without rewriting the source WAV, and it must combine with a tonal
  preset such as Acoustic guitar rather than competing for the same menu.
- New tracks start with FX **off**. `Dry / no effects` is not shown in the preset
  menu. With no chosen preset the menu says `Choose preset…`; choosing one turns
  FX on. Existing projects retain their saved preset/bypass state.
- Give Gain a -60 to +24 dB slider and explicit -/+ buttons that nudge by 0.1 dB.
  Put the live input meter beside REC/M/S rather than consuming the Gain row.
- Clip editing is locked by default. Three mutually exclusive, persistent tools
  unlock one job at a time: **Hand** moves/trims with open/closed hand cursors;
  **Split** shows a vertical cut guide and remains active after every cut; and
  **Delete** shows a bomb cursor and removes the clicked clip from the timeline.
  Raw source files remain on disk.
- Ctrl+Z walks backward through multiple edit checkpoints and Ctrl+Y walks
  forward. Undo/Redo do not consume permanent toolbar space.
- File sits in the upper-left and contains New, Open, Save now, Import audio,
  Export WAV and Audio setup. Sessions continue to autosave. The track-add
  control is a compact `+` beside `TRACK / INPUT` in the timeline header.
- Compact the note monitor to `Tuner`: input selector, note name, In tune/Flat/
  Sharp and cents, with its graphic immediately beside the text. Do not show Hz,
  `A4 = 440 Hz`, or `Play or sing a note`.

## Safety and verification

- Peak tamer combines fast compression with a -1 dBFS zero-lookahead safety
  ceiling after track Gain and reverb, so raising Gain cannot reintroduce a
  per-track overload after the limiter. It is designed for occasional spikes, not restoration of an input
  that was already clipped by the interface. Playback/export must match across
  block sizes and the original buffer must remain bit-identical.
- The visible saved waveform is a cached render through the same preset,
  reverb, track Gain and Peak tamer chain used by playback/export. Controls
  invalidate the cache, so the shape and `audible` peak readout update while the
  source PCM remains unchanged. Reverb decay after a clip's boundary is audible
  but is not drawn beyond that boundary in this pass.
- Editing state is intentionally not persisted: every launch/project begins
  locked. UI regression checks cover locked drag, repeated split locations,
  source offsets, Hand/Delete cursors and multi-level Ctrl+Z/Ctrl+Y.
- File-menu actions retain existing autosave and missing-media protections.
