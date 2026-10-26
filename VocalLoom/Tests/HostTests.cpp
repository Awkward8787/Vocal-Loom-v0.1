#include <CoreFoundation/CoreFoundation.h>
#include "PluginProcessor.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
thread_local bool countAllocations = false;
thread_local size_t allocations = 0;
int checks = 0;
void check(bool result, const char* description) {
    ++checks; if (!result) throw std::runtime_error(description);
}
void pump() { for (int i = 0; i < 12; ++i) CFRunLoopRunInMode(kCFRunLoopDefaultMode, .01, false); }
juce::Component* find(juce::Component& parent, const juce::String& name) {
    if (parent.getName() == name) return &parent;
    if (auto* button = dynamic_cast<juce::Button*>(&parent)) if (button->getButtonText() == name) return button;
    for (auto* child : parent.getChildren()) if (auto* result = find(*child, name)) return result;
    return nullptr;
}
void screenshot(juce::Component& editor, const juce::File& path) {
    juce::PNGImageFormat png;
    juce::FileOutputStream stream(path);
    stream.setPosition(0); stream.truncate();
    check(stream.openedOk() && png.writeImageToStream(editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.5f), stream), "Write native UI snapshot");
}
void blank(VocalLoomProcessor& processor, int samples) {
    juce::AudioBuffer<float> audio(2, samples); audio.clear(); juce::MidiBuffer midi;
    processor.processBlock(audio, midi);
}
} // namespace

void* operator new(std::size_t size) {
    if (countAllocations) ++allocations;
    if (void* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }

int main(int argc, char** argv) {
    try {
        juce::ScopedJuceInitialiser_GUI initialise;
        VocalLoomProcessor processor;
        processor.setRateAndBufferSizeDetails(48000, 128);
        processor.prepareToPlay(48000, 128);
        check(processor.getLatencySamples() == 576, "Reported nominal latency");
        processor.setControlValue("speed", 7); processor.setControlValue("root", 8);
        processor.setControlValue("scale", 2); processor.setControlValue("vocoder", 1);
        juce::MemoryBlock saved; processor.getStateInformation(saved);
        processor.setControlValue("speed", 150); processor.setControlValue("root", 0);
        processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        check(std::abs(processor.parameter("speed") - 7) < .001f && static_cast<int>(processor.parameter("root")) == 8, "Preset state round trip");
        const char bad[] = "not a preset";
        processor.setStateInformation(bad, sizeof(bad));
        check(std::abs(processor.parameter("speed") - 7) < .001f, "Malformed state leaves settings intact");
        processor.setCurrentProgram(3);
        check(static_cast<int>(processor.parameter("root")) == 8 && static_cast<int>(processor.parameter("scale")) == 2, "Factory presets preserve song key and scale");
        check(processor.parameter("tune") < .5f && processor.parameter("vocoder") > .5f && processor.parameter("source") > .5f, "Chrome preset enables MIDI vocoder without tuning");

        juce::AudioBuffer<float> audio(2, 64); audio.clear(); juce::MidiBuffer events;
        events.ensureSize(4096); events.addEvent(juce::MidiMessage::noteOn(1, 60, .8f), 63);
        processor.processBlock(audio, events); check(processor.midiNotes.load() == 1, "MIDI note at last sample accepted");
        events.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
        events.addEvent(juce::MidiMessage::noteOff(1, 60), 1);
        processor.processBlock(audio, events); check(processor.midiNotes.load() == 1, "Host sustain holds note");
        events.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
        processor.processBlock(audio, events); check(processor.midiNotes.load() == 0, "Host sustain release clears held note");
        events.addEvent(juce::MidiMessage::noteOn(2, 70, .9f), 0); processor.processBlock(audio, events);
        processor.requestPanic(); processor.processBlock(audio, events); check(processor.midiNotes.load() == 0, "UI panic reaches audio thread");

        processor.setControlValue("bypass", 1);
        for (int i = 0; i < 200; ++i) { audio.clear(); processor.processBlock(audio, events); }
        int impulseAt = -1; float impulseValue = 0;
        for (int b = 0; b < 20; ++b) {
            audio.clear(); if (b == 0) audio.setSample(0, 0, .5f);
            processor.processBlock(audio, events);
            for (int i = 0; i < 64; ++i) if (audio.getSample(0, i) > .49f) { impulseAt = b * 64 + i; impulseValue = audio.getSample(0, i); }
            for (int i = 0; i < 64; ++i) check(std::abs(audio.getSample(1, i)) < .0001f, "Bypass preserves stereo separation");
        }
        check(impulseAt == 576 && std::abs(impulseValue - .5f) < .0001f, "Bypass is raw and delay matched");
        processor.setControlValue("bypass", 0); processor.setControlValue("output", 6); processor.setControlValue("input", 24);
        processor.setControlValue("vocoder", 0);
        for (int b = 0; b < 160; ++b) {
            for (int i = 0; i < 64; ++i) for (int c = 0; c < 2; ++c) audio.setSample(c, i, static_cast<float>(4 * std::sin((b * 64 + i) * .09)));
            processor.processBlock(audio, events);
            if (b > 120) for (int i = 0; i < 64; ++i) check(std::isfinite(audio.getSample(0, i)) && std::abs(audio.getSample(0, i)) < .991f, "Soft ceiling remains finite and bounded");
        }
        audio.clear(); processor.processBlock(audio, events);
        allocations = 0; countAllocations = true;
        for (int i = 0; i < 200; ++i) processor.processBlock(audio, events);
        countAllocations = false;
        check(allocations == 0, "No C++ heap allocations in 200 warmed render callbacks");
        std::cout << "Render callback C++ heap allocations: " << allocations << " in 200 warmed blocks.\n";

        for (double rate : { 44100., 48000., 96000. }) for (int block : { 1, 32, 64, 128, 511, 1024 }) {
            processor.setRateAndBufferSizeDetails(rate, block); processor.prepareToPlay(rate, block);
            juce::AudioBuffer<float> buffer(2, block); buffer.clear();
            for (int i = 0; i < 4; ++i) { processor.processBlock(buffer, events);
                for (int c = 0; c < 2; ++c) for (int s = 0; s < block; ++s) check(std::isfinite(buffer.getSample(c, s)), "Sample rate/block-size render is finite");
            }
        }
        {
            auto layout = processor.getBusesLayout();
            layout.inputBuses.set(0, juce::AudioChannelSet::stereo());
            layout.outputBuses.set(0, juce::AudioChannelSet::mono());
            check(processor.setBusesLayout(layout), "Stereo-to-mono layout supported");
            processor.prepareToPlay(48000, 64); processor.setControlValue("tune", 0); processor.setControlValue("vocoder", 0);
            processor.setControlValue("input", 0); processor.setControlValue("output", -3);
            processor.setControlValue("cleanup", 0); processor.setControlValue("echo", 0); processor.setControlValue("space", 0);
            double rightOnlyEnergy = 0;
            for (int b = 0; b < 80; ++b) {
                audio.clear();
                for (int i = 0; i < 64; ++i) audio.setSample(1, i, static_cast<float>(.2 * std::sin((b * 64 + i) * .07)));
                processor.processBlock(audio, events);
                for (int i = 0; i < 64; ++i) rightOnlyEnergy += audio.getSample(0, i) * audio.getSample(0, i);
            }
            check(rightOnlyEnergy > .1, "Right input survives stereo-to-mono processing");
            layout.outputBuses.set(0, juce::AudioChannelSet::stereo());
            check(processor.setBusesLayout(layout), "Restore stereo output");
        }
        processor.setRateAndBufferSizeDetails(48000, 128); processor.prepareToPlay(48000, 128);
        processor.setCurrentProgram(0);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setVisible(true); pump();
        auto click = [&](const char* name) {
            auto* b = dynamic_cast<juce::Button*>(find(*editor, name)); check(b != nullptr, "Find native button"); b->triggerClick(); pump();
        };
        click("Play MIDI"); check(processor.parameter("source") > .5f, "MIDI mode button updates parameter");
        click("Vocoder"); check(processor.parameter("vocoder") > .5f, "Vocoder toggle updates parameter");
        click("Pitch correction"); check(processor.parameter("tune") < .5f, "Tuning toggle independent of vocoder");
        check(find(*editor, "Vocoder vowel shift")->isEnabled(), "Vocoder controls enabled");
        check(!find(*editor, "Retune speed")->isEnabled(), "Tuning controls disabled when tuning off");
        auto* slider = dynamic_cast<juce::Slider*>(find(*editor, "Vocoder vowel shift"));
        slider->setValue(3.5, juce::sendNotificationSync);
        check(std::abs(processor.parameter("vowel") - 3.5f) < .01f, "Native knob attachment updates parameter");
        auto* root = dynamic_cast<juce::ComboBox*>(find(*editor, "Song key"));
        root->setSelectedId(10, juce::sendNotificationSync);
        check(static_cast<int>(processor.parameter("root")) == 9, "Key selector updates parameter");
        processor.keyboard.noteOn(1, 60, .8f); blank(processor, 128);
        check(processor.midiNotes.load() == 1, "Audition keyboard reaches audio thread");
        click("Panic"); blank(processor, 128); check(processor.midiNotes.load() == 0, "Panic button releases audition notes");
        if (argc > 1) {
            auto folder = juce::File(argv[1]); folder.createDirectory();
            processor.setControlValue("input", 0); processor.setControlValue("output", -3);
            processor.setCurrentProgram(3); pump(); screenshot(*editor, folder.getChildFile("Vocal-Loom-Chrome.png"));
            processor.setCurrentProgram(1); pump(); screenshot(*editor, folder.getChildFile("Vocal-Loom-Tuning.png"));
        }
        std::cout << "PASS: processor state, MIDI, bypass, sample-rate/block-size, soft ceiling, render allocation and native UI checks (" << checks << " assertions).\n";
        editor.reset();
        return 0;
    } catch (const std::exception& error) { countAllocations = false; std::cerr << "FAIL: " << error.what() << "\n"; return 1; }
}
