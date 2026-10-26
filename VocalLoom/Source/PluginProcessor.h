#pragma once
#include <JuceHeader.h>
#include "DSP.h"

class VocalLoomProcessor final : public juce::AudioProcessor, private juce::MidiKeyboardStateListener {
public:
    VocalLoomProcessor();
    ~VocalLoomProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vocal Loom"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }
    int getNumPrograms() override { return 5; }
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    void setControlValue(const juce::String&, float);
    void requestPanic() { panic.store(true); }
    float parameter(const char*) const;
    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboard;
    std::atomic<float> inputMeter { 0 }, outputMeter { 0 }, frequency { 0 }, confidence { 0 }, cents { 0 };
    std::atomic<int> midiNotes { 0 };
    double trackerFrameMs = 0;
private:
    static constexpr std::array<const char*, 22> controlIds {
        "tune", "vocoder", "bypass", "source", "character", "root", "scale", "octave", "echoDivision",
        "speed", "amount", "humanize", "vocoderMix", "vowel", "bite", "release", "glide", "input", "output", "cleanup", "echo", "space"
    };
    std::array<std::atomic<float>*, controlIds.size()> controlValues {};
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    loom::Params readParameters() const;
    void render(juce::AudioBuffer<float>&, juce::MidiBuffer&, bool hostBypass);
    void handleMidi(const juce::MidiMessage&);
    void handleNoteOn(juce::MidiKeyboardState*, int, int, float) override;
    void handleNoteOff(juce::MidiKeyboardState*, int, int, float) override;
    void enqueue(int, int, float);
    struct NoteEvent { int channel = 1, note = 60; float velocity = 0; };
    juce::AbstractFifo fifo { 128 };
    std::array<NoteEvent, 128> events {};
    std::atomic<bool> panic { false };
    std::atomic<int> currentProgram { 0 };
    loom::Engine engine;
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbParams;
    std::array<std::vector<float>, 2> rawDelay, echoDelay;
    int rawPosition = 0, echoPosition = 0;
    double sampleRateHz = 48000, bpm = 120;
    float echoLowL = 0, echoLowR = 0, echoSamples = 12000, smoothEcho = 0, smoothSpace = 0, smoothOutput = 0;
    float fxSmoothing = 0, outputEnvelope = 0;
    void applyPreset(int);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalLoomProcessor)
};
