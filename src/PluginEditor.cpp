#include "PluginEditor.h"
#include "Parameters.h"
#include "Presets.h"

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
ChoiceButtons::ChoiceButtons (juce::RangedAudioParameter& param, const juce::StringArray& labels, juce::Colour accent,
                              const juce::StringArray& hints, bool isVertical)
    : vertical (isVertical),
      attachment (param, [this] (float v) { update (v); })
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->setColour (juce::TextButton::buttonOnColourId, accent);
        b->setTooltip (hints[i]);
        b->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }

    attachment.sendInitialUpdate();
}

void ChoiceButtons::update (float value)
{
    const int idx = juce::roundToInt (value);
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
}

void ChoiceButtons::resized()
{
    auto r = getLocalBounds();
    const int n = buttons.size();

    for (int i = 0; i < n; ++i)
    {
        if (vertical)
            buttons[i]->setBounds (r.removeFromTop (r.getHeight() / (n - i)).reduced (0, 2));
        else
            buttons[i]->setBounds (r.removeFromLeft (r.getWidth() / (n - i)).reduced (2, 0));
    }
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

WaveSelector::WaveSelector (juce::RangedAudioParameter& param, juce::Colour accent)
    : attachment (param, [this] (float v) { update (v); })
{
    for (int i = 0; i < 8; ++i)
    {
        auto* b = buttons.add (new WaveButton (i, kWaveShort[i], accent));
        b->setTooltip (kWaveInfo[i]);
        b->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }

    attachment.sendInitialUpdate();
}

void WaveSelector::update (float value)
{
    const int idx = juce::roundToInt (value);
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
}

void WaveSelector::resized()
{
    auto r = getLocalBounds();
    const int n = buttons.size();
    for (int i = 0; i < n; ++i)
        buttons[i]->setBounds (r.removeFromLeft (r.getWidth() / (n - i)).reduced (3, 0));
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

    auto level = [=] (float x)
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

bool ChipKeyboard::keyPressed (const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (c == 'z' || c == 'x')
    {
        baseOctave = juce::jlimit (1, 8, baseOctave + (c == 'x' ? 1 : -1));
        setKeyPressBaseOctave (baseOctave);
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
    panel = getLocalBounds().withSizeKeepingCentre (780, 520);
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
      waveSelector (*state.getParameter (ids::wave), pal::yellow),
      scope (p.scope),
      attack  (state, ids::attack,  "ATTACK",  pal::lime, "ATTACK: how quickly a note fades in. Low = instant 'blip', high = slow swell."),
      decay   (state, ids::decay,   "DECAY",   pal::lime, "DECAY: how fast the note falls from its loudest point down to the Sustain level."),
      sustain (state, ids::sustain, "SUSTAIN", pal::lime, "SUSTAIN: the volume a note holds while you keep the key down. 0% = every note is a short blip."),
      release (state, ids::release, "RELEASE", pal::lime, "RELEASE: how long a note takes to fade away after you let go of the key."),
      envelopeView (state),
      arpModeButtons (*state.getParameter (ids::arpMode), { "OFF", "CHORD", "NOTES" }, pal::cyan,
                      { "ARP OFF: normal notes - what you play is what you hear.",
                        "CHORD: hold ONE key and BitBuddy flips through a whole chord super fast. The classic 8-bit trick - old chips could only play one note per channel!",
                        "NOTES: hold several keys and they play one after another as a pattern (an 'arpeggio')." }),
      octaves (state, ids::arpOctaves, "OCTAVES", pal::cyan, "OCTAVES: repeats the arpeggio 1, 2 or 3 octaves higher for bigger, sparklier patterns."),
      vibDepth  (state, ids::vibDepth,  "VIBRATO",  pal::orange, "VIBRATO: wobbles the pitch up and down like a singer. Game leads use this a LOT. (Your mod wheel adds vibrato too.)"),
      vibRate   (state, ids::vibRate,   "VIB RATE", pal::orange, "VIB RATE: how fast the vibrato wobbles."),
      vibDelay  (state, ids::vibDelay,  "VIB WAIT", pal::orange, "VIB WAIT: waits this long before the wobble starts, so short notes stay clean and long notes 'bloom'. Very 8-bit!"),
      sweepAmt  (state, ids::sweepAmt,  "SWEEP",    pal::orange, "SWEEP: each note starts this many semitones away and slides to the right pitch. + = falls from above (lasers, kicks, 'pew!'), - = rises from below (jumps, power-ups).", true),
      sweepTime (state, ids::sweepTime, "SWP TIME", pal::orange, "SWEEP TIME: how long the Sweep slide takes."),
      pwm       (state, ids::pwm,       "PWM",      pal::orange, "PWM (pulse width modulation): slowly changes the width of the pulse wave for a lush, chorus-y sound. Works on Square and Pulse waves - a classic Commodore 64 trick!"),
      glide     (state, ids::glide,     "GLIDE",    pal::orange, "GLIDE: notes slide smoothly into each other instead of jumping. Works best with MONO."),
      voiceModeButtons (*state.getParameter (ids::voiceMode), { "POLY", "MONO" }, pal::orange,
                        { "POLY: play chords - up to 8 notes at once.",
                          "MONO: one note at a time, like a real sound-chip channel. Great for bass and leads with Glide." },
                        true),
      bits         (state, ids::bits,         "BITS",    pal::sky, "BITS: lowers the audio resolution. 16-bit = clean, 8-bit = classic, 4-bit and below = crunchy and gritty!"),
      rate         (state, ids::rate,         "RATE",    pal::sky, "RATE: lowers the sample rate for a grainy, crunchy old-hardware sound. Turn it all the way up ('Clean') to switch it off."),
      echoMix      (state, ids::echoMix,      "ECHO",    pal::sky, "ECHO: adds echoes that bounce between your left and right speakers. Chiptune composers used to fake this with a spare channel!"),
      echoFeedback (state, ids::echoFeedback, "REPEATS", pal::sky, "REPEATS: how many echoes you hear - low = one or two, high = lots."),
      volume       (state, ids::volume,       "VOLUME",  pal::sky, "VOLUME: the overall loudness of BitBuddy."),
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

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setTooltip ("PRESETS: ready-made sounds to start from. Pick one, play some notes, then tweak the knobs!");
    presetBox.setColour (juce::ComboBox::outlineColourId, pal::yellow);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id > 0)
            proc.selectPreset (id - 1);
    };

    presetTip.setFont (textFont (14.0f));
    presetTip.setColour (juce::Label::textColourId, pal::light);

    for (auto* c : { (juce::Component*) &helpButton, (juce::Component*) &prevButton, (juce::Component*) &nextButton,
                     (juce::Component*) &surpriseButton, (juce::Component*) &presetBox, (juce::Component*) &presetTip })
        addAndMakeVisible (c);

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
                "CHORD TYPE: which chord CHORD mode plays. Major = happy, Minor = sad, Power = rock, Fourth = the 'coin' sound.",
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

void MainPanel::mouseDown (const juce::MouseEvent&)
{
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
    g.drawText ("PRESET", 440, 12, 74, 28, juce::Justification::centredRight);

    drawSection (g, s1, "1", "PICK A SOUND", pal::yellow);
    drawSection (g, sScope, {}, "SCREEN", pal::green);
    drawSection (g, s2, "2", "SHAPE IT  (VOLUME ENVELOPE)", pal::lime);
    drawSection (g, s3, "3", "MAGIC CHORDS  (ARPEGGIATOR)", pal::cyan);
    drawSection (g, s4, "4", "ADD MOVEMENT", pal::orange);
    drawSection (g, s5, "5", "CRUNCH + ECHO", pal::sky);

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
    prevButton.setBounds (520, 12, 32, 30);
    presetBox.setBounds (556, 12, 254, 28);
    nextButton.setBounds (814, 12, 32, 30);
    surpriseButton.setBounds (852, 12, 138, 30);
    presetTip.setBounds (518, 42, 472, 18);

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

    hintArea = { 10, 550, 980, 28 };
    hintLabel.setBounds (hintArea.reduced (2).withTrimmedLeft (26).withTrimmedRight (176));
    keysStatus.setBounds (hintArea.getRight() - 176, hintArea.getY() + 2, 172, hintArea.getHeight() - 4);
    keyboard.setBounds (10, 584, 980, 70);
    help.setBounds (getLocalBounds());
}

void MainPanel::timerCallback()
{
    scope.refresh();
    envelopeView.refresh();

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

    // Whenever BitBuddy's own window is the active one (and no menu is open),
    // keep the letter keys on the piano. This also recovers focus after a menu
    // closes. It never runs while your DAW has focus, so DAW shortcuts keep working.
    if (windowIsActive())
        focusKeyboard();

    updateKeysStatus();
}

void MainPanel::syncPresetBox()
{
    const auto name = proc.getDisplayPresetName();
    if (name == lastPresetName)
        return;

    lastPresetName = name;
    const int current = proc.getCurrentProgram();

    if (name == proc.getProgramName (current))
        presetBox.setSelectedId (current + 1, juce::dontSendNotification);
    else
        presetBox.setText (name, juce::dontSendNotification);

    presetTip.setText (proc.getPresetTip(), juce::dontSendNotification);
}

void MainPanel::mouseEnter (const juce::MouseEvent& e)
{
    showHintFor (e.eventComponent);
}

void MainPanel::mouseExit (const juce::MouseEvent&)
{
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
    setResizeLimits (700, 462, 2000, 1320);
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

    const float scale = (float) getWidth() / (float) bb::MainPanel::baseWidth;
    panel->setTransform (juce::AffineTransform::scale (scale));
    panel->setBounds (0, 0, bb::MainPanel::baseWidth, bb::MainPanel::baseHeight);
}
