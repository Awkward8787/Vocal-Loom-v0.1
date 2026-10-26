#pragma once
#include "PluginProcessor.h"
#include "Theme.h"

class VocalLoomEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit VocalLoomEditor(VocalLoomProcessor&);
    ~VocalLoomEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    struct Knob {
        juce::Slider slider;
        juce::Label label;
        juce::String id;
        std::unique_ptr<SliderAttachment> attachment;
    };
    VocalLoomProcessor& audioProcessor;
    studio::Look look;
    juce::TooltipWindow tooltip { this, 600 };
    std::array<Knob, 13> knobs;
    juce::TextButton follow { "Follow voice" }, midi { "Play MIDI" }, panic { "Panic" }, save { "Save" }, load { "Load" };
    juce::ToggleButton tune { "Pitch correction" }, vocoder { "Vocoder" }, bypass { "Bypass" };
    juce::ComboBox root, scale, character, octave, division, preset;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    juce::MidiKeyboardComponent keys;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String noteText = "Sing to begin", hint = "", statusText;
    float displayedInput = 0, displayedOutput = 0, displayedConfidence = 0, displayedCents = 0;
    void timerCallback() override;
    void attach(juce::ComboBox&, const char*, const juce::StringArray&);
    void placeKnob(int, juce::Rectangle<int>);
    void savePreset();
    void loadPreset();
    void showFileError(const juce::String&);
    void drawPanel(juce::Graphics&, juce::Rectangle<int>);
    void drawMeter(juce::Graphics&, juce::Rectangle<int>, float, const juce::String&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalLoomEditor)
};
