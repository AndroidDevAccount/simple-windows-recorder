# Acoustic calibration report — 2026-09-29

## Setup

- Output: `Speakers (2- Realtek(R) Audio)`
- Input: laptop microphone array selected by the prototype
- Sample rate requested: 48 kHz
- Buffer size requested: 256 samples
- Driver-reported combined latency: 768 samples (16 ms at 48 kHz)
- Pattern: seven tapered 1.8 kHz clicks with irregular spacing
- Capture duration: 4.2 seconds per attempt

## Results

| Attempt | Result | Round trip | MAD scatter | Confidence |
|---:|---|---:|---:|---:|
| 1 | Rejected | 193.69 ms | 24.33 ms | 52% |
| 2 | Rejected | 190.96 ms | 11.06 ms | 52% |
| 3 | Rejected | 182.21 ms | 9.27 ms | 61% |

The audio path worked on every attempt, but the detected offsets were not stable
enough to use for overdub placement. The descending estimate and high scatter
are consistent with the simple per-click onset detector choosing different room
echoes or noise peaks.

## Next calibration revision

1. Correlate the complete irregular click pattern instead of independently
   choosing the largest novelty peak in each search window.
2. Use normalized FFT cross-correlation and fit one offset plus clock-drift slope
   across all clicks.
3. Gate candidate onsets by local signal-to-noise ratio and expected spacing.
4. Save the raw calibration capture when a diagnostic flag is enabled.
5. Add a speaker-volume check and try a swept/chirped pulse whose autocorrelation
   peak is sharper after laptop speaker and microphone filtering.

The current prototype correctly refuses to turn these measurements into a
compensation value.
