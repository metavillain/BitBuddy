#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bb
{
// The colour palette. Every colour in BitBuddy comes from here, so a colour
// theme just swaps these values (see applyTheme). Default: "Sweetie 16".
namespace pal
{
inline juce::Colour bg     { 0xff1a1c2c };
inline juce::Colour purple { 0xff5d275d };
inline juce::Colour red    { 0xffb13e53 };
inline juce::Colour orange { 0xffef7d57 };
inline juce::Colour yellow { 0xffffcd75 };
inline juce::Colour lime   { 0xffa7f070 };
inline juce::Colour green  { 0xff38b764 };
inline juce::Colour teal   { 0xff257179 };
inline juce::Colour navy   { 0xff29366f };
inline juce::Colour blue   { 0xff3b5dc9 };
inline juce::Colour sky    { 0xff41a6f6 };
inline juce::Colour cyan   { 0xff73eff7 };
inline juce::Colour white  { 0xfff4f4f4 };
inline juce::Colour light  { 0xff94b0c2 };
inline juce::Colour grey   { 0xff566c86 };
inline juce::Colour dark   { 0xff333c57 };
inline juce::Colour pink   { 0xfff49ac1 };   // extra: the PLAY ALONG section
inline juce::Colour screen { 0xff0b1a14 };   // the SCREEN's background
} // namespace pal

//==============================================================================
// Colour themes
constexpr int kNumThemes = 4;
const char* themeName (int theme);           // "Sweetie 16", "Game Boy", "Amber CRT", "NES"
void applyTheme (int theme);                 // swaps the palette
int currentTheme();
bool themeChosen();                          // has a theme been applied yet in this session?

/** The theme you picked, remembered for every BitBuddy (app and plugin). */
int loadThemeSetting();
void saveThemeSetting (int theme);

/** BitBuddy's own settings file (theme, box layout), shared by the app and every plugin. */
juce::String loadSetting (const juce::String& key, const juce::String& fallback);
void saveSetting (const juce::String& key, const juce::String& value);
void useSettingsFile (const juce::File&);   // tests: keep your real settings untouched

/** Tells every open BitBuddy window to repaint in the new colours. */
struct ThemeEvents : public juce::ChangeBroadcaster {};

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
    void applyPaletteColours();   // after a theme change

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
