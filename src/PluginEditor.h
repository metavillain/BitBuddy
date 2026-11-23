#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "RetroLookAndFeel.h"

namespace bb
{
//==============================================================================
/** Rotary knob with a title above and its value below. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
          juce::Colour accent, const juce::String& hint, bool bipolar = false);

    void resized() override;

    juce::Slider slider;
    juce::Label label;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
/** A row (or column) of buttons that picks one value of a choice parameter:
    the wave buttons, OFF / CHORD / NOTES and POLY / MONO. */
class ParamButtons : public juce::Component
{
public:
    using Factory = std::function<juce::Button* (int index)>;

    ParamButtons (juce::RangedAudioParameter& param, int count, const Factory& makeButton,
                  bool vertical = false, int gap = 2);

    void resized() override;

private:
    void update (float value);

    juce::OwnedArray<juce::Button> buttons;
    bool vertical = false;
    int gap = 2;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Big button with a little pixel drawing of the waveform. */
class WaveButton : public juce::Button
{
public:
    WaveButton (int waveIndex, const juce::String& shortName, juce::Colour accent);
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    int waveIndex;
    juce::Colour accent;
};

//==============================================================================
/** Little pixel die in a section's corner: re-rolls just that section. */
class DiceButton : public juce::Button
{
public:
    explicit DiceButton (juce::Colour accent);
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    juce::Colour accent;
};

//==============================================================================
/** A numbered, framed section of BitBuddy ("box") that owns its own controls,
    so it can be laid out (and later hidden or moved) as one piece. */
class Box : public juce::Component
{
public:
    Box (const juce::String& number, const juce::String& title, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onDice;   // what the corner dice re-rolls

protected:
    virtual void layoutContent (juce::Rectangle<int> area) = 0;
    juce::Colour accent;

private:
    juce::String number, title;
    DiceButton dice;
};

/** Draws which pitches the filter lets through. */
class FilterCurve : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit FilterCurve (juce::AudioProcessorValueTreeState& state);
    void refresh();
    void paint (juce::Graphics&) override;

private:
    juce::AudioProcessorValueTreeState& state;
    int type = -1;
    float cutoff = 0.0f, reso = 0.0f, env = 0.0f;
};

/** 6. C64 FILTER */
class FilterBox : public Box
{
public:
    explicit FilterBox (juce::AudioProcessorValueTreeState& state);
    void refresh() { curve.refresh(); }

private:
    void layoutContent (juce::Rectangle<int>) override;

    ParamButtons typeButtons;
    FilterCurve curve;
    Knob cutoff, reso, env;
};

//==============================================================================
/** Retro green "screen" that shows the live waveform. */
class Scope : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Scope (ScopeBuffer& source);
    void refresh();
    void paint (juce::Graphics&) override;

private:
    ScopeBuffer& source;
    std::array<float, 2048> samples {};
    int frame = 0;
    bool wasSilent = false, lastBlink = false;
};

//==============================================================================
/** DRAW YOUR WAVE: replaces the scope while the DRAW wave is picked. Click and
    drag to draw one cycle (32 steps x 16 levels, like the Game Boy). */
class WaveDrawer : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit WaveDrawer (BitBuddyProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> gridArea() const;
    void drawAt (juce::Point<float>, bool connectToLast);
    void applyShape (int shape);

    BitBuddyProcessor& proc;
    bb::CustomWave shown {};
    juce::OwnedArray<juce::TextButton> shapeButtons;
    int lastStep = -1, lastValue = 0;
};

//==============================================================================
/** Draws the ADSR volume shape. */
class EnvelopeView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EnvelopeView (juce::AudioProcessorValueTreeState& state);
    void refresh();
    void paint (juce::Graphics&) override;

private:
    juce::AudioProcessorValueTreeState& state;
    float a = 0, d = 0, s = 0, r = 0;
};

//==============================================================================
class ChipKeyboard : public juce::MidiKeyboardComponent, public juce::SettableTooltipClient
{
public:
    explicit ChipKeyboard (juce::MidiKeyboardState& state);
    void setDrumMode (bool shouldShowDrums);
    void setScale (int key, int scaleIndex);
    juce::String getWhiteNoteText (int midiNoteNumber) override;
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour noteFillColour) override;
    bool keyPressed (const juce::KeyPress&) override;
    void resized() override;
    int getBaseOctave() const noexcept { return baseOctave; }

private:
    juce::String letterFor (int note) const;   // the computer key that plays this note
    bool isDimmed (int note) const;

    bool drumMode = false;
    int baseOctave = 5;
    int scaleKey = 0, scaleIndex = 0;
};

//==============================================================================
class HelpOverlay : public juce::Component
{
public:
    HelpOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override { setVisible (false); }

private:
    juce::TextButton closeButton { "GOT IT!" };
    juce::Rectangle<int> panel;
};

//==============================================================================
/** Everything you see. Designed at 1000 x 778 and scaled to the window size. */
class MainPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int baseWidth = 1000, baseHeight = 778;

    MainPanel (BitBuddyProcessor&, RetroLookAndFeel&);
    ~MainPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void showHintFor (juce::Component*);

    // Computer-keyboard playing: only the piano ever takes keyboard focus, so
    // clicking knobs, buttons or menus never "steals" your letter keys.
    void stopFocusStealing (juce::Component& root);
    void focusKeyboard();
    bool windowIsActive() const;
    void updateKeysStatus();
    void syncPresetBox();
    void setupCombo (juce::ComboBox&, juce::Label&, const juce::String& title, const juce::StringArray& items,
                     const juce::String& hint, juce::Colour accent);
    void drawSection (juce::Graphics&, juce::Rectangle<int>, const juce::String& number, const juce::String& title,
                      juce::Colour accent);

    BitBuddyProcessor& proc;
    juce::AudioProcessorValueTreeState& state;

    // header
    juce::TextButton helpButton { "? HELP" };
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::TextButton saveButton { "SAVE" }, mutateButton { "MUTATE" };
    juce::TextButton surpriseButton { "SURPRISE ME!" };
    juce::ComboBox presetBox;
    juce::Label presetTip;
    juce::StringArray userPresets;
    void refreshPresetBox();
    void askToSavePreset();
    void savePresetAs (const juce::String& name);
    void askToDeletePreset();

    // one dice button per section
    DiceButton diceSound, diceShape, diceChords, diceMovement, diceCrunch;

    // 1. pick a sound
    ParamButtons waveSelector;
    juce::Label waveInfo;
    Scope scope;
    WaveDrawer waveDrawer;

    // 2. shape it
    Knob attack, decay, sustain, release;
    EnvelopeView envelopeView;

    // 3. magic chords
    ParamButtons arpModeButtons;
    juce::ComboBox chordBox, patternBox, speedBox;
    juce::Label chordLabel, patternLabel, speedLabel;
    Knob octaves;
    juce::Label arpTip;

    // 4. movement
    Knob vibDepth, vibRate, vibDelay, sweepAmt, sweepTime, pwm, glide;
    ParamButtons voiceModeButtons;

    // 5. crunch + echo
    Knob bits, rate, echoMix, echoFeedback, volume;
    juce::ComboBox echoTimeBox;
    juce::Label echoTimeLabel;

    // 6. C64 filter
    FilterBox filterBox;

    // play along: Scale Lock
    juce::ComboBox keyBox, scaleBox;
    juce::Label keyLabel, scaleLabel, scaleTip;
    void updateScaleTip();

    juce::Label hintLabel;
    juce::Label keysStatus;
    juce::String lastKeysStatus;
    ChipKeyboard keyboard;
    HelpOverlay help;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chordAtt, patternAtt, speedAtt, echoTimeAtt,
                                                                            keyAtt, scaleAtt;

    juce::Rectangle<int> s1, sScope, s2, s3, s4, s5, s6, sPlay, hintArea;
    int lastWave = -1, lastArpMode = -1, lastScaleState = -1;
    juce::String lastPresetName;
    bool lastWasUserPreset = false;
};
} // namespace bb

//==============================================================================
class BitBuddyEditor : public juce::AudioProcessorEditor
{
public:
    explicit BitBuddyEditor (BitBuddyProcessor&);
    ~BitBuddyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    bb::RetroLookAndFeel lnf;
    std::unique_ptr<bb::MainPanel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitBuddyEditor)
};
