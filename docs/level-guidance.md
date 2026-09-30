# Level guidance and visual waveform model

Take One 0.7 separates three questions that are often accidentally conflated:

1. **Was the analog input recorded well?** This is judged from the untouched
   WAV. Track/clip gain and effects do not influence the answer.
2. **Where should the instrument sit in this song?** Track Gain, presets and
   reverb answer this. Turning up a Scarlett preamp is not a mix-balance tool.
3. **Is the combined processed song competitively loud without clipping?**
   Master and Mix check answer this after tracks, effects and reverb are summed.

## Waveforms

The line shape uses a sign-preserving square-root display curve below full
scale. This deliberately makes ordinary musical detail readable: -12 dBFS is
shown at half height rather than one-quarter height. Exactly 0 dBFS still lands
on the red reference, and over-range samples extend beyond it and turn red.
The displayed shape is therefore perceptual, not a linear oscilloscope ruler.
The numeric raw and after-Gain dBFS readouts remain the authoritative values.

## Input-health guidance

After recording, the source segment is scanned in 50 ms windows. Take One shows
raw sample peak and an active-window RMS estimate, excluding very quiet gaps.
The generic peak bands are:

- below -24 dBFS: Quiet input; raise the interface preamp on the next take;
- -24 to -18 dBFS: Safe, slightly quiet; usable, raise hardware gain only if
  noise is objectionable;
- -18 to -3 dBFS: Healthy input;
- -3 to -0.5 dBFS: Hot; reduce hardware gain for unexpected peaks;
- -0.5 dBFS and above: Clipping risk; lower hardware gain and re-record.

These are deliberately broad engineering bands, not claims that every singer,
pickup or drum has one correct loudness. A clean 24-bit take with headroom is
preferable to chasing the red line. Software Gain can fix balance but cannot
undo analog clipping or improve the recorded signal-to-noise ratio.

## Mix check and Master

Mix check renders the same track, clip, preset, reverb, mute/solo and Master
settings used for export. It reports gated integrated loudness using 400 ms
K-weighted blocks (BS.1770-style) and the digital sample peak. A -14 LUFS target
and -1 dBFS sample-peak ceiling produce a conservative suggested Master change,
limited to the control's -24 to +12 dB range. The suggestion is not applied
automatically and is not a limiter or mastering algorithm.

The peak is a sample peak, not an oversampled true peak; lossy encoding can
create inter-sample overs. Mix check also cannot decide whether the vocal is
artistically prominent enough. It reports overall level and headroom while the
musician retains the balance decision.
