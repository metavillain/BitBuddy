#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

#include "ChipEngine.h"
#include "Effects.h"
#include "JamBand.h"
#include "NotePlayer.h"
#include "Recorder.h"

// Lock-free ring buffer so the UI "screen" can show the live waveform.
class ScopeBuffer
{
public:
    static constexpr int size = 4096;   // must be a power of two

    void push (float x) noexcept
    {
        const int w = writePos.load (std::memory_order_relaxed);
        data[(size_t) w].store (x, std::memory_order_relaxed);
        writePos.store ((w + 1) & (size - 1), std::memory_order_release);
    }

    /** While idle: flatten the display once, then stop writing. (Audio thread only.) */
    void pushSilence (int num) noexcept
    {
        for (; num > 0 && silentRun < size; --num, ++silentRun)
            push (0.0f);
    }

    void markActive() noexcept { silentRun = 0; }

    void copyLatest (float* dest, int num) const noexcept
    {
        const int w = writePos.load (std::memory_order_acquire);
        const int start = (w - num + size) & (size - 1);
        for (int i = 0; i < num; ++i)
            dest[i] = data[(size_t) ((start + i) & (size - 1))].load (std::memory_order_relaxed);
    }

private:
    std::array<std::atomic<float>, size> data {};
    std::atomic<int> writePos { 0 };
    int silentRun = 0;
};

class BitBuddyProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener
{
public:
    BitBuddyProcessor();
    ~BitBuddyProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BitBuddy"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    //==========================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    /** Called by the UI: loads a preset and tells the host about it. */
    void selectPreset (int index);
    /** "Surprise me!" - a random but musical sound. */
    void randomize();

    /** The parts of BitBuddy each dice button can re-roll on its own. */
    enum class Section { Sound, Shape, Chords, Movement, Crunch, Filter };
    void rollSection (Section);
    /** MUTATE: nudges the current sound a little instead of starting over. */
    void mutate();

    /** Name to show in the preset box ("Hero Lead", "Surprise #3"...). */
    juce::String getDisplayPresetName() const;
    /** One-line tip for the current preset (empty for random sounds). */
    juce::String getPresetTip() const;

    //==========================================================================
    // My Presets: your own sounds, saved as files (they show up in every DAW
    // project and in the BitBuddy app)
    static juce::File getUserPresetFolder();
    static juce::StringArray getUserPresetNames();
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    static bool deleteUserPreset (const juce::String& name);
    bool isUserPresetLoaded() const noexcept { return userPresetLoaded; }
    /** The sound no longer matches a saved preset: show its name with a "*". */
    void markRemixed();

    //==========================================================================
    // Draw your own wave (the DRAW waveform): 32 steps of 0..15
    int getCustomWaveStep (int index) const noexcept;
    void setCustomWaveStep (int index, int value) noexcept;
    void setCustomWave (const bb::CustomWave&) noexcept;
    bb::CustomWave getCustomWave() const noexcept;
    /** Call when you finish drawing: tells the DAW the project changed. */
    void customWaveEdited();
    /** 32 hex digits, e.g. "89ABCDEF..." - how drawings are saved. */
    static juce::String customWaveToString (const bb::CustomWave&);
    static bool customWaveFromString (const juce::String&, bb::CustomWave&);

    //==========================================================================
    // Instrument macros (VOL / ARP / PITCH / DUTY lanes)
    bb::Macro getMacro (int lane) const noexcept;
    void setMacro (int lane, const bb::Macro&) noexcept;
    void setMacroStep (int lane, int step, int value) noexcept;
    void clearMacros() noexcept;
    bool anyMacroOn() const noexcept;
    /** Call when you finish editing a macro: tells the DAW the project changed. */
    void macrosEdited();
    /** "length|loop|v,v,v..." - how macros are saved. */
    static juce::String macroToString (const bb::Macro&);
    static bool macroFromString (const juce::String&, bb::Macro&);

    //==========================================================================
    /** RECORD (BitBuddy app): WAV of everything you hear + MIDI of the notes you play. */
    bb::Recorder recorder;
    bool isStandaloneApp() const noexcept { return wrapperType == wrapperType_Standalone; }

    /** JAM (BitBuddy app): a backing band in your KEY + SCALE. */
    void setJamOn (bool on) noexcept       { jamOn = on; }
    bool isJamOn() const noexcept          { return jamOn; }
    void setJamStyle (int style) noexcept  { jamStyle = juce::jlimit (0, bb::kNumJamStyles - 1, style); jamTempo = (float) bb::jamStyleTempo (jamStyle); }
    int getJamStyle() const noexcept       { return jamStyle; }
    void setJamTempo (float bpm) noexcept  { jamTempo = juce::jlimit (40.0f, 240.0f, bpm); }
    float getJamTempo() const noexcept     { return jamTempo; }

    //==========================================================================
    // GAME SOUNDS (added in 1.2)
    /** A brand new sound effect of this kind (bb::Sfx), played once straight away. */
    void makeSoundEffect (int type);
    /** Plays the current sound the way the last sound effect was played (its note + length). */
    void playSoundEffect();
    /** Renders the current sound (one sound-effect note) into a WAV file. Works from
        the UI thread while BitBuddy keeps playing (it uses its own copy). */
    bool renderSoundToWav (const juce::File&);
    /** Where SAVE WAV puts sounds: Music (computers) or Documents (phones) / "BitBuddy Sounds". */
    static juce::File getSoundsFolder();
    /** A new jingle (bb::Jingle) in your Key + Scale, played with the current sound. */
    void playJingle (int type);

    //==========================================================================
    // SOUND CODES (added in 1.2): a whole sound as a line of text to send to a friend
    juce::String getSoundCode();
    /** Finds a sound code anywhere in the text and loads it. Returns false (and
        changes nothing) when there is no valid code. */
    bool loadSoundCode (const juce::String& text);

    /** Plays scheduled notes: sound effects, jingles (and in the app, MIDI songs). */
    bb::NotePlayer notePlayer;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // on-screen keyboard -> synth
    ScopeBuffer scope;

private:
    void updateEngineParams (double tempoBpm);
    void handleMidi (const juce::MidiMessage&);
    void writeRecording (const juce::AudioBuffer<float>&, int numSamples) noexcept;
    static int idxOf (std::atomic<float>* p, int maxIndex) noexcept;
    void loadProgram (int index);

    bb::ChipEngine engine;
    std::array<bb::ChannelFx, 2> fx;   // [0] = mono / left, [1] = right (STEREO Wide only)
    bb::PingPongEcho echo;
    bb::JamBand jam;
    std::atomic<bool> jamOn { false };
    std::atomic<int> jamStyle { 0 };
    std::atomic<float> jamTempo { 120.0f };
    juce::SmoothedValue<float> outputGain;
    float echoMix = 0.0f, echoFeedback = 0.0f;
    bool echoWasOn = false, filterWasOn = false, wideWasOn = false;
    int stereoMode = 0;          // 0 Middle, 1 Wide, 2 Swing
    double renderRate = 44100.0; // the sample rate prepareToPlay was given
    double swingPhase = 0.0, swingStep = 0.0;   // Swing: once from side to side per bar
    std::array<int, 64> filterTriggers {};   // sample positions of this block's new notes
    int numFilterTriggers = 0;
    bool effectsIdle = true;   // no notes and all effect tails have died away
    int quietSamples = 0;      // consecutive silent output samples with no notes

    void rollSection (Section, juce::Random&);
    void setParamValue (const char* id, float realValue);

    std::array<std::atomic<std::uint8_t>, bb::kCustomWaveSteps> customWave {};   // UI writes, audio reads

    struct MacroStore
    {
        std::atomic<int> length { 0 }, loop { -1 };
        std::array<std::atomic<std::int8_t>, bb::kMacroSteps> values {};
    };
    std::array<MacroStore, bb::kNumMacroLanes> macros;
    void writeExtras (const std::function<void (const juce::String& key, const juce::String& value)>& set) const;
    void readExtras (const std::function<juce::String (const juce::String& key)>& get, bool keepWaveIfMissing);

    // Engine settings are only re-read when something changed (cheap when idle)
    void parameterChanged (const juce::String&, float) override { paramsDirty = true; }
    double hostTempo() const;
    std::atomic<bool> paramsDirty { true };
    double lastTempo = 120.0;

    int currentProgram = 0;
    juce::String customName;
    bool userPresetLoaded = false;
    int randomCount = 0;
    int sfxNote = 72;
    double sfxLength = 0.4;

    std::atomic<float>* pWave = nullptr;
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pSustain = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pArpMode = nullptr;
    std::atomic<float>* pArpChord = nullptr;
    std::atomic<float>* pArpPattern = nullptr;
    std::atomic<float>* pArpSpeed = nullptr;
    std::atomic<float>* pArpOctaves = nullptr;
    std::atomic<float>* pSweepAmt = nullptr;
    std::atomic<float>* pSweepTime = nullptr;
    std::atomic<float>* pVibDepth = nullptr;
    std::atomic<float>* pVibRate = nullptr;
    std::atomic<float>* pVibDelay = nullptr;
    std::atomic<float>* pPwm = nullptr;
    std::atomic<float>* pGlide = nullptr;
    std::atomic<float>* pVoiceMode = nullptr;
    std::atomic<float>* pBits = nullptr;
    std::atomic<float>* pRate = nullptr;
    std::atomic<float>* pEchoMix = nullptr;
    std::atomic<float>* pEchoTime = nullptr;
    std::atomic<float>* pEchoFeedback = nullptr;
    std::atomic<float>* pVolume = nullptr;
    std::atomic<float>* pScaleKey = nullptr;
    std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pFilterType = nullptr;
    std::atomic<float>* pFilterCutoff = nullptr;
    std::atomic<float>* pFilterReso = nullptr;
    std::atomic<float>* pFilterEnv = nullptr;
    std::atomic<float>* pConsole = nullptr;
    std::atomic<float>* pSweepMode = nullptr;
    std::atomic<float>* pVibShape = nullptr;
    std::atomic<float>* pStereo = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitBuddyProcessor)
};
