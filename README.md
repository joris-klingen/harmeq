# HarmEQ

A pitch-tracking harmonic EQ plugin (VST3, plus a Standalone app) for macOS and Windows, built with JUCE. EQ bands follow the
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
step to also copy the VST3 into the system plugin folder (`~/Library/Audio/Plug-Ins/VST3` on macOS,
`C:\Program Files\Common Files\VST3` on Windows, which needs an elevated shell).
Add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for a universal binary.

macOS is the primary platform; Windows builds and passes validation in CI. The code also builds on Linux (used for development and tests) after
installing the JUCE dependencies:

```sh
sudo apt-get install libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxext-dev libgl1-mesa-dev
```

## Current status

![HarmEQ](docs/screenshot.png)

Done: the EQ bands, MIDI pitch tracking (Last / Lowest / Poly), audio pitch detection from the main input
or a sidechain with lookahead, and the GUI with its spectrum analyzer. Next: polish (presets, performance,
host testing; see PLAN.md, step 7).

## Pitch from audio (Main / Sidechain)

With **Source = Main** the plugin detects the pitch of the audio it is EQ-ing; with **Sidechain** it
listens to the sidechain input instead (route a track to HarmEQ's sidechain in your DAW). Detection is
monophonic: one note at a time.

- **Detect** sets the lowest and highest pitch to look for. Narrowing it to your material (e.g. 40–400 Hz
  for bass) makes detection faster to react and less likely to jump.
- **Conf** is the minimum confidence for a detected pitch to be used. The meter in the top bar shows the
  current confidence, with a tick at this threshold; it turns green when a pitch is accepted. Raise it if
  the bands jump around on noisy or breathy material; lower it if they keep dropping out.
- Short dropouts (a consonant, a pick scrape) keep the last pitch; after that, *No pitch* decides what
  happens. Octave jumps that last a single analysis frame are filtered out.
- Detection lags the audio by about two periods of the lowest Detect pitch (40 ms at 50 Hz, 20 ms at
  100 Hz), so without help a band arrives that much after each note change.
- **Lookahead** (in the second row, where Bend is for MIDI) delays the audio by exactly that lag and
  reports it to the host as plugin latency, so bands land on the note changes. Live compensates
  automatically; use it for mixing, and leave it off when playing live through the plugin. Raising
  Detect low shortens the delay. A changed delay takes effect after a quarter second, so dragging
  Detect low doesn't make the host re-align on every step. MIDI needs no lookahead and adds no latency.
- With Sidechain selected and nothing routed to it, the top bar says *No sidechain signal*.

## MIDI note modes

With **Source = MIDI**, **Notes** picks which notes the tracked bands follow:

- **Last**: the most recently pressed note (glides between notes).
- **Lowest**: the lowest held note, which is often what matters for low-end build-up.
- **Poly**: every held note, up to 6 (a Juno-106 has six voices). Each tracked band gets a copy per
  note, so a "notch on H1, cut H2" setup applies to every note of a chord. Fixed bands stay single.
  - New notes fade in at their pitch; there is no glide between chords.
  - *No pitch* = **Hold** keeps the last chord after you let go, until the next note, so ringing pad
    releases stay treated. *Fallback* switches to the fallback pitch, *Bypass bands* fades them out.
  - Harmonics shared between notes are merged: when two notes put bands of the same type within
    20 cents of each other (e.g. C's 3rd and G's 2nd harmonic in a C major chord), only one applies, so a
    −6 dB cut never becomes −12 dB just because of the chord.

## Using the GUI

The display shows the combined EQ curve (white), each band's curve in its colour, and lines at the
first 8 harmonics of the current pitch (in Poly, of every held note, with the lowest labelled; nodes
sit on the lowest note). The note readout uses Ableton's octave numbering (MIDI 60 = C3).

| On the display | Does |
|---|---|
| Drag a node left/right | Tracked band: moves between harmonics (¼, ½, 1…32). Fixed band: sets Hz |
| Shift + drag left/right | Tracked band: fine detune (±100 ct). Fixed band: fine Hz |
| Drag up/down | Gain (bell and shelves) |
| Mouse wheel on a node | Q |
| Double-click a node | Band on/off |
| Double-click empty space | Turns on the next free band at that harmonic |
| Right-click a node | Curve, Track pitch, slope, on/off, reset |

Knobs and value boxes drag up/down (Shift for fine) and reset on double-click. Window size, display range
and analyzer mode are saved with the project.

`build/HarmEQScreenshot out.png [scale] [poly|audio]` renders the editor with example settings and a
held MIDI note (a chord with `poly`, detected audio pitch with `audio`), which is handy for checking GUI
changes without a host.

## Trying it in Ableton Live

1. Build with `-DHARMEQ_COPY_PLUGIN=ON` (or copy `HarmEQ.vst3` by hand), then in Live open
   *Settings → Plug-Ins*, make sure VST3 is enabled and click *Rescan*.
2. Put **HarmEQ** on an audio track with a sustained sound (a pad, drone, or noise works well for hearing it).
3. Open the plugin window and set **Source** to **MIDI**.
4. Make a band audible — the defaults are flat (0 dB). For example Band 1: Gain **+12 dB**, Q **10**,
   Harmonic **2**. Turn Glide up to taste.
5. Create a MIDI track. In its *MIDI To* dropdown choose the audio track, and in the second dropdown choose
   **HarmEQ**. Play a clip, or set the MIDI track's Monitor to *In* and play your keyboard.

Each note moves the tracked bands to its harmonics; releasing all notes keeps the last pitch (*Hold*),
returns to *Fallback Pitch*, or fades the tracked bands out (*Bypass Bands*), depending on No-Pitch Mode.
