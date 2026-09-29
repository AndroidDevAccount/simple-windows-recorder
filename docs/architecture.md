# Architecture decision: low-latency Windows recorder

Status: proposed for prototype  
Date: 2026-09-29

## Decision

Build the recorder in **C++20 with JUCE 9 and CMake**, using a small custom
timeline/recording engine rather than a general DAW engine.

Audio backends, in preference order:

1. **ASIO** for dedicated interfaces such as the detected Scarlett Solo.
2. **WASAPI shared low-latency** for friendly built-in-device operation.
3. **WASAPI exclusive** as an advanced fallback when shared mode cannot reach a
   stable low period.

Default to 48 kHz, 32-bit floating-point internal audio, and the smallest buffer
that passes an automatic stability test. Do not promise a fixed buffer size;
64/128 samples is a target for the Scarlett, while laptop devices may require
more.

### Why this stack

- C++ offers deterministic ownership and lets the audio callback avoid garbage
  collection, locks, allocation, logging, and filesystem access.
- JUCE already provides Windows ASIO/WASAPI device discovery, callbacks, sample
  conversion, MIDI, file I/O, DSP primitives, and a native desktop UI. This is
  substantially less integration risk than combining a Rust or C# UI with a
  separate native audio engine.
- A full engine such as Tracktion Engine has many useful DAW features, but its
  graph, plugin, automation, and editing abstractions are more than this product
  needs. A narrow engine makes the primary workflow easier to reason about and
  test.
- C# is reasonable for tools and tests, but a managed real-time audio callback
  creates avoidable allocation/GC hazards. Rust is technically capable, but its
  Windows pro-audio and mature desktop-audio UI ecosystem still requires more
  glue for this project. Neither gives a latency advantage over a correct native
  callback design.

JUCE is dual licensed. A distributed closed-source build can require a paid
JUCE licence. JUCE's current bundled ASIO SDK code is offered under GPLv3 or a
separately signed proprietary Steinberg licence. The prototype can stay private;
before public distribution we must choose GPL-compatible open source or secure
the appropriate commercial/proprietary licences. This is an engineering note,
not legal advice.

## Real-time architecture

One process is appropriate, but it has strict thread boundaries:

```text
UI thread  --commands-->  real-time audio callback  --audio blocks--> writer
   ^                          |          |                            thread
   |                          |          +--> monitor/output
   +---meter snapshots--------+                                       |
                                                                      v
                                                              take WAV files
```

### Audio callback

The callback owns transport position and renders the current immutable session
snapshot. It may read/write preallocated buffers and lock-free SPSC queues only.
It must never allocate memory, acquire a mutex, wait, open a file, update a UI,
or call code with unbounded execution time.

Edits arrive as compact commands or a prepared immutable session snapshot that
is atomically swapped at a block boundary. Meter values leave through a
single-producer/single-consumer queue. Recorded blocks go to a preallocated ring
buffer drained by a normal-priority disk writer. The ring is sized for several
seconds and an overrun is surfaced clearly rather than silently corrupting a
take.

The callback/driver thread uses the driver's real-time facilities; for native
WASAPI work it is registered with MMCSS as `Pro Audio`. Drawing, waveform peak
generation, file decoding, saving, and loudness analysis run off the callback.

### Storage and recovery

- Stream each take immediately to a separate 32-bit-float WAV/RF64 file.
- Treat edits and level changes as project metadata; preserve source audio.
- Journal project mutations and atomically replace project manifests.
- On restart, recover orphaned take files and the last valid journal state.
- Build waveform peak caches in the background.

## Latency: measurement and compensation

Driver-reported input and output latency is only the initial estimate. Each
input/output device pair and sample-rate/buffer-size combination gets a stored
calibration profile.

### Calibration flow

1. Ask the user to turn speakers down to a comfortable level, select the input,
   and place the mic or connected cable where it will be used.
2. Schedule a count-in followed by a low-level, band-limited click pattern at
   exact output sample positions. Use a deliberately non-periodic spacing so a
   missed click cannot shift the answer by one beat.
3. Record continuously from before the first click through a short tail.
4. Band-pass/whiten the capture, cross-correlate it with the known pattern, then
   refine each detected peak to a fractional-sample estimate.
5. Reject echoes/outliers and use the median offset across several clicks.
6. Subtract only the known scheduled position. The result is the observed
   round-trip path, including acoustic travel time when speakers and a mic are
   used. That is desirable because it represents the user's actual overdub path.
7. Run a verification pass. Accept the profile only when residual alignment and
   variance are below defined limits; otherwise explain likely causes such as
   noise, echo cancellation, mismatched devices, or an unstable buffer.

On overdub, place newly captured sample `n` at timeline position
`captureStart + n - calibratedRoundTripSamples`. Preserve the original samples;
only their timeline origin changes. Store reported and measured values so a
diagnostic screen can explain the correction.

Calibration must be repeated when device, driver mode, sample rate, buffer size,
or relevant Windows audio processing changes. Prefer input and output on the
same interface because two independent devices have different clocks and can
drift during a long take. Direct monitoring on the Scarlett is the zero-latency
choice for the performer's live signal; software monitoring remains available
when effects are required.

## Automatic level behavior

There are two different problems and the UI must not pretend they are one.

### Before the ADC: prevent clipping and excess noise

Software cannot recover an analog input that clipped before conversion, and it
cannot create the signal-to-noise ratio that a very low physical preamp setting
lost. The detected device is a Scarlett Solo; Focusrite's published fourth-gen
Auto Gain list covers larger models but not the Solo.

Use a ten-second **Sound Check**:

- User plays/sings the loudest expected passage.
- App measures sample peak, short-term RMS, noise floor, and crest factor.
- A single large instruction says `turn the Scarlett knob up`, `down`, or
  `level is good`; target peaks roughly -12 dBFS with generous headroom.
- During recording, warn early about clipping but never change gain secretly.

For laptop microphones, optional Windows processing may alter gain. Calibration
and the level test should detect gross changes; an advanced setting can request
a raw WASAPI stream when supported.

### After capture: consistent playback level

Analyze completed clips off-thread and apply non-destructive clip gain toward a
content-aware target, bounded by a true-peak ceiling. A vocal/guitar take can be
made immediately audible without rewriting its file. Provide one visible
`Auto level` switch (on by default) and a reset; keep the calculated gain in
project metadata. Gentle compression/limiting can be an optional preset later,
not silently printed into the recording.

## Product interface

Use a horizontal time axis with vertically stacked tracks, inspired by the
clarity of early GarageBand and four-track recorders:

- Persistent top bar: back, tempo, time, metronome, play, record, undo, export.
- Each track header: icon/name, input, arm, mute, solo, one large level control.
- Main canvas: waveform clips arranged left-to-right; pinch/wheel zoom and a
  playhead. Clicking empty space moves the playhead. Dragging a clip moves it.
- `+ Track` asks only `Voice / Guitar / Bass / Keyboard / Other`, then chooses a
  sensible mono/stereo input and preset.
- Audio setup is a short sheet with device and `Safe / Fast / Faster`, not a
  wall of buffer/sample-format controls. An advanced disclosure exposes details.

No plugin hosting, buses, routing matrix, automation lanes, comping, or mastering
suite in the first usable version.

## Drums

Do not begin with generative AI. First ship a tempo-aware drummer that is useful
offline and deterministic:

1. Curated multitrack or stereo loop library with verse/chorus/fill tags.
2. A rule-based arranger driven by song sections, intensity, and fill frequency.
3. Humanization through constrained timing/velocity variation with a fixed seed.

This reproduces the useful experience—pick a style and get a coherent part—
without a model runtime, network dependency, or unpredictable edits. Later, a
model can propose the same editable section/variation parameters.

## Genesis composer: related, but a separate app

Build it as a second executable in the same repository, sharing transport,
undo, project serialization, and selected UI components. Recording and chip
composition have different mental models and release milestones; combining the
screens would compromise the recorder's simplicity.

The composer should use a horizontal piano-roll/step-sequencer arrangement like
FL Studio, with opinionated instrument packs rather than raw operator panels.
The playback engine models the Genesis constraints: six YM2612 FM channels
(with the DAC interaction made explicit) plus SN76489 PSG voices. A high-quality
YM2612/YM3438 core such as Nuked OPN2 is a candidate, subject to LGPL compliance.

Every note event must schedule an explicit note-off. Instrument presets must
have a finite release by default, and the engine also enforces a maximum voice
lifetime plus `all notes off` on stop/seek/device reset. This prevents the
never-ending-note failure described in DefleMask while still allowing an
advanced `hold` option when musically intentional.

## Delivery slices

### Slice 0: latency proof (throwaway UI)

- Enumerate ASIO/WASAPI devices and record/play a single channel.
- Measure callback xruns and round-trip latency at several buffers.
- Implement click-pattern calibration and a verification report.
- Test the detected Scarlett Solo and laptop microphone/speakers.

Exit criterion: ten repeated overdubs land within 1 ms of the calibrated target
without xruns during a 20-minute stress recording on the target machine.

### Slice 1: usable four-track recorder

- Vertical track list and horizontal waveform timeline.
- Record, playback, overdub, metronome/count-in, mute/solo, trim/split/move.
- Sound Check, Auto level, autosave/recovery, WAV mix export.

### Slice 2: friendly multitrack

- More tracks, loop recording, basic take selection, input presets, latency
  diagnostics, and bundled drum loops.

### Slice 3: drummer

- Song sections, style/intensity controls, deterministic fills, editable result.

### Parallel follow-on: Genesis composer

- Piano roll and playlist, tempo/looping, preset browser, finite envelopes,
  YM2612/PSG playback, then VGM/WAV export.

## Prototype risks to retire first

1. Stable ASIO operation and licensing for a redistributable build.
2. Accuracy of speaker-to-mic calibration in reflective rooms.
3. Long-take drift when users select input and output from different devices.
4. Disk and UI stalls without callback underruns.
5. Clear Sound Check guidance across guitar, bass, and vocal sources.

## Sources consulted

- Microsoft, WASAPI architecture and low-latency guidance:
  <https://learn.microsoft.com/en-us/windows/win32/coreaudio/user-mode-audio-components>
- Microsoft, low-latency audio and ASIO/WASAPI tradeoffs:
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio>
- Microsoft, Multimedia Class Scheduler Service:
  <https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service>
- JUCE audio-device APIs and WASAPI modes:
  <https://docs.juce.com/master/juce__audio__devices_8h.html>
- JUCE licensing:
  <https://juce.com/get-juce/>
- Current JUCE-bundled ASIO SDK licence text:
  <https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_devices/native/asio/LICENSE.txt>
- Focusrite fourth-generation Auto Gain model list:
  <https://support.focusrite.com/hc/en-gb/articles/21671741279762-Scarlett-4th-Gen-Multichannel-Auto-Gain>
- Tracktion Engine feature scope:
  <https://github.com/Tracktion/tracktion_engine/blob/develop/FEATURES.md>
- Nuked OPN2 emulator:
  <https://github.com/nukeykt/Nuked-OPN2>
