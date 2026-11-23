#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

#include "ChipEngine.h"
#include "Effects.h"

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

class BitBuddyProcessor : public juce::AudioProcessor
{
public:
    BitBuddyProcessor();
    ~BitBuddyProcessor() override = default;

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

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;   // on-screen keyboard -> synth
    ScopeBuffer scope;

private:
    void updateEngineParams();
    void handleMidi (const juce::MidiMessage&);
    void loadProgram (int index);

    bb::ChipEngine engine;
    bb::DcBlocker dcBlocker;
    bb::Crusher crusher;
    bb::PingPongEcho echo;
    bb::SidFilter filter;
    juce::SmoothedValue<float> outputGain;
    float echoMix = 0.0f, echoFeedback = 0.0f;
    bool echoWasOn = false, filterWasOn = false;
    std::array<int, 64> filterTriggers {};   // sample positions of this block's new notes
    int numFilterTriggers = 0;
    bool effectsIdle = true;   // no notes and all effect tails have died away
    int quietSamples = 0;      // consecutive silent output samples with no notes

    void rollSection (Section, juce::Random&);
    void setParamValue (const char* id, float realValue);

    std::array<std::atomic<std::uint8_t>, bb::kCustomWaveSteps> customWave {};   // UI writes, audio reads

    int currentProgram = 0;
    juce::String customName;
    bool userPresetLoaded = false;
    int randomCount = 0;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitBuddyProcessor)
};
