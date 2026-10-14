# HarmEQ — Implementation Plan

A pitch-tracking harmonic EQ plugin built with JUCE. Standard EQ curves (no dynamics), where each band's
frequency can follow the fundamental (f0) of a monophonic note — taken from the main input, a sidechain
input, or incoming MIDI — and sit on a chosen harmonic of it.

---

## 1. Scope

**In scope (v1)**
- 6–8 EQ bands with standard curves: Bell, Low Shelf, High Shelf, High Pass, Low Pass, Notch.
- Each band is either **Tracked** (frequency = f0 × harmonic × detune) or **Fixed** (plain Hz).
- Pitch source: **Main input**, **Sidechain**, or **MIDI**.
- Monophonic pitch detection (YIN or McLeod/MPM) with confidence gating and glide smoothing.
- MIDI: monophonic last-note priority, pitch bend.
- Response-curve display with harmonic markers and detected-note readout.
- Platforms: **macOS** primary (universal arm64 + x86_64) and **Windows**. Formats: VST3 and Standalone (no AU),
  kept building and validated in CI. CLAP optional via `clap-juce-extensions`.

**Out of scope (v1)**: dynamic EQ, polyphonic audio detection (Poly works from MIDI only), linear-phase mode, M/S processing, spectrum-match.

---

## 2. Tooling & project setup

- **JUCE 8** pulled in with CMake `FetchContent` (pinned tag) — no Projucer.
- C++20, CMake ≥ 3.22.
- `juce_add_plugin(HarmEQ ...)` with `NEEDS_MIDI_INPUT TRUE`, `IS_SYNTH FALSE`, `IS_MIDI_EFFECT FALSE`,
  `FORMATS VST3 Standalone`.
- Unit tests: Catch2 (FetchContent), separate `HarmEQTests` target linking the DSP sources (DSP code is
  kept free of `AudioProcessor` so it is testable headless).
- CI: GitHub Actions on macOS (universal build) and Windows: build, unit tests, **pluginval** at strictness 10
  on the VST3.

```
harmeq/
├─ CMakeLists.txt
├─ cmake/                    # FetchContent helpers
├─ source/
│  ├─ PluginProcessor.{h,cpp}
│  ├─ PluginEditor.{h,cpp}
│  ├─ Parameters.{h,cpp}     # APVTS layout, IDs, ranges
│  ├─ dsp/
│  │  ├─ PitchDetector.{h,cpp}   # YIN/MPM, fixed-size buffers, no allocs
│  │  ├─ PitchTracker.{h,cpp}    # source selection, gating, hold, glide
│  │  ├─ MidiNoteTracker.{h,cpp} # mono note stack + pitch bend
│  │  ├─ SvfFilter.h             # TPT/Cytomic SVF, all curve types
│  │  ├─ EqBand.{h,cpp}          # freq resolution, smoothing, Nyquist fade
│  │  └─ EqEngine.{h,cpp}        # band array, sub-block processing
│  └─ gui/
│     ├─ ResponseCurve.{h,cpp}
│     ├─ BandControls.{h,cpp}
│     └─ PitchDisplay.{h,cpp}
└─ tests/
```

---

## 3. Bus layout & I/O

- Main bus: stereo in → stereo out (also accept mono→mono).
- Sidechain bus: input only, mono or stereo, **disabled by default** (`isEnabledByDefault = false`) so hosts
  that dislike extra buses still load the plugin.
- `isBusesLayoutSupported`: main in == main out, mono or stereo; sidechain may be disabled, mono, or stereo.
- `acceptsMidi() = true`, `producesMidi() = false`.
- If the selected source is Sidechain but the bus is disabled/silent, fall back to "no pitch" behaviour (§5.4)
  and show a warning in the UI.

---

## 4. Parameters (AudioProcessorValueTreeState)

**Global**
| ID | Range | Notes |
|---|---|---|
| `source` | Main / Sidechain / MIDI | pitch source |
| `minFreq`, `maxFreq` | 30–2000 Hz | detection range (narrows search, improves accuracy & CPU) |
| `confidence` | 0–1 | YIN threshold / MPM clarity gate |
| `glide` | 0–500 ms | f0 smoothing time (log-frequency domain) |
| `noPitchMode` | Hold / Fixed fallback / Bypass bands | behaviour when unvoiced |
| `fallbackFreq` | 20–2000 Hz | f0 used in "Fixed fallback" mode |
| `lookahead` | on/off | delay audio by detector latency so EQ lines up with transients |
| `pitchBendRange` | 0–24 st | MIDI source |
| `transpose` | −24…+24 st | applied to f0 from any source |
| `outputGain` | −24…+24 dB | |

**Per band (×N)**
| ID | Range | Notes |
|---|---|---|
| `bandN_on` | bool | |
| `bandN_type` | Bell / LS / HS / HP / LP / Notch | |
| `bandN_track` | Tracked / Fixed | |
| `bandN_harmonic` | 0.25–32 (step 0.25, or integer-snap toggle) | allows sub-harmonics |
| `bandN_detune` | −100…+100 cents | |
| `bandN_freq` | 20–20 kHz (skewed) | used in Fixed mode |
| `bandN_gain` | −24…+24 dB | ignored for HP/LP/Notch |
| `bandN_q` | 0.1–30 | high Q is the typical harmonic use-case |
| `bandN_slope` | 12/24 dB/oct | HP/LP only (cascade 1–2 SVFs) |

All parameters are automatable; parameter version hints set for future-proofing.

---

## 5. DSP design

### 5.1 Pitch detection (main / sidechain)
- Mix the chosen bus to mono, apply a gentle HP (~20 Hz) and optional LP at `maxFreq × 4`.
- **Decimate** to ~8–11 kHz (fixed-factor, polyphase/half-band) — enough for f0 ≤ 2 kHz and cuts cost ~4×.
- **Algorithm:** YIN (cumulative mean normalised difference + parabolic interpolation). MPM is an
  alternative behind the same interface; pick after benchmarking on test material.
- Window must hold ≥ 2 periods of `minFreq` (e.g. 40 Hz → 50 ms); hop ~5 ms.
- Run on the audio thread in a ring buffer; analysis triggers whenever a hop's worth of samples arrives.
  All buffers pre-allocated in `prepareToPlay` sized from `minFreq` and sample rate. Optionally compute the
  difference function via FFT (`juce::dsp::FFT`) if the time-domain O(N·τ) cost is too high at low `minFreq`.
- Output per hop: `{ f0Hz, confidence, rmsLevel }`.

### 5.2 MIDI tracking
- Note stack (fixed array, 128 entries), last-note priority; note-off returns to the previous held note.
- f0 = 440 × 2^((note − 69 + bend × range) / 12).
- Handle events **sample-accurately**: process the block in sub-blocks split at MIDI event positions.
- All-notes-off / CC 123 clears the stack.

### 5.3 Pitch conditioning (`PitchTracker`)
- Gate: accept the detector's f0 only if `confidence ≥ threshold` and level above a noise floor.
- Octave-error guard: reject a jump of ~×2 / ×0.5 unless it persists for 2–3 hops (median-of-3 filter).
- Glide: one-pole smoother on **log2(f0)** so glides are musically uniform; jump instantly on new MIDI notes
  if glide = 0.
- Apply `transpose`.

### 5.4 No-pitch behaviour
- **Hold**: keep last valid f0 (default).
- **Fixed fallback**: glide to `fallbackFreq`.
- **Bypass bands**: tracked bands ramp gain to 0 dB (or fade wet→dry for HP/LP/Notch) over ~20 ms;
  fixed bands unaffected.

### 5.5 Filters
- Use a **TPT state-variable filter (Simper/Cytomic)** rather than direct-form biquads: it stays stable and
  click-free under fast frequency modulation, which is the core of this plugin. One SVF yields all curve
  types via output mixing; HP/LP 24 dB = two cascaded SVFs (Butterworth Qs).
- Coefficients recomputed per **sub-block of 16–32 samples** from the smoothed f0 (cheap: one `tan` per band
  per sub-block). Gain/Q use `SmoothedValue`.
- Band frequency = clamp(f0 × harmonic × 2^(detune/1200), 10 Hz, 0.45 × fs).
  When the target exceeds ~0.45 × fs, **fade the band out** instead of piling up at the clamp.
- Per-channel state; processing order is band 1 → N in series.

### 5.6 Latency / lookahead
- Detection inherently lags by roughly window/2 + hop. With `lookahead` on, delay the main audio path by
  that amount and report it with `setLatencySamples()` (updated in `prepareToPlay` when range/sample rate
  change). MIDI source → zero latency regardless.

### 5.7 Real-time safety
- No allocation, locks, or logging in `processBlock`.
- Detected f0/confidence published to the GUI via `std::atomic<float>`.
- Response-curve data computed on the message thread from parameter values + atomic f0.

---

## 6. GUI

- **Response curve** (log-freq 20 Hz–20 kHz): summed magnitude of all bands evaluated analytically from
  SVF transfer functions; draggable band nodes (x = harmonic/freq, y = gain, wheel = Q).
- **Harmonic grid**: faint vertical lines at f0, 2f0, 3f0… that move with the detected pitch.
- **Pitch readout**: note name + cents + Hz + confidence meter; source indicator (Main/SC/MIDI) with a
  warning when sidechain is unavailable.
- **Band strip**: per-band on/type/track/harmonic/detune/gain/Q.
- Optional (stretch): real-time FFT spectrum behind the curve.
- Resizable, 30 Hz timer for repaints, `juce::LookAndFeel_V4` subclass.

---

## 7. Testing

**Unit (Catch2)**
- Pitch detector: sines, saws, square, and noisy/vibrato signals across 40 Hz–2 kHz at 44.1/48/96 kHz;
  assert error < 5 cents and no octave errors on clean material.
- MIDI tracker: overlapping notes, legato release order, pitch bend, all-notes-off.
- SVF: measured magnitude response (sine sweep) vs. analytic within 0.1 dB for each type.
- Modulation stability: sweep f0 rapidly over the full range; assert output finite and no large spikes.
- Nyquist fade: harmonic × f0 above 0.45 fs → band contributes 0 dB.

**Integration**
- pluginval (strictness 10) in CI on macOS and Windows.
- Manual host matrix (macOS): Ableton Live, Reaper, Bitwig — specifically check
  sidechain routing and MIDI routing into an audio effect, which differ per host.

**Performance target**: < 2 % of one core at 48 kHz stereo, 8 bands, 40 Hz min freq.

---

## 8. Milestones

Status: 1–3 and 6 done (GUI, including the spectrum analyzer), plus MIDI Lowest Note and Poly modes (see README). Next: 4, audio pitch detection.

1. **Skeleton** — CMake + JUCE FetchContent, empty processor with buses (main + sidechain) and MIDI in,
   APVTS, generic editor, macOS CI with pluginval.
2. **Static EQ** — SVF + EqBand/EqEngine with Fixed-mode bands; unit tests for responses.
3. **MIDI tracking** — MidiNoteTracker, Tracked mode, sub-block coefficient updates, glide. (Fastest route to
   a usable harmonic EQ, and validates the modulation path before pitch detection exists.)
4. **Audio pitch detection** — decimator + YIN, PitchTracker conditioning, main/sidechain source switching,
   no-pitch modes; detector test suite.
5. **Lookahead & latency reporting.**
6. **Custom GUI** — response curve, harmonic grid, pitch readout, band controls.
7. **Polish** — presets (factory: "vocal harmonics de-ess", "bass 2nd-harmonic boost", "notch fundamental"),
   state versioning, performance pass, host testing, signing/notarisation for macOS.

---

## 9. Open questions / risks

- **Detector quality on real material** (breathy vocals, bass with weak fundamental): mitigate with octave
  guard, confidence gate, and the MIDI option as a reliable fallback.
- **Latency vs. low-frequency range**: low `minFreq` means longer windows; lookahead hides it for mixing but
  not for live use — document the trade-off.
- **Host quirks**: some hosts don't route MIDI to audio effects or expose sidechains poorly; consider a
  companion "MIDI-to-pitch" mode via sidechain if needed.
- Number of bands (6 vs 8) and whether to add a "harmonic series" macro band (one band applied to
  harmonics 1…N with tilt) — candidate for v1.1.
