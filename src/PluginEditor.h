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
/** The little "x" in a box's corner: hides the box (bring it back from VIEW). */
class HideButton : public juce::Button
{
public:
    HideButton();
    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;
};

/** A numbered, framed section of BitBuddy ("box") with its own controls. You can
    drag a box by its title bar to move it, or hide it with its "x". */
class Box : public juce::Component
{
public:
    Box (const juce::String& id, const juce::String& number, const juce::String& title, juce::Colour accent,
         int naturalWidth, int naturalHeight);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    void setDice (std::function<void()> rollSection);   // a dice in the corner re-rolls this section
    const juce::String& getId() const noexcept    { return id; }
    const juce::String& getTitle() const noexcept { return title; }
    int getNaturalWidth() const noexcept          { return naturalWidth; }
    int getNaturalHeight() const noexcept         { return naturalHeight; }

    std::function<void (Box&)> onHide;
    std::function<void (Box&, const juce::MouseEvent&)> onDragStart, onDragMove, onDragEnd;

    static constexpr int headerHeight = 24;

protected:
    virtual void layoutContent (juce::Rectangle<int> area) = 0;
    juce::Colour accent;

private:
    juce::String id, number, title;
    int naturalWidth, naturalHeight;
    DiceButton dice;
    HideButton hide;
    std::function<void()> onDice;
    bool dragging = false;
};

/** A box whose controls are positioned by a layout function (box-local coordinates). */
class SectionBox : public Box
{
public:
    using Box::Box;
    std::function<void()> layout;

private:
    void layoutContent (juce::Rectangle<int>) override { if (layout != nullptr) layout(); }
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
    float getLevel() const noexcept { return level; }   // loudness of the newest ~10 ms (for BIT)

private:
    ScopeBuffer& source;
    std::array<float, 2048> samples {};
    float level = 0.0f;
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
    void useDrawWave();

    BitBuddyProcessor& proc;
    bb::CustomWave shown {};
    juce::OwnedArray<juce::TextButton> shapeButtons;
    int lastStep = -1, lastValue = 0;
};

//==============================================================================
/** MACROS: draw how each note changes over time, 60 steps a second (like FamiTracker).
    Four lanes: VOL, ARP, PITCH, DUTY. Each has ON, a length and an optional loop. */
class MacroEditor : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit MacroEditor (BitBuddyProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> gridArea() const;
    juce::Rectangle<float> loopArea() const;
    int stepAt (float x) const;
    void selectLane (int);
    void drawAt (juce::Point<float>, bool connectToLast);
    void setLength (int newLength);
    void updateButtons();

    BitBuddyProcessor& proc;
    int lane = 0;
    std::array<bb::Macro, bb::kNumMacroLanes> shown {};
    std::array<int, bb::kNumMacroLanes> lengthWhenOff { 16, 16, 16, 16 };
    juce::OwnedArray<juce::TextButton> laneButtons;
    juce::TextButton onButton { "ON" }, minusButton { "-" }, plusButton { "+" }, clearButton { "CLR" };
    juce::Label lengthLabel;
    int lastStep = -1, lastValue = 0;
    bool edited = false;
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
/** BIT, the BitBuddy mascot: a little pixel microchip that bounces when you play,
    dances when the JAM band plays, and changes the colour theme when clicked. */
class Mascot : public juce::Component, public juce::SettableTooltipClient
{
public:
    Mascot();
    void update (float outputLevel, bool jamming, double bpm);   // ~30 times a second
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void()> onClick;

private:
    float level = 0.0f, jump = 0.0f;
    int frame = 0, lastLook = -1;
    bool jamming = false;
    double beat = 0.0;
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
/** Everything you see. 1000 units wide (scaled to the window); its height depends
    on which boxes are shown (778 with all of them). */
class MainPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int baseWidth = 1000, fullHeight = 778;

    MainPanel (BitBuddyProcessor&, RetroLookAndFeel&);
    ~MainPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    //==========================================================================
    // Layout: which boxes are shown, and in what order
    int getDesignHeight() const noexcept { return designHeight; }
    std::function<void()> onDesignHeightChanged;
    void setBoxVisible (const juce::String& id, bool shouldBeVisible);
    bool isBoxVisible (const juce::String& id) const { return ! hiddenBoxes.contains (id); }
    void moveBox (const juce::String& id, int newIndex);
    void resetLayout();
    juce::StringArray getBoxOrder() const { return boxOrder; }

private:
    void layoutBoxes();
    void loadLayout();
    void saveLayout() const;
    void showViewMenu();
    bool isBoxChangingSound (const juce::String& id) const;
    Box* findBox (const juce::String& id) const;
    void dragBoxTo (Box&, juce::Point<int> positionInPanel);

    void timerCallback() override;
    void showHintFor (juce::Component*);
    int choiceOf (const char* paramId, int maxIndex) const;   // a choice parameter's current index

    // Computer-keyboard playing: only the piano ever takes keyboard focus, so
    // clicking knobs, buttons or menus never "steals" your letter keys.
    void stopFocusStealing (juce::Component& root);
    void focusKeyboard();
    bool windowIsActive() const;
    void updateKeysStatus();
    void syncPresetBox();
    void setupCombo (juce::ComboBox&, juce::Label&, const juce::String& title, const juce::StringArray& items,
                     const juce::String& hint, juce::Colour accent);

    BitBuddyProcessor& proc;
    juce::AudioProcessorValueTreeState& state;

    // the boxes (each section of BitBuddy), in their default order
    SectionBox soundBox, screenBox, shapeBox, chordsBox, movementBox, crunchBox, playBox;
    std::vector<Box*> allBoxes;
    juce::StringArray boxOrder, hiddenBoxes;
    int designHeight = fullHeight;

    // header
    Mascot mascot;
    void showThemeMenu();
    juce::TextButton helpButton { "? HELP" }, viewButton { "VIEW" };
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

    // 1. pick a sound
    ParamButtons waveSelector;
    juce::Label waveInfo;
    juce::ComboBox consoleBox;
    juce::Label consoleLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> consoleAtt;
    Scope scope;
    WaveDrawer waveDrawer;
    MacroEditor macroEditor;
    juce::TextButton scopeViewButton { "SCOPE" }, waveViewButton { "WAVE" }, macroViewButton { "MACROS" };
    enum class ScreenView { Scope, Wave, Macros };
    ScreenView screenView = ScreenView::Scope;
    void setScreenView (ScreenView);
    bool lastAnyMacro = false;

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

    // play along: JAM band (BitBuddy app only)
    juce::TextButton jamButton { "JAM" }, tempoDownButton { "-" }, tempoUpButton { "+" };
    juce::ComboBox jamStyleBox;
    juce::Label tempoLabel;
    void updateJamControls();

    // play along: RECORD (BitBuddy app only)
    juce::TextButton recordButton { "REC" }, showRecordingButton { "SHOW" };
    juce::Label recordStatus, appOnlyNote;
    juce::ScopedMessageBox shareBox;
    void toggleRecording();
    void updateRecordStatus();
    bool wasRecording = false;

    juce::Label hintLabel;
    juce::Label keysStatus;
    juce::String lastKeysStatus;
    ChipKeyboard keyboard;
    HelpOverlay help;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chordAtt, patternAtt, speedAtt, echoTimeAtt,
                                                                            keyAtt, scaleAtt;

    juce::Rectangle<int> hintArea;
    int lastWave = -1, lastArpMode = -1, lastScaleState = -1;
    juce::String lastPresetName;
    bool lastWasUserPreset = false;
};
} // namespace bb

//==============================================================================
class BitBuddyEditor : public juce::AudioProcessorEditor, private juce::ChangeListener
{
public:
    explicit BitBuddyEditor (BitBuddyProcessor&);
    ~BitBuddyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // colour theme changed: rebuild in the new colours
    void createPanel();
    void fitWindowToPanel (int width);   // window shape follows the boxes you show

    BitBuddyProcessor& bitBuddy;
    bb::RetroLookAndFeel lnf;
    juce::SharedResourcePointer<bb::ThemeEvents> themeEvents;
    std::unique_ptr<bb::MainPanel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitBuddyEditor)
};
