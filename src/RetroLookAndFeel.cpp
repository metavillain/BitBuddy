#include "RetroLookAndFeel.h"
#include "BinaryData.h"

namespace bb
{
PixelTypeface::PixelTypeface()
    : typeface (juce::Typeface::createSystemTypefaceFor (BinaryData::PressStart2PRegular_ttf,
                                                         (size_t) BinaryData::PressStart2PRegular_ttfSize))
{
}

juce::Font pixelFont (float height)
{
    juce::SharedResourcePointer<PixelTypeface> holder;
    return juce::Font (juce::FontOptions (holder->typeface).withHeight (height));
}

juce::Font textFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
namespace
{
struct Theme
{
    const char* name;
    juce::uint32 bg, purple, red, orange, yellow, lime, green, teal, navy, blue, sky, cyan, white, light, grey, dark, pink, screen;
};

const Theme kThemes[kNumThemes] = {
    // Sweetie 16 by GrafxKid: the default
    { "Sweetie 16", 0xff1a1c2c, 0xff5d275d, 0xffb13e53, 0xffef7d57, 0xffffcd75, 0xffa7f070, 0xff38b764, 0xff257179,
      0xff29366f, 0xff3b5dc9, 0xff41a6f6, 0xff73eff7, 0xfff4f4f4, 0xff94b0c2, 0xff566c86, 0xff333c57, 0xfff49ac1, 0xff0b1a14 },
    // The original Game Boy screen: four shades of pea-soup green (plus a few in between)
    { "Game Boy",   0xff0f380f, 0xff306230, 0xff6b8c0f, 0xff8bac0f, 0xffc4d84a, 0xffa8c820, 0xff6b9a18, 0xff4a7a20,
      0xff255c25, 0xff4f7f1f, 0xff8bac0f, 0xff9bbc0f, 0xffe0f0c0, 0xff9bbc0f, 0xff306230, 0xff1e4d1e, 0xffb8d060, 0xff0b2a0b },
    // An old amber monitor
    { "Amber CRT",  0xff120a00, 0xff4a2800, 0xffb85000, 0xffff9a20, 0xffffc040, 0xffffb030, 0xffe08a10, 0xff5a3400,
      0xff3a2000, 0xff8a5000, 0xffffa828, 0xffffd070, 0xffffe8c0, 0xffc88a40, 0xff6a4000, 0xff2a1600, 0xffffb870, 0xff0a0500 },
    // The grey-and-red console
    { "NES",        0xff1c1c1c, 0xff5a1a2a, 0xffc01818, 0xffe04848, 0xffe8c070, 0xffd8d8d8, 0xffa8a8a8, 0xff3a5a6a,
      0xff2a2a3a, 0xff4060a0, 0xff8090c0, 0xffc0c8d8, 0xfff4f4f4, 0xffa0a0a0, 0xff6a6a6a, 0xff3a3a3a, 0xffe06060, 0xff101820 },
};

int themeNow = 0;
bool chosen = false;
} // namespace

const char* themeName (int theme) { return kThemes[juce::jlimit (0, kNumThemes - 1, theme)].name; }
int currentTheme() { return themeNow; }
bool themeChosen() { return chosen; }

void applyTheme (int theme)
{
    chosen = true;
    themeNow = juce::jlimit (0, kNumThemes - 1, theme);
    const auto& t = kThemes[themeNow];
    pal::bg = juce::Colour (t.bg);         pal::purple = juce::Colour (t.purple); pal::red = juce::Colour (t.red);
    pal::orange = juce::Colour (t.orange); pal::yellow = juce::Colour (t.yellow); pal::lime = juce::Colour (t.lime);
    pal::green = juce::Colour (t.green);   pal::teal = juce::Colour (t.teal);     pal::navy = juce::Colour (t.navy);
    pal::blue = juce::Colour (t.blue);     pal::sky = juce::Colour (t.sky);       pal::cyan = juce::Colour (t.cyan);
    pal::white = juce::Colour (t.white);   pal::light = juce::Colour (t.light);   pal::grey = juce::Colour (t.grey);
    pal::dark = juce::Colour (t.dark);     pal::pink = juce::Colour (t.pink);     pal::screen = juce::Colour (t.screen);
}

static juce::File& settingsFileOverride()
{
    static juce::File file;
    return file;
}

static std::unique_ptr<juce::PropertiesFile> openSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "BitBuddy";
    options.filenameSuffix = "settings";
    options.folderName = "BitBuddy";
    options.osxLibrarySubFolder = "Application Support";
    if (settingsFileOverride() != juce::File())
        return std::make_unique<juce::PropertiesFile> (settingsFileOverride(), options);
    return std::make_unique<juce::PropertiesFile> (options);
}

void useSettingsFile (const juce::File& file) { settingsFileOverride() = file; }

juce::String loadSetting (const juce::String& key, const juce::String& fallback)
{
    return openSettings()->getValue (key, fallback);
}

void saveSetting (const juce::String& key, const juce::String& value)
{
    auto settings = openSettings();
    settings->setValue (key, value);
    settings->saveIfNeeded();
}

int loadThemeSetting()
{
    return juce::jlimit (0, kNumThemes - 1, loadSetting ("theme", "0").getIntValue());
}

void saveThemeSetting (int theme)
{
    saveSetting ("theme", juce::String (theme));
}

//==============================================================================
RetroLookAndFeel::RetroLookAndFeel()
{
    applyPaletteColours();
}

void RetroLookAndFeel::applyPaletteColours()
{
    setColour (juce::ResizableWindow::backgroundColourId, pal::bg);

    setColour (juce::PopupMenu::backgroundColourId, pal::bg);
    setColour (juce::PopupMenu::textColourId, pal::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, pal::blue);
    setColour (juce::PopupMenu::highlightedTextColourId, pal::white);

    setColour (juce::ComboBox::backgroundColourId, pal::bg);
    setColour (juce::ComboBox::textColourId, pal::white);
    setColour (juce::ComboBox::outlineColourId, pal::light);
    setColour (juce::ComboBox::arrowColourId, pal::yellow);

    setColour (juce::TextButton::buttonColourId, pal::dark);
    setColour (juce::TextButton::buttonOnColourId, pal::yellow);
    setColour (juce::TextButton::textColourOffId, pal::white);
    setColour (juce::TextButton::textColourOnId, pal::bg);

    setColour (juce::Label::textColourId, pal::white);
    setColour (juce::Slider::textBoxTextColourId, pal::white);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, pal::yellow);
}

// Chunky knob: a ring of little LED blocks shows the value
void RetroLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                         float startAngle, float endAngle, juce::Slider& slider)
{
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float rOuter = size * 0.5f - 2.0f;
    const bool bipolar = (bool) slider.getProperties().getWithDefault ("bipolar", false);

    constexpr int segments = 11;
    for (int i = 0; i < segments; ++i)
    {
        const float frac = (float) i / (float) (segments - 1);
        const float angle = startAngle + frac * (endAngle - startAngle);
        const float px = c.x + std::sin (angle) * (rOuter - 3.0f);
        const float py = c.y - std::cos (angle) * (rOuter - 3.0f);

        bool lit;
        if (bipolar)
            lit = frac >= juce::jmin (0.5f, pos) - 0.01f && frac <= juce::jmax (0.5f, pos) + 0.01f;
        else
            lit = frac <= pos + 0.01f;

        g.setColour (lit ? accent : pal::bg);
        g.fillRect (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ std::round (px), std::round (py) }));
    }

    const float rBody = rOuter - 9.0f;
    g.setColour (pal::bg);
    g.fillEllipse (c.x - rBody, c.y - rBody + 3.0f, rBody * 2.0f, rBody * 2.0f);   // drop shadow
    g.setColour (pal::grey);
    g.fillEllipse (c.x - rBody, c.y - rBody, rBody * 2.0f, rBody * 2.0f);
    g.setColour (pal::light);
    g.drawEllipse (c.x - rBody + 1.0f, c.y - rBody + 1.0f, rBody * 2.0f - 2.0f, rBody * 2.0f - 2.0f, 2.0f);

    const float angle = startAngle + pos * (endAngle - startAngle);
    juce::Path pointer;
    pointer.addRectangle (-2.0f, -rBody + 3.0f, 4.0f, rBody * 0.6f);
    g.setColour (pal::white);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

juce::Label* RetroLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (pixelFont (8.0f));
    l->setColour (juce::Label::textColourId, pal::light);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setMinimumHorizontalScale (0.7f);
    l->setWantsKeyboardFocus (false);              // clicking a knob must not steal the
    l->setMouseClickGrabsKeyboardFocus (false);    // computer keyboard from the piano
    return l;
}

juce::Label* RetroLookAndFeel::createComboBoxTextBox (juce::ComboBox& box)
{
    auto* l = LookAndFeel_V4::createComboBoxTextBox (box);
    l->setWantsKeyboardFocus (false);
    l->setMouseClickGrabsKeyboardFocus (false);
    return l;
}

void RetroLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour,
                                             bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f, 1.0f);
    r.removeFromBottom (2.0f);
    const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
    const bool on = b.getToggleState();

    auto fill = on ? accent : backgroundColour;
    if (over)
        fill = fill.brighter (0.15f);

    const float pressOffset = down ? 2.0f : 0.0f;

    g.setColour (pal::bg);
    g.fillRect (r.translated (0.0f, 2.0f));                // pixel drop shadow
    g.setColour (fill);
    g.fillRect (r.translated (0.0f, pressOffset));
    g.setColour (on ? accent.brighter (0.5f) : pal::light.withAlpha (0.7f));
    g.drawRect (r.translated (0.0f, pressOffset), 2.0f);
}

juce::Font RetroLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return pixelFont (8.0f);
}

void RetroLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                     juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRect (r);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRect (r, 2.0f);

    // pixel-art down arrow
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    const float ax = (float) width - 16.0f;
    const float ay = (float) height * 0.5f - 3.0f;
    for (int i = 0; i < 3; ++i)
        g.fillRect (ax + 2.0f * (float) i, ay + 2.0f * (float) i, 10.0f - 4.0f * (float) i, 2.0f);
}

juce::Font RetroLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return pixelFont (8.0f);
}

void RetroLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setMinimumHorizontalScale (0.7f);
}

juce::Font RetroLookAndFeel::getPopupMenuFont()
{
    return pixelFont (9.0f);
}

void RetroLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (pal::bg);
    g.setColour (pal::light);
    g.drawRect (0, 0, width, height, 2);
}
} // namespace bb
