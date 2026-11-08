# Vega Vocal

A complete vocal chain in one VST3 plugin (pitch correction, de-esser, compressor, saturation, delay and reverb), written in C++20 with JUCE on an allocation-free, JUCE-independent DSP core.

*[Leer en español](README.es.md)*

<!-- Screenshot: run vocal_host_tests (see "Running tests"), save the PNG as docs/screenshot.png and uncomment:
![Vega Vocal](docs/screenshot.png)
-->

**Signal flow:** input gain → low cut → auto leveller → pitch correction → de-esser → compressor → saturation → delay + reverb (parallel sends) → output with a soft safety clipper

## Features

- **Pitch correction**
  - YIN pitch detection on a decimated mono signal (80 Hz to 1 kHz), with hysteresis so consonants do not flutter the voiced/unvoiced decision.
  - **PSOLA** (pitch-synchronous overlap-add) shifting. It moves the pitch without moving the formants, and grains are aligned to the previous grain to stay in phase. When no correction is needed, the shifter is a pure delay.
  - Key and scale: chromatic, major, natural minor, harmonic minor, major pentatonic and minor pentatonic. Note selection has hysteresis, so held notes between two scale degrees do not flip back and forth.
  - Correction amount (0 to 100%) and retune speed, from hard tune to slow, natural correction that keeps vibrato.
- **De-esser:** a dynamic high shelf with adjustable frequency. It compares the top band to the level of the whole signal, so it adapts to the singer's level and stays flat when there is no sibilance.
- **Compressor:** stereo-linked with a soft knee. It has threshold, ratio, attack, release and make-up gain.
- **Saturation:** tanh waveshaping with first-order antiderivative anti-aliasing, a small bias for even harmonics (removed afterwards by a DC blocker), a warm/bright tilt and a dry/wet mix.
- **Ping-pong delay:** filtered repeats with cross-feedback and adjustable stereo spread. The time is set freely or synced to the host tempo (1/2, dotted 1/4, 1/4, dotted 1/8, 1/8, 1/16).
- **Reverb:** input diffusion feeding an 8-line feedback delay network with damping. It has size, decay, damping, pre-delay and mix.
- **Ducking:** delay and reverb are lowered while the singer is singing, and come back between phrases.
- **Auto-Set ("AUTO-AJUSTE")**: the plugin listens to the voice and then sets input gain, low cut, de-esser amount and compressor threshold, and estimates the song key and scale with Krumhansl-Schmuckler key profiles.
- **Auto leveller** that slowly rides the gain toward -18 dB and holds still between phrases.
- **7 factory presets** exposed as host programs, a live tuner display (detected note, target note, correction in cents), input/output meters, and gain-reduction meters for the de-esser and compressor.
- A click-free bypass that is latency-compensated. The plugin reports about 34 ms of latency, and the dry path is delayed by the same amount.
- **Windows installer** (`VegaVocalSetup`): a single `.exe` with the Release plugin embedded as a resource. It can install, update and uninstall the plugin.

## Technical highlights

- **Allocation-free `process()`.** Every buffer is sized in `prepare()`, and nothing in [`Source/VocalCore.h`](Source/VocalCore.h) allocates afterwards. Smoothing runs per sample on gains and sends, so parameter changes are click-free.
- **Lock-free UI ↔ audio communication.** Meters, tuner data and the Auto-Set request/progress/result handshake all use atomics (with acquire/release ordering for the result). The audio thread is never blocked.
- **JUCE-independent DSP core.** The whole signal chain is plain C++20 in one header, so it is unit-tested without JUCE.
- **Tests at 44.1, 48 and 96 kHz.** `vocal_core_tests` runs every check at each rate. It covers a null test with all modules off, latency, scale snapping, correction amount, stepped phrases landing in tune without clicks, hard vs. slow tune on vibrato, de-esser selectivity, compression ratio, saturation harmonics, delay timing, reverb decay rate, a NaN/Inf and over-0 dBFS stress test, a CPU budget (at least 15x real time), and Auto-Set key detection.
- **Host-style test that loads the real `.vst3`.** `vocal_host_tests` drives the processor and editor (bus layouts, latency reporting, presets, state save/restore, latency-aligned bypass null test, Auto-Set) and writes a GUI snapshot. It then loads the built `.vst3` binary through JUCE's `VST3PluginFormat`, the same way a DAW does: it scans it, creates an instance, checks the latency, processes audio, opens the editor and saves state.

## Building

Requirements: CMake 3.22 or newer, Visual Studio 2022 (or its Build Tools) and Git. The installer target is Windows/MSVC-specific.

JUCE is not included in this repository. CMake uses `external/JUCE` if it exists. Otherwise, it downloads JUCE 9.0.4 automatically with `FetchContent`.

```sh
# Optional: use a local JUCE clone instead of the automatic download
git clone --branch 9.0.4 --depth 1 https://github.com/juce-framework/JUCE.git external/JUCE

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target VegaVocal_VST3 vocal_core_tests vocal_host_tests --parallel
```

The plugin is written to `build/VegaVocal_artefacts/Release/VST3/Vega Vocal.vst3`. To install it, copy it to `C:\Program Files\Common Files\VST3`.

Optional single-file installer (requires the Release VST3):

```sh
cmake --build build --config Release --target VegaVocalSetup
```

## Running tests

| Target | What it covers |
| --- | --- |
| `vocal_core_tests` | The DSP chain alone (no JUCE), at 44.1, 48 and 96 kHz. |
| `vocal_host_tests` | The full plugin driven like a host, a GUI snapshot and, if a path is given, the real `.vst3` loaded through the VST3 interface. |

```sh
build\Release\vocal_core_tests.exe
build\vocal_host_tests_artefacts\Release\vocal_host_tests.exe docs\screenshot.png "build\VegaVocal_artefacts\Release\VST3\Vega Vocal.vst3"
```

Each check prints `PASS`/`FAIL`, and the process exits with a non-zero code if any check fails.

## Project structure

```
Source/
  VocalCore.h           Entire DSP chain: pitch detection, PSOLA, de-esser, compressor,
                        saturation, delay, reverb, Auto-Set (no JUCE)
  PluginProcessor.*     Parameters, factory presets, state, tempo sync, latency-aligned bypass
  PluginEditor.*        GUI: module panels, tuner display, meters, Auto-Set button
tests/
  core_tests.cpp        Unit tests for VocalCore.h at several sample rates
  host_tests.cpp        Whole-plugin tests, GUI snapshot, real .vst3 loading
installer/
  setup.cpp             Windows installer/uninstaller with the plugin embedded
  setup.rc.in, setup_ids.h, LEEME.txt (end-user instructions, Spanish)
CMakeLists.txt
```

The user interface, parameter names and preset names are in Spanish. The code and comments are in English.

## License

This project is licensed under the **GNU Affero General Public License v3.0** (see [LICENSE](LICENSE)). It is built with [JUCE](https://juce.com), which is used under the AGPLv3.

Copyright (C) 2026 Brandon Duran Pineda
