# HarmEQ

A pitch-tracking harmonic EQ plugin for macOS (VST3 / AU / Standalone) built with JUCE. EQ bands follow the
fundamental of a monophonic note — detected from the main input, a sidechain, or taken from MIDI — and sit
on a chosen harmonic of it. See [PLAN.md](PLAN.md) for the design and milestones.

## Building

Requires CMake ≥ 3.22 and a C++20 compiler. JUCE and Catch2 are fetched automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

Plugins are written to `build/HarmEQ_artefacts/Release/`. Add `-DHARMEQ_COPY_PLUGIN=ON` to the configure
step to also copy them into `~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`.
Add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for a universal binary.

macOS is the target platform. The code also builds on Linux (used for development and tests) after
installing the JUCE dependencies:

```sh
sudo apt-get install libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev
```

## Current status

Milestones 1–3 of the plan are done: the EQ bands work, and tracked bands follow **MIDI**. Audio pitch
detection (Main Input / Sidechain sources) is not implemented yet — with those sources selected, tracked
bands behave as if no note is playing (see *No-Pitch Mode*).

## Trying it in Ableton Live

1. Build with `-DHARMEQ_COPY_PLUGIN=ON` (or copy `HarmEQ.vst3` / `HarmEQ.component` by hand), then in Live
   open *Settings → Plug-Ins*, make sure VST3 and/or Audio Units are enabled and click *Rescan*.
2. Put **HarmEQ** on an audio track with a sustained sound (a pad, drone, or noise works well for hearing it).
3. Open the plugin window and set **Pitch Source = MIDI**.
4. Make a band audible — the defaults are flat (0 dB). For example Band 1: Gain **+12 dB**, Q **10**,
   Harmonic **2**. Turn Glide up to taste.
5. Create a MIDI track. In its *MIDI To* dropdown choose the audio track, and in the second dropdown choose
   **HarmEQ**. Play a clip, or set the MIDI track's Monitor to *In* and play your keyboard.

Each note moves the tracked bands to its harmonics; releasing all notes keeps the last pitch (*Hold*),
returns to *Fallback Pitch*, or fades the tracked bands out (*Bypass Bands*), depending on No-Pitch Mode.
