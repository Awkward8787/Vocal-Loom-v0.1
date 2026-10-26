#include "PluginEditor.h"

using namespace juce;
using studio::Theme;
VocalLoomEditor::VocalLoomEditor(VocalLoomProcessor& p)
    : AudioProcessorEditor(p), audioProcessor(p), keys(p.keyboard, MidiKeyboardComponent::horizontalKeyboard) {
    setLookAndFeel(&look); setName("Vocal Loom live vocal audioProcessor");
    setSize(Theme::width, Theme::height); setResizable(false, false);
    const std::array<const char*, 13> ids { "speed", "amount", "humanize", "vocoderMix", "vowel", "bite",
        "input", "cleanup", "release", "glide", "echo", "space", "output" };
    const std::array<const char*, 13> names { "Retune", "Amount", "Humanize", "Blend", "Vowel", "Bite",
        "Input", "Cleanup", "Release", "Glide", "Echo", "Space", "Output" };
    const std::array<const char*, 13> tips {
        "Time to approach the target note. Lower values create a harder tuning effect.",
        "Strength of pitch correction. The song key and scale determine allowed notes.",
        "Reduces small corrections near a target note. This prototype does not preserve formants.",
        "Blends the vocoder with the processed vocal. Requires Vocoder to be enabled.",
        "Moves the vocoder's synthesis bands. Changes robotic vowel character, not the dry voice's formants.",
        "Blends saw and pulse carrier waves and adds saturation.",
        "Input trim before voice analysis. Aim for healthy levels without clipping your interface.",
        "Combines a gentle gate, compression and high-frequency taming. A clean microphone signal still matters.",
        "Release time for vocoder bands and synth notes.",
        "Glide time when the synth follows your voice. MIDI keyboard notes use direct pitch control.",
        "Tempo-synced stereo echo send. Uses the host tempo, or 120 BPM if unavailable.",
        "Stereo room reverb send. Turn down for a dry live monitor.",
        "Output trim before the soft ceiling. The ceiling is not a true-peak limiter." };
    for (size_t i = 0; i < knobs.size(); ++i) {
        auto& k = knobs[i]; k.id = ids[i];
        k.slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle(Slider::TextBoxBelow, false, 86, 22);
        k.slider.setName(audioProcessor.state.getParameter(ids[i])->getName(100));
        k.slider.setTooltip(tips[i]); k.slider.setWantsKeyboardFocus(true);
        k.slider.setDoubleClickReturnValue(true, audioProcessor.state.getParameter(ids[i])->convertFrom0to1(audioProcessor.state.getParameter(ids[i])->getDefaultValue()));
        const bool percent = i == 1 || i == 2 || i == 3 || i == 5 || i == 7 || i == 10 || i == 11;
        if (percent) {
            k.slider.textFromValueFunction = [](double value) { return String(roundToInt(value * 100)) + "%"; };
            k.slider.valueFromTextFunction = [](const String& value) { return value.getDoubleValue() * .01; };
        } else if (i == 0 || i == 8 || i == 9) k.slider.setTextValueSuffix(" ms");
        else if (i == 4) k.slider.setTextValueSuffix(" st");
        else k.slider.setTextValueSuffix(" dB");
        k.label.setText(names[i], dontSendNotification); k.label.setFont(Theme::font(Theme::body, true));
        k.label.setJustificationType(Justification::centred); k.label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(k.slider); addAndMakeVisible(k.label);
        k.attachment = std::make_unique<SliderAttachment>(p.state, ids[i], k.slider);
        // The attachment installs its own formatter; apply UI percentages after it.
        if (percent) {
            k.slider.textFromValueFunction = [](double value) { return String(roundToInt(value * 100)) + "%"; };
            k.slider.valueFromTextFunction = [](const String& value) { return value.getDoubleValue() * .01; };
            k.slider.updateText();
        }
    }
    attach(root, "root", { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" });
    attach(scale, "scale", { "Chromatic", "Major", "Minor", "Minor pentatonic" });
    attach(character, "character", { "Prism vocoder", "Chrome talk style" });
    attach(octave, "octave", { "-1 octave", "Unison", "+1 octave" });
    attach(division, "echoDivision", { "Echo: 1/8", "Echo: 1/4", "Echo: dotted 1/8" });
    for (int i = 0; i < p.getNumPrograms(); ++i) preset.addItem(p.getProgramName(i), i + 1);
    preset.setSelectedId(p.getCurrentProgram() + 1, dontSendNotification); preset.setName("Factory preset");
    preset.onChange = [this] { audioProcessor.setCurrentProgram(preset.getSelectedId() - 1); }; addAndMakeVisible(preset);
    for (auto* b : { &follow, &midi, &panic, &save, &load }) { addAndMakeVisible(b); b->setWantsKeyboardFocus(true); }
    for (auto* b : { &tune, &vocoder, &bypass }) { addAndMakeVisible(b); b->setWantsKeyboardFocus(true); }
    buttonAttachments.push_back(std::make_unique<ButtonAttachment>(p.state, "tune", tune));
    buttonAttachments.push_back(std::make_unique<ButtonAttachment>(p.state, "vocoder", vocoder));
    buttonAttachments.push_back(std::make_unique<ButtonAttachment>(p.state, "bypass", bypass));
    follow.onClick = [this] { audioProcessor.setControlValue("source", 0); };
    midi.onClick = [this] { audioProcessor.setControlValue("source", 1); };
    panic.onClick = [this] { audioProcessor.keyboard.allNotesOff(0); audioProcessor.requestPanic(); };
    save.onClick = [this] { savePreset(); }; load.onClick = [this] { loadPreset(); };
    follow.setTooltip("The synth follows the notes you sing. With pitch correction on, corrected notes drive it.");
    midi.setTooltip("Your MIDI keyboard controls the synth pitches. Sing to shape the sound. Enable Vocoder to hear it.");
    panic.setTooltip("Release all synth notes, sustain and pitch bend.");
    tune.setTooltip("Enable or disable pitch correction independently of the vocoder.");
    vocoder.setTooltip("Your voice shapes the synth. Choose Follow voice or Play MIDI for its notes.");
    bypass.setTooltip("Hear the original stereo input through a delay-matched bypass. Output trim and ceiling are bypassed too.");
    root.setTooltip("Select your song's key. This plug-in does not automatically detect the song key.");
    scale.setTooltip("Chromatic allows all twelve notes. Major, minor and minor pentatonic restrict tuning targets.");
    preset.setTooltip("Factory starting points. Song key, scale and input/output trim are preserved. Save stores all settings.");
    keys.setAvailableRange(36, 96); keys.setLowestVisibleKey(48); keys.setKeyWidth(25); keys.setMidiChannel(1); keys.setOctaveForMiddleC(4);
    keys.setName("Audition MIDI keyboard");
    keys.setColour(MidiKeyboardComponent::whiteNoteColourId, Theme::text);
    keys.setColour(MidiKeyboardComponent::blackNoteColourId, Theme::background);
    keys.setColour(MidiKeyboardComponent::keyDownOverlayColourId, Theme::accent.withAlpha(.8f));
    keys.setColour(MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::accent.withAlpha(.3f));
    addAndMakeVisible(keys);
    startTimerHz(Theme::tickHz); timerCallback();
}
VocalLoomEditor::~VocalLoomEditor() { stopTimer(); setLookAndFeel(nullptr); }
void VocalLoomEditor::attach(ComboBox& combo, const char* id, const StringArray& options) {
    combo.addItemList(options, 1); combo.setName(audioProcessor.state.getParameter(id)->getName(100));
    addAndMakeVisible(combo); comboAttachments.push_back(std::make_unique<ComboAttachment>(audioProcessor.state, id, combo));
}
void VocalLoomEditor::placeKnob(int index, Rectangle<int> box) {
    auto& k = knobs[static_cast<size_t>(index)];
    k.label.setBounds(box.removeFromTop(22)); k.slider.setBounds(box);
}
void VocalLoomEditor::resized() {
    follow.setBounds(622, 30, 142, 38); midi.setBounds(772, 30, 142, 38); bypass.setBounds(930, 32, 102, 34);
    preset.setBounds(28, 101, 226, 34); save.setBounds(264, 101, 64, 34); load.setBounds(336, 101, 64, 34);
    root.setBounds(600, 101, 80, 34); scale.setBounds(690, 101, 172, 34); division.setBounds(872, 101, 160, 34);
    tune.setBounds(46, 264, 180, 32); vocoder.setBounds(558, 264, 118, 32);
    character.setBounds(686, 264, 184, 32); octave.setBounds(880, 264, 132, 32);
    for (int i = 0; i < 3; ++i) placeKnob(i, { 49 + i * 155, 315, 148, 145 });
    for (int i = 0; i < 3; ++i) placeKnob(i + 3, { 561 + i * 155, 315, 148, 145 });
    for (int i = 0; i < 7; ++i) placeKnob(i + 6, { 47 + i * 140, 516, 128, 130 });
    keys.setBounds(46, 698, 878, 62); panic.setBounds(940, 712, 72, 34);
}
void VocalLoomEditor::drawPanel(Graphics& g, Rectangle<int> bounds) {
    g.setColour(Theme::surface); g.fillRoundedRectangle(bounds.toFloat(), Theme::radius);
    g.setColour(Theme::line.withAlpha(.65f)); g.drawRoundedRectangle(bounds.toFloat(), Theme::radius, 1);
}
void VocalLoomEditor::drawMeter(Graphics& g, Rectangle<int> bounds, float peak, const String& name) {
    g.setColour(Theme::muted); g.setFont(Theme::font(Theme::small));
    const float db = Decibels::gainToDecibels(peak, -60.f);
    g.drawText(name + "  " + (db < -59 ? String("-inf") : String(db, 1)) + " dB", bounds.removeFromTop(20), Justification::centredLeft);
    auto bar = bounds.removeFromTop(7).toFloat();
    g.setColour(Theme::raised); g.fillRoundedRectangle(bar, 3);
    g.setColour(peak >= .99f ? Theme::danger : Theme::accent);
    g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * jlimit(0.f, 1.f, (db + 60.f) / 60.f)), 3);
}
void VocalLoomEditor::paint(Graphics& g) {
    g.fillAll(Theme::background);
    g.setColour(Theme::text); g.setFont(Theme::font(Theme::display, true));
    g.drawText("VOCAL LOOM", 28, 20, 360, 48, Justification::centredLeft);
    g.setColour(Theme::muted); g.setFont(Theme::font(Theme::small));
    g.drawText("LIVE VOCAL PROCESSOR   /   DEVELOPMENT BUILD 0.1", 30, 68, 460, 20, Justification::centredLeft);
    g.drawText("SONG KEY", 512, 101, 80, 34, Justification::centredRight);
    drawPanel(g, { 28, 155, 1004, 86 });
    g.setColour(Theme::muted); g.setFont(Theme::font(Theme::small)); g.drawText("VOICE TRACKING", 48, 167, 170, 18, Justification::centredLeft);
    g.setColour(Theme::text); g.setFont(Theme::font(Theme::heading, true)); g.drawText(noteText, 48, 187, 220, 30, Justification::centredLeft);
    g.setFont(Theme::font(Theme::small)); g.setColour(Theme::muted);
    g.drawText("Confidence  " + String(roundToInt(displayedConfidence * 100)) + "%", 286, 174, 160, 20, Justification::centredLeft);
    g.drawText("Correction  " + String(displayedCents, 0) + " cents", 286, 198, 180, 20, Justification::centredLeft);
    drawMeter(g, { 500, 177, 148, 46 }, displayedInput, "IN");
    drawMeter(g, { 676, 177, 148, 46 }, displayedOutput, "OUT");
    g.setFont(Theme::font(Theme::body, true)); g.setColour(Theme::accent);
    g.drawText(statusText, 854, 174, 156, 22, Justification::centredLeft);
    g.setFont(Theme::font(Theme::small)); g.setColour(Theme::muted);
    g.drawText("Nominal DSP delay", 854, 199, 156, 18, Justification::centredLeft);
    drawPanel(g, { 28, 251, 492, 225 }); drawPanel(g, { 540, 251, 492, 225 });
    g.setColour(Theme::muted); g.setFont(Theme::font(Theme::small));
    g.drawText("01  /  TUNE", 360, 265, 140, 28, Justification::centredRight);
    drawPanel(g, { 28, 492, 1004, 167 });
    g.setColour(Theme::muted);
    g.drawText("VOCAL FINISH", 48, 495, 200, 22, Justification::centredLeft);
    g.drawText(hint, 48, 666, 964, 24, Justification::centredLeft);
    g.setColour(Theme::line); g.drawRoundedRectangle(Rectangle<float>(28, 691, 1004, 80), Theme::radius, 1);
}
void VocalLoomEditor::timerCallback() {
    const bool midiMode = audioProcessor.parameter("source") > .5f;
    follow.setToggleState(!midiMode, dontSendNotification); midi.setToggleState(midiMode, dontSendNotification);
    const bool tuning = audioProcessor.parameter("tune") > .5f, robotic = audioProcessor.parameter("vocoder") > .5f;
    for (int i = 0; i < 3; ++i) knobs[static_cast<size_t>(i)].slider.setEnabled(tuning);
    for (int i = 3; i < 6; ++i) knobs[static_cast<size_t>(i)].slider.setEnabled(robotic);
    knobs[8].slider.setEnabled(robotic); knobs[9].slider.setEnabled(robotic && !midiMode);
    character.setEnabled(robotic); octave.setEnabled(robotic);
    keys.setEnabled(midiMode);
    displayedInput = audioProcessor.inputMeter.load(); displayedOutput = audioProcessor.outputMeter.load();
    displayedConfidence = audioProcessor.confidence.load(); displayedCents = audioProcessor.cents.load();
    const float f = audioProcessor.frequency.load();
    if (f > 0 && displayedConfidence > .84f) {
        const int note = roundToInt(loom::midi(f));
        noteText = MidiMessage::getMidiNoteName(note, true, true, 4) + "  /  " + String(f, 1) + " Hz";
    } else noteText = displayedInput < .002f ? "Sing to begin" : "Unvoiced / searching";
    const double rate = audioProcessor.getSampleRate();
    statusText = rate > 0 ? String(1000.0 * audioProcessor.getLatencySamples() / rate, 1) + " ms" : "Audio stopped";
    if (midiMode && robotic) hint = "PLAY MIDI  /  " + String(audioProcessor.midiNotes.load()) + " notes held. Sing to shape the synth. Sustain pedal and pitch bend supported.";
    else if (midiMode) hint = "Enable Vocoder to hear keyboard-controlled vocals. Pitch correction remains independent.";
    else if (robotic) hint = "FOLLOW VOICE  /  Sing one clear note at a time. Enable pitch correction to tune the synth's melody.";
    else hint = "LIVE TUNING  /  Select your song key and scale. Use headphones and disable duplicate dry monitoring in your interface.";
    if (preset.getSelectedId() != audioProcessor.getCurrentProgram() + 1) preset.setSelectedId(audioProcessor.getCurrentProgram() + 1, dontSendNotification);
    repaint();
}
void VocalLoomEditor::showFileError(const String& message) {
    AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "Preset file", message);
}
void VocalLoomEditor::savePreset() {
    chooser = std::make_unique<FileChooser>("Save Vocal Loom preset", File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("My vocal.vloom"), "*.vloom");
    chooser->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
        [safe = Component::SafePointer<VocalLoomEditor>(this)](const FileChooser& fc) {
            if (!safe) return; const auto file = fc.getResult(); if (file == File()) return;
            MemoryBlock block; safe->audioProcessor.getStateInformation(block);
            if (!file.withFileExtension("vloom").replaceWithData(block.getData(), block.getSize())) safe->showFileError("Could not save the preset. Choose a writable folder.");
        });
}
void VocalLoomEditor::loadPreset() {
    chooser = std::make_unique<FileChooser>("Load Vocal Loom preset", File::getSpecialLocation(File::userDocumentsDirectory), "*.vloom");
    chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
        [safe = Component::SafePointer<VocalLoomEditor>(this)](const FileChooser& fc) {
            if (!safe) return; const auto file = fc.getResult(); if (file == File()) return;
            MemoryBlock block;
            if (file.getSize() > 1024 * 1024 || !file.loadFileAsData(block)) { safe->showFileError("Could not read this preset file."); return; }
            const auto xml = AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
            if (!xml || !xml->hasTagName("VocalLoomState") || xml->getIntAttribute("schema", 1) != 1) {
                safe->showFileError("Choose a compatible Vocal Loom preset (.vloom)."); return;
            }
            safe->audioProcessor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        });
}
