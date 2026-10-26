# Vocal Loom 0.1

A native Mac vocal processor for Anthony Jones. Working name. This is a development prototype, not a finished professional pitch-correction product.

## What works in this build

- Independent pitch correction and vocoder switches.
- Voice-follow carrier: the synth follows your sung pitch, corrected when tuning is enabled.
- MIDI carrier: eight synth voices, chords, velocity, sustain, channel note-off, and pitch bend (fixed +/-2 semitones). Sing into the microphone to articulate the synth. MIDI notes alone do not produce a vocal sound.
- A 24-band vocoder and a brighter, saturated, talk-box-inspired character called Chrome. This is software vocal synthesis, not a physical talk box emulation.
- Song root and chromatic, major, natural minor, or minor pentatonic scales. Key selection is manual.
- Retune speed, tuning amount, humanize, vocoder blend, vowel-band shift, carrier bite, octave, voice-follow glide, and release controls.
- Gentle input gate, compression and high-frequency taming controlled by Cleanup. Stereo tempo-synced echo, reverb, input/output trims, meters and a zero-lookahead soft ceiling.
- Host automation and session recall, five factory starting points, and user preset Save/Load (.vloom).
- Audition keyboard, panic button, and delay-matched stereo bypass.

## Start in FL Studio on Mac

1. Install `Vocal Loom.component` into `~/Library/Audio/Plug-Ins/Components/`, or use the VST3 version in `~/Library/Audio/Plug-Ins/VST3/`. The supplied Install.command copies both builds to these user folders and refuses to replace an existing installation.
2. In FL Studio, open **Options > Manage plugins** and run a scan with **Verify plugins** enabled. Find **Vocal Loom** and load it on your microphone's mixer insert. AU supports MIDI input through its Music Effect type; VST3 is supplied as an alternative for host routing.
3. Select your microphone input on that mixer insert. Start with headphones and a low monitor level. Disable your interface's direct dry monitor if you want to hear only the processed voice.
4. Start at 48 kHz and a 64- or 128-sample device buffer if your interface and project can run it without dropouts. These are starting settings, not a guaranteed round-trip latency.
5. For melodic tuning, select **Clean stage**, set your song key and scale, and enable **Pitch correction**. Faster Retune creates a harder tuning effect. Set Echo and Space to zero while judging the live monitor feel.
6. For the keyboard-controlled sound, select **Chrome funk** or **MIDI prism**. **Play MIDI** and **Vocoder** should be enabled. Pitch correction can remain off. Route your keyboard through FL Studio to this effect; match the wrapper's MIDI input port to a MIDI Out channel's port, then select that MIDI Out channel for live playing. Exact routing must still be checked in the installed FL Studio version. The on-screen keyboard is an immediate test of the plug-in's internal note path.
7. For automatic robotic melody, enable **Follow voice** and **Vocoder**. Enable **Pitch correction** to snap the carrier melody to your chosen scale. Sing one clear note at a time.

Factory presets are original starting points, not artist-endorsed replicas. They preserve the song key, scale and input/output trims. Save/Load stores all parameters. Host project saving also preserves the settings.

## Latency and current limits

The nominal plug-in delay is **12 ms** (576 samples at 48 kHz), reported to the host. The bypass and unshifted path have this measured delay. The granular pitch shifter uses delay taps spanning approximately **2 to 22 ms**, with 12 ms nominal alignment. Shifted transients do not have a single invariant delay. Delay compensation cannot remove the delay you hear while singing live.

The pitch detector analyzes about **35.5 ms** of recent audio at 48 kHz, updates about every 4 ms and adds the selected retune response. This acquisition/response time is separate from audio-path delay. Interface converters, driver buffers, FL Studio routing and other plug-ins add more round-trip delay. Actual microphone-to-headphone delay has not been measured.

The pitch-correction path is an initial time-domain granular engine. It can produce modulation, transient smearing or coloration, especially at large shifts, low notes and noisy inputs. It does **not** yet preserve the natural voice's formants. Humanize reduces small corrections near a note; it is not a complete vibrato-preservation algorithm. The Vowel control shifts the vocoder's synthesis bands only. Pitch detection is monophonic, approximately 65-1000 Hz, intended for an isolated voice. The stereo input is summed to mono for vocal processing; echo/reverb restore stereo width. Bypass preserves the original stereo input.

The soft ceiling operates on sample peaks with no lookahead; it is not a true-peak limiter. Bypass restores the original input, including its level. Desktop builds are Apple Silicon (arm64), locally ad-hoc signed, and not Developer ID signed or notarized for public distribution.

Real singer recordings, microphone/interface round-trip latency, listening comparisons, VoiceOver and a full FL Studio live session remain acceptance tests. Do not interpret synthetic-tone tests as proof of professional vocal quality.

## Development

Requires Xcode/Command Line Tools, Git, CMake 3.22+ and internet access for the initial JUCE fetch. JUCE is pinned to 8.0.12, commit `29396c22c93392d6738e021b83196283d6e4d850`.

```sh
cmake -S . -B /path/to/work/vocal-loom-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build /path/to/work/vocal-loom-build -j 6
ctest --test-dir /path/to/work/vocal-loom-build --output-on-failure
```

An existing checkout can be supplied with `-DJUCE_DIR=/absolute/path/to/JUCE`. For an Intel or universal build, set the CMake architecture accordingly; those builds have not been tested here. `build.sh` is a convenience wrapper. The DSP test uses no JUCE dependency; the processor/UI test uses the real native editor.

## Source layout

- `Source/DSP.h`: scale logic, pitch detection, granular shifting, carrier and filter-bank vocoder.
- `Source/PluginProcessor.*`: AU/VST3 host integration, parameters, MIDI, state, echo, reverb and bypass.
- `Source/PluginEditor.*` and `Theme.h`: native interface and semantic studio theme.
- `Tests/`: deterministic DSP and processor/UI regression checks.
- `docs/`: interface adapter and validation report.

## Next sound-quality milestones

1. Test clean sung vowels, consonants, slides, vibrato, breath and quiet/low notes using Anthony's recordings.
2. Compare the current granular tuner with a better formant-aware engine under the same measured latency budget; tune tracking hysteresis and onset handling.
3. Measure round-trip monitoring delay on the actual interface and validate FL Studio's MIDI routing and project recall.
4. Tune vocoder consonant intelligibility and Chrome's vowel articulation by ear. Refine the presets against supplied song references.
5. Finish release signing, installer/update behavior and broader host testing after the sound is approved.

References: [FL Studio installation and supported formats](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm), [JUCE CMake API](https://github.com/juce-framework/JUCE/blob/8.0.12/docs/CMake%20API.md). Framework and dependency notices are in THIRD_PARTY.md.
