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

RetroLookAndFeel::RetroLookAndFeel()
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
