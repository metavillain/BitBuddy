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
/** A row (or column) of buttons that picks one value of a choice parameter. */
class ChoiceButtons : public juce::Component
{
public:
    ChoiceButtons (juce::RangedAudioParameter& param, const juce::StringArray& labels, juce::Colour accent,
                   const juce::StringArray& hints, bool vertical = false);

    void resized() override;

private:
    void update (float value);

    juce::OwnedArray<juce::TextButton> buttons;
    bool vertical = false;
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

class WaveSelector : public juce::Component
{
public:
    WaveSelector (juce::RangedAudioParameter& param, juce::Colour accent);
    void resized() override;

private:
    void update (float value);

    juce::OwnedArray<WaveButton> buttons;
    juce::ParameterAttachment attachment;
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
    juce::String getWhiteNoteText (int midiNoteNumber) override;
    bool keyPressed (const juce::KeyPress&) override;
    void resized() override;

private:
    bool drumMode = false;
    int baseOctave = 5;
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
/** Everything you see. Designed at 1000 x 660 and scaled to the window size. */
class MainPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int baseWidth = 1000, baseHeight = 660;

    MainPanel (BitBuddyProcessor&, RetroLookAndFeel&);
    ~MainPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void showHintFor (juce::Component*);
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
    juce::TextButton surpriseButton { "SURPRISE ME!" };
    juce::ComboBox presetBox;
    juce::Label presetTip;

    // 1. pick a sound
    WaveSelector waveSelector;
    juce::Label waveInfo;
    Scope scope;

    // 2. shape it
    Knob attack, decay, sustain, release;
    EnvelopeView envelopeView;

    // 3. magic chords
    ChoiceButtons arpModeButtons;
    juce::ComboBox chordBox, patternBox, speedBox;
    juce::Label chordLabel, patternLabel, speedLabel;
    Knob octaves;
    juce::Label arpTip;

    // 4. movement
    Knob vibDepth, vibRate, vibDelay, sweepAmt, sweepTime, pwm, glide;
    ChoiceButtons voiceModeButtons;

    // 5. crunch + echo
    Knob bits, rate, echoMix, echoFeedback, volume;
    juce::ComboBox echoTimeBox;
    juce::Label echoTimeLabel;

    juce::Label hintLabel;
    ChipKeyboard keyboard;
    HelpOverlay help;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chordAtt, patternAtt, speedAtt, echoTimeAtt;

    juce::Rectangle<int> s1, sScope, s2, s3, s4, s5, hintArea;
    int lastWave = -1, lastArpMode = -1;
    juce::String lastPresetName;
    bool focusGrabbed = false;
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
