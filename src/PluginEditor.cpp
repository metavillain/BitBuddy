#include "PluginEditor.h"
#include "Parameters.h"
#include "Presets.h"
#include "Scales.h"

#include <cmath>

namespace bb
{
namespace
{
const char* const kWaveShort[8] = { "SQUARE", "PULSE 25", "PULSE 12", "TRIANGLE", "SAW", "NOISE", "METAL", "DRUMS" };

const char* const kWaveInfo[8] = {
    "SQUARE (50% pulse): bold, hollow and bright. The workhorse of 8-bit music - great for leads and bass.",
    "PULSE 25%: a thinner, nasal square wave. THE classic NES lead sound.",
    "PULSE 12.5%: very thin and buzzy. Cuts through everything - perfect for arpeggios and sparkly leads.",
    "TRIANGLE: soft and round, built from 16 little steps like the NES chip. Usually used for bass lines.",
    "SAW: bright and buzzy, like a brassy synth. Not on the NES, but found on later sound chips.",
    "NOISE: random hiss from a 15-bit shift register. Low notes = rumble and explosions, high notes = hi-hats and wind.",
    "METAL NOISE: the noise chip's 'short mode' - a buzzy, metallic tone tuned to your keys. Bells, robots, weird FX!",
    "DRUM KIT - every key is a drum!  C Kick,  C# Rim,  D Snare,  D# Clap,  E Snare 2,  F/G/A Toms,  "
    "F# Hi-Hat,  G# Pedal Hat,  A# Open Hat,  B Crash",
};

const char* const kArpTips[3] = {
    "Tip: set CHORD, then hold just ONE key for instant 8-bit chords!",
    "CHORD is on: hold ONE key - BitBuddy flips through the chord notes super fast.",
    "NOTES is on: hold 2-4 keys - they play one after another as a pattern.",
};

const char* const kDefaultHint = "Hover over anything to learn what it does.   Double-click a knob to reset it.";

// Preset menu item ids: factory presets are 1..N
constexpr int kUserPresetIdBase = 1001;   // MY PRESETS
constexpr int kDeletePresetId   = 5000;   // "Delete <this preset>..."

// A tiny drawing of each waveform, for the wave buttons. x runs 0..2 (two cycles).
float iconSample (int wave, float x)
{
    const float f = x - std::floor (x);
    switch (wave)
    {
        case 0: return f < 0.5f ? 1.0f : -1.0f;
        case 1: return f < 0.25f ? 1.0f : -1.0f;
        case 2: return f < 0.125f ? 1.0f : -1.0f;
        case 3:
        {
            const int step = (int) (f * 16.0f);
            const int level = step < 8 ? 7 - step : step - 8;
            return (float) level / 3.5f - 1.0f;
        }
        case 4: return 2.0f * f - 1.0f;
        case 5:
        {
            const auto h = (std::uint32_t) ((int) (x * 14.0f) + 7) * 2654435761u;
            return (float) ((h >> 13) % 9u) / 4.0f - 1.0f;
        }
        case 6:
        {
            static const float pattern[5] = { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f };
            return pattern[(int) (x * 16.0f) % 5];
        }
        case 7:
        {
            const float env = std::exp (-x * 1.6f);
            const float ph = x * (5.0f - x * 1.5f);
            return env * ((ph - std::floor (ph)) < 0.5f ? 1.0f : -1.0f);
        }
        default: return 0.0f;
    }
}

void drawPixelLine (juce::Graphics& g, juce::Rectangle<float> area, float pixel,
                    const std::function<float (float)>& valueAt /* x 0..1 -> y -1..1 */)
{
    const int cols = juce::jmax (2, (int) (area.getWidth() / pixel));
    float prevY = 0.0f;

    for (int c = 0; c < cols; ++c)
    {
        const float v = juce::jlimit (-1.0f, 1.0f, valueAt ((float) c / (float) (cols - 1)));
        float y = area.getCentreY() - v * area.getHeight() * 0.5f;
        y = std::round (y / pixel) * pixel;
        const float x = area.getX() + (float) c * pixel;

        if (c > 0 && std::abs (y - prevY) > 0.5f)
            g.fillRect (x, juce::jmin (prevY, y), pixel, std::abs (y - prevY) + pixel);
        else
            g.fillRect (x, y, pixel, pixel);

        prevY = y;
    }
}

// The C64 filter's colour: the palette's red, lifted so text stays readable
juce::Colour filterAccent() { return pal::red.brighter (0.45f); }

// The frame of every section: dark panel, coloured border, number badge, title
void drawBoxFrame (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& number, const juce::String& title,
                   juce::Colour accent)
{
    g.setColour (pal::dark);
    g.fillRect (r);
    g.setColour (accent);
    g.drawRect (r, 2);

    int textX = r.getX() + 10;
    if (number.isNotEmpty())
    {
        const juce::Rectangle<int> badge (r.getX() + 8, r.getY() + 6, 16, 16);
        g.fillRect (badge);
        g.setColour (pal::bg);
        g.setFont (pixelFont (8.0f));
        g.drawText (number, badge.translated (1, 1), juce::Justification::centred);
        textX = badge.getRight() + 8;
    }

    g.setColour (accent);
    g.setFont (pixelFont (8.0f));
    g.drawText (title, textX, r.getY() + 6, r.getWidth() - (textX - r.getX()) - 8, 17, juce::Justification::centredLeft);
}
} // namespace

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
            juce::Colour accent, const juce::String& hint, bool bipolar)
{
    label.setText (title, juce::dontSendNotification);
    label.setFont (pixelFont (7.0f));
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, accent);
    label.setMinimumHorizontalScale (0.6f);
    label.setTooltip (hint);
    addAndMakeVisible (label);

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 70, 14);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setTooltip (hint);
    slider.getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

    if (auto* p = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (12));
    slider.setBounds (r);
}

//==============================================================================
ParamButtons::ParamButtons (juce::RangedAudioParameter& param, int count, const Factory& makeButton,
                            bool isVertical, int gapPixels)
    : vertical (isVertical),
      gap (gapPixels),
      attachment (param, [this] (float v) { update (v); })
{
    for (int i = 0; i < count; ++i)
    {
        auto* b = buttons.add (makeButton (i));
        b->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }

    attachment.sendInitialUpdate();
}

void ParamButtons::update (float value)
{
    const int idx = juce::roundToInt (value);
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
}

void ParamButtons::resized()
{
    auto r = getLocalBounds();
    const int n = buttons.size();

    for (int i = 0; i < n; ++i)
    {
        if (vertical)
            buttons[i]->setBounds (r.removeFromTop (r.getHeight() / (n - i)).reduced (0, gap));
        else
            buttons[i]->setBounds (r.removeFromLeft (r.getWidth() / (n - i)).reduced (gap, 0));
    }
}

static ParamButtons::Factory textButtons (const juce::StringArray& labels, const juce::StringArray& hints, juce::Colour accent)
{
    return [=] (int i) -> juce::Button*
    {
        auto* b = new juce::TextButton (labels[i]);
        b->setColour (juce::TextButton::buttonOnColourId, accent);
        b->setTooltip (hints[i]);
        return b;
    };
}

//==============================================================================
DiceButton::DiceButton (juce::Colour c) : juce::Button ("DICE"), accent (c)
{
    setTooltip ("DICE: rolls random (but musical) settings for just this section. Keep rolling until you love it!");
}

void DiceButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f).translated (0.0f, down ? 1.0f : 0.0f);
    g.setColour (over ? accent : pal::white);
    g.fillRect (r);
    g.setColour (pal::bg);
    g.drawRect (r, 1.0f);

    // five pips, pixel style
    const float s = juce::jmax (2.0f, std::floor (r.getWidth() / 6.0f));
    auto pip = [&] (float fx, float fy)
    {
        g.fillRect (std::round (r.getX() + r.getWidth() * fx - s * 0.5f), std::round (r.getY() + r.getHeight() * fy - s * 0.5f), s, s);
    };
    pip (0.28f, 0.28f); pip (0.72f, 0.28f); pip (0.5f, 0.5f); pip (0.28f, 0.72f); pip (0.72f, 0.72f);
}

//==============================================================================
Box::Box (const juce::String& numberText, const juce::String& titleText, juce::Colour c)
    : accent (c), number (numberText), title (titleText), dice (c)
{
    dice.onClick = [this] { if (onDice != nullptr) onDice(); };
    addAndMakeVisible (dice);
}

void Box::paint (juce::Graphics& g)
{
    drawBoxFrame (g, getLocalBounds(), number, title, accent);
}

void Box::resized()
{
    dice.setBounds (getWidth() - 26, 5, 18, 18);
    layoutContent (getLocalBounds().reduced (8, 6).withTrimmedTop (20));
}

//==============================================================================
FilterCurve::FilterCurve (juce::AudioProcessorValueTreeState& s) : state (s)
{
    setTooltip ("FILTER CURVE: which pitches get through (high) and which get cut (low). "
                "The bump is the resonance; the faint line shows how far ENV opens it on each note.");
}

void FilterCurve::refresh()
{
    const int t = juce::roundToInt (state.getRawParameterValue (ids::filterType)->load());
    const float c = state.getRawParameterValue (ids::filterCutoff)->load();
    const float r = state.getRawParameterValue (ids::filterReso)->load();
    const float e = state.getRawParameterValue (ids::filterEnv)->load();
    if (t != type || c != cutoff || r != reso || e != env)
    {
        type = t; cutoff = c; reso = r; env = e;
        repaint();
    }
}

void FilterCurve::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (pal::bg);
    g.fillRect (area);
    g.setColour (pal::navy);
    g.drawRect (area, 1.0f);

    auto plot = area.reduced (4.0f);
    constexpr float lowHz = 30.0f, highHz = 16000.0f, minDb = -30.0f, maxDb = 18.0f;
    auto hzAt = [&] (float x01) { return lowHz * std::pow (highHz / lowHz, x01); };
    auto curveAt = [&] (float cut)
    {
        return [&, cut] (float x01)
        {
            const float db = juce::Decibels::gainToDecibels (SidFilter::magnitudeAt (type, hzAt (x01), cut, reso), minDb);
            return juce::jmap (juce::jlimit (minDb, maxDb, db), minDb, maxDb, -1.0f, 1.0f);
        };
    };

    // 0 dB line
    g.setColour (pal::dark);
    const float zeroY = juce::jmap (0.0f, minDb, maxDb, plot.getBottom(), plot.getY());
    g.fillRect (plot.getX(), zeroY, plot.getWidth(), 1.0f);

    if (type == SidFilter::Off)
    {
        g.setColour (pal::grey);
        g.setFont (pixelFont (7.0f));
        g.drawText ("OFF", plot.withTrimmedBottom (plot.getHeight() * 0.55f), juce::Justification::centred);
    }
    else if (env > 0.01f)
    {
        g.setColour (filterAccent().withAlpha (0.35f));
        drawPixelLine (g, plot, 2.0f, curveAt (juce::jmin (16000.0f, cutoff * std::exp2 (5.0f * env))));
    }

    g.setColour (type == SidFilter::Off ? pal::grey : filterAccent());
    drawPixelLine (g, plot, 2.0f, curveAt (cutoff));
}

//==============================================================================
FilterBox::FilterBox (juce::AudioProcessorValueTreeState& state)
    : Box ("6", "C64 FILTER  (SQUELCH!)", filterAccent()),
      typeButtons (*state.getParameter (ids::filterType), 4,
                   textButtons ({ "OFF", "LOW", "BAND", "HIGH" },
                                { "FILTER OFF: the raw chip sound, nothing filtered.",
                                  "LOW PASS: keeps the low part of the sound and cuts the fizz. Turn CUTOFF down for darker, rounder "
                                  "sounds - the classic Commodore 64 bass!",
                                  "BAND PASS: keeps only a slice in the middle - thin, nasal, 'wah' and telephone sounds.",
                                  "HIGH PASS: cuts the bass and keeps the sizzle - thin and airy, nice for hi-hats and effects." },
                                filterAccent()),
                   true, 1),
      curve (state),
      cutoff (state, ids::filterCutoff, "CUTOFF", filterAccent(),
              "CUTOFF: where the filter starts cutting. Low = dark and muffled, high = bright. Try turning it while you play!"),
      reso (state, ids::filterReso, "RESO", filterAccent(),
            "RESONANCE: makes the filter 'sing' at the cutoff - the squelchy, rubbery C64 sound. Very high settings whistle!"),
      env (state, ids::filterEnv, "ENV", filterAccent(),
           "ENV (filter envelope): every new note opens the filter, then it closes over the DECAY time. "
           "With LOW and some RESO you get 'wow' basses and 'pew' leads!")
{
    for (auto* c : { (juce::Component*) &typeButtons, (juce::Component*) &curve, (juce::Component*) &cutoff,
                     (juce::Component*) &reso, (juce::Component*) &env })
        addAndMakeVisible (c);
}

void FilterBox::layoutContent (juce::Rectangle<int> area)
{
    typeButtons.setBounds (area.removeFromLeft (66));
    area.removeFromLeft (8);
    curve.setBounds (area.removeFromLeft (170).reduced (0, 2));
    area.removeFromLeft (6);
    const int w = area.getWidth() / 3;
    for (auto* k : { &cutoff, &reso, &env })
        k->setBounds (area.removeFromLeft (w));
}

//==============================================================================
WaveButton::WaveButton (int index, const juce::String& shortName, juce::Colour c)
    : juce::Button (shortName), waveIndex (index), accent (c)
{
}

void WaveButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    r.removeFromBottom (3.0f);

    g.setColour (pal::bg);
    g.fillRect (r.translated (0.0f, 3.0f));

    auto body = r.translated (0.0f, down ? 2.0f : 0.0f);
    g.setColour (on ? accent : (over ? pal::grey : pal::navy));
    g.fillRect (body);
    g.setColour (on ? accent.brighter (0.6f) : pal::light.withAlpha (0.5f));
    g.drawRect (body, 2.0f);

    auto labelArea = body.removeFromBottom (18.0f);
    auto iconArea = body.reduced (8.0f, 12.0f);

    g.setColour (on ? pal::bg : pal::white);
    drawPixelLine (g, iconArea, 2.0f, [this] (float x) { return iconSample (waveIndex, x * 2.0f); });

    g.setFont (pixelFont (7.0f));
    g.drawFittedText (getButtonText(), labelArea.toNearestInt(), juce::Justification::centred, 1, 0.6f);
}

static ParamButtons::Factory waveButtons (juce::Colour accent)
{
    return [=] (int i) -> juce::Button*
    {
        auto* b = new WaveButton (i, kWaveShort[i], accent);
        b->setTooltip (kWaveInfo[i]);
        return b;
    };
}

//==============================================================================
Scope::Scope (ScopeBuffer& s) : source (s)
{
    setTooltip ("SCREEN: shows the actual shape of the sound wave you're playing. Square waves look like blocks, "
                "triangles like little staircases, noise like TV static!");
}

void Scope::refresh()
{
    source.copyLatest (samples.data(), (int) samples.size());
    ++frame;

    // Silence only needs a redraw when the "PLAY A NOTE!" text blinks
    float peak = 0.0f;
    for (auto s : samples)
        peak = juce::jmax (peak, std::abs (s));

    const bool silent = peak < 0.002f;
    const bool blinkOn = (frame / 20) % 2 == 0;
    if (silent && wasSilent && blinkOn == lastBlink)
        return;

    wasSilent = silent;
    lastBlink = blinkOn;
    repaint();
}

void Scope::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff0b1a14));
    g.fillRect (r);

    g.setColour (pal::green.withAlpha (0.12f));
    for (float x = r.getX() + 10.0f; x < r.getRight(); x += 20.0f)
        g.fillRect (x, r.getY(), 1.0f, r.getHeight());
    for (float y = r.getY() + 6.0f; y < r.getBottom(); y += 20.0f)
        g.fillRect (r.getX(), y, r.getWidth(), 1.0f);

    // Trigger on a rising zero crossing so the picture stands still
    constexpr int show = 700;
    const int total = (int) samples.size();
    int start = total - show;
    for (int i = 1; i < total - show; ++i)
        if (samples[(size_t) i - 1] <= 0.0f && samples[(size_t) i] > 0.0f)
        {
            start = i;
            break;
        }

    float peak = 0.0f;
    for (int i = start; i < start + show; ++i)
        peak = juce::jmax (peak, std::abs (samples[(size_t) i]));

    const auto plot = r.reduced (4.0f, 10.0f);

    if (peak < 0.002f)
    {
        g.setColour (pal::green.withAlpha (0.5f));
        g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 2.0f);

        if ((frame / 20) % 2 == 0)
        {
            g.setColour (pal::green);
            g.setFont (pixelFont (8.0f));
            g.drawText ("PLAY A NOTE!", r.withTrimmedTop (r.getHeight() * 0.6f).toNearestInt(),
                        juce::Justification::centred);
        }
    }
    else
    {
        const float scale = juce::jmin (40.0f, 0.9f / peak);
        auto valueAt = [this, start, scale] (float x)
        {
            const int idx = start + juce::jlimit (0, show - 1, (int) (x * (float) (show - 1)));
            return samples[(size_t) idx] * scale;
        };

        g.setColour (pal::green.withAlpha (0.25f));
        drawPixelLine (g, plot.expanded (0.0f, 2.0f), 3.0f, valueAt);
        g.setColour (pal::lime);
        drawPixelLine (g, plot, 3.0f, valueAt);
    }

    g.setColour (juce::Colours::black.withAlpha (0.2f));   // CRT scanlines
    for (float y = r.getY(); y < r.getBottom(); y += 3.0f)
        g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
}

//==============================================================================
EnvelopeView::EnvelopeView (juce::AudioProcessorValueTreeState& s) : state (s)
{
    setTooltip ("This picture shows the volume of one note over time: it rises (Attack), falls (Decay), "
                "holds while you keep the key down (Sustain) and fades after you let go (Release).");
}

void EnvelopeView::refresh()
{
    const float na = state.getRawParameterValue (ids::attack)->load();
    const float nd = state.getRawParameterValue (ids::decay)->load();
    const float ns = state.getRawParameterValue (ids::sustain)->load();
    const float nr = state.getRawParameterValue (ids::release)->load();

    if (na != a || nd != d || ns != s || nr != r)
    {
        a = na; d = nd; s = ns; r = nr;
        repaint();
    }
}

void EnvelopeView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (pal::bg);
    g.fillRect (bounds);
    g.setColour (pal::lime.withAlpha (0.4f));
    g.drawRect (bounds, 2.0f);

    const auto plot = bounds.reduced (8.0f, 8.0f).withTrimmedBottom (12.0f);

    const float wa = 0.03f + 0.3f * std::sqrt (a / 2.0f);
    const float wd = 0.03f + 0.3f * std::sqrt (d / 3.0f);
    const float ws = 0.22f;
    const float wr = 0.03f + 0.3f * std::sqrt (r / 3.0f);
    const float total = wa + wd + ws + wr;

    const float x1 = wa / total, x2 = x1 + wd / total, x3 = x2 + ws / total;

    auto level = [=, this] (float x)
    {
        if (x < x1) return x / x1;
        if (x < x2) return 1.0f - (1.0f - s) * (x - x1) / (x2 - x1);
        if (x < x3) return s;
        return s * (1.0f - (x - x3) / (1.0f - x3));
    };

    g.setColour (pal::lime);
    drawPixelLine (g, plot, 3.0f, [&level] (float x) { return level (x) * 2.0f - 1.0f; });

    g.setColour (pal::light);
    g.setFont (pixelFont (7.0f));
    const float labelY = bounds.getBottom() - 16.0f;
    auto drawLabel = [&] (const char* text, float from, float to)
    {
        const float cx = plot.getX() + plot.getWidth() * (from + to) * 0.5f;
        g.drawText (text, juce::Rectangle<float> (cx - 10.0f, labelY, 20.0f, 12.0f), juce::Justification::centred);
    };
    drawLabel ("A", 0.0f, x1);
    drawLabel ("D", x1, x2);
    drawLabel ("S", x2, x3);
    drawLabel ("R", x3, 1.0f);
}

//==============================================================================
ChipKeyboard::ChipKeyboard (juce::MidiKeyboardState& state)
    : juce::MidiKeyboardComponent (state, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setAvailableRange (36, 96);
    setScrollButtonsVisible (false);
    setKeyPressBaseOctave (baseOctave);
    setWantsKeyboardFocus (true);

    setColour (whiteNoteColourId, pal::white);
    setColour (blackNoteColourId, pal::bg);
    setColour (keySeparatorLineColourId, pal::light);
    setColour (mouseOverKeyOverlayColourId, pal::yellow.withAlpha (0.45f));
    setColour (keyDownOverlayColourId, pal::orange);
    setColour (textLabelColourId, pal::grey);
    setColour (shadowColourId, pal::bg.withAlpha (0.4f));

    setTooltip ("KEYBOARD: click to play, or use your computer keys:  A W S E D F T G Y H U J K  = one octave.  "
                "Z / X = octave down / up.  (In a DAW, use your MIDI keyboard or draw notes in the piano roll.)");
}

void ChipKeyboard::setDrumMode (bool shouldShowDrums)
{
    if (drumMode != shouldShowDrums)
    {
        drumMode = shouldShowDrums;
        repaint();
    }
}

juce::String ChipKeyboard::getWhiteNoteText (int note)
{
    if (drumMode)
    {
        switch (note % 12)
        {
            case 0:  return "KICK";
            case 2:  return "SNR";
            case 4:  return "SNR2";
            case 5:
            case 7:
            case 9:  return "TOM";
            case 11: return "CRSH";
            default: return {};
        }
    }
    return juce::MidiKeyboardComponent::getWhiteNoteText (note);
}

void ChipKeyboard::setScale (int key, int newScaleIndex)
{
    if (key != scaleKey || newScaleIndex != scaleIndex)
    {
        scaleKey = key;
        scaleIndex = newScaleIndex;
        repaint();
    }
}

juce::String ChipKeyboard::letterFor (int note) const
{
    static const juce::String keys ("AWSEDFTGYHUJKOLP;");   // JUCE's default computer-key layout
    const int offset = note - baseOctave * 12;
    return offset >= 0 && offset < keys.length() ? keys.substring (offset, offset + 1) : juce::String();
}

bool ChipKeyboard::isDimmed (int note) const
{
    return ! drumMode && ! inScale (note, scaleKey, (Scale) scaleIndex);
}

void ChipKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                  juce::Colour lineColour, juce::Colour textColour)
{
    juce::MidiKeyboardComponent::drawWhiteNote (note, g, area, isDown, isOver, lineColour, textColour);

    if (isDimmed (note) && ! isDown)
    {
        g.setColour (pal::grey.withAlpha (0.55f));
        g.fillRect (area.reduced (1.0f, 0.0f));
    }

    // The KEY's home note gets a pink marker while Scale Lock is on
    if (! drumMode && scaleIndex != 0 && degreeOf (note, scaleKey) == 0)
    {
        g.setColour (pal::pink);
        g.fillRect (area.getCentreX() - 4.0f, area.getY() + 4.0f, 8.0f, 8.0f);
    }

    if (const auto letter = letterFor (note); letter.isNotEmpty())
    {
        g.setColour (pal::navy);
        g.setFont (pixelFont (7.0f));
        g.drawText (letter, area.withTrimmedTop (area.getHeight() - 32.0f).withHeight (12.0f),
                    juce::Justification::centred);
    }
}

void ChipKeyboard::drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                  juce::Colour noteFillColour)
{
    juce::MidiKeyboardComponent::drawBlackNote (note, g, area, isDown, isOver, noteFillColour);

    if (isDimmed (note) && ! isDown)
    {
        g.setColour (pal::grey.withAlpha (0.7f));
        g.fillRect (area.reduced (area.getWidth() * 0.3f, 0.0f).withTrimmedTop (area.getHeight() * 0.15f)
                        .withHeight (2.0f));   // a thin grey dash = "snaps to a neighbour"
    }

    if (! drumMode && scaleIndex != 0 && degreeOf (note, scaleKey) == 0)
    {
        g.setColour (pal::pink);
        g.fillRect (area.getCentreX() - 3.0f, area.getY() + 4.0f, 6.0f, 6.0f);
    }

    if (const auto letter = letterFor (note); letter.isNotEmpty())
    {
        g.setColour (pal::light);
        g.setFont (pixelFont (6.0f));
        g.drawText (letter, area.withTrimmedTop (area.getHeight() - 14.0f), juce::Justification::centred);
    }
}

bool ChipKeyboard::keyPressed (const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (c == 'z' || c == 'x')
    {
        baseOctave = juce::jlimit (1, 8, baseOctave + (c == 'x' ? 1 : -1));
        setKeyPressBaseOctave (baseOctave);
        repaint();   // the letter labels move with the octave
        return true;
    }
    return juce::MidiKeyboardComponent::keyPressed (key);
}

void ChipKeyboard::resized()
{
    setKeyWidth ((float) getWidth() / 36.0f);   // 36 white keys between note 36 and 96
    juce::MidiKeyboardComponent::resized();
}

//==============================================================================
HelpOverlay::HelpOverlay()
{
    closeButton.setColour (juce::TextButton::buttonColourId, pal::purple);
    closeButton.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeButton);
}

void HelpOverlay::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (780, 590);
    closeButton.setBounds (panel.getCentreX() - 70, panel.getBottom() - 50, 140, 34);
}

void HelpOverlay::paint (juce::Graphics& g)
{
    g.fillAll (pal::bg.withAlpha (0.88f));
    g.setColour (pal::dark);
    g.fillRect (panel);
    g.setColour (pal::yellow);
    g.drawRect (panel, 3);

    auto r = panel.reduced (28, 22);
    g.setFont (pixelFont (16.0f));
    g.drawText ("HOW TO MAKE 8-BIT MUSIC", r.removeFromTop (26), juce::Justification::centred);
    r.removeFromTop (14);

    struct Line { const char* head; const char* body; };
    static const Line lines[] = {
        { "1. PLAY", "Pick a preset at the top, then click the keyboard at the bottom - or use your computer keys "
                     "A W S E D F T G Y H U J K  (Z / X = octave down / up)." },
        { "2. PICK A SOUND", "Old consoles only had a few waveforms: Square & Pulse (leads), Triangle (bass) and "
                             "Noise (drums and effects). Try each one!" },
        { "3. SHAPE IT", "Attack, Decay, Sustain and Release control how the volume of each note starts, falls, "
                         "holds and ends. Short = plucky blips, long = smooth pads." },
        { "4. MAGIC CHORDS", "The chips could only play ONE note per channel, so composers flipped between chord "
                             "notes super fast. Set CHORD and hold one key - instant chiptune chord!" },
        { "5. MOVE & CRUNCH", "Vibrato makes leads sing. Sweep makes lasers, jumps and drum hits. "
                              "Bits / Rate add grit, Echo adds space." },
        { "PLAY ALONG", "Pick a KEY and SCALE LOCK (bottom right) and every key you press fits the song - no wrong "
                        "notes. Set CHORD to SMART and each key plays the chord that belongs to it." },
        { "MAKING A SONG", "Use one BitBuddy per track in your DAW, just like the NES used separate channels: "
                           "Lead (Hero Lead), Chords (Chord Arp), Bass (Triangle Bass), Drums (Drum Kit)." },
    };

    for (const auto& line : lines)
    {
        g.setColour (pal::cyan);
        g.setFont (pixelFont (9.0f));
        g.drawText (line.head, r.removeFromTop (14), juce::Justification::left);
        g.setColour (pal::white);
        g.setFont (textFont (15.0f));
        g.drawFittedText (line.body, r.removeFromTop (40), juce::Justification::topLeft, 2, 1.0f);
        r.removeFromTop (6);
    }
}

//==============================================================================
MainPanel::MainPanel (BitBuddyProcessor& p, RetroLookAndFeel& lookAndFeel)
    : proc (p),
      state (p.apvts),
      diceSound (pal::yellow), diceShape (pal::lime), diceChords (pal::cyan), diceMovement (pal::orange), diceCrunch (pal::sky),
      waveSelector (*state.getParameter (ids::wave), 8, waveButtons (pal::yellow), false, 3),
      scope (p.scope),
      attack  (state, ids::attack,  "ATTACK",  pal::lime, "ATTACK: how quickly a note fades in. Low = instant 'blip', high = slow swell."),
      decay   (state, ids::decay,   "DECAY",   pal::lime, "DECAY: how fast the note falls from its loudest point down to the Sustain level."),
      sustain (state, ids::sustain, "SUSTAIN", pal::lime, "SUSTAIN: the volume a note holds while you keep the key down. 0% = every note is a short blip."),
      release (state, ids::release, "RELEASE", pal::lime, "RELEASE: how long a note takes to fade away after you let go of the key."),
      envelopeView (state),
      arpModeButtons (*state.getParameter (ids::arpMode), 3,
                      textButtons ({ "OFF", "CHORD", "NOTES" },
                                   { "ARP OFF: normal notes - what you play is what you hear.",
                                     "CHORD: hold ONE key and BitBuddy flips through a whole chord super fast. The classic 8-bit trick - old chips could only play one note per channel!",
                                     "NOTES: hold several keys and they play one after another as a pattern (an 'arpeggio')." },
                                   pal::cyan)),
      octaves (state, ids::arpOctaves, "OCTAVES", pal::cyan, "OCTAVES: repeats the arpeggio 1, 2 or 3 octaves higher for bigger, sparklier patterns."),
      vibDepth  (state, ids::vibDepth,  "VIBRATO",  pal::orange, "VIBRATO: wobbles the pitch up and down like a singer. Game leads use this a LOT. (Your mod wheel adds vibrato too.)"),
      vibRate   (state, ids::vibRate,   "VIB RATE", pal::orange, "VIB RATE: how fast the vibrato wobbles."),
      vibDelay  (state, ids::vibDelay,  "VIB WAIT", pal::orange, "VIB WAIT: waits this long before the wobble starts, so short notes stay clean and long notes 'bloom'. Very 8-bit!"),
      sweepAmt  (state, ids::sweepAmt,  "SWEEP",    pal::orange, "SWEEP: each note starts this many semitones away and slides to the right pitch. + = falls from above (lasers, kicks, 'pew!'), - = rises from below (jumps, power-ups).", true),
      sweepTime (state, ids::sweepTime, "SWP TIME", pal::orange, "SWEEP TIME: how long the Sweep slide takes."),
      pwm       (state, ids::pwm,       "PWM",      pal::orange, "PWM (pulse width modulation): slowly changes the width of the pulse wave for a lush, chorus-y sound. Works on Square and Pulse waves - a classic Commodore 64 trick!"),
      glide     (state, ids::glide,     "GLIDE",    pal::orange, "GLIDE: notes slide smoothly into each other instead of jumping. Works best with MONO."),
      voiceModeButtons (*state.getParameter (ids::voiceMode), 2,
                        textButtons ({ "POLY", "MONO" },
                                     { "POLY: play chords - up to 8 notes at once.",
                                       "MONO: one note at a time, like a real sound-chip channel. Great for bass and leads with Glide." },
                                     pal::orange),
                        true),
      bits         (state, ids::bits,         "BITS",    pal::sky, "BITS: lowers the audio resolution. 16-bit = clean, 8-bit = classic, 4-bit and below = crunchy and gritty!"),
      rate         (state, ids::rate,         "RATE",    pal::sky, "RATE: lowers the sample rate for a grainy, crunchy old-hardware sound. Turn it all the way up ('Clean') to switch it off."),
      echoMix      (state, ids::echoMix,      "ECHO",    pal::sky, "ECHO: adds echoes that bounce between your left and right speakers. Chiptune composers used to fake this with a spare channel!"),
      echoFeedback (state, ids::echoFeedback, "REPEATS", pal::sky, "REPEATS: how many echoes you hear - low = one or two, high = lots."),
      volume       (state, ids::volume,       "VOLUME",  pal::sky, "VOLUME: the overall loudness of BitBuddy."),
      filterBox (p.apvts),
      keyboard (p.keyboardState)
{
    // --- header ---
    helpButton.setTooltip ("HELP: a quick guide to making 8-bit music with BitBuddy.");
    helpButton.setColour (juce::TextButton::buttonColourId, pal::teal);
    helpButton.onClick = [this] { help.setVisible (true); help.toFront (false); };

    prevButton.setTooltip ("Previous preset.");
    nextButton.setTooltip ("Next preset.");
    prevButton.onClick = [this]
    {
        const int n = proc.getNumPrograms();
        proc.selectPreset ((proc.getCurrentProgram() + n - 1) % n);
    };
    nextButton.onClick = [this] { proc.selectPreset ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };

    surpriseButton.setTooltip ("SURPRISE ME! Rolls the dice and makes a random (but musical) new sound.");
    surpriseButton.setColour (juce::TextButton::buttonColourId, pal::purple);
    surpriseButton.onClick = [this] { proc.randomize(); };

    mutateButton.setTooltip ("MUTATE: nudges every knob a little - a cousin of the sound you have now. "
                             "Press it a few times to let your sound evolve!");
    mutateButton.setColour (juce::TextButton::buttonColourId, pal::blue);
    mutateButton.onClick = [this] { proc.mutate(); };

    saveButton.setTooltip ("SAVE: keep the sound you made as one of your own presets (under MY PRESETS in the preset menu).");
    saveButton.setColour (juce::TextButton::buttonColourId, pal::green);
    saveButton.onClick = [this] { askToSavePreset(); };

    presetBox.setTooltip ("PRESETS: ready-made sounds to start from - and your own under MY PRESETS. "
                          "Pick one, play some notes, then tweak the knobs!");
    presetBox.setColour (juce::ComboBox::outlineColourId, pal::yellow);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id == kDeletePresetId)
            askToDeletePreset();
        else if (id >= kUserPresetIdBase)
        {
            if (juce::isPositiveAndBelow (id - kUserPresetIdBase, userPresets.size()))
                proc.loadUserPreset (userPresets[id - kUserPresetIdBase]);
        }
        else if (id > 0)
            proc.selectPreset (id - 1);
    };
    refreshPresetBox();

    presetTip.setFont (textFont (14.0f));
    presetTip.setColour (juce::Label::textColourId, pal::light);

    for (auto* c : { (juce::Component*) &helpButton, (juce::Component*) &prevButton, (juce::Component*) &nextButton,
                     (juce::Component*) &saveButton, (juce::Component*) &mutateButton,
                     (juce::Component*) &surpriseButton, (juce::Component*) &presetBox, (juce::Component*) &presetTip })
        addAndMakeVisible (c);

    // --- dice: one per section ---
    using Section = BitBuddyProcessor::Section;
    const std::pair<DiceButton*, Section> dice[] = { { &diceSound, Section::Sound }, { &diceShape, Section::Shape },
                                                      { &diceChords, Section::Chords }, { &diceMovement, Section::Movement },
                                                      { &diceCrunch, Section::Crunch } };
    for (auto [button, section] : dice)
    {
        button->onClick = [this, section = section] { proc.rollSection (section); };
        addAndMakeVisible (button);
    }

    // --- 1. pick a sound ---
    addAndMakeVisible (waveSelector);
    waveInfo.setFont (textFont (15.0f, true));
    waveInfo.setColour (juce::Label::textColourId, pal::yellow);
    waveInfo.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (waveInfo);
    addAndMakeVisible (scope);

    // --- 2. shape it ---
    for (auto* k : { &attack, &decay, &sustain, &release })
        addAndMakeVisible (k);
    addAndMakeVisible (envelopeView);

    // --- 3. magic chords ---
    addAndMakeVisible (arpModeButtons);
    setupCombo (chordBox, chordLabel, "CHORD", arpChordNames(),
                "CHORD TYPE: which chord CHORD mode plays. Major = happy, Minor = sad, Power = rock, Fourth = the 'coin' sound. "
                "SMART = follows your KEY + SCALE: every key gets the chord that fits the song.",
                pal::cyan);
    setupCombo (patternBox, patternLabel, "PATTERN", arpPatternNames(),
                "PATTERN: the order the notes play in. ONCE plays through a single time and then holds the top note - great for sound effects.",
                pal::cyan);
    setupCombo (speedBox, speedLabel, "SPEED", arpSpeedNames(),
                "SPEED: how fast the notes flip. 1/16, 1/32... lock to your song's tempo. 15 / 30 / 60 Hz copy old consoles' frame rate - 60 Hz is the buzzy NES chord.",
                pal::cyan);
    chordAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ids::arpChord, chordBox);
    patternAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ids::arpPattern, patternBox);
    speedAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ids::arpSpeed, speedBox);
    addAndMakeVisible (octaves);
    arpTip.setFont (textFont (14.0f, true));
    arpTip.setColour (juce::Label::textColourId, pal::cyan);
    arpTip.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (arpTip);

    // --- 4. movement ---
    for (auto* k : { &vibDepth, &vibRate, &vibDelay, &sweepAmt, &sweepTime, &pwm, &glide })
        addAndMakeVisible (k);
    addAndMakeVisible (voiceModeButtons);

    // --- 5. crunch + echo ---
    for (auto* k : { &bits, &rate, &echoMix, &echoFeedback, &volume })
        addAndMakeVisible (k);
    setupCombo (echoTimeBox, echoTimeLabel, "TIME", echoTimeNames(),
                "ECHO TIME: the gap between echoes, locked to your song's tempo. 1/8 D (dotted eighth) is a classic choice.",
                pal::sky);
    echoTimeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ids::echoTime, echoTimeBox);

    // --- 6. C64 filter ---
    filterBox.onDice = [this] { proc.rollSection (BitBuddyProcessor::Section::Filter); };
    addAndMakeVisible (filterBox);

    // --- play along: Scale Lock ---
    setupCombo (keyBox, keyLabel, "KEY", keyNames(),
                "KEY: the home note of your song - the scale starts here. Not sure? Leave it on C.",
                pal::pink);
    setupCombo (scaleBox, scaleLabel, "SCALE LOCK", scaleNames(),
                "SCALE LOCK: pick a scale and every key you press snaps to the nearest note in it - no wrong notes! "
                "Major = happy, Minor = sad, Penta = can't miss, Blues, Dorian = adventure, Harm. Minor = spooky.",
                pal::pink);
    keyAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ids::scaleKey, keyBox);
    scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ids::scale, scaleBox);
    scaleTip.setFont (textFont (13.0f, true));
    scaleTip.setColour (juce::Label::textColourId, pal::pink);
    scaleTip.setJustificationType (juce::Justification::topLeft);
    scaleTip.setMinimumHorizontalScale (0.85f);
    addAndMakeVisible (scaleTip);

    // --- bottom ---
    hintLabel.setFont (textFont (15.0f));
    hintLabel.setColour (juce::Label::textColourId, pal::white);
    hintLabel.setText (kDefaultHint, juce::dontSendNotification);
    hintLabel.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (hintLabel);

    keysStatus.setFont (pixelFont (7.0f));
    keysStatus.setJustificationType (juce::Justification::centred);
    keysStatus.setTooltip ("COMPUTER KEYBOARD: when this says KEYS ON, your letter keys play notes "
                           "(A W S E D F T G Y H U J K = one octave, Z / X = octave down / up). "
                           "If it says CLICK FOR KEYS, click anywhere in BitBuddy.");
    addAndMakeVisible (keysStatus);

    addAndMakeVisible (keyboard);
    addChildComponent (help);

    addMouseListener (this, true);
    setLookAndFeel (&lookAndFeel);   // (re)creates slider / combo text boxes...
    stopFocusStealing (*this);       // ...so do this afterwards
    setSize (baseWidth, baseHeight);
    startTimerHz (30);
}

void MainPanel::stopFocusStealing (juce::Component& root)
{
    for (auto* c : root.getChildren())
    {
        if (c == &keyboard)
            continue;

        c->setWantsKeyboardFocus (false);
        c->setMouseClickGrabsKeyboardFocus (false);
        stopFocusStealing (*c);
    }
}

bool MainPanel::windowIsActive() const
{
    if (auto* peer = getPeer())
        return peer->isFocused();
    return false;
}

void MainPanel::focusKeyboard()
{
    // Never interrupt an open menu or dialog
    if (keyboard.isShowing() && ! keyboard.hasKeyboardFocus (false)
        && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0)
        keyboard.grabKeyboardFocus();
}

void MainPanel::updateKeysStatus()
{
    const bool on = keyboard.hasKeyboardFocus (false);
    const auto text = on ? "KEYS ON  OCT " + juce::MidiMessage::getMidiNoteName (keyboard.getBaseOctave() * 12, true, true, 3)
                         : juce::String ("CLICK FOR KEYS");
    if (text != lastKeysStatus)
    {
        lastKeysStatus = text;
        keysStatus.setText (text, juce::dontSendNotification);
        keysStatus.setColour (juce::Label::textColourId, on ? pal::lime : pal::orange);
    }
}

void MainPanel::mouseDown (const juce::MouseEvent& e)
{
    // Touch screens have no hover: tapping a control shows its help instead
    if (e.source.isTouch())
        showHintFor (e.eventComponent);

    // Any click inside BitBuddy (knob, button, wave...) hands the letter keys to the piano
    focusKeyboard();
}

MainPanel::~MainPanel()
{
    stopTimer();
    removeMouseListener (this);
    setLookAndFeel (nullptr);
}

void MainPanel::setupCombo (juce::ComboBox& box, juce::Label& label, const juce::String& title,
                            const juce::StringArray& items, const juce::String& hint, juce::Colour accent)
{
    box.addItemList (items, 1);
    box.setTooltip (hint);
    box.setColour (juce::ComboBox::outlineColourId, accent);
    box.setColour (juce::ComboBox::arrowColourId, accent);

    label.setText (title, juce::dontSendNotification);
    label.setFont (pixelFont (7.0f));
    label.setColour (juce::Label::textColourId, accent);
    label.setTooltip (hint);

    addAndMakeVisible (box);
    addAndMakeVisible (label);
}

void MainPanel::drawSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& number,
                             const juce::String& title, juce::Colour accent)
{
    drawBoxFrame (g, r, number, title, accent);
}

void MainPanel::paint (juce::Graphics& g)
{
    g.fillAll (pal::bg);

    // Rainbow logo
    const juce::String logo ("BITBUDDY");
    const juce::Colour colours[] = { pal::red, pal::orange, pal::yellow, pal::lime, pal::green, pal::cyan, pal::sky, pal::blue };
    g.setFont (pixelFont (24.0f));
    for (int i = 0; i < logo.length(); ++i)
    {
        const juce::Rectangle<int> cell (16 + i * 25, 10, 26, 30);
        const auto ch = juce::String::charToString (logo[i]);
        g.setColour (pal::purple);
        g.drawText (ch, cell.translated (2, 3), juce::Justification::centred);
        g.setColour (colours[i]);
        g.drawText (ch, cell, juce::Justification::centred);
    }
    g.setColour (pal::light);
    g.setFont (pixelFont (8.0f));
    g.drawText ("8-BIT CHIPTUNE SYNTH", 18, 44, 210, 12, juce::Justification::left);

    drawSection (g, s1, "1", "PICK A SOUND", pal::yellow);
    drawSection (g, sScope, {}, "SCREEN", pal::green);
    drawSection (g, s2, "2", "SHAPE IT  (VOLUME ENVELOPE)", pal::lime);
    drawSection (g, s3, "3", "MAGIC CHORDS  (ARPEGGIATOR)", pal::cyan);
    drawSection (g, s4, "4", "ADD MOVEMENT", pal::orange);
    drawSection (g, s5, "5", "CRUNCH + ECHO", pal::sky);
    drawSection (g, sPlay, {}, "PLAY ALONG  (KEY + SCALE)", pal::pink);

    // Hint bar
    g.setColour (pal::navy);
    g.fillRect (hintArea);
    g.setColour (pal::grey);
    g.drawRect (hintArea, 2);
    const juce::Rectangle<int> q (hintArea.getX() + 6, hintArea.getY() + 6, 16, 16);
    g.setColour (pal::yellow);
    g.fillRect (q);
    g.setColour (pal::bg);
    g.setFont (pixelFont (8.0f));
    g.drawText ("?", q.translated (1, 1), juce::Justification::centred);
}

void MainPanel::resized()
{
    helpButton.setBounds (240, 12, 92, 30);
    prevButton.setBounds (430, 12, 30, 30);
    presetBox.setBounds (464, 12, 206, 28);
    nextButton.setBounds (674, 12, 30, 30);
    saveButton.setBounds (710, 12, 62, 30);
    mutateButton.setBounds (778, 12, 80, 30);
    surpriseButton.setBounds (864, 12, 126, 30);
    presetTip.setBounds (430, 42, 560, 18);

    s1 = { 10, 64, 600, 168 };
    sScope = { 620, 64, 370, 168 };
    waveSelector.setBounds (17, 90, 586, 86);
    waveInfo.setBounds (18, 180, 584, 48);
    scope.setBounds (630, 90, 350, 132);

    s2 = { 10, 238, 470, 150 };
    int x = 20;
    for (auto* k : { &attack, &decay, &sustain, &release })
    {
        k->setBounds (x, 262, 66, 118);
        x += 68;
    }
    envelopeView.setBounds (296, 266, 174, 110);

    s3 = { 490, 238, 500, 150 };
    arpModeButtons.setBounds (498, 264, 404, 30);
    chordLabel.setBounds (500, 300, 140, 12);
    chordBox.setBounds (500, 314, 140, 26);
    patternLabel.setBounds (650, 300, 120, 12);
    patternBox.setBounds (650, 314, 120, 26);
    speedLabel.setBounds (780, 300, 120, 12);
    speedBox.setBounds (780, 314, 120, 26);
    arpTip.setBounds (498, 346, 404, 36);
    octaves.setBounds (914, 262, 66, 118);

    s4 = { 10, 394, 560, 150 };
    x = 18;
    for (auto* k : { &vibDepth, &vibRate, &vibDelay, &sweepAmt, &sweepTime, &pwm, &glide })
    {
        k->setBounds (x, 418, 66, 118);
        x += 66;
    }
    voiceModeButtons.setBounds (488, 426, 72, 88);

    s5 = { 580, 394, 410, 150 };
    bits.setBounds (588, 418, 62, 118);
    rate.setBounds (650, 418, 62, 118);
    echoMix.setBounds (712, 418, 62, 118);
    echoTimeLabel.setBounds (778, 426, 76, 12);
    echoTimeBox.setBounds (778, 442, 76, 26);
    echoFeedback.setBounds (858, 418, 62, 118);
    volume.setBounds (922, 418, 62, 118);

    // dice in each numbered section's top-right corner
    const std::pair<DiceButton*, juce::Rectangle<int>> dice[] = { { &diceSound, s1 }, { &diceShape, s2 }, { &diceChords, s3 },
                                                                   { &diceMovement, s4 }, { &diceCrunch, s5 } };
    for (auto [button, section] : dice)
        button->setBounds (section.getRight() - 26, section.getY() + 5, 18, 18);

    filterBox.setBounds (10, 550, 480, 112);
    sPlay = { 500, 550, 490, 112 };
    keyLabel.setBounds (510, 578, 70, 12);
    keyBox.setBounds (510, 592, 70, 26);
    scaleLabel.setBounds (590, 578, 140, 12);
    scaleBox.setBounds (590, 592, 140, 26);
    scaleTip.setBounds (740, 576, 244, 46);

    hintArea = { 10, 668, 980, 28 };
    hintLabel.setBounds (hintArea.reduced (2).withTrimmedLeft (26).withTrimmedRight (176));
    keysStatus.setBounds (hintArea.getRight() - 176, hintArea.getY() + 2, 172, hintArea.getHeight() - 4);
    keyboard.setBounds (10, 702, 980, 70);
    help.setBounds (getLocalBounds());
}

void MainPanel::timerCallback()
{
    scope.refresh();
    envelopeView.refresh();
    filterBox.refresh();

    const int wave = juce::jlimit (0, 7, juce::roundToInt (state.getRawParameterValue (ids::wave)->load()));
    if (wave != lastWave)
    {
        lastWave = wave;
        waveInfo.setText (kWaveInfo[wave], juce::dontSendNotification);
        keyboard.setDrumMode (wave == 7);
    }

    const int arpMode = juce::jlimit (0, 2, juce::roundToInt (state.getRawParameterValue (ids::arpMode)->load()));
    if (arpMode != lastArpMode)
    {
        lastArpMode = arpMode;
        arpTip.setText (kArpTips[arpMode], juce::dontSendNotification);
    }

    syncPresetBox();
    updateScaleTip();

    // Whenever BitBuddy's own window is the active one (and no menu is open),
    // keep the letter keys on the piano. This also recovers focus after a menu
    // closes. It never runs while your DAW has focus, so DAW shortcuts keep working.
    if (windowIsActive())
        focusKeyboard();

    updateKeysStatus();
}

void MainPanel::updateScaleTip()
{
    auto idx = [this] (const char* id, int maxIndex)
    { return juce::jlimit (0, maxIndex, juce::roundToInt (state.getRawParameterValue (id)->load())); };

    const int key = idx (ids::scaleKey, 11), scale = idx (ids::scale, kNumScales - 1);
    const bool smart = idx (ids::arpChord, kSmartChordIndex) == kSmartChordIndex;
    const int stateNow = key + 12 * scale + 12 * kNumScales * (smart ? 1 : 0);
    if (stateNow == lastScaleState)
        return;
    lastScaleState = stateNow;
    keyboard.setScale (key, scale);

    const auto name = keyNames()[key] + " " + scaleNames()[scale];
    juce::String text;
    if (scale == 0)
        text = smart ? "SMART chords use the " + keyNames()[key] + " major scale. Pick a SCALE to lock your keys too!"
                     : "Pick a SCALE and every key you press fits the song - no wrong notes!";
    else
        text = "SCALE LOCK on: every key plays a note of " + name + (smart ? ", and SMART chords follow it." : ". Dim keys snap to a neighbour.");

    scaleTip.setText (text, juce::dontSendNotification);
}

void MainPanel::syncPresetBox()
{
    const auto name = proc.getDisplayPresetName();
    const bool isUser = proc.isUserPresetLoaded();
    if (name == lastPresetName && isUser == lastWasUserPreset)
        return;

    lastPresetName = name;
    lastWasUserPreset = isUser;
    refreshPresetBox();   // the "Delete..." item comes and goes with your own presets

    const int current = proc.getCurrentProgram();
    if (isUser && userPresets.contains (name))
        presetBox.setSelectedId (kUserPresetIdBase + userPresets.indexOf (name), juce::dontSendNotification);
    else if (! isUser && name == proc.getProgramName (current))
        presetBox.setSelectedId (current + 1, juce::dontSendNotification);
    else
        presetBox.setText (name, juce::dontSendNotification);

    presetTip.setText (proc.getPresetTip(), juce::dontSendNotification);
}

void MainPanel::refreshPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);

    userPresets = BitBuddyProcessor::getUserPresetNames();
    if (! userPresets.isEmpty())
    {
        presetBox.addSectionHeading ("MY PRESETS");
        for (int i = 0; i < userPresets.size(); ++i)
            presetBox.addItem (userPresets[i], kUserPresetIdBase + i);
    }

    if (proc.isUserPresetLoaded())
    {
        presetBox.addSeparator();
        presetBox.addItem ("Delete \"" + proc.getDisplayPresetName() + "\"...", kDeletePresetId);
    }
}

void MainPanel::askToSavePreset()
{
    // A dialog inside BitBuddy's own window (works the same in every DAW and on phones)
    auto* w = new juce::AlertWindow ("SAVE PRESET", "Give your sound a name.\nIt will appear under MY PRESETS in the preset menu.",
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", proc.getDisplayPresetName().trimCharactersAtEnd (" *"), "Name:");
    w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    addAndMakeVisible (w);
    w->setCentrePosition (getLocalBounds().getCentre());

    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result != 1)
            return;

        const auto name = w->getTextEditorContents ("name").trim();
        if (name.isEmpty())
            return;

        if (BitBuddyProcessor::getUserPresetNames().contains (name))
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withTitle ("REPLACE PRESET?")
                                              .withMessage ("You already have a preset called \"" + name + "\". Replace it with this sound?")
                                              .withButton ("REPLACE")
                                              .withButton ("CANCEL")
                                              .withAssociatedComponent (this),
                                          [safe = juce::Component::SafePointer<MainPanel> (this), name] (int choice)
                                          {
                                              if (safe != nullptr && choice == 1)
                                                  safe->savePresetAs (name);
                                          });
            return;
        }
        savePresetAs (name);
    }), true);
}

void MainPanel::savePresetAs (const juce::String& name)
{
    if (! proc.saveUserPreset (name))
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withTitle ("COULDN'T SAVE")
                                          .withMessage ("BitBuddy couldn't write to " + BitBuddyProcessor::getUserPresetFolder().getFullPathName())
                                          .withButton ("OK")
                                          .withAssociatedComponent (this),
                                      nullptr);
    lastPresetName = {};   // re-sync the menu
}

void MainPanel::askToDeletePreset()
{
    const auto name = proc.getDisplayPresetName();
    lastPresetName = {};   // put the menu back to the current preset name
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withTitle ("DELETE PRESET?")
                                      .withMessage ("Delete your preset \"" + name + "\"? It goes to the recycle bin / trash. "
                                                    "The sound you hear now stays until you pick another one.")
                                      .withButton ("DELETE")
                                      .withButton ("CANCEL")
                                      .withAssociatedComponent (this),
                                  [safe = juce::Component::SafePointer<MainPanel> (this), name] (int choice)
                                  {
                                      if (safe == nullptr || choice != 1)
                                          return;
                                      BitBuddyProcessor::deleteUserPreset (name);
                                      safe->proc.markRemixed();
                                      safe->lastPresetName = {};
                                  });
}

void MainPanel::mouseEnter (const juce::MouseEvent& e)
{
    showHintFor (e.eventComponent);
}

void MainPanel::mouseExit (const juce::MouseEvent& e)
{
    // On a touch screen the finger "leaves" as soon as it lifts: keep the hint up
    if (! e.source.isTouch())
        hintLabel.setText (kDefaultHint, juce::dontSendNotification);
}

void MainPanel::showHintFor (juce::Component* c)
{
    while (c != nullptr && c != this)
    {
        if (auto* tc = dynamic_cast<juce::TooltipClient*> (c))
        {
            const auto text = tc->getTooltip();
            if (text.isNotEmpty())
            {
                hintLabel.setText (text, juce::dontSendNotification);
                return;
            }
        }
        c = c->getParentComponent();
    }
    hintLabel.setText (kDefaultHint, juce::dontSendNotification);
}
} // namespace bb

//==============================================================================
BitBuddyEditor::BitBuddyEditor (BitBuddyProcessor& p)
    : AudioProcessorEditor (&p)
{
    panel = std::make_unique<bb::MainPanel> (p, lnf);
    addAndMakeVisible (*panel);

    setResizable (true, true);
    setResizeLimits (700, 545, 2000, 1556);
    if (auto* sizeLimits = getConstrainer())
        sizeLimits->setFixedAspectRatio ((double) bb::MainPanel::baseWidth / (double) bb::MainPanel::baseHeight);

    setSize (bb::MainPanel::baseWidth, bb::MainPanel::baseHeight);
}

BitBuddyEditor::~BitBuddyEditor()
{
    panel.reset();
}

void BitBuddyEditor::paint (juce::Graphics& g)
{
    g.fillAll (bb::pal::bg);
}

void BitBuddyEditor::resized()
{
    if (panel == nullptr)
        return;

    // Fit the 1000 x 660 design inside any window or phone screen, centred
    const float w = (float) bb::MainPanel::baseWidth, h = (float) bb::MainPanel::baseHeight;
    const float scale = juce::jmin ((float) getWidth() / w, (float) getHeight() / h);
    panel->setTransform (juce::AffineTransform::scale (scale)
                             .translated (((float) getWidth() - w * scale) * 0.5f, ((float) getHeight() - h * scale) * 0.5f));
    panel->setBounds (0, 0, bb::MainPanel::baseWidth, bb::MainPanel::baseHeight);
}
