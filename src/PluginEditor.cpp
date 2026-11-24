#include "PluginEditor.h"
#include "Parameters.h"
#include "GameSounds.h"
#include "Presets.h"
#include "Scales.h"

#include <cmath>

namespace bb
{
namespace
{
const char* const kWaveShort[kNumWaves] = { "SQUARE", "PULSE 25", "PULSE 12", "TRIANGLE", "SAW", "NOISE", "METAL", "DRUMS", "DRAW", "VOICE" };

const char* const kWaveInfo[kNumWaves] = {
    "SQUARE (50% pulse): bold, hollow and bright. The workhorse of 8-bit music - great for leads and bass.",
    "PULSE 25%: a thinner, nasal square wave. THE classic NES lead sound.",
    "PULSE 12.5%: very thin and buzzy. Cuts through everything - perfect for arpeggios and sparkly leads.",
    "TRIANGLE: soft and round, built from 16 little steps like the NES chip. Usually used for bass lines.",
    "SAW: bright and buzzy, like a brassy synth. Not on the NES, but found on later sound chips.",
    "NOISE: random hiss from a 15-bit shift register. Low notes = rumble and explosions, high notes = hi-hats and wind.",
    "METAL NOISE: the noise chip's 'short mode' - a buzzy, metallic tone tuned to your keys. Bells, robots, weird FX!",
    "DRUM KIT - every key is a drum!  C Kick,  C# Rim,  D Snare,  D# Clap,  E Snare 2,  F/G/A Toms,  "
    "F# Hi-Hat,  G# Pedal Hat,  A# Open Hat,  B Crash",
    "DRAW: draw your own wave on the SCREEN, like the Game Boy's wave channel! Smooth shapes sound soft, "
    "jagged ones buzzy. Try SINE / SAW / RND, then draw over them.",
    "VOICE: plays a recording like the NES sample channel - a sung 'aah' until you record your own in box 8 "
    "(or load a WAV). Middle C = its own pitch. Try CHIP: NES for a crunchy 1-bit voice!",
};

const char* const kArpTips[3] = {
    "Tip: set CHORD, then hold just ONE key for instant 8-bit chords!",
    "CHORD is on: hold ONE key - BitBuddy flips through the chord notes super fast.",
    "NOTES is on: hold 2-4 keys - they play one after another as a pattern.",
};

#if JUCE_IOS || JUCE_ANDROID
const char* const kDefaultHint = "Tap anything to learn what it does.   Swipe up / down to see more.";
#else
const char* const kDefaultHint = "Hover over anything to learn what it does.   Double-click a knob to reset it.";
#endif

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
        case 8:   // a wobbly hand-drawn staircase
        {
            static const float steps[8] = { 0.2f, 0.9f, 0.6f, -0.2f, -0.9f, -0.5f, 0.4f, -0.1f };
            return steps[(int) (f * 8.0f) & 7];
        }
        case 9:   // a little burst of speech: wobbly, rising then fading
        {
            const float env = std::sin (juce::jlimit (0.0f, 1.0f, x * 0.5f) * juce::MathConstants<float>::pi);
            return env * 0.9f * std::sin (x * 22.0f + 3.0f * std::sin (x * 5.0f)) * (0.6f + 0.4f * std::sin (x * 41.0f));
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

// Drawing with the mouse (waves, macros): a fast mouse skips steps, so fill
// them in on a straight line from the last point
void fillSkippedSteps (int fromStep, int fromValue, int toStep, int toValue, const std::function<void (int, int)>& set)
{
    if (fromStep < 0 || fromStep == toStep)
        return;
    const int dir = toStep > fromStep ? 1 : -1;
    for (int s = fromStep + dir; s != toStep; s += dir)
        set (s, juce::roundToInt (juce::jmap ((float) (s - fromStep), 0.0f, (float) (toStep - fromStep),
                                              (float) fromValue, (float) toValue)));
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

    setViewportIgnoreDragFlag (true);   // on a phone: dragging a knob turns it, it doesn't scroll the page
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
HideButton::HideButton() : juce::Button ("HIDE")
{
    setTooltip ("HIDE this box (it keeps working). Bring it back from VIEW at the top.");
}

void HideButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f).translated (0.0f, down ? 1.0f : 0.0f);
    g.setColour (over ? pal::red : pal::grey);
    const float s = juce::jmax (2.0f, std::floor (r.getWidth() / 7.0f));
    for (int i = 0; i < 5; ++i)   // a chunky pixel "x"
    {
        const float o = (float) i * (r.getWidth() - s) / 4.0f;
        g.fillRect (r.getX() + o, r.getY() + o, s, s);
        g.fillRect (r.getRight() - s - o, r.getY() + o, s, s);
    }
}

Box::Box (const juce::String& boxId, const juce::String& numberText, const juce::String& titleText, juce::Colour c,
          int w, int h)
    : accent (c), id (boxId), number (numberText), title (titleText), naturalWidth (w), naturalHeight (h), dice (c)
{
    dice.onClick = [this] { if (onDice != nullptr) onDice(); };
    addChildComponent (dice);
    hide.onClick = [this] { if (onHide != nullptr) onHide (*this); };
    addAndMakeVisible (hide);
}

void Box::setDice (std::function<void()> roll)
{
    onDice = std::move (roll);
    setDiceShown (diceShown);
}

void Box::setDiceShown (bool shouldShow)
{
    diceShown = shouldShow;
    dice.setVisible (onDice != nullptr && diceShown);
    resized();
}

void Box::paint (juce::Graphics& g)
{
    drawBoxFrame (g, getLocalBounds(), number, title, accent);
}

void Box::resized()
{
    const bool hasDice = onDice != nullptr && diceShown;
    dice.setBounds (getWidth() - 26, 5, 18, 18);
    hide.setBounds (getWidth() - (hasDice ? 46 : 26), 5, 18, 18);
    layoutContent (getLocalBounds().reduced (8, 6).withTrimmedTop (20));
}

// Drag a box by its title bar to move it
void Box::mouseDown (const juce::MouseEvent& e)
{
    dragging = e.eventComponent == this && e.y < headerHeight;
    if (dragging && onDragStart != nullptr)
        onDragStart (*this, e);
}

void Box::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging && onDragMove != nullptr)
        onDragMove (*this, e);
}

void Box::mouseUp (const juce::MouseEvent& e)
{
    if (dragging && onDragEnd != nullptr)
        onDragEnd (*this, e);
    dragging = false;
}

void Box::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (e.y < headerHeight ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
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
    : Box ("filter", "6", "C64 FILTER  (SQUELCH!)", filterAccent(), 480, 112),
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
GameSoundsBox::GameSoundsBox (BitBuddyProcessor& p)
    : Box ("game", "7", "GAME SOUNDS  (SOUND FX + JINGLES)", pal::yellow, 600, 96), proc (p)
{
    fxLabel.setFont (pixelFont (8.0f));
    fxLabel.setColour (juce::Label::textColourId, accent);
    fxLabel.setText ("NEW FX", juce::dontSendNotification);
    addAndMakeVisible (fxLabel);

    for (int i = 0; i < kNumSfx; ++i)
    {
        auto* b = fxButtons.add (new juce::TextButton (sfxName (i)));
        b->setTooltip (juce::String (sfxHint (i)) + "  (It becomes your sound: tweak it, SAVE it, or SAVE WAV for your game.)");
        b->setColour (juce::TextButton::buttonColourId, pal::navy);
        b->onClick = [this, i] { proc.makeSoundEffect (i); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < kNumJingles; ++i)
    {
        auto* b = jingleButtons.add (new juce::TextButton (jingleName (i)));
        b->setTooltip (jingleHint (i));
        b->setColour (juce::TextButton::buttonColourId, pal::purple);
        b->onClick = [this, i] { proc.playJingle (i); };
        addAndMakeVisible (b);
    }

    againButton.setTooltip ("AGAIN: plays the current sound like a sound effect again (the same note, just as long).");
    againButton.onClick = [this] { proc.playSoundEffect(); };
    saveButton.setTooltip ("SAVE WAV: saves the current sound effect as a WAV file - ready for your own game! "
                           "(Folder: " + BitBuddyProcessor::getSoundsFolder().getFullPathName() + ")");
    saveButton.setColour (juce::TextButton::buttonColourId, pal::green);
    saveButton.onClick = [this] { saveWav(); };
    for (auto* b : { &againButton, &saveButton })
        addAndMakeVisible (b);
}

void GameSoundsBox::layoutContent (juce::Rectangle<int> area)
{
    area = area.reduced (2, 4);
    auto row1 = area.removeFromTop (26);
    area.removeFromTop (6);
    auto row2 = area.removeFromTop (26);

    fxLabel.setBounds (row1.removeFromLeft (64));
    const int fxWidth = row1.getWidth() / kNumSfx;
    for (auto* b : fxButtons)
        b->setBounds (row1.removeFromLeft (fxWidth).reduced (2, 0));

    // (the box title says JINGLES, so the jingle buttons get the whole rest of the row)
    againButton.setBounds (row2.removeFromLeft (64).reduced (2, 0));
    saveButton.setBounds (row2.removeFromLeft (92).reduced (2, 0));
    row2.removeFromLeft (8);
    const int jWidth = row2.getWidth() / kNumJingles;
    for (auto* b : jingleButtons)
        b->setBounds (row2.removeFromLeft (jWidth).reduced (2, 0));
}

void GameSoundsBox::saveWav()
{
    const auto name = juce::File::createLegalFileName (proc.getDisplayPresetName().removeCharacters ("*#").trim());
    const auto file = BitBuddyProcessor::getSoundsFolder()
                          .getNonexistentChildFile (name.isEmpty() ? "BitBuddy sound" : name, ".wav", true);
    if (! proc.renderSoundToWav (file))
    {
        if (onMessage != nullptr)
            onMessage ("Couldn't save the WAV in " + file.getParentDirectory().getFullPathName());
        return;
    }
    if (onMessage != nullptr)
        onMessage ("Saved " + file.getFileName() + "  in  " + file.getParentDirectory().getFullPathName());
   #if JUCE_CONTENT_SHARING && (JUCE_IOS || JUCE_ANDROID)
    shareBox = juce::ContentSharer::shareFilesScoped ({ juce::URL (file) }, [] (bool, const juce::String&) {}, this);
   #else
    file.revealToUser();
   #endif
}

//==============================================================================
void VoiceView::refresh (const juce::String& status, float micLevel)
{
    if (voice.getVersion() != shownVersion || peaks.size() != (size_t) juce::jmax (1, getWidth()))
    {
        shownVersion = voice.getVersion();
        peaks.assign ((size_t) juce::jmax (1, getWidth()), 0.0f);
        const int length = voice.getLength();
        for (int i = 0; i < length; ++i)
        {
            auto& p = peaks[(size_t) (i * (int) peaks.size() / juce::jmax (1, length))];
            p = juce::jmax (p, std::abs (voice.at (i)));
        }
        repaint();
    }
    if (status != statusText || std::abs (micLevel - level) > 0.02f)
    {
        statusText = status;
        level = micLevel;
        repaint();
    }
}

void VoiceView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (pal::screen);
    g.fillRoundedRectangle (area, 3.0f);

    const float mid = area.getCentreY(), half = area.getHeight() * 0.42f;
    g.setColour (pal::cyan.withAlpha (statusText.isEmpty() ? 0.9f : 0.35f));
    for (size_t x = 0; x < peaks.size(); x += 2)   // chunky 2-pixel columns
        g.fillRect ((float) x, mid - peaks[x] * half, 2.0f, juce::jmax (1.0f, peaks[x] * half * 2.0f));

    if (statusText.isNotEmpty())
    {
        if (level > 0.0f)   // microphone level bar along the bottom
        {
            g.setColour (level > 0.9f ? pal::red : pal::lime);
            g.fillRect (area.withTop (area.getBottom() - 3.0f).withWidth (area.getWidth() * juce::jmin (1.0f, level * 1.5f)));
        }
        g.setColour (pal::white);
        g.setFont (pixelFont (7.0f));
        g.drawText (statusText, area.reduced (4.0f, 0.0f), juce::Justification::centred);
    }
}

//==============================================================================
VoiceBox::VoiceBox (BitBuddyProcessor& p)
    : Box ("voice", "8", "YOUR VOICE  (VOICE WAVE + HUM)", pal::cyan, 370, 96), proc (p), view (p.getVoiceSample())
{
    recButton.setTooltip ("REC VOICE: say or sing something short (up to 3 seconds) - BitBuddy turns it into the VOICE wave. "
                          "Then play it on the keys: middle C = your own pitch. Try CHIP: NES for a crunchy 1-bit voice!");
    recButton.setColour (juce::TextButton::buttonColourId, pal::red);
    recButton.onClick = [this] { toggleRecording(); };

    loadButton.setTooltip ("LOAD WAV: use the start of any WAV file (up to 3 seconds of sound) as the VOICE wave.");
    loadButton.onClick = [this] { loadWav(); };

    aahButton.setTooltip ("AAH: back to BitBuddy's own sung 'aah'.");
    aahButton.onClick = [this]
    {
        proc.resetVoice();
        useVoice();
        say ("VOICE: back to the sung 'aah'.");
    };

    humButton.setTooltip ("HUM: hum or sing a tune into the microphone and BitBuddy plays it with your sound! "
                          "(SCALE LOCK keeps every note in key. Use headphones so BitBuddy doesn't hear itself.)");
    humButton.setColour (juce::TextButton::buttonColourId, pal::purple);
    humButton.setClickingTogglesState (false);
    humButton.onClick = [this] { toggleHum(); };

    view.setTooltip ("Your voice recording (the VOICE wave plays this).");

    appOnly.setFont (textFont (12.0f, true));
    appOnly.setColour (juce::Label::textColourId, pal::light);
    appOnly.setText ("REC VOICE + HUM: in the BitBuddy app", juce::dontSendNotification);

    const bool hasMic = MicInput::isAvailable();
    for (auto* c : { (juce::Component*) &loadButton, (juce::Component*) &aahButton, (juce::Component*) &view })
        addAndMakeVisible (c);
    if (hasMic)
    {
        addAndMakeVisible (recButton);
        addAndMakeVisible (humButton);
    }
    else
    {
        addAndMakeVisible (appOnly);
    }
    startTimerHz (15);
}

VoiceBox::~VoiceBox()
{
    stopMic();
}

void VoiceBox::layoutContent (juce::Rectangle<int> area)
{
    area = area.reduced (2, 4);
    auto row1 = area.removeFromTop (26);
    area.removeFromTop (6);
    view.setBounds (area.removeFromTop (26));

    if (MicInput::isAvailable())
    {
        recButton.setBounds (row1.removeFromLeft (104).reduced (2, 0));
        loadButton.setBounds (row1.removeFromLeft (92).reduced (2, 0));
        aahButton.setBounds (row1.removeFromLeft (56).reduced (2, 0));
        humButton.setBounds (row1.reduced (2, 0));
    }
    else
    {
        loadButton.setBounds (row1.removeFromLeft (92).reduced (2, 0));
        aahButton.setBounds (row1.removeFromLeft (56).reduced (2, 0));
        appOnly.setBounds (row1.reduced (4, 0));
    }
}

void VoiceBox::timerCallback()
{
    juce::String status;
    if (micUse == MicUse::Record)
    {
        const double seconds = captured.load() / juce::jmax (1.0, mic.getSampleRate());
        status = "RECORDING " + juce::String (juce::jmax (0.0, 3.0 - seconds), 1) + " s";
        if (seconds >= 3.0)
            finishRecording();
    }
    else if (micUse == MicUse::Hum)
    {
        const float hz = proc.humTracker.getLastPitch();
        status = hz > 0.0f ? "HUM: " + juce::MidiMessage::getMidiNoteName (juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0)), true, true, 4)
                           : juce::String ("HUM: sing!");
    }
    view.refresh (status, micUse != MicUse::None ? mic.getLevel() : 0.0f);
}

void VoiceBox::stopMic()
{
    mic.stop();
    if (micUse == MicUse::Hum)
        proc.humTracker.reset();   // (the microphone thread has stopped: safe to end the note here)
    micUse = MicUse::None;
    recButton.setButtonText ("REC VOICE");
    humButton.setToggleState (false, juce::dontSendNotification);
    humButton.setButtonText ("HUM");
}

void VoiceBox::toggleRecording()
{
    if (micUse == MicUse::Record)
    {
        finishRecording();
        return;
    }
    stopMic();

    capture.assign ((size_t) (96000.0 * 3.2), 0.0f);   // enough for 3 s at up to 96 kHz
    captured = 0;
    micUse = MicUse::Record;
    recButton.setButtonText ("STOP");
    mic.start ([this] (const float* x, int n, double)
               {
                   const int w = captured.load (std::memory_order_relaxed);
                   const int k = juce::jmin (n, (int) capture.size() - w);
                   if (k > 0)
                   {
                       std::copy (x, x + k, capture.begin() + w);
                       captured.store (w + k, std::memory_order_release);
                   }
               },
               [safe = juce::Component::SafePointer<VoiceBox> (this)] (bool ok, const juce::String& problem)
               {
                   if (safe == nullptr)
                       return;
                   if (ok)
                       safe->say ("Recording! Say or sing something (up to 3 seconds)... press STOP when you're done.");
                   else
                   {
                       safe->micUse = MicUse::None;
                       safe->recButton.setButtonText ("REC VOICE");
                       safe->say (problem);
                   }
               });
}

void VoiceBox::finishRecording()
{
    const double rate = mic.getSampleRate();
    stopMic();   // (after this the microphone thread has let go of `capture`)
    juce::AudioBuffer<float> audio (1, juce::jmax (1, captured.load()));
    audio.copyFrom (0, 0, capture.data(), audio.getNumSamples());
    capture = {};
    if (proc.setVoiceRecording (audio, rate))
    {
        useVoice();
        say ("Got it! That's your VOICE wave now - play it on the keys (middle C = your own pitch). Try CHIP: NES!");
    }
    else
    {
        say ("BitBuddy didn't hear anything. Is your microphone on (and allowed in your settings)?");
    }
}

void VoiceBox::toggleHum()
{
    if (micUse == MicUse::Hum)
    {
        stopMic();
        say ("HUM is off.");
        return;
    }
    stopMic();
    micUse = MicUse::Hum;
    humButton.setToggleState (true, juce::dontSendNotification);
    humButton.setButtonText ("HUM ON");
    auto& tracker = proc.humTracker;
    mic.start ([&tracker] (const float* x, int n, double rate) { tracker.process (x, n, rate); },
               [safe = juce::Component::SafePointer<VoiceBox> (this)] (bool ok, const juce::String& problem)
               {
                   if (safe == nullptr)
                       return;
                   if (ok)
                       safe->say ("HUM: hum or sing a tune - BitBuddy plays it! (Headphones help: then it doesn't hear itself.)");
                   else
                   {
                       safe->stopMic();
                       safe->say (problem);
                   }
               });
}

void VoiceBox::loadWav()
{
    chooser = std::make_unique<juce::FileChooser> ("Pick a sound for the VOICE wave", juce::File(), "*.wav;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<VoiceBox> (this)] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getURLResults().isEmpty())
            return;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto stream = fc.getURLResult().createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress));
        std::unique_ptr<juce::AudioFormatReader> reader (stream != nullptr ? formats.createReaderFor (std::move (stream)) : nullptr);
        if (reader == nullptr)
        {
            safe->say ("BitBuddy couldn't read that file. Try a WAV or AIFF.");
            return;
        }
        const int length = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 20.0));
        juce::AudioBuffer<float> audio ((int) juce::jmin (2u, reader->numChannels), juce::jmax (1, length));
        reader->read (&audio, 0, audio.getNumSamples(), 0, true, audio.getNumChannels() > 1);
        if (safe->proc.setVoiceRecording (audio, reader->sampleRate))
        {
            safe->useVoice();
            safe->say ("Loaded \"" + fc.getURLResult().getFileName() + "\" as your VOICE wave!");
        }
        else
        {
            safe->say ("That file seems to be silent.");
        }
    });
}

void VoiceBox::useVoice()
{
    if (auto* wave = proc.apvts.getParameter (ids::wave))
        wave->setValueNotifyingHost (wave->convertTo0to1 ((float) Wave::Voice));
    const double seconds = proc.getVoiceSample().getLength() / VoiceSample::rate;
    proc.notePlayer.play ({ { 0.0, seconds + 0.05, VoiceSample::rootNote, 0.9f } });
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
WaveDrawer::WaveDrawer (BitBuddyProcessor& p) : proc (p)
{
    setTooltip ("DRAW YOUR WAVE: click and drag to draw one cycle of the sound - 32 steps x 16 levels, just like the "
                "Game Boy's wave channel. Smooth = soft and flute-like, jagged = buzzy and bright.");
    setViewportIgnoreDragFlag (true);   // dragging draws, it doesn't scroll

    static const char* const names[] = { "SINE", "TRI", "SAW", "SQR", "RND" };
    static const char* const hints[] = { "SINE: a smooth, round wave - soft and flute-like.",
                                         "TRI: a triangle - soft, a bit hollow.",
                                         "SAW: a ramp - bright and brassy.",
                                         "SQR: a square - bold and hollow.",
                                         "RND: a random (smooth-ish) wave - every press is a new sound!" };
    for (int i = 0; i < 5; ++i)
    {
        auto* b = shapeButtons.add (new juce::TextButton (names[i]));
        b->setTooltip (hints[i]);
        b->setColour (juce::TextButton::buttonColourId, pal::screen);
        b->setColour (juce::TextButton::textColourOffId, pal::lime);
        b->onClick = [this, i] { applyShape (i); };
        addAndMakeVisible (b);
    }
}

void WaveDrawer::resized()
{
    auto row = getLocalBounds().removeFromTop (18).removeFromRight (5 * 40).reduced (0, 2);
    for (auto* b : shapeButtons)
        b->setBounds (row.removeFromLeft (40).reduced (1, 0));
}

juce::Rectangle<float> WaveDrawer::gridArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (20.0f).reduced (6.0f, 4.0f);
}

void WaveDrawer::refresh()
{
    const auto now = proc.getCustomWave();
    if (now != shown)
    {
        shown = now;
        repaint();
    }
}

void WaveDrawer::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (pal::screen);
    g.fillRect (r);

    const auto grid = gridArea();
    const float colW = grid.getWidth() / kCustomWaveSteps, rowH = grid.getHeight() / 16.0f;

    g.setColour (pal::green.withAlpha (0.12f));
    for (int c = 0; c <= kCustomWaveSteps; ++c)
        g.fillRect (grid.getX() + c * colW, grid.getY(), 1.0f, grid.getHeight());
    for (int row = 0; row <= 16; ++row)
        g.fillRect (grid.getX(), grid.getY() + row * rowH, grid.getWidth(), 1.0f);
    g.setColour (pal::green.withAlpha (0.3f));
    g.fillRect (grid.getX(), grid.getCentreY(), grid.getWidth(), 1.0f);

    // Each step: a soft bar from the middle line, topped by a bright pixel line
    for (int i = 0; i < kCustomWaveSteps; ++i)
    {
        const float x = grid.getX() + i * colW;
        const float top = grid.getBottom() - (shown[(size_t) i] + 1) * rowH;
        const float mid = grid.getCentreY();
        g.setColour (pal::lime.withAlpha (0.25f));
        g.fillRect (x + 1.0f, juce::jmin (top + rowH, mid), colW - 1.0f, std::abs (mid - (top + rowH)));
        g.setColour (pal::lime);
        g.fillRect (x, top + rowH * 0.5f - 1.5f, colW, 3.0f);
    }

    g.setColour (pal::green);
    g.setFont (pixelFont (6.0f));
    g.drawText ("CLICK + DRAG", r.withHeight (18.0f).withTrimmedLeft (6.0f), juce::Justification::centredLeft);
}

void WaveDrawer::drawAt (juce::Point<float> pos, bool connectToLast)
{
    const auto grid = gridArea();
    const int step = juce::jlimit (0, kCustomWaveSteps - 1, (int) ((pos.x - grid.getX()) / grid.getWidth() * kCustomWaveSteps));
    const int value = juce::jlimit (0, 15, (int) std::floor ((grid.getBottom() - pos.y) / grid.getHeight() * 16.0f));

    if (connectToLast)
        fillSkippedSteps (lastStep, lastValue, step, value, [this] (int s, int v) { proc.setCustomWaveStep (s, v); });
    proc.setCustomWaveStep (step, value);
    lastStep = step;
    lastValue = value;
    refresh();
}

void WaveDrawer::mouseDown (const juce::MouseEvent& e)
{
    if (gridArea().expanded (4.0f).contains (e.position))
    {
        useDrawWave();
        drawAt (e.position, false);
    }
}

// Drawing only makes sense if you can hear it: switch the sound to DRAW
void WaveDrawer::useDrawWave()
{
    if (auto* wave = proc.apvts.getParameter (ids::wave))
        if (juce::roundToInt (wave->convertFrom0to1 (wave->getValue())) != (int) Wave::Custom)
            wave->setValueNotifyingHost (wave->convertTo0to1 ((float) Wave::Custom));
}

void WaveDrawer::mouseDrag (const juce::MouseEvent& e)
{
    if (lastStep >= 0)
        drawAt (e.position, true);
}

void WaveDrawer::mouseUp (const juce::MouseEvent&)
{
    if (lastStep >= 0)
        proc.customWaveEdited();
    lastStep = -1;
}

void WaveDrawer::applyShape (int shape)
{
    CustomWave w {};
    juce::Random rng;
    float walk = 7.5f;
    for (int i = 0; i < kCustomWaveSteps; ++i)
    {
        const float x = (i + 0.5f) / kCustomWaveSteps;
        float v = 0.0f;
        switch (shape)
        {
            case 0:  v = 7.5f + 7.5f * std::sin (juce::MathConstants<float>::twoPi * x); break;
            case 1:  v = x < 0.5f ? 15.0f * 2.0f * x : 15.0f * (2.0f - 2.0f * x); break;
            case 2:  v = 15.0f * x; break;
            case 3:  v = x < 0.5f ? 15.0f : 0.0f; break;
            default: walk = juce::jlimit (0.0f, 15.0f, walk + (rng.nextFloat() - 0.5f) * 7.0f); v = walk; break;
        }
        w[(size_t) i] = (std::uint8_t) juce::jlimit (0, 15, juce::roundToInt (v));
    }
    useDrawWave();
    proc.setCustomWave (w);
    proc.customWaveEdited();
    refresh();
}

//==============================================================================
namespace
{
juce::Colour laneColour (int lane) { const juce::Colour c[kNumMacroLanes] = { pal::lime, pal::cyan, pal::orange, pal::yellow }; return c[juce::jlimit (0, kNumMacroLanes - 1, lane)]; }
const char* const kLaneHints[kNumMacroLanes] = {
    "VOL macro: the volume of each step (0-15). Fades, stutters and fake echoes - draw it going down for a 'pluck'.",
    "ARP macro: semitones added to the note at each step. 0,4,7 looping = the classic NES chord; big jumps = blips and coins.",
    "PITCH macro: small pitch bends (1/8 semitone per step). Scoop into notes, warble, or wobble like a broken tape.",
    "DUTY macro: the pulse width at each step (12.5 / 25 / 50 / 75%) for SQUARE and PULSE waves. Tone that changes as it plays!",
};
} // namespace

MacroEditor::MacroEditor (BitBuddyProcessor& p) : proc (p)
{
    setTooltip ("MACROS: draw how each note changes over time - 60 steps every second, like FamiTracker. Pick a lane, "
                "turn it ON, click + drag to draw. LEN = number of steps; click the strip at the bottom to set where it loops.");
    setViewportIgnoreDragFlag (true);   // dragging draws, it doesn't scroll

    for (int i = 0; i < kNumMacroLanes; ++i)
    {
        auto* b = laneButtons.add (new juce::TextButton (kMacroLanes[i].name));
        b->setTooltip (kLaneHints[i]);
        b->setColour (juce::TextButton::buttonOnColourId, laneColour (i));
        b->onClick = [this, i] { selectLane (i); };
        addAndMakeVisible (b);
    }

    onButton.setTooltip ("ON: use this lane. Lanes that are off don't change the sound.");
    onButton.onClick = [this] { setLength (shown[(size_t) lane].isOn() ? 0 : lengthWhenOff[(size_t) lane]); };
    minusButton.setTooltip ("LEN -: one step shorter.");
    minusButton.onClick = [this] { setLength (juce::jmax (1, shown[(size_t) lane].length - 1)); };
    plusButton.setTooltip ("LEN +: one step longer (up to 32).");
    plusButton.onClick = [this] { setLength (juce::jmin (kMacroSteps, juce::jmax (1, shown[(size_t) lane].length) + 1)); };
    clearButton.setTooltip ("CLR: flatten this lane back to its starting value.");
    clearButton.onClick = [this]
    {
        for (int s = 0; s < kMacroSteps; ++s)
            proc.setMacroStep (lane, s, kMacroLanes[lane].defaultValue);
        proc.macrosEdited();
        refresh();
    };

    lengthLabel.setFont (pixelFont (6.0f));
    lengthLabel.setJustificationType (juce::Justification::centred);
    lengthLabel.setColour (juce::Label::textColourId, pal::white);

    for (auto* c : { (juce::Component*) &onButton, (juce::Component*) &minusButton, (juce::Component*) &plusButton,
                     (juce::Component*) &clearButton, (juce::Component*) &lengthLabel })
        addAndMakeVisible (c);

    for (auto* b : { &onButton, &minusButton, &plusButton, &clearButton })
    {
        b->setColour (juce::TextButton::buttonColourId, pal::screen);
        b->setColour (juce::TextButton::textColourOffId, pal::white);
    }
    selectLane (0);
}

void MacroEditor::resized()
{
    auto row = getLocalBounds().removeFromTop (17).reduced (0, 1);
    for (auto* b : laneButtons)
        b->setBounds (row.removeFromLeft (row.getWidth() >= 300 ? 46 : 40).reduced (1, 0));
    row.removeFromLeft (6);
    onButton.setBounds (row.removeFromLeft (30).reduced (1, 0));
    row.removeFromLeft (6);
    minusButton.setBounds (row.removeFromLeft (18).reduced (1, 0));
    lengthLabel.setBounds (row.removeFromLeft (46));
    plusButton.setBounds (row.removeFromLeft (18).reduced (1, 0));
    clearButton.setBounds (row.removeFromRight (34).reduced (1, 0));
}

juce::Rectangle<float> MacroEditor::gridArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (20.0f).withTrimmedBottom (14.0f).reduced (4.0f, 2.0f);
}

juce::Rectangle<float> MacroEditor::loopArea() const
{
    const auto grid = gridArea();
    return { grid.getX(), grid.getBottom() + 3.0f, grid.getWidth(), 9.0f };
}

int MacroEditor::stepAt (float x) const
{
    const auto grid = gridArea();
    return juce::jlimit (0, kMacroSteps - 1, (int) ((x - grid.getX()) / grid.getWidth() * kMacroSteps));
}

void MacroEditor::selectLane (int newLane)
{
    lane = juce::jlimit (0, kNumMacroLanes - 1, newLane);
    updateButtons();
    repaint();
}

void MacroEditor::updateButtons()
{
    for (int i = 0; i < laneButtons.size(); ++i)
    {
        laneButtons[i]->setToggleState (i == lane, juce::dontSendNotification);
        laneButtons[i]->setColour (juce::TextButton::buttonColourId,
                                   shown[(size_t) i].isOn() ? laneColour (i).withAlpha (0.35f) : pal::screen);
        laneButtons[i]->setColour (juce::TextButton::textColourOffId, laneColour (i));
    }
    const auto& m = shown[(size_t) lane];
    onButton.setButtonText (m.isOn() ? "ON" : "OFF");
    onButton.setColour (juce::TextButton::buttonColourId, m.isOn() ? laneColour (lane).withAlpha (0.6f) : pal::screen);
    lengthLabel.setText ("LEN " + juce::String (m.isOn() ? m.length : 0), juce::dontSendNotification);
}

void MacroEditor::refresh()
{
    bool changed = false;
    for (int i = 0; i < kNumMacroLanes; ++i)
    {
        const auto now = proc.getMacro (i);
        auto& old = shown[(size_t) i];
        if (now.length != old.length || now.loop != old.loop || now.values != old.values)
        {
            old = now;
            changed = true;
        }
    }
    if (changed)
    {
        updateButtons();
        repaint();
    }
}

void MacroEditor::setLength (int newLength)
{
    auto m = proc.getMacro (lane);
    if (m.isOn())
        lengthWhenOff[(size_t) lane] = m.length;
    m.length = juce::jlimit (0, kMacroSteps, newLength);
    proc.setMacro (lane, m);
    proc.macrosEdited();
    refresh();
}

void MacroEditor::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (pal::screen);
    g.fillRect (r);

    const auto& info = kMacroLanes[lane];
    const auto& m = shown[(size_t) lane];
    const auto colour = laneColour (lane);
    const auto grid = gridArea();
    const float colW = grid.getWidth() / kMacroSteps;
    const float range = (float) (info.maxValue - info.minValue);
    auto yFor = [&] (float value) { return grid.getBottom() - (value - (float) info.minValue) / range * grid.getHeight(); };

    // grid + zero line
    g.setColour (pal::green.withAlpha (0.1f));
    for (int c = 0; c <= kMacroSteps; c += 4)
        g.fillRect (grid.getX() + c * colW, grid.getY(), 1.0f, grid.getHeight());
    const float baseValue = info.minValue < 0 ? 0.0f : (float) info.minValue;
    g.setColour (pal::green.withAlpha (0.35f));
    g.fillRect (grid.getX(), yFor (baseValue), grid.getWidth(), 1.0f);

    // steps: bright inside the length, dim after it
    for (int i = 0; i < kMacroSteps; ++i)
    {
        const bool inside = m.isOn() && i < m.length;
        const float value = (float) m.values[(size_t) i];
        const float x = grid.getX() + i * colW;
        const float yTop = juce::jmin (yFor (value), yFor (baseValue)), yBottom = juce::jmax (yFor (value), yFor (baseValue));
        g.setColour (colour.withAlpha (inside ? 0.35f : 0.08f));
        g.fillRect (x + 1.0f, yTop, colW - 2.0f, juce::jmax (1.0f, yBottom - yTop));
        g.setColour (colour.withAlpha (inside ? 1.0f : 0.25f));
        g.fillRect (x + 1.0f, yFor (value) - 1.5f, colW - 2.0f, 3.0f);
    }

    // loop strip
    const auto loop = loopArea();
    g.setColour (pal::dark);
    g.fillRect (loop);
    if (m.isOn() && m.loop >= 0)
    {
        g.setColour (colour.withAlpha (0.6f));
        g.fillRect (loop.getX() + m.loop * colW, loop.getY(), (m.length - m.loop) * colW, loop.getHeight());
    }
    g.setColour (pal::light);
    g.setFont (pixelFont (5.0f));
    g.drawText (m.isOn() && m.loop >= 0 ? "LOOP" : "CLICK HERE TO LOOP", loop, juce::Justification::centred);

    if (! m.isOn())
    {
        g.setColour (pal::light);
        g.setFont (pixelFont (6.0f));
        g.drawText (juce::String (info.name) + " IS OFF - DRAW OR PRESS ON", grid, juce::Justification::centred);
    }
}

void MacroEditor::drawAt (juce::Point<float> pos, bool connectToLast)
{
    const auto& info = kMacroLanes[lane];
    const auto grid = gridArea();
    const int step = stepAt (pos.x);
    const int value = juce::jlimit (info.minValue, info.maxValue,
                                    juce::roundToInt ((float) info.minValue
                                                      + (grid.getBottom() - pos.y) / grid.getHeight() * (float) (info.maxValue - info.minValue)));

    if (connectToLast)
        fillSkippedSteps (lastStep, lastValue, step, value, [this] (int s, int v) { proc.setMacroStep (lane, s, v); });
    proc.setMacroStep (lane, step, value);

    // Drawing past the end (or on a lane that's off) makes the lane long enough
    auto m = proc.getMacro (lane);
    if (m.length <= step)
    {
        m.length = step + 1;
        proc.setMacro (lane, m);
    }
    lastStep = step;
    lastValue = value;
    edited = true;
    refresh();
}

void MacroEditor::mouseDown (const juce::MouseEvent& e)
{
    edited = false;
    if (loopArea().expanded (0.0f, 3.0f).contains (e.position))
    {
        auto m = proc.getMacro (lane);
        const int step = stepAt (e.position.x);
        if (m.isOn() && step < m.length)
        {
            m.loop = m.loop == step ? -1 : step;
            proc.setMacro (lane, m);
            proc.macrosEdited();
            refresh();
        }
        return;
    }
    if (gridArea().expanded (2.0f).contains (e.position))
        drawAt (e.position, false);
}

void MacroEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (lastStep >= 0)
        drawAt (e.position, true);
}

void MacroEditor::mouseUp (const juce::MouseEvent&)
{
    if (edited)
        proc.macrosEdited();
    edited = false;
    lastStep = -1;
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
    level = 0.0f;
    for (size_t i = 0; i < samples.size(); ++i)
    {
        peak = juce::jmax (peak, std::abs (samples[i]));
        if (i >= samples.size() - 512)
            level = juce::jmax (level, std::abs (samples[i]));
    }

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
    g.setColour (pal::screen);
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
    if (getWidth() > 0)
        setKeyWidth ((float) getWidth() / 36.0f);   // 36 white keys between note 36 and 96
    juce::MidiKeyboardComponent::resized();
}

//==============================================================================
Mascot::Mascot()
{
    setTooltip ("BIT: your BitBuddy! Bounces when you play and dances with the JAM band. Click BIT to change BitBuddy's colours.");
    setRepaintsOnMouseActivity (false);
}

void Mascot::update (float outputLevel, bool isJamming, double bpm)
{
    level = juce::jmax (level * 0.85f, juce::jlimit (0.0f, 1.0f, outputLevel * 2.5f));
    jump *= 0.8f;
    jamming = isJamming;
    beat += bpm / 60.0 / 30.0;   // ~30 updates a second
    ++frame;

    // Only repaint when the picture actually changes (BIT sitting still costs nothing)
    const int look = juce::roundToInt (jump * 2.0f) + 16 * juce::roundToInt (level * 8.0f)
                   + 256 * ((frame % 100) < 4 ? 1 : 0) + 512 * ((int) (frame / 90) % 2)
                   + 1024 * (jamming ? 1 + (int) std::fmod (beat * 2.0, 4.0) : 0);
    if (look != lastLook)
    {
        lastLook = look;
        repaint();
    }
}

void Mascot::mouseDown (const juce::MouseEvent&)
{
    jump = 5.0f;
    if (onClick != nullptr)
        onClick();
}

void Mascot::paint (juce::Graphics& g)
{
    // A 16 x 16 pixel sprite, 3 screen pixels per sprite pixel
    const float u = juce::jmin ((float) getWidth(), (float) getHeight()) / 16.0f;
    const float lift = jump + (level > 0.02f ? level * 2.0f : 0.0f) + (jamming && std::fmod (beat, 1.0) < 0.5 ? 1.0f : 0.0f);
    auto px = [&] (float x, float y, float w, float h, juce::Colour c)
    {
        g.setColour (c);
        g.fillRect (x * u, (y - lift) * u + u, w * u, h * u);
    };

    const bool happy = level > 0.08f || jamming || jump > 0.5f;
    const bool blink = (frame % 100) < 4 && ! happy;
    const bool leftUp = jamming ? std::fmod (beat, 2.0) < 1.0 : happy;
    const bool rightUp = jamming ? ! leftUp : happy;

    // feet (they tap when dancing)
    const float tapL = jamming && std::fmod (beat, 1.0) < 0.5 ? -1.0f : 0.0f;
    px (4, 13 + tapL, 2, 2, pal::light);
    px (10, 13 - tapL - (jamming ? 1.0f : 0.0f), 2, 2, pal::light);

    // arms = the chip's pins
    px (0, leftUp ? 3.0f : 7.0f, 2, 1, pal::light);
    px (1, leftUp ? 4.0f : 8.0f, 1, 3, pal::light);
    px (14, rightUp ? 3.0f : 7.0f, 2, 1, pal::light);
    px (14, rightUp ? 4.0f : 8.0f, 1, 3, pal::light);

    // body: a little microchip with its notch
    px (2, 3, 12, 10, pal::bg);
    px (3, 4, 10, 8, pal::sky);
    px (7, 4, 2, 1, pal::bg);

    // eyes
    if (blink)
    {
        px (5, 7, 2, 1, pal::bg);
        px (9, 7, 2, 1, pal::bg);
    }
    else
    {
        px (5, 6, 2, 2, pal::white);
        px (9, 6, 2, 2, pal::white);
        const float look = happy ? 0.0f : (float) ((frame / 90) % 2);   // glances left and right now and then
        px (5 + look, 7, 1, 1, pal::bg);
        px (9 + look, 7, 1, 1, pal::bg);
    }

    // cheeks + mouth
    px (4, 9, 1, 1, pal::pink);
    px (11, 9, 1, 1, pal::pink);
    if (happy)
    {
        px (6, 9, 4, 2, pal::bg);
        px (7, 10, 2, 1, pal::red);
    }
    else
    {
        px (6, 10, 4, 1, pal::bg);
    }
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
        { "PLAY ALONG", "Pick a KEY and SCALE LOCK (bottom right): every key fits the song. CHORD + SMART = the right "
                        "chord per key. In the app, JAM starts a backing band and REC records you." },
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
      soundBox    ("sound",    "1", "PICK A SOUND",                 pal::yellow, 600, 168),
      screenBox   ("screen",   {},  "SCREEN",                       pal::green,  370, 168),
      shapeBox    ("shape",    "2", "SHAPE IT  (VOLUME ENVELOPE)",  pal::lime,   470, 150),
      chordsBox   ("chords",   "3", "MAGIC CHORDS  (ARPEGGIATOR)",  pal::cyan,   500, 150),
      movementBox ("movement", "4", "ADD MOVEMENT",                 pal::orange, 560, 172),
      crunchBox   ("crunch",   "5", "CRUNCH + ECHO + STEREO",       pal::sky,    410, 172),
      playBox     ("play",     {},  p.isStandaloneApp() ? "PLAY ALONG  (KEY, SCALE, JAM, REC)" : "PLAY ALONG  (KEY + SCALE)",
                   pal::pink, 490, 112),
      waveSelector (*state.getParameter (ids::wave), kNumWaves, waveButtons (pal::yellow), false, 3),
      scope (p.scope),
      waveDrawer (p),
      macroEditor (p),
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
      vibShapeButtons (*state.getParameter (ids::vibShape), 3,
                       textButtons ({ "SMOOTH", "TRILL", "RANDOM" },
                                    { "SMOOTH vibrato: a singer's wobble, up and down.",
                                      "TRILL: jumps between your note and the one above it, very fast - a classic chiptune trick. VIBRATO sets how far (1.0 = 2 semitones), VIB RATE how fast.",
                                      "RANDOM vibrato: wobbles to a new random height each time, like a broken old tape." },
                                    pal::orange)),
      sweepModeButtons (*state.getParameter (ids::sweepMode), 3,
                        textButtons ({ "SLIDE", "FOREVER", "BOUNCE" },
                                     { "SLIDE: each note starts SWEEP semitones away and slides to the right pitch.",
                                       "FOREVER: the pitch keeps falling (or rising, with - SWEEP) for as long as you hold the note, like the NES sweep unit. Falling bombs, power-downs! SWP TIME = how long each SWEEP step takes.",
                                       "BOUNCE: slides away and back, over and over - sirens and alarms." },
                                     pal::orange)),
      bits         (state, ids::bits,         "BITS",    pal::sky, "BITS: lowers the audio resolution. 16-bit = clean, 8-bit = classic, 4-bit and below = crunchy and gritty!"),
      rate         (state, ids::rate,         "RATE",    pal::sky, "RATE: lowers the sample rate for a grainy, crunchy old-hardware sound. Turn it all the way up ('Clean') to switch it off."),
      echoMix      (state, ids::echoMix,      "ECHO",    pal::sky, "ECHO: adds echoes that bounce between your left and right speakers. Chiptune composers used to fake this with a spare channel!"),
      echoFeedback (state, ids::echoFeedback, "REPEATS", pal::sky, "REPEATS: how many echoes you hear - low = one or two, high = lots."),
      stereoButtons (*state.getParameter (ids::stereo), 3,
                     textButtons ({ "MIDDLE", "WIDE", "SWING" },
                                  { "STEREO MIDDLE: the sound comes from straight in front of you.",
                                    "STEREO WIDE: every new note goes to the other side (left, right, left...), like a Game Boy in headphones. Drums get their own spots.",
                                    "STEREO SWING: the whole sound swings slowly from one speaker to the other, once a bar." },
                                  pal::sky)),
      volume       (state, ids::volume,       "VOLUME",  pal::sky, "VOLUME: the overall loudness of BitBuddy."),
      filterBox (p.apvts),
      gameBox (p),
      voiceBox (p),
      keyboard (p.keyboardState)
{
    // --- header ---
    mascot.onClick = [this] { showThemeMenu(); };
    addAndMakeVisible (mascot);

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

    shareButton.setTooltip ("SHARE: turn your sound into a SOUND CODE - a line of text you can send to a friend. "
                            "They PASTE it into their BitBuddy and get exactly your sound!");
    shareButton.setColour (juce::TextButton::buttonColourId, pal::teal);
    shareButton.onClick = [this] { showShareMenu(); };

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

    viewButton.setTooltip ("VIEW: show or hide BitBuddy's boxes, or reset the layout. Tip: drag any box by its "
                           "title bar to move it somewhere else!");
    viewButton.setColour (juce::TextButton::buttonColourId, pal::navy);
    viewButton.onClick = [this] { showViewMenu(); };

    for (auto* c : { (juce::Component*) &helpButton, (juce::Component*) &viewButton, (juce::Component*) &prevButton,
                     (juce::Component*) &nextButton, (juce::Component*) &saveButton, (juce::Component*) &mutateButton,
                     (juce::Component*) &surpriseButton, (juce::Component*) &presetBox, (juce::Component*) &presetTip,
                     (juce::Component*) &shareButton })
        addAndMakeVisible (c);

    // --- 1. pick a sound ---
    addAndMakeVisible (waveSelector);
    waveInfo.setFont (textFont (15.0f, true));
    waveInfo.setColour (juce::Label::textColourId, pal::yellow);
    waveInfo.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (waveInfo);
    addAndMakeVisible (scope);
    addChildComponent (waveDrawer);
    addChildComponent (macroEditor);

    // The SCREEN shows the live wave, your drawn wave, or the macro editor
    scopeViewButton.setTooltip ("SCOPE: watch the shape of the sound you're playing.");
    waveViewButton.setTooltip ("WAVE: draw your own wave (it's what the DRAW sound plays).");
    macroViewButton.setTooltip ("MACROS: draw how each note changes over time - volume steps, fast arps, pitch bends, "
                                "pulse width. Lights up when a macro is on.");
    const std::pair<juce::TextButton*, ScreenView> views[] = { { &scopeViewButton, ScreenView::Scope },
                                                               { &waveViewButton, ScreenView::Wave },
                                                               { &macroViewButton, ScreenView::Macros } };
    for (auto [button, view] : views)
    {
        button->setClickingTogglesState (false);
        button->setColour (juce::TextButton::buttonOnColourId, pal::green);
        button->onClick = [this, view = view] { setScreenView (view); };
        addAndMakeVisible (button);
    }
    setScreenView (ScreenView::Scope);
    setupCombo (consoleBox, consoleLabel, "CHIP", consoleNames(),
                "CHIP: make BitBuddy behave like a real console. NES: only 2 notes at once, 16 volume steps, NES tuning "
                "(high notes a bit off), on/off triangle. GAME BOY: 2 notes, 16 volume steps, softer top. C64: 3 notes - "
                "pair it with the C64 FILTER! MODERN: no limits.",
                pal::yellow);
    consoleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ids::console, consoleBox);

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
    addAndMakeVisible (vibShapeButtons);
    addAndMakeVisible (sweepModeButtons);

    // --- 5. crunch + echo ---
    for (auto* k : { &bits, &rate, &echoMix, &echoFeedback, &volume })
        addAndMakeVisible (k);
    addAndMakeVisible (stereoButtons);
    setupCombo (echoTimeBox, echoTimeLabel, "TIME", echoTimeNames(),
                "ECHO TIME: the gap between echoes, locked to your song's tempo. 1/8 D (dotted eighth) is a classic choice.",
                pal::sky);
    echoTimeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, ids::echoTime, echoTimeBox);


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

    // --- play along: RECORD (in the BitBuddy app; in a DAW you record with the DAW) ---
    recordButton.setTooltip ("REC: records everything you hear to a WAV file (send it to a friend!) and the notes you "
                             "play to a MIDI file. Press again to stop.");
    recordButton.setColour (juce::TextButton::buttonColourId, pal::red);
    recordButton.onClick = [this] { toggleRecording(); };
   #if JUCE_CONTENT_SHARING && (JUCE_IOS || JUCE_ANDROID)
    showRecordingButton.setButtonText ("SHARE");
    showRecordingButton.setTooltip ("SHARE: send your last recording (WAV + MIDI) to another app or a friend.");
   #else
    showRecordingButton.setTooltip ("SHOW: opens the folder with your recordings (Music / BitBuddy Recordings).");
   #endif
    showRecordingButton.setColour (juce::TextButton::buttonColourId, pal::teal);
    showRecordingButton.onClick = [this]
    {
        const auto wav = proc.recorder.getWavFile();
       #if JUCE_CONTENT_SHARING && (JUCE_IOS || JUCE_ANDROID)
        shareBox = juce::ContentSharer::shareFilesScoped ({ juce::URL (wav), juce::URL (proc.recorder.getMidiFile()) },
                                                          [] (bool, const juce::String&) {}, this);
       #else
        wav.revealToUser();
       #endif
    };
    recordStatus.setFont (pixelFont (7.0f));
    recordStatus.setJustificationType (juce::Justification::centred);
    recordStatus.setColour (juce::Label::textColourId, pal::light);
    appOnlyNote.setFont (textFont (13.0f, true));
    appOnlyNote.setColour (juce::Label::textColourId, pal::light);
    appOnlyNote.setText ("JAM + RECORD live in the BitBuddy app. In your DAW, record with the DAW itself!",
                         juce::dontSendNotification);

    // --- play along: JAM band ---
    jamButton.setTooltip ("JAM: starts a little chiptune band - drums, bass and chords - in your KEY and SCALE. "
                          "Make up a melody on top! (With SCALE LOCK on, every note fits.)");
    jamButton.setColour (juce::TextButton::buttonColourId, pal::green);
    jamButton.onClick = [this] { proc.setJamOn (! proc.isJamOn()); updateJamControls(); };
    for (int i = 0; i < kNumJamStyles; ++i)
        jamStyleBox.addItem (juce::String (jamStyleName (i)).toUpperCase(), i + 1);
    jamStyleBox.setTooltip ("STYLE: what kind of song the band plays. ADVENTURE = happy hero theme, DUNGEON = dark and "
                            "slow, RACING = fast and pumping, CHILL = relaxed.");
    jamStyleBox.setColour (juce::ComboBox::outlineColourId, pal::pink);
    jamStyleBox.onChange = [this]
    {
        if (jamStyleBox.getSelectedId() > 0)
            proc.setJamStyle (jamStyleBox.getSelectedId() - 1);
        updateJamControls();
    };
    tempoDownButton.setTooltip ("TEMPO -: the band plays slower.");
    tempoUpButton.setTooltip ("TEMPO +: the band plays faster.");
    tempoDownButton.onClick = [this] { proc.setJamTempo (proc.getJamTempo() - 4.0f); updateJamControls(); };
    tempoUpButton.onClick   = [this] { proc.setJamTempo (proc.getJamTempo() + 4.0f); updateJamControls(); };
    tempoLabel.setFont (pixelFont (7.0f));
    tempoLabel.setJustificationType (juce::Justification::centred);
    tempoLabel.setColour (juce::Label::textColourId, pal::pink);
    tempoLabel.setTooltip ("TEMPO: how fast the band plays, in beats per minute.");

    if (proc.isStandaloneApp())
    {
        for (auto* c : { (juce::Component*) &jamButton, (juce::Component*) &jamStyleBox, (juce::Component*) &tempoDownButton,
                         (juce::Component*) &tempoLabel, (juce::Component*) &tempoUpButton })
            addAndMakeVisible (c);
        updateJamControls();

        addAndMakeVisible (recordButton);
        addAndMakeVisible (recordStatus);
        addChildComponent (showRecordingButton);
    }
    else
    {
        addAndMakeVisible (appOnlyNote);
    }

    // --- put every control into its box (positions are inside the box) ---
    auto into = [] (juce::Component& box, std::initializer_list<juce::Component*> parts)
    {
        for (auto* c : parts)
            box.addChildComponent (c);   // moves it into the box, keeping it shown or hidden as it was
    };
    into (soundBox,    { &waveSelector, &waveInfo, &consoleLabel, &consoleBox });
    into (screenBox,   { &scope, &waveDrawer, &macroEditor, &scopeViewButton, &waveViewButton, &macroViewButton });
    into (shapeBox,    { &attack, &decay, &sustain, &release, &envelopeView });
    into (chordsBox,   { &arpModeButtons, &chordLabel, &chordBox, &patternLabel, &patternBox, &speedLabel, &speedBox,
                         &arpTip, &octaves });
    into (movementBox, { &vibDepth, &vibRate, &vibDelay, &sweepAmt, &sweepTime, &pwm, &glide, &voiceModeButtons,
                         &vibShapeButtons, &sweepModeButtons });
    into (crunchBox,   { &bits, &rate, &echoMix, &echoTimeLabel, &echoTimeBox, &echoFeedback, &volume, &stereoButtons });
    into (playBox,     { &keyLabel, &keyBox, &scaleLabel, &scaleBox, &scaleTip, &jamButton, &jamStyleBox, &tempoDownButton,
                         &tempoLabel, &tempoUpButton, &recordButton, &recordStatus, &showRecordingButton, &appOnlyNote });

    soundBox.layout = [this]
    {
        waveSelector.setBounds (7, 26, 586, 86);
        waveInfo.setBounds (8, 116, 584, 48);
        consoleLabel.setBounds (392, 7, 36, 12);
        consoleBox.setBounds (428, 3, 112, 20);
    };
    screenBox.layout = [this]
    {
        const juce::Rectangle<int> view (10, 26, screenBox.getWidth() - 20, screenBox.getHeight() - 36);
        scope.setBounds (view);
        waveDrawer.setBounds (view);
        macroEditor.setBounds (view);
        const int right = screenBox.getWidth() - 32;
        macroViewButton.setBounds (right - 78, 3, 78, 18);
        waveViewButton.setBounds (right - 128, 3, 48, 18);
        scopeViewButton.setBounds (right - 184, 3, 54, 18);
    };
    shapeBox.layout = [this]
    {
        int x = 10;
        for (auto* k : { &attack, &decay, &sustain, &release })
        {
            k->setBounds (x, 24, 66, 118);
            x += 68;
        }
        envelopeView.setBounds (286, 28, 174, 110);
    };
    chordsBox.layout = [this]
    {
        arpModeButtons.setBounds (8, 26, 404, 30);
        chordLabel.setBounds (10, 62, 140, 12);
        chordBox.setBounds (10, 76, 140, 26);
        patternLabel.setBounds (160, 62, 120, 12);
        patternBox.setBounds (160, 76, 120, 26);
        speedLabel.setBounds (290, 62, 120, 12);
        speedBox.setBounds (290, 76, 120, 26);
        arpTip.setBounds (8, 108, 404, 36);
        octaves.setBounds (424, 24, 66, 118);
    };
    movementBox.layout = [this]
    {
        int x = 8;
        for (auto* k : { &vibDepth, &vibRate, &vibDelay, &sweepAmt, &sweepTime, &pwm, &glide })
        {
            k->setBounds (x, 24, 66, 118);
            x += 66;
        }
        voiceModeButtons.setBounds (478, 32, 72, 88);
        vibShapeButtons.setBounds (10, 144, 194, 22);    // under VIBRATO, VIB RATE, VIB WAIT
        sweepModeButtons.setBounds (208, 144, 194, 22);  // under SWEEP, SWP TIME (+ PWM)
    };
    crunchBox.layout = [this]
    {
        bits.setBounds (8, 24, 62, 118);
        rate.setBounds (70, 24, 62, 118);
        echoMix.setBounds (132, 24, 62, 118);
        echoTimeLabel.setBounds (198, 32, 76, 12);
        echoTimeBox.setBounds (198, 48, 76, 26);
        echoFeedback.setBounds (278, 24, 62, 118);
        volume.setBounds (342, 24, 62, 118);
        stereoButtons.setBounds (10, 144, 192, 22);
    };
    playBox.layout = [this]
    {
        keyLabel.setBounds (10, 28, 70, 12);
        keyBox.setBounds (10, 42, 70, 26);
        scaleLabel.setBounds (90, 28, 140, 12);
        scaleBox.setBounds (90, 42, 140, 26);
        scaleTip.setBounds (240, 26, 244, 46);
        jamButton.setBounds (10, 78, 62, 26);
        jamStyleBox.setBounds (76, 80, 100, 22);
        tempoDownButton.setBounds (180, 78, 18, 26);
        tempoLabel.setBounds (198, 78, 58, 26);
        tempoUpButton.setBounds (256, 78, 18, 26);
        recordButton.setBounds (280, 78, 64, 26);
        recordStatus.setBounds (346, 78, 74, 26);
        showRecordingButton.setBounds (422, 78, 60, 26);
        appOnlyNote.setBounds (10, 76, 472, 30);
    };

    using Section = BitBuddyProcessor::Section;
    soundBox.setDice    ([this] { proc.rollSection (Section::Sound); });
    shapeBox.setDice    ([this] { proc.rollSection (Section::Shape); });
    chordsBox.setDice   ([this] { proc.rollSection (Section::Chords); });
    movementBox.setDice ([this] { proc.rollSection (Section::Movement); });
    crunchBox.setDice   ([this] { proc.rollSection (Section::Crunch); });
    filterBox.setDice   ([this] { proc.rollSection (Section::Filter); });

    // Boxes can be hidden (their "x") and moved (drag the title bar)
    allBoxes = { &soundBox, &screenBox, &shapeBox, &chordsBox, &movementBox, &crunchBox, &filterBox, &playBox, &gameBox, &voiceBox };
    for (auto* box : allBoxes)
    {
        box->onHide = [this] (Box& b) { setBoxVisible (b.getId(), false); };
        box->onDragStart = [this] (Box& b, const juce::MouseEvent&)
        {
            if (boxDraggingEnabled)
            {
                b.toFront (false);
                b.setAlpha (0.8f);
            }
        };
        box->onDragMove = [this] (Box& b, const juce::MouseEvent& e)
        {
            if (boxDraggingEnabled)
                dragBoxTo (b, e.getEventRelativeTo (this).getPosition());
        };
        box->onDragEnd = [this] (Box& b, const juce::MouseEvent&)
        {
            if (b.getAlpha() < 1.0f)
            {
                b.setAlpha (1.0f);
                saveLayout();
            }
        };
        addAndMakeVisible (box);
    }
    loadLayout();
    diceShown = loadSetting ("showDice", "1") != "0";   // VIEW > Show dice buttons
    for (auto* box : allBoxes)
        box->setDiceShown (diceShown);
    gameBox.onMessage = voiceBox.onMessage = [this] (const juce::String& text) { showHint (text); };

    // --- bottom ---
    hintLabel.setFont (textFont (15.0f));
    hintLabel.setColour (juce::Label::textColourId, pal::white);
    hintLabel.setText (kDefaultHint, juce::dontSendNotification);
    hintLabel.setMinimumHorizontalScale (0.8f);
    bottomStrip.addAndMakeVisible (hintLabel);

    keysStatus.setFont (pixelFont (7.0f));
    keysStatus.setJustificationType (juce::Justification::centred);
    keysStatus.setTooltip ("COMPUTER KEYBOARD: when this says KEYS ON, your letter keys play notes "
                           "(A W S E D F T G Y H U J K = one octave, Z / X = octave down / up). "
                           "If it says CLICK FOR KEYS, click anywhere in BitBuddy.");
   #if ! (JUCE_IOS || JUCE_ANDROID)
    bottomStrip.addAndMakeVisible (keysStatus);   // phones have no computer keyboard to report on
   #endif
    bottomStrip.addAndMakeVisible (keyboard);
    addAndMakeVisible (bottomStrip);
    addChildComponent (help);

    addMouseListener (this, true);
    setLookAndFeel (&lookAndFeel);   // (re)creates slider / combo text boxes...
    stopFocusStealing (*this);       // ...so do this afterwards
    setSize (baseWidth, fullHeight);
    layoutBoxes();
    setSize (baseWidth, designHeight);
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
}

//==============================================================================
// The help bar + keyboard: under the boxes, or pinned to the bottom of a phone screen
void BottomStrip::paint (juce::Graphics& g)
{
    g.fillAll (pal::bg);
    const auto hintArea = getHintArea();
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

void BottomStrip::resized()
{
    const auto hintArea = getHintArea();
    hint.setBounds (hintArea.reduced (2).withTrimmedLeft (26).withTrimmedRight (176));
    keysStatus.setBounds (hintArea.getRight() - 176, hintArea.getY() + 2, 172, hintArea.getHeight() - 4);
    keyboard.setBounds (10, 34, 980, 70);
}

//==============================================================================
// Phone layout (set by the editor): the strip lives outside the scrolling panel
void MainPanel::setBottomStripDetached (bool shouldBeDetached)
{
    if (stripDetached == shouldBeDetached)
        return;
    stripDetached = shouldBeDetached;
    if (! stripDetached)
        addAndMakeVisible (bottomStrip);   // back under the boxes
    help.toFront (false);
    layoutBoxes();
}

void MainPanel::resized()
{
    helpButton.setBounds (240, 12, 66, 30);
    viewButton.setBounds (310, 12, 54, 30);
    mascot.setBounds (366, 4, 46, 52);
    prevButton.setBounds (414, 12, 28, 30);
    presetBox.setBounds (446, 12, 180, 28);
    nextButton.setBounds (630, 12, 28, 30);
    shareButton.setBounds (662, 12, 64, 30);
    saveButton.setBounds (730, 12, 56, 30);
    mutateButton.setBounds (790, 12, 72, 30);
    surpriseButton.setBounds (866, 12, 124, 30);
    presetTip.setBounds (414, 42, 576, 18);

    layoutBoxes();
}

//==============================================================================
// Layout: the boxes flow into rows (in your order), and each row is stretched
// to the full width so the edges stay tidy. Hidden boxes take no space.
namespace
{
juce::StringArray defaultBoxOrder()
{
    return { "sound", "screen", "shape", "chords", "movement", "crunch", "filter", "play", "game", "voice" };
}
} // namespace

void MainPanel::layoutBoxes()
{
    constexpr int left = 10, right = 990, gap = 10, rowGap = 6;
    int y = 64, rowWidth = 0;
    std::vector<Box*> row;

    auto placeRow = [&]
    {
        if (row.empty())
            return;
        int natural = 0, rowHeight = 0;
        for (auto* b : row)
        {
            natural += b->getNaturalWidth();
            rowHeight = juce::jmax (rowHeight, b->getNaturalHeight());
        }
        const int space = (right - left) - gap * ((int) row.size() - 1);
        int x = left;
        for (size_t i = 0; i < row.size(); ++i)
        {
            const int w = i + 1 == row.size() ? right - x
                                              : juce::roundToInt ((double) row[i]->getNaturalWidth() * space / natural);
            row[i]->setBounds (x, y, w, rowHeight);
            x += w + gap;
        }
        y += rowHeight + rowGap;
        row.clear();
        rowWidth = 0;
    };

    for (const auto& id : boxOrder)
    {
        auto* box = findBox (id);
        if (box == nullptr)
            continue;
        const bool show = ! hiddenBoxes.contains (id);
        box->setVisible (show);
        if (! show)
            continue;
        if (! row.empty() && rowWidth + gap + box->getNaturalWidth() > right - left)
            placeRow();
        rowWidth += (row.empty() ? 0 : gap) + box->getNaturalWidth();
        row.push_back (box);
    }
    placeRow();

    // The help bar + keyboard sit under the boxes (unless a phone screen pins them to the bottom)
    if (stripDetached)
        bottomStrip.setBounds (0, 0, baseWidth, BottomStrip::designHeight);   // the editor positions it (transform)
    else
        bottomStrip.setBounds (0, y, baseWidth, BottomStrip::designHeight);
    const int newHeight = stripDetached ? y + 4 : y + BottomStrip::designHeight;
    help.setBounds (0, 0, baseWidth, newHeight);
    repaint();

    if (newHeight != designHeight)
    {
        designHeight = newHeight;
        if (onDesignHeightChanged != nullptr)
            onDesignHeightChanged();
    }
}

Box* MainPanel::findBox (const juce::String& id) const
{
    for (auto* b : allBoxes)
        if (b->getId() == id)
            return b;
    return nullptr;
}

void MainPanel::loadLayout()
{
    const auto known = defaultBoxOrder();
    boxOrder.clear();
    for (const auto& id : juce::StringArray::fromTokens (loadSetting ("boxOrder", {}), ",", {}))
        if (known.contains (id) && ! boxOrder.contains (id))
            boxOrder.add (id);
    for (const auto& id : known)   // boxes from newer versions go at the end
        if (! boxOrder.contains (id))
            boxOrder.add (id);

    hiddenBoxes.clear();
    for (const auto& id : juce::StringArray::fromTokens (loadSetting ("boxHidden", {}), ",", {}))
        if (known.contains (id))
            hiddenBoxes.addIfNotAlreadyThere (id);
}

void MainPanel::setDiceShown (bool shouldShow)
{
    diceShown = shouldShow;
    for (auto* box : allBoxes)
        box->setDiceShown (shouldShow);
    saveSetting ("showDice", shouldShow ? "1" : "0");
}

void MainPanel::saveLayout() const
{
    saveSetting ("boxOrder", boxOrder.joinIntoString (","));
    saveSetting ("boxHidden", hiddenBoxes.joinIntoString (","));
}

void MainPanel::setBoxVisible (const juce::String& id, bool shouldBeVisible)
{
    if (shouldBeVisible)
        hiddenBoxes.removeString (id);
    else
        hiddenBoxes.addIfNotAlreadyThere (id);
    layoutBoxes();
    saveLayout();
}

void MainPanel::moveBox (const juce::String& id, int newIndex)
{
    if (! boxOrder.contains (id))
        return;
    boxOrder.removeString (id);
    boxOrder.insert (juce::jlimit (0, boxOrder.size(), newIndex), id);
    layoutBoxes();
    saveLayout();
}

void MainPanel::resetLayout()
{
    boxOrder = defaultBoxOrder();
    hiddenBoxes.clear();
    layoutBoxes();
    saveLayout();
}

// While you drag a box by its title bar: it swaps into the slot under the mouse
void MainPanel::dragBoxTo (Box& dragged, juce::Point<int> pos)
{
    for (auto* other : allBoxes)
    {
        if (other == &dragged || ! other->isVisible() || ! other->getBounds().contains (pos))
            continue;

        juce::StringArray newOrder (boxOrder);
        newOrder.removeString (dragged.getId());
        const bool after = pos.x > other->getBounds().getCentreX();
        newOrder.insert (newOrder.indexOf (other->getId()) + (after ? 1 : 0), dragged.getId());
        if (newOrder != boxOrder)
        {
            boxOrder = newOrder;
            layoutBoxes();
        }
        return;
    }
}

// A hidden box still does its job. The VIEW menu warns when one is changing the sound.
bool MainPanel::isBoxChangingSound (const juce::String& id) const
{
    auto value = [this] (const char* paramId) { return state.getRawParameterValue (paramId)->load(); };

    if (id == "chords")   return juce::roundToInt (value (ids::arpMode)) != 0;
    if (id == "movement") return value (ids::vibDepth) > 0.0f || std::abs (value (ids::sweepAmt)) > 0.5f || value (ids::pwm) > 0.0f
                              || value (ids::glide) >= 0.001f || juce::roundToInt (value (ids::voiceMode)) == 1;
    if (id == "crunch")   return value (ids::echoMix) > 0.0f || value (ids::rate) < 44000.0f || value (ids::bits) <= 6.0f
                              || juce::roundToInt (value (ids::stereo)) != 0;
    if (id == "filter")   return juce::roundToInt (value (ids::filterType)) != 0;
    if (id == "play")     return juce::roundToInt (value (ids::scale)) != 0 || proc.isJamOn();
    if (id == "screen")   return proc.anyMacroOn();   // macros live on the SCREEN
    return false;
}

void MainPanel::showViewMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("SHOW / HIDE BOXES");
    for (int i = 0; i < (int) allBoxes.size(); ++i)
    {
        const auto& id = allBoxes[(size_t) i]->getId();
        const bool shown = isBoxVisible (id);
        auto name = allBoxes[(size_t) i]->getTitle();
        if (! shown && isBoxChangingSound (id))
            name << "   (hidden - still ON!)";
        menu.addItem (i + 1, name, true, shown);
    }
    menu.addSeparator();
    menu.addItem (100, "SHOW ALL BOXES", ! hiddenBoxes.isEmpty());
    menu.addItem (101, "RESET LAYOUT (ORDER + ALL BOXES)");
    menu.addSeparator();
    menu.addItem (103, "SHOW DICE BUTTONS (RE-ROLL ONE BOX)", true, diceShown);
    menu.addSeparator();
    menu.addItem (102, "TIP: DRAG A BOX BY ITS TITLE TO MOVE IT", false);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&viewButton),
                        [safe = juce::Component::SafePointer<MainPanel> (this)] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            if (result == 100)
                            {
                                safe->hiddenBoxes.clear();
                                safe->layoutBoxes();
                                safe->saveLayout();
                            }
                            else if (result == 101)
                                safe->resetLayout();
                            else if (result == 103)
                                safe->setDiceShown (! safe->diceShown);
                            else if (juce::isPositiveAndBelow (result - 1, (int) safe->allBoxes.size()))
                            {
                                const auto& id = safe->allBoxes[(size_t) result - 1]->getId();
                                safe->setBoxVisible (id, ! safe->isBoxVisible (id));
                            }
                        });
}

void MainPanel::timerCallback()
{
    scope.refresh();
    envelopeView.refresh();
    filterBox.refresh();

    // BIT bounces to the music and dances with the band
    mascot.update (scope.getLevel(), proc.isJamOn(), proc.getJamTempo());

    const int wave = choiceOf (ids::wave, kNumWaves - 1);
    if (wave != lastWave)
    {
        lastWave = wave;
        waveInfo.setText (kWaveInfo[wave], juce::dontSendNotification);
        keyboard.setDrumMode (wave == (int) Wave::Drums);

        // Picking DRAW opens the drawing board; leaving it goes back to the scope
        if (wave == (int) Wave::Custom)
            setScreenView (ScreenView::Wave);
        else if (screenView == ScreenView::Wave)
            setScreenView (ScreenView::Scope);
    }
    if (waveDrawer.isVisible())
        waveDrawer.refresh();
    if (macroEditor.isVisible())
        macroEditor.refresh();

    if (const bool anyMacro = proc.anyMacroOn(); anyMacro != lastAnyMacro)
    {
        lastAnyMacro = anyMacro;
        macroViewButton.setButtonText (anyMacro ? "MACROS ON" : "MACROS");
        macroViewButton.setColour (juce::TextButton::buttonColourId, anyMacro ? pal::lime.withAlpha (0.45f) : pal::dark);
    }

    const int arpMode = choiceOf (ids::arpMode, 2);
    if (arpMode != lastArpMode)
    {
        lastArpMode = arpMode;
        arpTip.setText (kArpTips[arpMode], juce::dontSendNotification);
    }

    syncPresetBox();
    updateScaleTip();
    if (proc.isStandaloneApp())
        updateRecordStatus();

    // Whenever BitBuddy's own window is the active one (and no menu is open),
    // keep the letter keys on the piano. This also recovers focus after a menu
    // closes. It never runs while your DAW has focus, so DAW shortcuts keep working.
    if (windowIsActive())
        focusKeyboard();

    updateKeysStatus();
}

void MainPanel::showThemeMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("COLOUR THEME");
    for (int i = 0; i < kNumThemes; ++i)
        menu.addItem (i + 1, juce::String (themeName (i)).toUpperCase(), true, i == currentTheme());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&mascot), [] (int result)
    {
        if (result <= 0 || result - 1 == currentTheme())
            return;
        applyTheme (result - 1);
        saveThemeSetting (result - 1);
        juce::SharedResourcePointer<ThemeEvents>()->sendChangeMessage();   // every open BitBuddy repaints
    });
}

void MainPanel::updateJamControls()
{
    const bool on = proc.isJamOn();
    jamButton.setButtonText (on ? "STOP" : "JAM");
    jamButton.setColour (juce::TextButton::buttonColourId, on ? pal::orange : pal::green);
    jamStyleBox.setSelectedId (proc.getJamStyle() + 1, juce::dontSendNotification);
    tempoLabel.setText (juce::String (juce::roundToInt (proc.getJamTempo())) + " BPM", juce::dontSendNotification);
}

void MainPanel::toggleRecording()
{
    if (proc.recorder.isRecording())
        proc.recorder.stop();
    else if (! proc.recorder.start (proc.getSampleRate()))
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withTitle ("COULDN'T RECORD")
                                          .withMessage ("BitBuddy couldn't create a file in " + Recorder::getRecordingsFolder().getFullPathName())
                                          .withButton ("OK")
                                          .withAssociatedComponent (this),
                                      nullptr);
    updateRecordStatus();
}

void MainPanel::updateRecordStatus()
{
    const bool recording = proc.recorder.isRecording();
    if (recording)
    {
        const int seconds = (int) proc.recorder.getSecondsRecorded();
        recordButton.setButtonText ("STOP");
        recordStatus.setText (juce::String::formatted ("REC %d:%02d", seconds / 60, seconds % 60), juce::dontSendNotification);
        recordStatus.setColour (juce::Label::textColourId, (juce::Time::getMillisecondCounter() / 500) % 2 == 0 ? pal::red.brighter (0.5f)
                                                                                                                 : pal::white);
        showRecordingButton.setVisible (false);
    }
    else if (wasRecording)
    {
        recordButton.setButtonText ("REC");
        recordStatus.setText ("SAVED!", juce::dontSendNotification);
        recordStatus.setColour (juce::Label::textColourId, pal::lime);
        showRecordingButton.setVisible (true);
    }
    wasRecording = recording;
}

void MainPanel::setScreenView (ScreenView view)
{
    screenView = view;
    scope.setVisible (view == ScreenView::Scope);
    waveDrawer.setVisible (view == ScreenView::Wave);
    macroEditor.setVisible (view == ScreenView::Macros);
    scopeViewButton.setToggleState (view == ScreenView::Scope, juce::dontSendNotification);
    waveViewButton.setToggleState (view == ScreenView::Wave, juce::dontSendNotification);
    macroViewButton.setToggleState (view == ScreenView::Macros, juce::dontSendNotification);
    if (view == ScreenView::Macros)
        macroEditor.refresh();
}

int MainPanel::choiceOf (const char* paramId, int maxIndex) const
{
    return juce::jlimit (0, maxIndex, juce::roundToInt (state.getRawParameterValue (paramId)->load()));
}

void MainPanel::updateScaleTip()
{
    const int key = choiceOf (ids::scaleKey, 11), scale = choiceOf (ids::scale, kNumScales - 1);
    const bool smart = choiceOf (ids::arpChord, kSmartChordIndex) == kSmartChordIndex;
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

void MainPanel::showHint (const juce::String& text)
{
    hintLabel.setText (text, juce::dontSendNotification);
}

void MainPanel::showShareMenu()
{
    enum { copyCode = 1, sendCode, pasteCode };
    juce::PopupMenu menu;
    menu.addSectionHeader ("SOUND CODES");
    menu.addItem (copyCode, "Copy my sound code");
   #if JUCE_CONTENT_SHARING && (JUCE_IOS || JUCE_ANDROID)
    menu.addItem (sendCode, "Send my sound to a friend...");
   #endif
    menu.addItem (pasteCode, "Paste a sound code");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (shareButton),
                        [safe = juce::Component::SafePointer<MainPanel> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& self = *safe;
                            const auto code = self.proc.getSoundCode();
                            if (result == copyCode)
                            {
                                juce::SystemClipboard::copyTextToClipboard (code);
                                self.showHint ("Copied! Paste it into a message to a friend:  " + code);
                            }
                            else if (result == sendCode)
                            {
                               #if JUCE_CONTENT_SHARING && (JUCE_IOS || JUCE_ANDROID)
                                self.shareTextBox = juce::ContentSharer::shareTextScoped (
                                    "My BitBuddy sound \"" + self.proc.getDisplayPresetName().trimCharactersAtEnd (" *")
                                        + "\" - open BitBuddy, press SHARE > Paste a sound code:\n" + code,
                                    [] (bool, const juce::String&) {}, &self);
                               #endif
                            }
                            else if (result == pasteCode)
                            {
                                // A code on the clipboard loads straight away; otherwise ask for one
                                if (self.proc.loadSoundCode (juce::SystemClipboard::getTextFromClipboard()))
                                    self.showHint ("Loaded \"" + self.proc.getDisplayPresetName() + "\" from the code!");
                                else
                                    self.askForSoundCode();
                            }
                        });
}

void MainPanel::askForSoundCode()
{
    auto* w = new juce::AlertWindow ("PASTE A SOUND CODE", "Paste the code your friend sent you (it starts with BB1-).",
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("code", {}, "Code:");
    w->addButton ("LOAD", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    addAndMakeVisible (w);
    w->setCentrePosition (getLocalBounds().getCentre());
    w->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<MainPanel> (this), w] (int result)
        {
            if (safe == nullptr || result != 1)
                return;
            if (safe->proc.loadSoundCode (w->getTextEditorContents ("code")))
                safe->showHint ("Loaded \"" + safe->proc.getDisplayPresetName() + "\" from the code!");
            else
                safe->showHint ("That doesn't look like a BitBuddy sound code (they start with BB1-). Check it was copied whole.");
        }), true);
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
    : AudioProcessorEditor (&p), bitBuddy (p)
{
    if (! bb::themeChosen())
        bb::applyTheme (bb::loadThemeSetting());   // the first BitBuddy window opened: use your saved theme
    lnf.applyPaletteColours();
    themeEvents->addChangeListener (this);

    // Phone layout: a scrolling page (swipe to scroll; knobs and drawings still drag normally)
    viewport.setViewedComponent (&scrollContent, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::nonHover);
    viewport.setScrollBarsShown (true, false);
    viewport.setWantsKeyboardFocus (false);
    viewport.getVerticalScrollBar().setWantsKeyboardFocus (false);
    viewport.getHorizontalScrollBar().setWantsKeyboardFocus (false);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, bb::pal::light);
    addChildComponent (viewport);

    createPanel();

   #if JUCE_IOS || JUCE_ANDROID
    // Phones and tablets: the screen decides the size - never ask for a bigger window
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        setSize (display->userArea.getWidth(), display->userArea.getHeight());
    else
        setSize (bb::MainPanel::baseWidth, bb::MainPanel::fullHeight);
   #else
    setResizable (true, true);
    fitWindowToPanel (bb::MainPanel::baseWidth);
   #endif
}

void BitBuddyEditor::createPanel()
{
    phoneLayout = false;
    panel = std::make_unique<bb::MainPanel> (bitBuddy, lnf);
    panel->onDesignHeightChanged = [this]
    {
        // Boxes were hidden / shown / moved: reshape the window (or, in the phone layout,
        // just the scrolling page) once this layout pass is done
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<BitBuddyEditor> (this)]
        {
            if (safe == nullptr)
                return;
            if (safe->phoneLayout)
                safe->resized();
            else
                safe->fitWindowToPanel (safe->getWidth());
        });
    };
    addAndMakeVisible (*panel);
}

void BitBuddyEditor::fitWindowToPanel (int width)
{
   #if JUCE_IOS || JUCE_ANDROID
    juce::ignoreUnused (width);   // the screen size is fixed: just re-fit the content
   #else
    // Computer: the window keeps the shape of everything you show (help bar + keyboard included)
    const double ratio = (double) bb::MainPanel::baseWidth / (double) panel->getFullDesignHeight();
    setResizeLimits (600, juce::roundToInt (600.0 / ratio), 2000, juce::roundToInt (2000.0 / ratio));
    if (auto* sizeLimits = getConstrainer())
        sizeLimits->setFixedAspectRatio (ratio);

    width = juce::jlimit (600, 2000, width);
    setSize (width, juce::roundToInt (width / ratio));
   #endif
    resized();
}

BitBuddyEditor::~BitBuddyEditor()
{
    themeEvents->removeChangeListener (this);
    panel.reset();
}

void BitBuddyEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    lnf.applyPaletteColours();
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, bb::pal::light);
    panel.reset();
    createPanel();
    fitWindowToPanel (getWidth());
    repaint();
}

void BitBuddyEditor::paint (juce::Graphics& g)
{
    g.fillAll (bb::pal::bg);
}

void BitBuddyEditor::setPhoneLayout (bool shouldUsePhoneLayout)
{
    if (shouldUsePhoneLayout == phoneLayout)
        return;
    phoneLayout = shouldUsePhoneLayout;

    auto& strip = panel->getBottomStrip();
    if (phoneLayout)
    {
        scrollContent.addAndMakeVisible (*panel);
        panel->setBottomStripDetached (true);
        addAndMakeVisible (strip);       // pinned to the bottom of the screen
        viewport.setVisible (true);
        panel->setBoxDraggingEnabled (false);
    }
    else
    {
        viewport.setVisible (false);
        strip.setTransform ({});
        panel->setBottomStripDetached (false);   // back under the boxes
        addAndMakeVisible (*panel);
        panel->setBoxDraggingEnabled (true);
    }
}

void BitBuddyEditor::resized()
{
    if (panel == nullptr || getWidth() <= 0 || getHeight() <= 0)
        return;

    const float width = (float) getWidth(), height = (float) getHeight();
    const float designWidth = (float) bb::MainPanel::baseWidth;

    // A short, wide screen (a phone held sideways) would shrink everything to a few pixels:
    // fill the width instead, pin the keyboard to the bottom and scroll the rest
    setPhoneLayout ((float) panel->getFullDesignHeight() * (width / designWidth) > height * 1.08f);

    if (! phoneLayout)
    {
        // Fit the design (1000 units wide, as tall as your boxes need) inside the window, centred
        const float h = (float) panel->getDesignHeight();
        const float scale = juce::jmin (width / designWidth, height / h);
        panel->setTransform (juce::AffineTransform::scale (scale)
                                 .translated ((width - designWidth * scale) * 0.5f, (height - h * scale) * 0.5f));
        panel->setBounds (0, 0, bb::MainPanel::baseWidth, panel->getDesignHeight());
        return;
    }

    auto& strip = panel->getBottomStrip();
    const float stripScale = width / designWidth;
    const int stripHeight = juce::roundToInt ((float) bb::BottomStrip::designHeight * stripScale);
    strip.setTransform (juce::AffineTransform::scale (stripScale).translated (0.0f, height - (float) stripHeight));

    const int scrollBar = juce::jmax (10, juce::roundToInt (16.0f * stripScale));
    const float scale = (width - (float) scrollBar) / designWidth;
    viewport.setScrollBarThickness (scrollBar);
    viewport.setBounds (0, 0, getWidth(), getHeight() - stripHeight);
    panel->setTransform (juce::AffineTransform::scale (scale));
    panel->setBounds (0, 0, bb::MainPanel::baseWidth, panel->getDesignHeight());
    scrollContent.setSize (getWidth() - scrollBar, juce::roundToInt ((float) panel->getDesignHeight() * scale));
}
