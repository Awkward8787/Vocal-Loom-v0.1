#include "DSP.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int assertions = 0;
void require(bool condition, const std::string& name) {
    ++assertions;
    if (!condition) throw std::runtime_error(name);
}
double detected(double frequency, double rate, bool harmonics = false) {
    loom::PitchDetector detector; detector.prepare(rate);
    for (int i = 0; i < static_cast<int>(rate * .3); ++i) {
        const double phase = 2 * loom::pi * frequency * i / rate;
        const float x = static_cast<float>(harmonics ? .14 * std::sin(phase) + .23 * std::sin(2 * phase) + .1 * std::sin(3 * phase) : .3 * std::sin(phase));
        detector.push(x);
    }
    require(detector.voiced, "Pitch must be voiced at " + std::to_string(frequency));
    return detector.frequency;
}
void wav(const std::filesystem::path& path, const std::vector<float>& samples, uint32_t sr) {
    std::ofstream file(path, std::ios::binary);
    auto u16 = [&](uint16_t x) { file.put(static_cast<char>(x & 255)); file.put(static_cast<char>((x >> 8) & 255)); };
    auto u32 = [&](uint32_t x) { u16(static_cast<uint16_t>(x)); u16(static_cast<uint16_t>(x >> 16)); };
    const auto bytes = static_cast<uint32_t>(samples.size() * 2);
    file.write("RIFF", 4); u32(bytes + 36); file.write("WAVEfmt ", 8); u32(16); u16(1); u16(1);
    u32(sr); u32(sr * 2); u16(2); u16(16); file.write("data", 4); u32(bytes);
    for (float x : samples) u16(static_cast<uint16_t>(static_cast<int16_t>(loom::clamp(x, -1, 1) * 32767)));
    require(file.good(), "Write audio example");
}
float voice(double t) {
    const double note = 57.3 + 1.6 * std::sin(t * 1.7) + .06 * std::sin(t * 32);
    // Continuous phase for a fixed detuned test vowel, amplitude-shaped like speech.
    const double f = loom::hz(note);
    (void) f;
    const double phase = 2 * loom::pi * loom::hz(57.3) * t;
    double value = 0;
    for (int h = 1; h < 24; ++h) {
        const double fh = loom::hz(57.3) * h;
        const double formant = .2 + 2 * std::exp(-std::pow((fh - 700) / 250, 2)) + std::exp(-std::pow((fh - 1300) / 300, 2));
        value += std::sin(phase * h) * formant / h;
    }
    const double envelope = .25 + .75 * std::pow(std::sin(t * 8), 2);
    return static_cast<float>(value * envelope * .15);
}
} // namespace

int main(int argc, char** argv) {
    try {
        for (double rate : { 44100., 48000., 96000. })
            for (double frequency : { 73.42, 110., 220., 440., 783.99 }) {
                const double estimate = detected(frequency, rate);
                require(std::abs(1200 * std::log2(estimate / frequency)) < 8, "Pitch accuracy within 8 cents");
            }
        for (double frequency : { 98., 164.81, 329.63 })
            require(std::abs(1200 * std::log2(detected(frequency, 48000, true) / frequency)) < 8, "Harmonic pitch accuracy");
        std::cout << "Pitch detection: 18 steady-tone cases across 44.1/48/96 kHz within 8 cents.\n";
        require(loom::snap(61, 0, 1) == 60, "C major excludes C sharp");
        require(loom::allowed(63, 0, 2), "C minor includes Eb");
        require(!loom::allowed(64, 0, 2), "C minor excludes E");
        for (int root = 0; root < 12; ++root) for (int scale = 0; scale < 4; ++scale)
            for (int n = 24; n < 96; ++n) require(loom::allowed(static_cast<int>(loom::snap(n + .27, root, scale)), root, scale), "All scale targets belong to scale");
        loom::PitchShifter shifter; shifter.prepare(48000);
        int first = -1;
        for (int i = 0; i < 1500; ++i) {
            auto out = shifter.process(i == 0 ? 1.f : 0.f, 1);
            if (std::abs(out[0]) > .5f && first < 0) first = i;
        }
        require(first == 576, "Unshifted path impulse delay is 576 samples at 48kHz");
        std::cout << "Unshifted nominal path delay: " << first << " samples (12 ms at 48 kHz).\n";
        loom::Carrier carrier; carrier.prepare(48000);
        carrier.noteOn(1, 60, 1); carrier.pedal(1, true); carrier.noteOff(1, 60);
        require(carrier.activeNotes() == 1, "Sustain holds released key");
        carrier.pedal(1, false); require(carrier.activeNotes() == 0, "Pedal release releases note");
        for (int i = 0; i < 20; ++i) carrier.noteOn(1, 48 + i, .8f);
        require(carrier.activeNotes() == 8, "Polyphony bounded to eight notes");
        carrier.allOff(1, true); require(carrier.activeNotes() == 0, "Panic kills notes");

        loom::Engine correction; correction.prepare(48000);
        loom::Params p; p.speed = 1; p.humanize = 0; p.cleanup = 0; p.output = 0; correction.set(p);
        loom::PitchDetector correctedDetector; correctedDetector.prepare(48000);
        const double detuned = loom::hz(57.35);
        for (int i = 0; i < 48000 * 2; ++i) {
            const auto out = correction.process(static_cast<float>(.25 * std::sin(2 * loom::pi * detuned * i / 48000)));
            if (i > 48000) correctedDetector.push(out[0]);
        }
        require(correctedDetector.voiced, "Corrected tone remains voiced");
        const double correctedCents = 1200 * std::log2(correctedDetector.frequency / 220.0);
        std::cout << "Correction test: input +35 cents, output " << correctedCents << " cents relative to A3.\n";
        require(std::abs(correctedCents) < 15, "Steady correction reaches target within 15 cents");

        loom::Engine robot; robot.prepare(48000); p.tune = false; p.vocoder = true; p.source = 1; robot.set(p);
        double silenceEnergy = 0, robotEnergy = 0;
        for (int i = 0; i < 24000; ++i) { auto out = robot.process(voice(i / 48000.0)); if (i > 12000) silenceEnergy += out[0] * out[0]; }
        require(silenceEnergy < 1e-8, "MIDI vocoder is silent without notes");
        robot.carrier.noteOn(1, 57, .8f); robot.carrier.noteOn(1, 64, .8f);
        for (int i = 0; i < 24000; ++i) { auto out = robot.process(voice(i / 48000.0)); robotEnergy += out[0] * out[0]; }
        require(robotEnergy > .01, "MIDI vocoder works with tuning switched off");
        robot.carrier.reset(); require(robot.carrier.activeNotes() == 0, "Panic resets all notes");

        for (double rate : { 44100., 48000., 96000. }) {
            loom::Engine extreme; extreme.prepare(rate);
            p = {}; p.vocoder = true; p.vowel = 12; p.bite = 1; p.cleanup = 1; p.input = 24; p.output = 6; extreme.set(p);
            uint32_t random = 1;
            for (int i = 0; i < static_cast<int>(rate * .3); ++i) {
                random = random * 1664525u + 1013904223u;
                const float in = static_cast<float>(static_cast<int32_t>(random)) / 2147483648.f;
                auto out = extreme.process(in * 4);
                require(std::isfinite(out[0]), "Finite output under extreme input");
            }
            require(std::isfinite(extreme.process(std::nanf(""))[0]), "NaN input sanitized");
        }

        const auto begin = std::chrono::steady_clock::now();
        loom::Engine benchmark; benchmark.prepare(48000); p = {}; p.vocoder = true; benchmark.set(p);
        double checksum = 0;
        for (int i = 0; i < 480000; ++i) checksum += benchmark.process(voice(i / 48000.0))[0];
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        std::cout << "DSP benchmark: 10 seconds rendered in " << elapsed << " seconds; " << elapsed * 10 << "% of one core offline. Checksum " << checksum << ".\n";
        if (argc > 1) {
            const std::filesystem::path folder(argv[1]); std::filesystem::create_directories(folder);
            std::vector<float> dry(144000), tuned(dry.size()), chrome(dry.size()), following(dry.size());
            loom::Engine a, b, c; a.prepare(48000); b.prepare(48000); c.prepare(48000);
            p = {}; p.speed = 3; p.humanize = 0; p.cleanup = .2f; a.set(p);
            p.tune = false; p.vocoder = true; p.source = 1; p.character = 1; p.bite = .85f; p.vowel = 2; b.set(p);
            p.source = 0; p.tune = true; c.set(p);
            b.carrier.noteOn(1, 57, .8f);
            for (size_t i = 0; i < dry.size(); ++i) {
                if (i == 48000) { b.carrier.noteOff(1, 57); b.carrier.noteOn(1, 60, .8f); }
                if (i == 96000) { b.carrier.noteOff(1, 60); b.carrier.noteOn(1, 64, .8f); }
                dry[i] = voice(static_cast<double>(i) / 48000.0);
                tuned[i] = a.process(dry[i])[0]; chrome[i] = b.process(dry[i])[0]; following[i] = c.process(dry[i])[0];
            }
            wav(folder / "01-synthetic-vowel-input.wav", dry, 48000);
            wav(folder / "02-pitch-correction-prototype.wav", tuned, 48000);
            wav(folder / "03-chrome-midi-keys.wav", chrome, 48000);
            wav(folder / "04-chrome-voice-follow.wav", following, 48000);
            std::cout << "Wrote synthetic DSP examples to " << folder << ". These are not recorded singer tests.\n";
        }
        std::cout << "PASS: " << assertions << " assertions.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << "\n"; return 1; }
}
