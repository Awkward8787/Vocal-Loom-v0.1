#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Audio-thread code has bounded work, fixed-capacity note storage, and no locks.
namespace loom {
constexpr double pi = 3.14159265358979323846;
inline float clamp(float x, float a, float b) { return std::clamp(x, a, b); }
inline double hz(double midi) { return 440.0 * std::exp2((midi - 69.0) / 12.0); }
inline double midi(double frequency) { return 69.0 + 12.0 * std::log2(frequency / 440.0); }
inline float gain(float db) { return std::pow(10.f, db / 20.f); }
inline float follow(float current, float target, float c) { return target + c * (current - target); }

struct Params {
    bool tune = true, vocoder = false, bypass = false;
    int source = 0, character = 0, root = 0, scale = 0, octave = 0, echoDivision = 0;
    float speed = 20.f, amount = 1.f, humanize = .15f;
    float vocoderMix = 1.f, vowel = 0.f, bite = .3f, release = 80.f, glide = 25.f;
    float input = 0.f, output = -3.f, cleanup = .35f, echo = 0.f, space = 0.f;
};

inline bool allowed(int note, int root, int scale) {
    constexpr std::array<unsigned, 4> masks { 0xfff, 0xab5, 0x5ad, 0x4a9 };
    const int degree = ((note - root) % 12 + 12) % 12;
    return (masks[static_cast<size_t>(std::clamp(scale, 0, 3))] & (1u << degree)) != 0;
}
inline double snap(double note, int root, int scale) {
    int best = static_cast<int>(std::round(note));
    double distance = 1000;
    for (int n = best - 12; n <= best + 12; ++n)
        if (allowed(n, root, scale) && std::abs(n - note) < distance) {
            distance = std::abs(n - note); best = n;
        }
    return best;
}

class PitchDetector {
public:
    void prepare(double rate) {
        decimation = std::max(1, static_cast<int>(std::round(rate / 16000.0)));
        analysisRate = rate / decimation;
        maxLag = std::min(480, static_cast<int>(analysisRate / 65.0));
        minLag = std::max(2, static_cast<int>(analysisRate / 1000.0));
        window = std::min(512, static_cast<int>(analysisRate * .020));
        needed = window + maxLag + 2;
        lowpassCoefficient = static_cast<float>(std::exp(-2.0 * pi * 2400.0 / rate));
        reset();
    }
    void reset() {
        ring.fill(0); diff.fill(0); normalized.fill(1); low1 = low2 = 0;
        position = collected = counter = hop = 0; frequency = confidence = 0;
        voiced = false;
    }
    void push(float x) {
        low1 = follow(low1, x, lowpassCoefficient);
        low2 = follow(low2, low1, lowpassCoefficient);
        if (++counter < decimation) return;
        counter = 0;
        ring[static_cast<size_t>(position)] = low2;
        position = (position + 1) % capacity;
        collected = std::min(collected + 1, needed);
        if (++hop >= 64 && collected >= needed) { hop = 0; analyze(); }
    }
    double frequency = 0;
    float confidence = 0;
    bool voiced = false;
    double frameMilliseconds() const { return 1000.0 * needed / analysisRate; }
private:
    static constexpr int capacity = 2048;
    std::array<float, capacity> ring {};
    std::array<double, 514> diff {}, normalized {};
    int decimation = 3, maxLag = 246, minLag = 16, window = 320, needed = 568;
    int position = 0, collected = 0, counter = 0, hop = 0;
    double analysisRate = 16000;
    float low1 = 0, low2 = 0, lowpassCoefficient = 0;
    float at(int n) const { return ring[static_cast<size_t>((position - needed + n + capacity * 2) % capacity)]; }
    void analyze() {
        double energy = 0;
        for (int i = 0; i < window; ++i) energy += at(i) * at(i);
        if (energy / window < 1.0e-7) { voiced = false; confidence = 0; return; }
        double running = 0;
        normalized[0] = 1;
        for (int lag = 1; lag <= maxLag; ++lag) {
            double sum = 0;
            for (int i = 0; i < window; ++i) {
                const double delta = at(i) - at(i + lag);
                sum += delta * delta;
            }
            diff[static_cast<size_t>(lag)] = sum; running += sum;
            normalized[static_cast<size_t>(lag)] = running > 1e-20 ? sum * lag / running : 1;
        }
        int candidate = -1;
        for (int lag = minLag; lag < maxLag - 1; ++lag)
            if (normalized[static_cast<size_t>(lag)] < .16) {
                while (lag < maxLag - 1 && normalized[static_cast<size_t>(lag + 1)] < normalized[static_cast<size_t>(lag)]) ++lag;
                candidate = lag; break;
            }
        if (candidate < 0) { voiced = false; confidence = 0; return; }
        const double a = normalized[static_cast<size_t>(candidate - 1)];
        const double b = normalized[static_cast<size_t>(candidate)];
        const double c = normalized[static_cast<size_t>(candidate + 1)];
        const double denominator = a - 2 * b + c;
        const double offset = std::abs(denominator) > 1e-12 ? .5 * (a - c) / denominator : 0;
        frequency = analysisRate / (candidate + std::clamp(offset, -.5, .5));
        confidence = static_cast<float>(1 - b); voiced = true;
    }
};

// Two complementary Hann-window delay taps. The dry path uses the same nominal
// delay. Shifted grains span 2..22 ms; 12 ms is the nominal latency, not a bound
// on pitch-tracker acquisition or total interface-to-headphone round trip.
class PitchShifter {
public:
    void prepare(double rate) {
        span = rate * .020; minimum = rate * .002;
        delay.assign(static_cast<size_t>(std::ceil(rate * .06)) + 8, 0);
        position = 0; phase = 0; shiftBlend = 0; ratio = 1;
        latency = static_cast<int>(std::round(minimum + .5 * span));
        smoothing = static_cast<float>(std::exp(-1.0 / (.005 * rate)));
    }
    std::array<float, 2> process(float input, double targetRatio) {
        delay[static_cast<size_t>(position)] = input;
        ratio += .002 * (targetRatio - ratio);
        const float dry = read(latency);
        shiftBlend = follow(shiftBlend, std::abs(ratio - 1) > .0005 ? 1.f : 0.f, smoothing);
        phase += (1.0 - ratio) / span;
        phase -= std::floor(phase);
        double second = phase + .5; second -= std::floor(second);
        const float weight = static_cast<float>(.5 - .5 * std::cos(2 * pi * phase));
        const float shifted = weight * read(minimum + span * phase) + (1 - weight) * read(minimum + span * second);
        const float wet = dry + shiftBlend * (shifted - dry);
        position = (position + 1) % static_cast<int>(delay.size());
        return { wet, dry };
    }
    int latency = 576;
private:
    std::vector<float> delay;
    int position = 0;
    double span = 960, minimum = 96, phase = 0, ratio = 1;
    float shiftBlend = 0, smoothing = 0;
    float read(double samples) const {
        double p = position - samples;
        if (p < 0) p += delay.size();
        const int i = static_cast<int>(p);
        const float fraction = static_cast<float>(p - i);
        return delay[static_cast<size_t>(i)] * (1 - fraction)
            + delay[static_cast<size_t>((i + 1) % static_cast<int>(delay.size()))] * fraction;
    }
};

struct Bandpass {
    double a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0;
    void configure(double rate, double frequency, double q) {
        const double g = std::tan(pi * std::clamp(frequency, 40.0, rate * .42) / rate);
        k = 1.0 / q; a1 = 1.0 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    float process(float x) {
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        return static_cast<float>(k * v1);
    }
    void reset() { ic1 = ic2 = 0; }
};

class Carrier {
public:
    void prepare(double rate) { sr = rate; reset(); }
    void reset() { voices = {}; sustain.fill(false); bends.fill(0); age = 0; followPhase = 0; followHz = 220; followEnvelope = 0; }
    void noteOn(int channel, int note, float velocity) {
        Voice* selected = nullptr;
        for (auto& v : voices) if (v.note == note && v.channel == channel && v.active) { selected = &v; break; }
        if (!selected) for (auto& v : voices) if (!v.active) { selected = &v; break; }
        if (!selected) selected = &*std::min_element(voices.begin(), voices.end(), [](const Voice& a, const Voice& b) { return a.age < b.age; });
        *selected = Voice {}; selected->active = selected->held = true;
        selected->note = note; selected->channel = channel; selected->velocity = velocity; selected->age = ++age;
    }
    void noteOff(int channel, int note) {
        for (auto& v : voices) if (v.active && v.note == note && v.channel == channel) {
            v.held = false; if (!sustain[static_cast<size_t>(channel - 1)]) v.released = true;
        }
    }
    void pedal(int channel, bool down) {
        sustain[static_cast<size_t>(channel - 1)] = down;
        if (!down) for (auto& v : voices) if (v.channel == channel && !v.held) v.released = true;
    }
    void allOff(int channel, bool immediately) {
        sustain[static_cast<size_t>(channel - 1)] = false;
        for (auto& v : voices) if (v.channel == channel) {
            if (immediately) v.active = false;
            else { v.held = false; v.released = true; }
        }
    }
    void bend(int channel, float semitones) { bends[static_cast<size_t>(channel - 1)] = semitones; }
    int activeNotes() const { int n = 0; for (const auto& v : voices) if (v.active && !v.released) ++n; return n; }
    float process(bool useMidi, double voiceNote, bool voiced, int octave, float bite, float glide, float release) {
        const float attackC = static_cast<float>(std::exp(-1.0 / (.003 * sr)));
        const float releaseC = static_cast<float>(std::exp(-1.0 / (std::max(10.f, release) * .001 * sr)));
        float sum = 0;
        for (auto& v : voices) if (v.active) {
            v.envelope = follow(v.envelope, v.released ? 0.f : v.velocity, v.released ? releaseC : attackC);
            const double frequency = hz(v.note + octave * 12 + bends[static_cast<size_t>(v.channel - 1)]);
            const float signal = oscillator(v.phase, frequency, bite);
            if (useMidi) sum += signal * v.envelope;
            if (v.released && v.envelope < .0001f) v.active = false;
        }
        const float glideC = static_cast<float>(std::exp(-1.0 / (std::max(1.f, glide) * .001 * sr)));
        followHz = follow(static_cast<float>(followHz), static_cast<float>(hz(voiceNote + octave * 12)), glideC);
        followEnvelope = follow(followEnvelope, voiced ? .8f : 0.f, voiced ? attackC : releaseC);
        const float following = oscillator(followPhase, followHz, bite) * followEnvelope;
        return useMidi ? sum * .33f : following * .65f;
    }
private:
    struct Voice { bool active = false, held = false, released = false; int note = 0, channel = 1; uint64_t age = 0; double phase = 0; float envelope = 0, velocity = 0; };
    std::array<Voice, 8> voices {};
    std::array<bool, 16> sustain {};
    std::array<float, 16> bends {};
    double sr = 48000, followPhase = 0, followHz = 220;
    float followEnvelope = 0;
    uint64_t age = 0;
    static double blep(double t, double dt) {
        if (t < dt) { t /= dt; return t + t - t * t - 1; }
        if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
        return 0;
    }
    float oscillator(double& phase, double frequency, float bite) const {
        const double dt = std::clamp(frequency / sr, .00001, .2);
        phase += dt; phase -= std::floor(phase);
        const double saw = 2 * phase - 1 - blep(phase, dt);
        double shifted = phase + .5; shifted -= std::floor(shifted);
        const double pulse = (phase < .5 ? 1.0 : -1.0) + blep(phase, dt) - blep(shifted, dt);
        return static_cast<float>((1 - bite * .5) * saw + bite * .5 * pulse);
    }
};

class Vocoder {
public:
    void prepare(double rate) {
        sr = rate; lastVowel = 100; lastCharacter = -1;
        envelopes.fill(0); for (auto& b : analysis) b.reset(); for (auto& b : synthesis) b.reset();
        attackC = static_cast<float>(std::exp(-1.0 / (.003 * sr)));
        set(0, 0, 80);
    }
    void set(float vowel, int character, float release) {
        releaseC = static_cast<float>(std::exp(-1.0 / (std::max(10.f, release) * .001 * sr)));
        if (std::abs(vowel - lastVowel) < .001f && character == lastCharacter) return;
        lastVowel = vowel; lastCharacter = character;
        for (size_t i = 0; i < bands; ++i) {
            const double frequency = 110.0 * std::pow(7000.0 / 110.0, static_cast<double>(i) / (bands - 1));
            const double q = character == 1 ? 4.0 : 6.0;
            analysis[i].configure(sr, frequency, q);
            synthesis[i].configure(sr, frequency * std::exp2(vowel / 12.0), q);
        }
    }
    float process(float speech, float carrier, float bite) {
        float sum = 0;
        for (size_t i = 0; i < bands; ++i) {
            const float level = std::abs(analysis[i].process(speech));
            envelopes[i] = follow(envelopes[i], level, level > envelopes[i] ? attackC : releaseC);
            const float articulation = lastCharacter == 1 ? (i > 7 && i < 21 ? 1.5f : .45f) : 1.f;
            sum += synthesis[i].process(carrier) * envelopes[i] * articulation;
        }
        const float drive = 1.f + bite * 2.f;
        return std::tanh(sum * 12.f * drive) / drive;
    }
private:
    static constexpr size_t bands = 24;
    std::array<Bandpass, bands> analysis {}, synthesis {};
    std::array<float, bands> envelopes {};
    double sr = 48000;
    float attackC = 0, releaseC = 0, lastVowel = 100;
    int lastCharacter = -1;
};

class Engine {
public:
    void prepare(double rate) {
        sr = rate; detector.prepare(sr); shifter.prepare(sr); carrier.prepare(sr); vocoder.prepare(sr);
        smooth = Params {}; tuneBlend = 0; vocoderBlend = 0; bypassBlend = 0;
        dcX = dcY = low = 0; env = 0; gainReduction = 1; correction = 0; targetNote = 69; noteValid = false;
        smoothC = static_cast<float>(std::exp(-1.0 / (.010 * sr)));
        dcC = static_cast<float>(std::exp(-2.0 * pi * 70.0 / sr));
        lowC = static_cast<float>(std::exp(-2.0 * pi * 4500.0 / sr));
        attackC = static_cast<float>(std::exp(-1.0 / (.004 * sr)));
        releaseC = static_cast<float>(std::exp(-1.0 / (.100 * sr)));
        latency = shifter.latency;
    }
    void set(const Params& p) { params = p; vocoder.set(p.vowel, p.character, p.release); }
    std::array<float, 2> process(float input) {
        smooth.input = follow(smooth.input, params.input, smoothC);
        smooth.output = follow(smooth.output, params.output, smoothC);
        smooth.amount = follow(smooth.amount, params.amount, smoothC);
        smooth.vocoderMix = follow(smooth.vocoderMix, params.vocoderMix, smoothC);
        smooth.cleanup = follow(smooth.cleanup, params.cleanup, smoothC);
        smooth.bite = follow(smooth.bite, params.bite, smoothC);
        input = std::isfinite(input) ? clamp(input * gain(smooth.input), -8.f, 8.f) : 0.f;
        const float dryInput = input;
        const float hp = input - dcX + dcC * dcY; dcX = input; dcY = hp;
        input = hp;
        const float absolute = std::abs(input);
        env = follow(env, absolute, absolute > env ? attackC : releaseC);
        const float gate = clamp((env - .00015f) / .001f, 0.f, 1.f);
        input *= 1 - smooth.cleanup + smooth.cleanup * gate;
        const float desiredGain = env > .18f ? std::pow(.18f / env, .6f) : 1.f;
        gainReduction = follow(gainReduction, desiredGain, desiredGain < gainReduction ? attackC : releaseC);
        input *= 1.f + smooth.cleanup * (gainReduction - 1.f);
        low = follow(low, input, lowC);
        const float high = input - low;
        input -= high * smooth.cleanup * clamp((std::abs(high) - .05f) * 3.f, 0.f, .6f);
        detector.push(input);
        const bool valid = detector.voiced && detector.confidence > .84f;
        const double detectedNote = valid ? midi(detector.frequency) : (noteValid ? targetNote : 69.0);
        if (valid) {
            const double nearest = snap(detectedNote, params.root, params.scale);
            if (!noteValid || !allowed(static_cast<int>(targetNote), params.root, params.scale)
                || std::abs(detectedNote - nearest) + .12 < std::abs(detectedNote - targetNote)) targetNote = nearest;
            noteValid = true;
        } else noteValid = false;
        const double error = valid ? targetNote - detectedNote : 0.0;
        const double softened = error * (1.0 - params.humanize * std::exp(-std::abs(error) * 5));
        const double time = std::max(1.f, params.speed) * .001;
        const double c = std::exp(-1.0 / (time * sr));
        correction = softened + c * (correction - softened);
        tuneBlend = follow(tuneBlend, params.tune ? 1.f : 0.f, smoothC);
        vocoderBlend = follow(vocoderBlend, params.vocoder ? smooth.vocoderMix : 0.f, smoothC);
        bypassBlend = follow(bypassBlend, params.bypass ? 1.f : 0.f, smoothC);
        const double ratio = std::exp2(clamp(static_cast<float>(correction * smooth.amount * tuneBlend), -12.f, 12.f) / 12.0);
        const auto shifted = shifter.process(input, ratio);
        const double carrierNote = detectedNote + correction * smooth.amount * tuneBlend;
        const float synth = carrier.process(params.source == 1, carrierNote, valid, params.octave, smooth.bite, params.glide, params.release);
        const float robotic = vocoder.process(shifted[1], synth, smooth.bite);
        const float processed = shifted[0] + vocoderBlend * (robotic - shifted[0]);
        inputPeak = std::max(inputPeak * .9995f, std::abs(dryInput));
        currentCorrection = static_cast<float>(correction * smooth.amount * tuneBlend * 100.0);
        // Host bypass is implemented outside this engine with a raw, delayed path.
        return { processed * gain(smooth.output), shifted[1] };
    }
    Params params;
    PitchDetector detector;
    Carrier carrier;
    int latency = 576;
    float inputPeak = 0, currentCorrection = 0, bypassBlend = 0;
private:
    double sr = 48000, correction = 0, targetNote = 69;
    bool noteValid = false;
    Params smooth;
    PitchShifter shifter;
    Vocoder vocoder;
    float tuneBlend = 0, vocoderBlend = 0, smoothC = 0, dcX = 0, dcY = 0, low = 0, env = 0;
    float gainReduction = 1, dcC = 0, lowC = 0, attackC = 0, releaseC = 0;
};
} // namespace loom
