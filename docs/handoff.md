# Engineering handoff

## Repository state

- Language/framework: C++20, JUCE 9, CMake.
- Licence intent: AGPL-3.0-or-later, including the ASIO integration path.
- First target: `SimpleRecorder`, a Windows desktop latency-calibration proof.
- JUCE is pinned as the `vendor/JUCE` Git submodule.
- Windows CI builds the release executable on every push and pull request.

## Prototype behavior

The prototype enumerates plausible same-family device pairs, preferring the
laptop microphone array and speakers for the requested acoustic test. It emits
seven low-level, non-periodically spaced 1.8 kHz tapered clicks, records 4.2
seconds, finds onset-energy peaks, and reports median round-trip latency,
median-absolute-deviation scatter, and signal confidence.

This is an experiment, not yet production compensation. The next iteration
should extract calibration DSP into a separately tested library and replace the
peak-per-window detector with full-pattern normalized cross-correlation plus
robust model fitting. Persist a successful result by device IDs, backend,
sample rate, buffer size, and Windows audio-processing mode.

## Known issues and immediate work

1. Validate that JUCE exposes the laptop mic/speaker names as expected on this
   machine; the combo box permits choosing a Focusrite pair if not.
2. Run two successful acoustic passes and compare offsets. A result with more
   than 2 ms scatter is intentionally rejected.
3. Add a wired loopback test path for laboratory validation.
4. Separate UI, device service, calibration DSP, and persistence into modules.
5. Add simulated impulse/echo/noise/drift test fixtures before using the result
   to place recorded clips.
6. Implement a real-time-safe recording FIFO and a disk-writer thread.
7. Add structured diagnostics: backend, endpoint IDs, buffer size, sample rate,
   driver latencies, xruns, measured correction, and timestamp.

## Definition of done for Slice 0

- Ten repeated overdubs align within 1 ms after compensation.
- No xruns during a 20-minute capture while resizing and scrolling the UI.
- Device change/restart and failed calibration paths are understandable.
- The source recording is never shifted or rewritten; compensation is timeline
  metadata.

## Build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

Executable:
`build/SimpleRecorder_artefacts/Release/Simple Recorder.exe`
