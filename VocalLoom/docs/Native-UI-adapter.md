# Native JUCE interface adapter

Domain: live music production.
Audience and tone: singers and producers, immediate and precise.
Mood: focused.
Motion: live meters only, no animated decoration.
Layout: performance source in the header, session settings, tracking strip, tuning and vocoder sections, finishing controls, keyboard.
Reference anchor: a studio rack with a dark graphite surface and one lime accent.

JUCE Components and LookAndFeel implement the UI directly; no browser, web fonts, network service or AI API is involved. Theme.h is the single semantic palette and typography source. Knobs use standard Slider accessibility handlers, editable values, keyboard focus and double-click reset. Buttons and ComboBoxes use JUCE keyboard and accessibility behavior. JUCE popup menus provide the selection model.

States: enabled, hover, pressed, selected and focus are drawn. Controls that do not affect the selected signal path are disabled. No async DSP state requires a loading animation. Preset I/O uses a native chooser; invalid files trigger an error. The studio surface has a fixed logical size (1060 x 790) with host/Retina scaling. A mobile web layout and browser ARIA/axe gates do not apply to this native AU editor. Automated web gates are not evidence of native accessibility. VoiceOver and full host keyboard testing remain manual checks.
