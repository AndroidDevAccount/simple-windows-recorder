# Independent track reverb — 0.4

Reference: [EHX Holy Grail](https://www.ehx.com/products/holy-grail/) offers
Spring, Hall and Flerb; Flerb combines flanging and reverb. Our feature is
inspired by those categories and simplicity, not a measured/modelled replica,
not licensed/endorsed by EHX, and does not use their proprietary DSP or assets.

Controls on every track: independent Verb enable, style, wet/dry Mix, and ?
explanation. Default off / Spring / 20%. Existing projects remain dry. EQ/comp
FX bypass does not bypass reverb. Settings participate in session undo/redo,
save/autosave/load. Missing optional fields load off; mix clamps to 0–100%.

Signal: clips -> EQ/compressor preset -> reverb wet/dry -> track volume -> mix.
Raw WAV takes are never modified. No software input monitoring. No block or
lookahead delay added to the direct sound. Wet reflections are delayed by
design. Controls lock during transport, matching existing effects behavior.

Implementation: JUCE Freeverb-style tank; compact, bright spring-inspired
variant uses a 120 Hz high-pass, three dispersive allpasses, and a 5.5 kHz
low-pass. This is a stylized spring sound, not a physical spring-tank model.
Hall uses a larger tank. Flerb uses slow 0.17 Hz wet-only stereo flanging with
1–5 ms fractional delays. No modulation touches the direct path. Mix is a
linear dry/wet crossfade, not an attempted reproduction of EHX's knob taper.
Feedback/freeze controls are not exposed; tails decay instead of sustaining.

All buffers allocate during prepare before transport starts. Processing uses
fixed-size scratch arrays and carries filter/delay/modulation state across
blocks. Stop silences playback; playback/seek starts reset the effect state.
No historical DSP preroll, so starting mid-song won't reproduce the tail from
earlier unheard notes. These are the same restart semantics as the compressor.

Export runs the same chain and preserves tails: budget up to 6 seconds after
Spring clips, 12 Hall, 10 Flerb (audible tracks only, nonzero mix). Final 100 ms
is faded to avoid an abrupt truncation. This conservative budget can leave a
little trailing silence. Dry/bypassed projects retain their original duration.

Regression tests: modes at 44.1/48/96 kHz; finite decaying impulse tails; no
direct-path delay; exact bypass and 0% mix; 100% wet; reset; block-size parity;
project persistence/legacy defaults; exported tail vs real engine output;
untouched source. Human audition remains outstanding. No physical playback
or recording is necessary to run these tests.

Verified locally: release build and full regression suite passed, including
combined lead-vocal preset plus Hall playback/export parity. Synthetic UI
preview inspected. No hardware recording or speaker playback performed.

Build remains `build/effects/SimpleRecorder_artefacts/Release/Simple Recorder.exe`.
Close the previous application before launching the updated build.
