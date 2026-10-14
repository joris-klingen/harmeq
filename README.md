# HarmEQ

A pitch-tracking harmonic EQ plugin (VST3 / AU / Standalone) built with JUCE. EQ bands follow the
fundamental of a monophonic note — detected from the main input, a sidechain, or taken from MIDI — and sit
on a chosen harmonic of it. See [PLAN.md](PLAN.md) for the design and milestones.

## Building

Requires CMake ≥ 3.22 and a C++20 compiler. JUCE and Catch2 are fetched automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

Plugins are written to `build/HarmEQ_artefacts/Release/`.

On Linux, install the JUCE dependencies first:

```sh
sudo apt-get install libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev
```
