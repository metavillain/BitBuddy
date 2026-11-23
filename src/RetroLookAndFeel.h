#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bb
{
// A cheerful 16-colour retro palette
namespace pal
{
inline const juce::Colour bg     { 0xff1a1c2c };
inline const juce::Colour purple { 0xff5d275d };
inline const juce::Colour red    { 0xffb13e53 };
inline const juce::Colour orange { 0xffef7d57 };
inline const juce::Colour yellow { 0xffffcd75 };
inline const juce::Colour lime   { 0xffa7f070 };
inline const juce::Colour green  { 0xff38b764 };
inline const juce::Colour teal   { 0xff257179 };
inline const juce::Colour navy   { 0xff29366f };
inline const juce::Colour blue   { 0xff3b5dc9 };
inline const juce::Colour sky    { 0xff41a6f6 };
inline const juce::Colour cyan   { 0xff73eff7 };
inline const juce::Colour white  { 0xfff4f4f4 };
inline const juce::Colour light  { 0xff94b0c2 };
inline const juce::Colour grey   { 0xff566c86 };
inline const juce::Colour dark   { 0xff333c57 };
inline const juce::Colour pink   { 0xfff49ac1 };   // extra: the PLAY ALONG section
} // namespace pal

/** Keeps the embedded pixel font alive while the UI is open. */
struct PixelTypeface
{
    PixelTypeface();
    juce::Typeface::Ptr typeface;
};

/** The "Press Start 2P" pixel font at the given height. */
juce::Font pixelFont (float height);

/** A readable regular font for longer help texts. */
juce::Font textFont (float height, bool bold = false);

class RetroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RetroLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Label* createComboBoxTextBox (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

private:
    juce::SharedResourcePointer<PixelTypeface> typefaceHolder;
};
} // namespace bb
