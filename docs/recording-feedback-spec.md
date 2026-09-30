# Recording feedback and tuner spec — 2026-09-30

User requests all five features below. This is the implementation checklist and
session handoff; do not mark a feature complete without verification. Baseline
commit df0371e (0.4 reverb). Work in src/StudioComponent, SessionEngine and new
input-analysis module; keep existing nondestructive clips, calibration and FX.

## 1. Visible metronome track

When Click is enabled, show a dedicated compact lane above audio tracks, below
the ruler. It is generated, not a file or editable recorded track. Label it
Metronome with tempo and 4/4; draw click transients at the exact song-time beat
positions used by the audio engine. Accented downbeats have stronger peaks.
It follows horizontal zoom/scroll. The lane is first in the vertical stack
(not a separate permanently pinned viewport). Hide it when Click is off;
count-in remains a separate four-beat lead-in and does not become song audio.
Click stays excluded from export. Use the same click-sample function in audio
and display, with a minimum visible peak width at low zoom. Lane clicks seek;
they never select/drag/record a clip. Offset every track's hit testing/layout.

Acceptance: 100 BPM peaks at 0, .6, 1.2 seconds; first beat accented; 120 BPM
peaks every .5 sec. Toggle/zoom/scroll and record with lane visible without
shifting clip start, calibration or track/input assignment.

## 2. Live recording waveform

Draw actual per-input min/max peaks inside the growing red take, not synthetic
animation. A flat line means silence; show clipping in red. Publish bounded
peak packets via a preallocated SPSC queue; audio callback never locks,
allocates, waits or touches UI. UI drains at 30 Hz, stores compact 10 ms bins,
and draws only visible bins. Per-take bins follow the already compensated
timeline: skip initial latency padding, map remaining samples from punch start.
Expect the newest waveform edge to trail capture by the compensation + one bin.
Include compensation drain; clear on new take; replace with saved PCM waveform
after finalization. Multi-arm tracks use their own physical input. Overflow of
visualization data may omit bins but must never interrupt capture/disk writing.

Acceptance: fake-device signal produces correct signed peaks on each armed
track before Stop; initial delay padding does not appear; silence remains flat;
count-in produces no recorded waveform. Old engine punch/tail tests still pass.

## 3. Grabbable playhead

Stopped playhead has a roughly 8 px hit zone, pointing/dragging hand cursor,
highlighted line and larger head on hover/drag. Hit zone takes precedence over
clip dragging. Drag updates position continuously, clamps to zero and reveals
the timeline edge as needed. No clip edits/history entries from cursor drags.
Ruler/blank-lane clicks continue to seek. Transport locks seeking while busy;
do not advertise a draggable cursor during recording/playback. Leaving hover
clears highlight. Keep a visible moving transport cursor while recording.

Acceptance: cursor can be grabbed over a clip without moving/trimming the clip;
drag to zero/right edge works; mouse-up/exit returns normal appearance.

## 4. Return to recording start automatically

Record anchor is captured before count-in. On Stop, continue the existing
silent compensation tail and disk finalization; once idle, restore playhead to
the anchor, not the end. Restore the pre-record horizontal view so repeated
takes/playback require no rewind. Applies to cancelled count-in and partial
device/disk-failure finalization too. Retain actual take end for clip length;
never truncate the captured tail merely to move the UI cursor. Plain playback
Stop stays where it stopped (this request concerns recording). Return button
remains available. Save restored playhead in the project.

Acceptance: record at 30, stop at 32, take covers 30–32 but cursor is 30; Play
starts there; next Record punches there. Zero/count-in cancellation/device loss
follow the same return rule. Saved project reloads anchor.

## 5. Full-time monophonic guitar/voice note monitor

Always-visible compact monitor above the timeline. Dedicated physical input
selector (mic 1 or guitar 2), independent of armed tracks, avoids ambiguity when
multiple tracks record. A4=440 Hz chromatic note name/octave, frequency and cents
flat/sharp with centered in-tune indicator (within 5 cents). Works while idle,
playing and recording; input monitoring does not route audio to speakers.
Display 'Play or sing one note' for silence/uncertain pitch; clear stale results
on device/input changes or signal loss. No claims to identify chords. Bass-low
notes optional; initial target E2–C6, analysis search ~60–1100 Hz.

Audio callback copies a selected mono channel into a bounded SPSC FIFO. Analysis
is off the callback (UI timer, bounded window): downsample with averaging to
approximately 12 kHz, 2048 samples, estimate periodicity by normalized squared
difference with first reliable minimum and parabolic refinement. RMS gate and
confidence threshold reject silence/noise; clear old data if consumer lags.
UI refresh about 10 Hz; expect roughly 100–200 ms observation latency, unrelated
to recording/monitor latency. No extra plugins or drivers. Persist tuner input
as a workspace preference, not a change to the project audio routing.

Acceptance: synthesized guitar fundamentals E2/A2/D3/G3/B3/E4, A4 and +/-20-cent
signals near expected note/cents; harmonic-rich voice/guitar fixtures; silence
and deterministic noise rejected; unavailable channel reports no signal;
recording output remains unchanged. Hardware audition remains a user check.

## Sequence and progress

- [x] Spec saved before implementation.
- [x] Return-to-anchor engine + UI and regressions.
- [x] Shared metronome signal + visible lane.
- [x] Playhead hover/grab interaction.
- [x] Bounded live waveform telemetry + renderer and tests.
- [x] Note monitor analysis + UI/input persistence and tests.
- [x] Release build, full regressions, synthetic UI inspection, docs updated.
- [x] Commit/push; executable: build/effects/SimpleRecorder_artefacts/Release/Simple Recorder.exe. No user instance terminated.

Build normally uses build/effects. If that executable is running, build into
a separate directory rather than terminating the user's app. Self-test/preview
modes can run without opening audio devices. Do not play test tones or record
the user's room for synthetic verification. Current limitations: no hardware
audition, no polyphonic tuner, no waveform recovery guarantee after UI stalls.

## Implemented handoff (0.5)

InputAnalysis.h/.cpp contains the bounded SPSC queue, peak packet, note naming,
normalized difference pitch detector, shared metronome signal and decimated
input feed. Producer sends 2048-sample windows near 12 kHz (about 170 ms), so
actual refresh is about 6 Hz, not the proposed 10 Hz. UI clears stale pitch
after 500 ms or device loss; sequence tags reject queued pitch windows after a
consumer stall rather than presenting old notes as current. Cents are rounded
for display; green means +/-5.
Analysis happens on the UI thread; no pitch math occurs in the audio callback.

Engine aggregates 10 ms signed peaks after compensation padding. Each track's
UI vector caps at 180001 bins (30 minutes); only visible bins are scanned when
drawing. Overflow drops visualization packets, never audio. Last partial bin
under 10 ms appears when finalized audio replaces the live overlay. Input
clipping is orange on the otherwise light live waveform. All takes remain dry.

Engine poll() restores the anchor even for cancellation/device loss; UI also
restores the pre-record horizontal view. Recording end remains separate and
unchanged. Metronome accents now vary in amplitude and tone, with downbeat-only
labels at low zoom to prevent crowded text. The lane is generated, not editable.

Tests: prior full suite plus FeedbackTests (harmonic pitch, detuning, silence,
noise, physical input isolation, sample rates, shared click playback and queue
overflow); EngineTests now assert live multi-input signed peaks before Stop,
padding exclusion, return on normal/cancel/device-loss paths. A synthetic
StudioComponent interaction test drags the playhead over an existing clip,
checks that media/history remain unchanged, toggles lane layout, records fake
input, checks waveform delivery, returns to 30 seconds and auditions there.
This test writes feedback-live-preview.png beside test-results.txt; inspected
the screenshot with visible incoming waveform, metronome lane and A2 note.

No physical speakers/microphones were used for verification. Remaining user
checks: real guitar/SM58 tuning stability, real recorded waveform, mouse feel
at Windows display scaling, and physical overdub alignment. No pending feature
implementation from this request; those are hardware/usability checks.
