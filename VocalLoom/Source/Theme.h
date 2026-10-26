#pragma once
#include <JuceHeader.h>

// Native JUCE adapter: all visual primitives live here. Components consume
// semantic roles; JUCE provides accessibility handlers, keyboard navigation,
// popup dismissal and scaling. The studio panel uses one dark theme.
namespace studio {
struct Theme {
    static inline const juce::Colour background { 0xff101518 }, surface { 0xff192126 }, raised { 0xff242e34 };
    static inline const juce::Colour text { 0xffeef3ed }, muted { 0xffa7b4b9 }, line { 0xff405158 };
    static inline const juce::Colour controlEdge { 0xff74878f };
    static inline const juce::Colour accent { 0xffc9f17b }, ink { 0xff18200c }, danger { 0xffffa69c };
    static constexpr int width = 1060, height = 790, margin = 28, gap = 16, radius = 12;
    static constexpr float body = 14, small = 12, heading = 20, display = 36;
    static constexpr int tickHz = 24, target = 32;
    static juce::Font font(float size, bool bold = false) {
        return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
    }
};
class Look final : public juce::LookAndFeel_V4 {
public:
    Look() {
        setColour(juce::Label::textColourId, Theme::text);
        setColour(juce::ComboBox::backgroundColourId, Theme::raised);
        setColour(juce::ComboBox::textColourId, Theme::text);
        setColour(juce::ComboBox::outlineColourId, Theme::controlEdge);
        setColour(juce::ComboBox::focusedOutlineColourId, Theme::accent);
        setColour(juce::ComboBox::arrowColourId, Theme::muted);
        setColour(juce::PopupMenu::backgroundColourId, Theme::surface);
        setColour(juce::PopupMenu::textColourId, Theme::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::accent);
        setColour(juce::PopupMenu::highlightedTextColourId, Theme::ink);
        setColour(juce::TextButton::buttonColourId, Theme::raised);
        setColour(juce::TextButton::buttonOnColourId, Theme::accent);
        setColour(juce::TextButton::textColourOffId, Theme::text);
        setColour(juce::TextButton::textColourOnId, Theme::ink);
        setColour(juce::ToggleButton::textColourId, Theme::text);
        setColour(juce::ToggleButton::tickColourId, Theme::accent);
        setColour(juce::ToggleButton::tickDisabledColourId, Theme::controlEdge);
        setColour(juce::Slider::textBoxTextColourId, Theme::text);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::CaretComponent::caretColourId, Theme::accent);
        setColour(juce::TooltipWindow::backgroundColourId, Theme::raised);
        setColour(juce::TooltipWindow::textColourId, Theme::text);
        setColour(juce::TooltipWindow::outlineColourId, Theme::line);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return Theme::font(Theme::body); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return Theme::font(Theme::body, true); }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override {
        auto bounds = b.getLocalBounds().toFloat().reduced(1);
        auto colour = b.getToggleState() ? Theme::accent : Theme::raised;
        if (hover) colour = colour.brighter(.08f);
        if (down) colour = colour.darker(.12f);
        g.setColour(colour.withMultipliedAlpha(b.isEnabled() ? 1.f : .4f));
        g.fillRoundedRectangle(bounds, 6);
        g.setColour(b.hasKeyboardFocus(true) ? Theme::accent : Theme::controlEdge);
        g.drawRoundedRectangle(bounds, 6, b.hasKeyboardFocus(true) ? 2.f : 1.f);
    }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float value,
                         float start, float end, juce::Slider& slider) override {
        const auto box = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)).reduced(8);
        const float radius = juce::jmin(box.getWidth(), box.getHeight()) * .5f;
        const auto centre = box.getCentre();
        const float angle = start + value * (end - start);
        const float alpha = slider.isEnabled() ? 1.f : .35f;
        juce::Path track, arc;
        track.addCentredArc(centre.x, centre.y, radius, radius, 0, start, end, true);
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0, start, angle, true);
        const juce::PathStrokeType stroke(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(Theme::controlEdge.withAlpha(alpha)); g.strokePath(track, stroke);
        g.setColour(Theme::accent.withAlpha(alpha)); g.strokePath(arc, stroke);
        const float face = radius - 7;
        g.setColour((slider.isMouseOverOrDragging() ? Theme::raised.brighter(.1f) : Theme::raised).withAlpha(alpha));
        g.fillEllipse(centre.x - face, centre.y - face, face * 2, face * 2);
        g.setColour(Theme::text.withAlpha(alpha));
        const juce::Point<float> tip(centre.x + std::sin(angle) * (face - 5), centre.y - std::cos(angle) * (face - 5));
        g.drawLine(centre.x + std::sin(angle) * (face - 14), centre.y - std::cos(angle) * (face - 14), tip.x, tip.y, 2);
        if (slider.hasKeyboardFocus(true)) { g.setColour(Theme::accent); g.drawEllipse(box.expanded(3), 2); }
    }
};
} // namespace studio
