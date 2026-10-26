# Validation: development build 0.1

Run on Anthony's Apple Silicon Mac, October 26, 2026. Native C++17 release build using Apple Clang and JUCE 8.0.12. AU, VST3 and standalone targets compiled.

## Automated checks

The latest CTest run passed **3/3 test groups**:

| Test | Result and scope |
| --- | --- |
| DSP | 18 steady sinusoidal/harmonic pitch cases at 44.1/48/96 kHz within 8 cents; all scale/root combinations produce allowed targets; 576-sample unshifted impulse delay at 48 kHz; one +35-cent detuned A3 corrected to about +1.58 cents; sustain, bounded polyphony, panic, MIDI vocoder with tuning off, and finite output under stressed inputs. |
| ProcessorAndUI | Session-state round trip; malformed-state rejection; preset key preservation; MIDI note/sustain/panic handling; stereo bypass isolation and exact impulse delay; soft-ceiling bounds; 18 rate/block-size combinations (44.1/48/96 kHz and 1/32/64/128/511/1024 samples); native source/toggle/knob/key controls; audition keyboard queue; real editor snapshots. |
| AUBundle | Loads the actual compiled AU bundle, calls its exported public plug-in interface, initializes and renders stereo float audio at 44.1/48/96 kHz, automates the tuning/vocoder/source parameters, sends a MIDI note, and saves/restores AU class state. MIDI vocoder output remains nonzero with tuning off. This uses a direct test interface with no system component instance, so it does not test system registry discovery, host callbacks or full host integration. |

The warmed processor callback produced **0 intercepted C++ `new` allocations across 200 blocks**. This check does not intercept every C allocation or prove that the host/framework is entirely real-time safe. Parameter values are cached as atomic pointers; long SysEx messages are ignored rather than copied during rendering. No files, network requests, mutexes or dynamic containers are used by the project's DSP render path.

The DSP benchmark rendered 10 seconds of generated input in about 0.46 seconds in this session. This is an offline measurement, includes generated-test-input work, and is not a deadline/dropout guarantee for a loaded FL Studio project.

## Latency

- Unshifted/dry and raw bypass: 576 samples at 48 kHz, measured by an impulse.
- AU reported nominal latency: 11.9955 ms at 44.1 kHz; 12 ms at 48/96 kHz.
- Shifted grains use moving taps spanning approximately 2-22 ms; 12 ms is nominal alignment, not a fixed delay for every shifted transient.
- Pitch tracking needs about 35.5 ms of recent signal at 48 kHz, plus update/retune behavior. Actual voice-onset and note-change response has not been acceptance-tested on a singer.
- Hardware microphone-to-headphone round-trip delay is unmeasured.

## Interface checks

The actual native editor was rendered and visually inspected in its tuning and Chrome MIDI states. Percent formatting, labels, mode-dependent enabled states, keyboard octave labels, and input/output trim display were corrected during QA.

Measured palette pairs using the design kit's contrast.py:

| Pair | Measured ratio |
| --- | --- |
| Main text #eef3ed on panel #192126 | 14.51:1 |
| Secondary text #a7b4b9 on panel #192126 | 7.67:1 |
| Selected-button text #18200c on accent #c9f17b | 13.07:1 |
| Interactive boundary #74878f on raised surface #242e34 | 3.70:1 (UI graphics) |

These are palette measurements, not a complete WCAG or VoiceOver audit. Standard JUCE slider, button, combo and keyboard accessibility behavior is used. Browser state, responsive and axe gates do not exercise this native AU editor and were not claimed as passes. The kit's emoji scanner reported no scannable C++ files; it was not counted as a pass. No independent-agent design review was performed.

## Installation and remaining validation

The new bundles were installed in the user's Audio Plug-Ins/Components and Audio Plug-Ins/VST3 directories, each under its own Vocal Loom name. The local bundles are ad-hoc signed; both installed plug-in signatures were verified on disk. The final installed AU also passed the direct interface render test. The binaries target macOS 11.0 or newer on arm64, but runtime testing was performed only on this Mac.

`auval -v aumf VcLm AnJo -strict` could not find the component from this terminal session. `auval -l` returned no discoverable Audio Units, so the system registry check remains unresolved in this environment. This is **not an auval pass**. The direct AU test is a separate, narrower check.

Still required before professional use or release:

- FL Studio scan/discovery, MIDI routing, automation, project reopen and live monitoring on the user's actual audio interface.
- Full Apple auval validation from a session with working component-registry access.
- Listening tests with real isolated vocal recordings and supplied song references, including low notes, slides, vibrato, sibilants, onset, consonants and room noise.
- Formant-preserving correction and a higher-quality shifting algorithm if the prototype's coloration is unacceptable.
- VoiceOver, keyboard-focus traversal, native Save/Load chooser interactions, repeated editor open/close, and additional host testing.
- Intel/universal builds, release signing, notarization and distribution licensing.

Synthetic audio examples in AudioExamples are test vowels, not singer recordings or evidence of artist matching.
