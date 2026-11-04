# Kick 96 — project notes

BoomTown Beats Kick 96: JUCE 9.0.4 audio effect (AU + VST3), distortion made for kick drums. Part of the BoomTown Beats
96 line (festerlabz-cyber/console96, reverb96, delay96, clip96, pump96), sharing their look and build setup.
Owner speaks Dutch; write user-facing docs in Dutch, GUI text in English.

## Layout
- `Source/Kick96DSP.h` — whole DSP engine, no JUCE dependency. Port of `prototype/dsp.js`; keep them in sync
  (`tests/dsp_pairs.cpp` output matches the JS engine to float precision for all modes and qualities).
  Input gain, linked transient shaper (click / tail), low cut, punch peak EQ, oversampled core (stage 1 drive, focus
  peak EQ, stage 2 drive per mode, DC block), high cut, per-mode level compensation, latency-aligned dry mix, clean LR4
  sub band, soft ceiling, output. Oversampling FIRs 129 / 257 taps: exactly 64 samples latency at 2× and 4×, 0 in Eco.
- `Source/Parameters.h` — parameter IDs (same keys as the prototype), ranges, formatting, 9 factory presets.
- `Source/PluginProcessor.*` — APVTS, presets as programs, bypass parameter, latency follows Quality (async update).
- `Source/PluginEditor.*`, `UIComponents.h` — GUI at 1160×372, scaled as a whole. Same layout as the prototype:
  Input/Mix column, big Drive knob, six groups (Punch, Focus, Shape, Sub, Tone, Out), bottom bar Mode / Quality / Presets.
- `.github/workflows/build-macos.yml` — universal AU + VST3, auval, DMG artifact with install script.

## Decisions
- Manufacturer code `Btbt`, plug-in code `Kk96`, bundle id `com.boomtownbeats.kick96`. Never change these.
- Own algorithm and names; do not use other brands' names.
- Presets never change Quality or Bypass.
- `g.setOpacity (1.0f)` before `drawImage`: drawing uses the current opacity left by earlier `setColour` calls.

## Testing
- `cmake -S . -B build-linux -G Ninja -DJUCE_DIR=<juce> -DKK96_BUILD_TESTS=ON && cmake --build build-linux --target Kick96RenderTest`
  then `xvfb-run -a build-linux/Kick96RenderTest_artefacts/Release/Kick96RenderTest <outdir>`: per-preset levels on a
  909-style kick, measured vs reported latency per quality, state round trip, GUI PNG at 1× and 2×.
