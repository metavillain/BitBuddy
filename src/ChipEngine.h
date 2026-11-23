#pragma once

#include <array>
#include <cstdint>

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
enum class Wave : int { Square = 0, Pulse25, Pulse12, Triangle, Saw, Noise, Metal, Drums };
enum class ArpMode : int { Off = 0, Chord, Notes };
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

    /** Adds numSamples of mono audio into out. */
    void render (float* out, int numSamples) noexcept;

    int getNumActiveVoices() const noexcept;

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    struct Voice
    {
        bool active = false;
        bool held = false;            // key still down
        int note = 60;
        float velocity = 1.0f;
        std::uint64_t age = 0;

        float pitch = 60.0f, targetPitch = 60.0f;   // MIDI note numbers (fractional)
        double t = 0.0;                              // seconds since note start

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
    };

    float renderTone (Voice&) noexcept;
    float renderDrum (Voice&) noexcept;
    float oscillate (Voice&, float hz) noexcept;
    float noiseStep (Voice&, double clockHz, bool shortMode) noexcept;

    void startVoice (Voice&, int note, float velocity);
    void startDrum (Voice&, int note, float velocity);
    void retrigger (Voice&, int note);
    void releaseVoice (Voice&) noexcept;
    Voice& allocateVoice (int note, bool drum);

    void doNoteOff (int note);
    void addHeld (int note, float velocity);
    void removeHeld (int note);
    void rebuildChordSequence();
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

    float attackStep = 0, decayStep = 0, releaseStep = 0, glideCoeff = 1.0f;
    double arpStepSamples = 2756.0;
    float chokeMul = 0.99f;

    float pitchBend = 0.0f, modWheel = 0.0f;

    // keys
    struct HeldNote { int note; float velocity; };
    std::array<HeldNote, 32> held {};
    int numHeld = 0;
    std::array<bool, 128> keyDown {};
    std::array<bool, 128> sustained {};
    bool pedalDown = false;

    // chord-mode arp sequence (semitone offsets)
    std::array<int, 16> chordSeq {};
    int chordSeqLen = 1;
    int builtChord = -1, builtChordOctaves = -1;

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
