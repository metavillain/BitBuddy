#pragma once

#include <array>
#include <cstdint>

#include "Scales.h"

// =============================================================================
//  BitBuddy chip engine
//
//  Recreates the tricks of 8-bit sound chips (NES 2A03, Game Boy, C64...):
//    * pulse waves with fixed "duty cycles" (12.5%, 25%, 50%)
//    * a 4-bit stepped triangle (the NES triangle has only 16 volume steps)
//    * noise from a 15-bit LFSR shift register, incl. the metallic "short mode"
//    * fast arpeggios to fake chords, pitch sweeps, delayed vibrato
//    * a synthesized drum kit (pitch-dropping triangle + noise bursts)
//
//  The engine is plain C++ (no JUCE) so it is easy to read and test.
// =============================================================================
namespace bb
{
enum class Wave : int { Square = 0, Pulse25, Pulse12, Triangle, Saw, Noise, Metal, Drums, Custom };
constexpr int kNumWaves = 9;

// "Draw your own wave": one cycle as 32 steps of 0..15, exactly like the
// Game Boy's wave channel (its 32-sample, 4-bit "wave RAM").
constexpr int kCustomWaveSteps = 32;
using CustomWave = std::array<std::uint8_t, kCustomWaveSteps>;
CustomWave defaultCustomWave();   // a 4-bit sine
enum class ArpMode : int { Off = 0, Chord, Notes };

// Console modes copy each real chip's limits and quirks (Modern = no limits)
enum class Console : int { Modern = 0, NES, GameBoy, C64 };

// Instrument macros, like FamiTracker: little step sequences that play 60 steps
// a second from the start of every note and shape it as it goes.
enum MacroLaneId : int { kMacroVolume = 0, kMacroArp, kMacroPitch, kMacroDuty, kNumMacroLanes };
constexpr int kMacroSteps = 32;
constexpr double kMacroStepsPerSecond = 60.0;   // the NES frame rate

struct MacroLaneInfo { const char* name; int minValue, maxValue, defaultValue; };
constexpr MacroLaneInfo kMacroLanes[kNumMacroLanes] = {
    { "VOL",   0,   15, 15 },   // volume 0..15 (like the chips' 4-bit volume)
    { "ARP",   -12, 24, 0  },   // semitones added to the note
    { "PITCH", -12, 12, 0  },   // fine pitch, 1/8 semitone per step
    { "DUTY",  0,   3,  2  },   // pulse width: 12.5%, 25%, 50%, 75%
};

struct Macro
{
    int length = 0;                                   // 0 = this lane is off
    int loop = -1;                                    // step to jump back to at the end (-1 = hold the last value)
    std::array<std::int8_t, kMacroSteps> values {};

    bool isOn() const noexcept { return length > 0; }
    int valueAt (int frame) const noexcept
    {
        if (frame >= length)
            frame = loop >= 0 && loop < length ? loop + (frame - length) % (length - loop) : length - 1;
        return values[(size_t) frame];
    }
};
enum class ArpPattern : int { Up = 0, Down, UpDown, Random, Once };

struct EngineParams
{
    Wave wave = Wave::Square;
    float attack = 0.003f, decay = 0.35f, sustain = 0.7f, release = 0.12f; // seconds / 0..1
    ArpMode arpMode = ArpMode::Off;
    int arpChord = 0;
    ArpPattern arpPattern = ArpPattern::Up;
    int arpOctaves = 1;
    double arpStepSeconds = 0.0625;
    float sweepAmt = 0.0f, sweepTime = 0.15f;                  // semitones, seconds
    float vibDepth = 0.0f, vibRate = 6.0f, vibDelay = 0.2f;    // semitones, Hz, seconds
    float pwm = 0.0f;                                          // 0..1
    float glide = 0.0f;                                        // seconds
    bool mono = false;

    // Scale Lock: every key snaps to the nearest note of the scale (Off = no change)
    int scaleKey = 0;                                          // 0 = C ... 11 = B
    Scale scale = Scale::Off;
    bool smartChords = false;                                  // CHORD mode picks the chord from the scale

    CustomWave customWave = defaultCustomWave();               // for Wave::Custom
    Console console = Console::Modern;
    std::array<Macro, kNumMacroLanes> macros {};
};

class ChipEngine
{
public:
    static constexpr int maxVoices = 8;

    void prepare (double sampleRate);
    void setParams (const EngineParams& newParams);

    void noteOn (int note, float velocity);   // velocity 0..1
    void noteOff (int note);
    void allNotesOff (bool immediate);
    void setPitchBend (float semitones) noexcept { pitchBend = semitones; }
    void setModWheel (float amount01) noexcept   { modWheel = amount01; }
    void setSustainPedal (bool isDown);

    /** Adds numSamples of mono audio into out. Returns false (and does no work)
        when nothing is playing. */
    bool render (float* out, int numSamples) noexcept;

    /** True when no voice is sounding. */
    bool isIdle() const noexcept;

    /** How many voices are sounding right now. */
    int getNumActiveVoices() const noexcept;

    /** How many notes can sound at once: 8, or the console's channel count. */
    int getVoiceLimit() const noexcept;

    /** The frequency a real chip would actually play for a wanted frequency
        (its pitch registers only have so many steps). Modern returns hz unchanged. */
    static float chipFrequency (Console, Wave, float hz) noexcept;

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    // Glide, sweep and vibrato are updated once every kControlInterval samples
    // (~2.8 kHz). The original chips did it 60 times a second, so this is still
    // perfectly smooth, but the expensive pitch -> frequency maths runs 16x less often.
    static constexpr int kControlInterval = 16;

    struct Voice
    {
        bool active = false;
        bool held = false;            // key still down
        int note = 60;
        float velocity = 1.0f;
        std::uint64_t age = 0;

        float pitch = 60.0f, targetPitch = 60.0f;   // MIDI note numbers (fractional)
        double t = 0.0;                              // seconds since note start

        float ctrlNote = 60.0f;          // pitch incl. glide, sweep, vibrato, bend (control rate)
        int ctrlCountdown = 0;           // samples until the next control-rate update
        float cachedNote = -1000.0f;     // note the frequency below was computed for
        float hz = 0.0f, inc = 0.0f;     // frequency, and phase increment per sample

        double phase = 0.0, lfoPhase = 0.25, pwmPhase = 0.0;
        std::uint32_t lfsr = 1;
        double noiseClockPhase = 0.0;

        Stage stage = Stage::Idle;
        float env = 0.0f;

        int arpIndex = 0, arpDir = 1;
        double arpCounter = 0.0;
        std::uint32_t rng = 0x1234567u;

        // drums
        bool isDrum = false;
        int drumType = 0;
        float dToneEnv = 0, dNoiseEnv = 0, dSweepEnv = 0;
        float dToneMul = 0, dNoiseMul = 0, dSweepMul = 0;
        bool choking = false;
        float chokeGain = 1.0f;

        float gate = 0.0f;   // NES triangle: no volume control, just on / off (smoothed against clicks)

        // instrument macros
        int macroFrame = 0;
        double macroCounter = 0.0;
        float macroVolume = 1.0f, macroNotes = 0.0f;   // gain, semitones (arp + fine pitch)
        int macroDuty = -1;                            // -1 = the wave's own duty
    };

    float renderTone (Voice&) noexcept;
    float chipLevel (Voice&) noexcept;
    void restartMacros (Voice&) noexcept;
    void applyMacroFrame (Voice&) noexcept;
    bool anyMacroOn = false;
    double macroStepSamples = 735.0;
    double noiseClock (double wantedClock) const noexcept;
    float renderDrum (Voice&) noexcept;
    void updateModulation (Voice&) noexcept;
    float oscillate (Voice&) noexcept;
    float noiseStep (Voice&, double clockHz, bool shortMode) noexcept;

    void startVoice (Voice&, int note, float velocity);
    void startDrum (Voice&, int note, float velocity);
    void retrigger (Voice&, int note);
    void releaseVoice (Voice&) noexcept;
    Voice& allocateVoice (int note, bool drum);

    void startNote (int note, float velocity);   // note = what sounds (after Scale Lock)
    void stopNote (int note);
    void doNoteOff (int note);
    void addHeld (int note, float velocity);
    void removeHeld (int note);
    int chordShapeFor (int note) const noexcept;
    void rebuildChordSequences();
    void rebuildSmartChords();
    void rebuildHeldSequence();
    void tickHeldArp() noexcept;
    void releaseAllTones() noexcept;

    static int nextIndex (int idx, int& dir, int len, ArpPattern p, std::uint32_t& rng) noexcept;

    double sampleRate = 44100.0, invSr = 1.0 / 44100.0;
    EngineParams p;
    bool paramsInitialised = false;

    std::array<Voice, maxVoices> voices {};
    std::uint64_t ageCounter = 0;
    int lastNote = -1;

    float attackStep = 0, decayStep = 0, releaseStep = 0;
    bool glideOn = false;
    float glideCoeff = 1.0f;   // per control-rate update
    double arpStepSamples = 2756.0;
    float chokeMul = 0.99f;

    float pitchBend = 0.0f, modWheel = 0.0f;

    // keys
    struct HeldNote { int note; float velocity; };
    std::array<HeldNote, 32> held {};
    int numHeld = 0;
    std::array<bool, 128> keyDown {};      // by sounding note
    std::array<bool, 128> sustained {};
    bool pedalDown = false;

    // Scale Lock: which note each key on your keyboard is playing, and how many
    // keys play each note (C and C# can both land on C in C major)
    std::array<std::int8_t, 128> playedAs {};
    std::array<std::uint8_t, 128> keyCount {};

    // chord-mode arp sequences (semitone offsets), one per chord shape
    static constexpr int kNumShapes = 10;  // the 9 chord types + augmented (Smart Chords only)
    std::array<std::array<int, 16>, kNumShapes> chordSeqs {};
    std::array<int, kNumShapes> chordSeqLens {};
    int builtChordOctaves = -1;
    std::array<int, 12> smartShape {};     // chord shape for each degree of the scale
    Scale builtSmartScale = Scale::Off;
    bool smartBuilt = false;

    // notes-mode arp (held keys) sequence
    std::array<int, 128> heldSeq {};
    int heldSeqLen = 0;
    int builtHeldOctaves = -1;
    bool heldArpRunning = false;
    int heldArpPos = 0, heldArpDir = 1;
    double heldArpCounter = 0.0;
    std::uint32_t heldRng = 0xBADC0DEu;
};
} // namespace bb
