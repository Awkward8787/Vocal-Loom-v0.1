#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;
AudioProcessorValueTreeState::ParameterLayout VocalLoomProcessor::layout() {
    AudioProcessorValueTreeState::ParameterLayout p;
    auto boolean = [&](const char* id, const char* name, bool value) {
        p.add(std::make_unique<AudioParameterBool>(ParameterID { id, 1 }, name, value));
    };
    auto choice = [&](const char* id, const char* name, StringArray names, int value) {
        p.add(std::make_unique<AudioParameterChoice>(ParameterID { id, 1 }, name, names, value));
    };
    auto number = [&](const char* id, const char* name, float lo, float hi, float value, float interval, const char* unit) {
        auto range = NormalisableRange<float>(lo, hi, interval);
        if (String(id) == "speed" || String(id) == "release" || String(id) == "glide") range.setSkewForCentre((lo + hi) * .22f);
        p.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, range, value,
            AudioParameterFloatAttributes().withLabel(unit)));
    };
    boolean("tune", "Pitch correction", true); boolean("vocoder", "Vocoder", false); boolean("bypass", "Bypass", false);
    choice("source", "Carrier note source", { "Follow voice", "MIDI keyboard" }, 0);
    choice("character", "Vocoder character", { "Prism vocoder", "Chrome talk style" }, 0);
    choice("root", "Song key", { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }, 0);
    choice("scale", "Scale", { "Chromatic", "Major", "Minor", "Minor pentatonic" }, 0);
    choice("octave", "Carrier octave", { "-1 octave", "Unison", "+1 octave" }, 1);
    choice("echoDivision", "Echo rhythm", { "1/8", "1/4", "Dotted 1/8" }, 0);
    number("speed", "Retune speed", 1, 200, 20, 1, "ms");
    number("amount", "Correction amount", 0, 1, 1, .01f, "");
    number("humanize", "Humanize", 0, 1, .15f, .01f, "");
    number("vocoderMix", "Vocoder blend", 0, 1, 1, .01f, "");
    number("vowel", "Vocoder vowel shift", -12, 12, 0, .1f, "st");
    number("bite", "Carrier bite", 0, 1, .3f, .01f, "");
    number("release", "Vocoder release", 10, 300, 80, 1, "ms");
    number("glide", "Voice carrier glide", 1, 150, 25, 1, "ms");
    number("input", "Input trim", -24, 24, 0, .1f, "dB");
    number("output", "Output trim", -24, 6, -3, .1f, "dB");
    number("cleanup", "Vocal cleanup", 0, 1, .35f, .01f, "");
    number("echo", "Echo send", 0, .65f, 0, .01f, "");
    number("space", "Space send", 0, .65f, 0, .01f, "");
    return p;
}

VocalLoomProcessor::VocalLoomProcessor()
    : AudioProcessor(BusesProperties().withInput("Vocal input", AudioChannelSet::stereo(), true)
        .withOutput("Output", AudioChannelSet::stereo(), true)), state(*this, nullptr, "VocalLoomState", layout()) {
    for (size_t i = 0; i < controlIds.size(); ++i) controlValues[i] = state.getRawParameterValue(controlIds[i]);
    keyboard.addListener(this);
}
VocalLoomProcessor::~VocalLoomProcessor() { keyboard.removeListener(this); }
bool VocalLoomProcessor::isBusesLayoutSupported(const BusesLayout& b) const {
    return (b.getMainInputChannelSet() == AudioChannelSet::mono() || b.getMainInputChannelSet() == AudioChannelSet::stereo())
        && (b.getMainOutputChannelSet() == AudioChannelSet::mono() || b.getMainOutputChannelSet() == AudioChannelSet::stereo());
}
void VocalLoomProcessor::prepareToPlay(double rate, int) {
    sampleRateHz = rate; engine.prepare(rate); setLatencySamples(engine.latency);
    trackerFrameMs = engine.detector.frameMilliseconds();
    for (auto& channel : rawDelay) channel.assign(static_cast<size_t>(engine.latency + 1), 0);
    for (auto& channel : echoDelay) channel.assign(static_cast<size_t>(rate * 2.1) + 8, 0);
    rawPosition = echoPosition = 0; echoLowL = echoLowR = 0; outputEnvelope = 0;
    echoSamples = static_cast<float>(rate * .25); smoothEcho = smoothSpace = 0; smoothOutput = parameter("output");
    fxSmoothing = static_cast<float>(std::exp(-1.0 / (.020 * rate)));
    reverb.setSampleRate(rate); reverb.reset();
    reverbParams.roomSize = .65f; reverbParams.damping = .6f; reverbParams.wetLevel = 1; reverbParams.dryLevel = 0;
    reverbParams.width = .85f; reverbParams.freezeMode = 0; reverb.setParameters(reverbParams);
}
float VocalLoomProcessor::parameter(const char* id) const {
    // Avoid constructing JUCE Strings or doing ValueTree lookups in render callbacks.
    for (size_t i = 0; i < controlIds.size(); ++i) if (std::strcmp(id, controlIds[i]) == 0) return controlValues[i]->load();
    jassertfalse; return 0;
}
loom::Params VocalLoomProcessor::readParameters() const {
    loom::Params p;
    p.tune = parameter("tune") > .5f; p.vocoder = parameter("vocoder") > .5f; p.bypass = parameter("bypass") > .5f;
    p.source = static_cast<int>(parameter("source")); p.character = static_cast<int>(parameter("character"));
    p.root = static_cast<int>(parameter("root")); p.scale = static_cast<int>(parameter("scale"));
    p.octave = static_cast<int>(parameter("octave")) - 1; p.echoDivision = static_cast<int>(parameter("echoDivision"));
    p.speed = parameter("speed"); p.amount = parameter("amount"); p.humanize = parameter("humanize");
    p.vocoderMix = parameter("vocoderMix"); p.vowel = parameter("vowel"); p.bite = parameter("bite");
    p.release = parameter("release"); p.glide = parameter("glide"); p.input = parameter("input");
    p.output = 0; p.cleanup = parameter("cleanup"); p.echo = parameter("echo"); p.space = parameter("space");
    return p;
}
void VocalLoomProcessor::enqueue(int channel, int note, float velocity) {
    const auto scope = fifo.write(1);
    if (scope.blockSize1) events[static_cast<size_t>(scope.startIndex1)] = { channel, note, velocity };
    else panic.store(true);
}
void VocalLoomProcessor::handleNoteOn(MidiKeyboardState*, int channel, int note, float velocity) { enqueue(channel, note, velocity); }
void VocalLoomProcessor::handleNoteOff(MidiKeyboardState*, int channel, int note, float) { enqueue(channel, note, 0); }
void VocalLoomProcessor::handleMidi(const MidiMessage& m) {
    if (m.getChannel() < 1 || m.getChannel() > 16) return;
    if (m.isNoteOn()) engine.carrier.noteOn(m.getChannel(), m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff()) engine.carrier.noteOff(m.getChannel(), m.getNoteNumber());
    else if (m.isSustainPedalOn() || m.isSustainPedalOff()) engine.carrier.pedal(m.getChannel(), m.isSustainPedalOn());
    else if (m.isAllSoundOff()) engine.carrier.allOff(m.getChannel(), true);
    else if (m.isAllNotesOff()) engine.carrier.allOff(m.getChannel(), false);
    else if (m.isPitchWheel()) engine.carrier.bend(m.getChannel(), (m.getPitchWheelValue() - 8192) / 8192.f * 2.f);
    else if (m.isController() && m.getControllerNumber() == 121) {
        engine.carrier.pedal(m.getChannel(), false); engine.carrier.bend(m.getChannel(), 0);
    }
}
void VocalLoomProcessor::processBlock(AudioBuffer<float>& audio, MidiBuffer& midiBuffer) { render(audio, midiBuffer, false); }
void VocalLoomProcessor::processBlockBypassed(AudioBuffer<float>& audio, MidiBuffer& midiBuffer) { render(audio, midiBuffer, true); }
void VocalLoomProcessor::render(AudioBuffer<float>& audio, MidiBuffer& midiBuffer, bool hostBypass) {
    ScopedNoDenormals noDenormals;
    auto p = readParameters(); p.bypass = p.bypass || hostBypass;
    engine.set(p);
    if (panic.exchange(false)) engine.carrier.reset();
    if (auto* playHead = getPlayHead()) if (auto position = playHead->getPosition())
        if (auto tempo = position->getBpm()) bpm = std::clamp(*tempo, 30.0, 300.0);
    const float beats = p.echoDivision == 1 ? 1.f : (p.echoDivision == 2 ? .75f : .5f);
    const float desiredEchoSamples = static_cast<float>(sampleRateHz * 60.0 / bpm * beats);
    const float desiredOutput = parameter("output");
    {
        const auto scope = fifo.read(fifo.getNumReady());
        auto consume = [&](int start, int count) { for (int j = start; j < start + count; ++j) {
            const auto e = events[static_cast<size_t>(j)];
            if (e.velocity > 0) engine.carrier.noteOn(e.channel, e.note, e.velocity);
            else engine.carrier.noteOff(e.channel, e.note);
        }};
        consume(scope.startIndex1, scope.blockSize1); consume(scope.startIndex2, scope.blockSize2);
    }
    auto event = midiBuffer.cbegin();
    const int channels = getTotalNumOutputChannels();
    const int inputs = getTotalNumInputChannels();
    if (channels == 0) return;
    auto* left = audio.getWritePointer(0);
    auto* right = channels > 1 ? audio.getWritePointer(1) : nullptr;
    const auto* inputRight = inputs > 1 && audio.getNumChannels() > 1 ? audio.getReadPointer(1) : nullptr;
    const int rawSize = static_cast<int>(rawDelay[0].size());
    const int echoSize = static_cast<int>(echoDelay[0].size());
    for (int i = 0; i < audio.getNumSamples(); ++i) {
        while (event != midiBuffer.cend() && (*event).samplePosition <= i) {
            const auto metadata = *event;
            // No SysEx support; constructing a long MidiMessage would allocate.
            if (metadata.numBytes <= 3) handleMidi(metadata.getMessage());
            ++event;
        }
        const float inL = std::isfinite(left[i]) ? left[i] : 0.f;
        const float inR = inputRight && std::isfinite(inputRight[i]) ? inputRight[i] : inL;
        const auto processed = engine.process((inL + inR) * .5f);
        rawDelay[0][static_cast<size_t>(rawPosition)] = inL;
        rawDelay[1][static_cast<size_t>(rawPosition)] = inR;
        const int readRaw = (rawPosition + 1) % rawSize;
        const float dryL = rawDelay[0][static_cast<size_t>(readRaw)], dryR = rawDelay[1][static_cast<size_t>(readRaw)];
        rawPosition = readRaw;
        smoothEcho = loom::follow(smoothEcho, p.echo, fxSmoothing);
        smoothSpace = loom::follow(smoothSpace, p.space, fxSmoothing);
        smoothOutput = loom::follow(smoothOutput, desiredOutput, fxSmoothing);
        echoSamples = loom::follow(echoSamples, desiredEchoSamples, fxSmoothing);
        double read = echoPosition - echoSamples; if (read < 0) read += echoSize;
        const int e0 = static_cast<int>(read), e1 = (e0 + 1) % echoSize;
        const float fraction = static_cast<float>(read - e0);
        const float tapL = echoDelay[0][static_cast<size_t>(e0)] * (1 - fraction) + echoDelay[0][static_cast<size_t>(e1)] * fraction;
        const float tapR = echoDelay[1][static_cast<size_t>(e0)] * (1 - fraction) + echoDelay[1][static_cast<size_t>(e1)] * fraction;
        echoLowL += .12f * (tapL - echoLowL); echoLowR += .12f * (tapR - echoLowR);
        echoDelay[0][static_cast<size_t>(echoPosition)] = processed[0] * smoothEcho + echoLowR * .32f;
        echoDelay[1][static_cast<size_t>(echoPosition)] = processed[0] * smoothEcho * .75f + echoLowL * .32f;
        echoPosition = (echoPosition + 1) % echoSize;
        float revL = processed[0] * smoothSpace, revR = revL;
        reverb.processStereo(&revL, &revR, 1);
        const float outputGain = loom::gain(smoothOutput);
        float wetL = (processed[0] + echoLowL + revL) * outputGain;
        float wetR = (processed[0] + echoLowR + revR) * outputGain;
        // Zero-lookahead soft ceiling; this is not a true-peak mastering limiter.
        auto ceiling = [](float x) {
            const float a = std::abs(x);
            return a <= .85f ? x : std::copysign(.85f + .14f * std::tanh((a - .85f) / .14f), x);
        };
        wetL = ceiling(wetL); wetR = ceiling(wetR);
        const float bypass = engine.bypassBlend;
        const float outL = wetL + bypass * (dryL - wetL), outR = wetR + bypass * (dryR - wetR);
        left[i] = right ? outL : (outL + outR) * .5f;
        if (right) right[i] = outR;
        outputEnvelope = std::max(std::max(std::abs(outL), std::abs(outR)), outputEnvelope * .9995f);
    }
    while (event != midiBuffer.cend()) {
        const auto metadata = *event;
        if (metadata.numBytes <= 3) handleMidi(metadata.getMessage());
        ++event;
    }
    for (int c = 2; c < channels; ++c) audio.clear(c, 0, audio.getNumSamples());
    inputMeter.store(engine.inputPeak); outputMeter.store(outputEnvelope);
    frequency.store(engine.detector.voiced ? static_cast<float>(engine.detector.frequency) : 0.f);
    confidence.store(engine.detector.confidence); cents.store(engine.currentCorrection); midiNotes.store(engine.carrier.activeNotes());
    midiBuffer.clear();
}
AudioProcessorParameter* VocalLoomProcessor::getBypassParameter() const { return state.getParameter("bypass"); }
void VocalLoomProcessor::setControlValue(const String& id, float value) {
    if (auto* p = state.getParameter(id)) {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(value)); p->endChangeGesture();
    }
}
const String VocalLoomProcessor::getProgramName(int i) {
    static const StringArray names { "Clean stage", "Velvet drift", "Orbit lead", "Chrome funk", "MIDI prism" };
    return names[jlimit(0, names.size() - 1, i)];
}
void VocalLoomProcessor::setCurrentProgram(int i) { currentProgram.store(jlimit(0, 4, i)); applyPreset(currentProgram.load()); }
void VocalLoomProcessor::applyPreset(int i) {
    for (auto* base : getParameters()) if (auto* p = dynamic_cast<RangedAudioParameter*>(base))
        if (p->paramID != "root" && p->paramID != "scale" && p->paramID != "input" && p->paramID != "output")
            p->setValueNotifyingHost(p->getDefaultValue());
    if (i == 1) { setControlValue("speed", 35); setControlValue("humanize", .35f); setControlValue("echo", .15f); setControlValue("space", .22f); }
    if (i == 2) { setControlValue("speed", 4); setControlValue("humanize", .05f); setControlValue("echo", .22f); setControlValue("space", .14f); }
    if (i == 3) { setControlValue("tune", 0); setControlValue("vocoder", 1); setControlValue("source", 1); setControlValue("character", 1);
        setControlValue("bite", .85f); setControlValue("vowel", 2); setControlValue("release", 45); setControlValue("space", .08f); }
    if (i == 4) { setControlValue("tune", 0); setControlValue("vocoder", 1); setControlValue("source", 1);
        setControlValue("bite", .45f); setControlValue("release", 110); setControlValue("echo", .12f); setControlValue("space", .18f); }
}
void VocalLoomProcessor::getStateInformation(MemoryBlock& block) {
    auto tree = state.copyState(); tree.setProperty("schema", 1, nullptr); tree.setProperty("program", currentProgram.load(), nullptr);
    if (auto xml = tree.createXml()) copyXmlToBinary(*xml, block);
}
void VocalLoomProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) if (xml->hasTagName(state.state.getType())) {
        auto tree = ValueTree::fromXml(*xml);
        if (static_cast<int>(tree.getProperty("schema", 1)) != 1) return;
        currentProgram.store(jlimit(0, 4, static_cast<int>(tree.getProperty("program", 0))));
        state.replaceState(tree); panic.store(true);
    }
}
AudioProcessorEditor* VocalLoomProcessor::createEditor() { return new VocalLoomEditor(*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VocalLoomProcessor; }
