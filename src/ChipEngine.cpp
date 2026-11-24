#include "ChipEngine.h"
#include "VoiceSample.h"

#include <algorithm>
#include <cmath>

namespace bb
{
namespace
{
constexpr float kVoiceGain = 0.3f;   // per-voice level, leaves headroom for chords
constexpr float kPwmRate   = 2.5f;   // Hz, speed of the pulse-width wobble

// PolyBLEP: a tiny correction applied right at each jump of a square / saw
// wave. It removes the harsh "aliasing" fizz digital square waves get on high
// notes, while keeping the hard-edged chip character.
inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

inline float noteToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

// Triangle LFO, -1..1
inline float triLfo (double phase) noexcept
{
    const auto x = (float) phase;
    return x < 0.5f ? 4.0f * x - 1.0f : 3.0f - 4.0f * x;
}

// The NES triangle channel steps through 32 positions: 15,14..0,0,1..15.
// Only 16 distinct levels -> that slightly gritty "4-bit" triangle sound.
inline float steppedTriangle (double phase) noexcept
{
    const int step  = (int) (phase * 32.0) & 31;
    const int level = step < 16 ? 15 - step : step - 16;
    return (float) level / 7.5f - 1.0f;
}

inline float velocityToGain (float v) noexcept { return 0.35f + 0.65f * std::clamp (v, 0.0f, 1.0f); }

// Chord shapes for the "Chord" arpeggiator (semitones above the key you press)
struct Chord { int count; int iv[4]; };
constexpr Chord kChords[] = {
    { 3, { 0, 4, 7 } },      // Major
    { 3, { 0, 3, 7 } },      // Minor
    { 3, { 0, 7, 12 } },     // Power
    { 2, { 0, 12 } },        // Octave
    { 3, { 0, 5, 7 } },      // Sus4
    { 4, { 0, 4, 7, 11 } },  // Major 7
    { 4, { 0, 3, 7, 10 } },  // Minor 7
    { 3, { 0, 3, 6 } },      // Diminished
    { 2, { 0, 5 } },         // Fourth (the "coin" sound)
    { 3, { 0, 4, 8 } },      // Augmented (only used by Smart Chords, e.g. in harmonic minor)
};
constexpr int kNumChords = 9;   // the chord types you can pick (Augmented is internal)
constexpr int kShapeMajor = 0, kShapeMinor = 1, kShapePower = 2, kShapeDim = 7, kShapeAug = 9;
static_assert (sizeof (kChords) / sizeof (kChords[0]) == 10);

// Drum kit: one entry per note name (C, C#, D ... B) so every octave is a kit.
struct DrumDef
{
    int toneWave;      // 0 none, 1 triangle, 2 square
    float f0, f1;      // tone start / end frequency (Hz)
    float sweep;       // pitch-drop time constant (s)
    float toneDecay;   // tone volume time constant (s)
    float toneLevel;
    int noiseMode;     // 0 none, 1 long (hiss), 2 short (metallic)
    float noiseClock;  // LFSR clock (Hz) - higher = brighter
    float noiseDecay;  // noise volume time constant (s)
    float noiseLevel;
    bool clap, chokesHat, isOpenHat;
};

constexpr DrumDef kDrums[12] = {
    /* C  Kick       */ { 1, 200.0f,  45.0f, 0.040f, 0.16f, 1.00f, 1,  30000.0f, 0.008f, 0.35f, false, false, false },
    /* C# Rim        */ { 2, 1000.0f, 750.0f, 0.010f, 0.02f, 0.45f, 2,  60000.0f, 0.012f, 0.25f, false, false, false },
    /* D  Snare      */ { 1, 250.0f, 160.0f, 0.030f, 0.05f, 0.60f, 1,  45000.0f, 0.090f, 0.75f, false, false, false },
    /* D# Clap       */ { 0, 0.0f,     0.0f, 0.000f, 0.00f, 0.00f, 1,  30000.0f, 0.080f, 0.85f, true,  false, false },
    /* E  Snare 2    */ { 1, 220.0f, 190.0f, 0.020f, 0.04f, 0.40f, 2,  28000.0f, 0.110f, 0.60f, false, false, false },
    /* F  Low tom    */ { 1, 160.0f,  80.0f, 0.120f, 0.20f, 1.00f, 0,      0.0f, 0.000f, 0.00f, false, false, false },
    /* F# Closed hat */ { 0, 0.0f,     0.0f, 0.000f, 0.00f, 0.00f, 1, 220000.0f, 0.022f, 0.42f, false, true,  false },
    /* G  Mid tom    */ { 1, 230.0f, 120.0f, 0.100f, 0.17f, 1.00f, 0,      0.0f, 0.000f, 0.00f, false, false, false },
    /* G# Pedal hat  */ { 0, 0.0f,     0.0f, 0.000f, 0.00f, 0.00f, 1, 180000.0f, 0.045f, 0.38f, false, true,  false },
    /* A  High tom   */ { 1, 320.0f, 170.0f, 0.080f, 0.15f, 1.00f, 0,      0.0f, 0.000f, 0.00f, false, false, false },
    /* A# Open hat   */ { 0, 0.0f,     0.0f, 0.000f, 0.00f, 0.00f, 1, 220000.0f, 0.160f, 0.40f, false, false, true  },
    /* B  Crash      */ { 0, 0.0f,     0.0f, 0.000f, 0.00f, 0.00f, 1, 140000.0f, 0.550f, 0.50f, false, false, false },
};
constexpr float kDrumEndLevel = 1.0e-3f; // -60 dB
} // namespace

CustomWave defaultCustomWave()
{
    CustomWave w {};
    for (int i = 0; i < kCustomWaveSteps; ++i)
        w[(size_t) i] = (std::uint8_t) std::lround (7.5 + 7.5 * std::sin (2.0 * 3.14159265358979 * (i + 0.5) / kCustomWaveSteps));
    return w;
}

//==============================================================================
void ChipEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    invSr = 1.0 / sampleRate;

    for (auto& v : voices)
        v = Voice {};

    numHeld = 0;
    keyDown.fill (false);
    sustained.fill (false);
    playedAs.fill (-1);
    keyCount.fill (0);
    pedalDown = false;
    heldArpRunning = false;
    lastNote = -1;
    pitchBend = 0.0f;   // controllers start centred whenever playback restarts
    modWheel = 0.0f;
    chokeMul = (float) std::exp (-1.0 / (0.006 * sampleRate));

    paramsInitialised = false;
    builtChordOctaves = builtHeldOctaves = -1;
    smartBuilt = false;
    setParams (p);
}

void ChipEngine::setParams (const EngineParams& np)
{
    const bool modeChanged = paramsInitialised
                          && (np.arpMode != p.arpMode || np.mono != p.mono || np.console != p.console);

    p = np;
    p.arpChord   = std::clamp (p.arpChord, 0, kNumChords - 1);
    p.arpOctaves = std::clamp (p.arpOctaves, 1, 3);
    p.scaleKey   = std::clamp (p.scaleKey, 0, 11);
    if ((int) p.scale < 0 || (int) p.scale >= kNumScales)
        p.scale = Scale::Off;
    paramsInitialised = true;

    const double sr = sampleRate;
    attackStep  = (float) (1.0 / (std::max (0.001f, p.attack)  * sr));
    decayStep   = (float) (1.0 / (std::max (0.001f, p.decay)   * sr));
    releaseStep = (float) (1.0 / (std::max (0.001f, p.release) * sr));
    glideOn     = p.glide >= 0.001f;
    glideCoeff  = glideOn ? (float) (1.0 - std::exp (-(double) kControlInterval / (p.glide * 0.25 * sr))) : 1.0f;
    arpStepSamples = std::max (16.0, p.arpStepSeconds * sr);
    macroStepSamples = sr / kMacroStepsPerSecond;

    anyMacroOn = false;
    for (auto& m : p.macros)
    {
        m.length = std::clamp (m.length, 0, kMacroSteps);
        if (m.loop >= m.length)
            m.loop = -1;
        anyMacroOn = anyMacroOn || m.isOn();
    }

    if (p.arpOctaves != builtChordOctaves)
        rebuildChordSequences();

    if (! smartBuilt || p.scale != builtSmartScale)
        rebuildSmartChords();

    if (p.arpOctaves != builtHeldOctaves)
        rebuildHeldSequence();

    // Switching between poly / mono / arp modes while notes are sounding:
    // let the old notes fade out cleanly instead of getting stuck.
    if (modeChanged)
    {
        releaseAllTones();
        heldArpRunning = false;
    }
}

//==============================================================================
// Keys -> notes. With Scale Lock on, the key you press can play a different
// note (C# -> C in C major). Remember which, so its note-off stops the right
// note even if you change the scale while holding it. Two keys can land on
// the same note: it keeps sounding until both are released.
void ChipEngine::noteOn (int key, float velocity, bool useScaleLock)
{
    if (key < 0 || key > 127)
        return;

    if (velocity <= 0.0f)
    {
        noteOff (key);
        return;
    }

    if (const int old = playedAs[(size_t) key]; old >= 0)   // a repeated note-on without a note-off
    {
        playedAs[(size_t) key] = -1;
        if (keyCount[(size_t) old] > 0 && --keyCount[(size_t) old] == 0)
            stopNote (old);
    }

    const int note = (p.wave == Wave::Drums || ! useScaleLock) ? key : snapToScale (key, p.scaleKey, p.scale);
    playedAs[(size_t) key] = (std::int8_t) note;
    if (keyCount[(size_t) note] < 255)
        ++keyCount[(size_t) note];

    startNote (note, velocity);
}

void ChipEngine::noteOff (int key)
{
    if (key < 0 || key > 127)
        return;

    const int note = playedAs[(size_t) key];
    if (note < 0)
    {
        stopNote (key);   // a note-off we never saw the note-on for: treat it as itself
        return;
    }

    playedAs[(size_t) key] = -1;
    if (keyCount[(size_t) note] > 0 && --keyCount[(size_t) note] > 0)
        return;           // another key is still holding this note

    stopNote (note);
}

void ChipEngine::startNote (int note, float velocity)
{
    keyDown[(size_t) note] = true;
    sustained[(size_t) note] = false;

    if (p.wave == Wave::Drums)
    {
        auto& v = allocateVoice (note, true);
        startDrum (v, note, velocity);
        return;
    }

    const bool hadHeld = numHeld > 0;
    addHeld (note, velocity);

    if (p.arpMode == ArpMode::Notes)
    {
        rebuildHeldSequence();
        auto& v = voices[0];

        if (! heldArpRunning || ! v.active || v.isDrum || v.stage == Stage::Release)
        {
            heldArpRunning = true;
            heldArpDir = 1;
            heldArpPos = (p.arpPattern == ArpPattern::Down) ? heldSeqLen - 1 : 0;
            heldArpCounter = 0.0;
            startVoice (v, heldSeq[(size_t) heldArpPos], velocity);
        }
        else
        {
            v.velocity = velocityToGain (velocity);
        }
        return;
    }

    if (p.mono)
    {
        auto& v = voices[0];
        if (hadHeld && v.active && ! v.isDrum && v.stage != Stage::Release)
        {
            // Legato: slide to the new note without restarting the envelope
            v.targetPitch = (float) note;
            v.note = note;
            v.held = true;
            if (! glideOn)
                v.pitch = (float) note;
            v.ctrlCountdown = 0;   // apply the new pitch on the very next sample
            lastNote = note;
        }
        else
        {
            startVoice (v, note, velocity);
        }
        return;
    }

    auto& v = allocateVoice (note, false);
    startVoice (v, note, velocity);
}

void ChipEngine::stopNote (int note)
{
    keyDown[(size_t) note] = false;

    if (pedalDown)
    {
        sustained[(size_t) note] = true;
        return;
    }

    doNoteOff (note);
}

void ChipEngine::doNoteOff (int note)
{
    sustained[(size_t) note] = false;
    removeHeld (note);

    if (p.arpMode == ArpMode::Notes)
    {
        rebuildHeldSequence();
        if (numHeld == 0 && heldArpRunning)
        {
            releaseVoice (voices[0]);
            heldArpRunning = false;
        }
        return;
    }

    if (p.mono)
    {
        auto& v = voices[0];
        if (v.active && ! v.isDrum && v.note == note)
        {
            if (numHeld > 0)
            {
                // Go back to the previous key that is still held (classic mono-synth behaviour)
                const int back = held[(size_t) numHeld - 1].note;
                v.targetPitch = (float) back;
                v.note = back;
                if (! glideOn)
                    v.pitch = (float) back;
                v.ctrlCountdown = 0;
                lastNote = back;
            }
            else
            {
                releaseVoice (v);
            }
        }
        return;
    }

    for (auto& v : voices)
        if (v.active && v.held && ! v.isDrum && v.note == note)
            releaseVoice (v);
}

void ChipEngine::setSustainPedal (bool isDown)
{
    pedalDown = isDown;

    if (! isDown)
        for (int n = 0; n < 128; ++n)
            if (sustained[(size_t) n])
            {
                sustained[(size_t) n] = false;
                if (! keyDown[(size_t) n])
                    doNoteOff (n);
            }
}

void ChipEngine::allNotesOff (bool immediate)
{
    numHeld = 0;
    keyDown.fill (false);
    sustained.fill (false);
    playedAs.fill (-1);
    keyCount.fill (0);
    heldArpRunning = false;
    rebuildHeldSequence();

    for (auto& v : voices)
    {
        if (immediate)
        {
            v.active = false;
            v.stage = Stage::Idle;
            v.env = 0.0f;
        }
        else
        {
            releaseVoice (v);
        }
    }
}

int ChipEngine::getNumActiveVoices() const noexcept
{
    int n = 0;
    for (auto& v : voices)
        n += v.active ? 1 : 0;
    return n;
}

int ChipEngine::getVoiceLimit() const noexcept
{
    switch (p.console)
    {
        case Console::NES:     return 2;   // two pulse channels
        case Console::GameBoy: return 2;   // two pulse channels
        case Console::C64:     return 3;   // the SID's three voices
        case Console::Modern:  break;
    }
    return maxVoices;
}

// Real chips set pitch with a whole-number "period" or "frequency" register,
// so only certain frequencies exist. High notes on the NES come out a little
// flat or sharp - part of its sound.
float ChipEngine::chipFrequency (Console console, Wave wave, float hz) noexcept
{
    if (hz <= 0.0f || wave == Wave::Voice)   // (a recording's speed isn't a pitch register)
        return hz;

    switch (console)
    {
        case Console::NES:
        {
            constexpr double cpu = 1789773.0;   // NTSC NES CPU clock
            const double steps = wave == Wave::Triangle ? 32.0 : 16.0;   // triangle sequencer has 32 steps
            const double t = std::clamp (std::round (cpu / (steps * hz) - 1.0), wave == Wave::Triangle ? 0.0 : 8.0, 2047.0);
            return (float) (cpu / (steps * (t + 1.0)));
        }
        case Console::GameBoy:
        {
            // Pulse channels: 131072 / (2048 - x); the wave channel (drawings, triangle) runs half as fast
            const double base = (wave == Wave::Custom || wave == Wave::Triangle) ? 65536.0 : 131072.0;
            const double x = std::clamp (std::round (2048.0 - base / hz), 0.0, 2047.0);
            return (float) (base / (2048.0 - x));
        }
        case Console::C64:
        {
            constexpr double clock = 985248.0, scale = 16777216.0;   // PAL C64, 24-bit phase accumulator
            const double reg = std::clamp (std::round (hz * scale / clock), 1.0, 65535.0);
            return (float) (reg * clock / scale);
        }
        case Console::Modern: break;
    }
    return hz;
}

// Noise pitch: the NES noise channel has only 16 speeds
double ChipEngine::noiseClock (double wanted) const noexcept
{
    if (p.console != Console::NES)
        return wanted;

    static constexpr double periods[16] = { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };
    double best = 1789773.0 / periods[0];
    for (double period : periods)
    {
        const double clock = 1789773.0 / period;
        if (std::abs (std::log (clock / wanted)) < std::abs (std::log (best / wanted)))
            best = clock;
    }
    return best;
}

// The volume a real chip can make: 16 steps on the NES and Game Boy (4-bit
// volume), 256 on the C64. The NES triangle has no volume at all: on or off.
float ChipEngine::chipLevel (Voice& v) noexcept
{
    const float level = v.env * v.velocity * (anyMacroOn ? v.macroVolume : 1.0f);
    switch (p.console)
    {
        case Console::NES:
            if (p.wave == Wave::Triangle)
            {
                const bool silenced = anyMacroOn && v.macroVolume <= 0.0f;   // volume 0 is the only "volume" it has
                const float target = (silenced || v.stage == Stage::Release || v.stage == Stage::Idle) ? 0.0f : 1.0f;
                v.gate += (target - v.gate) * 0.02f;   // ~1 ms: on / off without a click
                return v.gate;
            }
            return std::round (level * 15.0f) / 15.0f;
        case Console::GameBoy: return std::round (level * 15.0f) / 15.0f;
        case Console::C64:     return std::round (level * 255.0f) / 255.0f;
        case Console::Modern:  break;
    }
    return level;
}

bool ChipEngine::isIdle() const noexcept
{
    if (heldArpRunning)
        return false;
    for (auto& v : voices)
        if (v.active)
            return false;
    return true;
}

//==============================================================================
void ChipEngine::addHeld (int note, float velocity)
{
    removeHeld (note);

    if (numHeld == (int) held.size())
    {
        std::move (held.begin() + 1, held.end(), held.begin());
        --numHeld;
    }

    held[(size_t) numHeld++] = { note, velocity };
}

void ChipEngine::removeHeld (int note)
{
    for (int i = 0; i < numHeld; ++i)
    {
        if (held[(size_t) i].note == note)
        {
            for (int k = i; k < numHeld - 1; ++k)
                held[(size_t) k] = held[(size_t) k + 1];
            --numHeld;
            return;
        }
    }
}

void ChipEngine::rebuildChordSequences()
{
    for (int shape = 0; shape < kNumShapes; ++shape)
    {
        const auto& c = kChords[shape];
        int tmp[16] {};
        int n = 0;

        for (int o = 0; o < p.arpOctaves; ++o)
            for (int i = 0; i < c.count; ++i)
            {
                const int value = c.iv[i] + 12 * o;
                bool duplicate = false;
                for (int k = 0; k < n; ++k)
                    duplicate = duplicate || tmp[k] == value;
                if (! duplicate && n < 16)
                    tmp[n++] = value;
            }

        std::sort (tmp, tmp + n);
        for (int i = 0; i < n; ++i)
            chordSeqs[(size_t) shape][(size_t) i] = tmp[i];
        chordSeqLens[(size_t) shape] = std::max (1, n);
    }

    builtChordOctaves = p.arpOctaves;
}

// Smart Chords: the chord that belongs on each note of the scale, e.g. in
// C major C -> C major, D -> D minor, B -> B diminished. Notes outside the
// scale get a power chord (root + fifth), which never clashes.
void ChipEngine::rebuildSmartChords()
{
    for (int degree = 0; degree < 12; ++degree)
    {
        switch (triadOn (degree, p.scale))
        {
            case Triad::Major:      smartShape[(size_t) degree] = kShapeMajor; break;
            case Triad::Minor:      smartShape[(size_t) degree] = kShapeMinor; break;
            case Triad::Diminished: smartShape[(size_t) degree] = kShapeDim;   break;
            case Triad::Augmented:  smartShape[(size_t) degree] = kShapeAug;   break;
            case Triad::Outside:    smartShape[(size_t) degree] = kShapePower; break;
        }
    }
    builtSmartScale = p.scale;
    smartBuilt = true;
}

int ChipEngine::chordShapeFor (int note) const noexcept
{
    return p.smartChords ? smartShape[(size_t) degreeOf (note, p.scaleKey)] : p.arpChord;
}

void ChipEngine::rebuildHeldSequence()
{
    int sorted[32] {};
    for (int i = 0; i < numHeld; ++i)
        sorted[i] = held[(size_t) i].note;
    std::sort (sorted, sorted + numHeld);

    heldSeqLen = 0;
    for (int o = 0; o < p.arpOctaves; ++o)
        for (int i = 0; i < numHeld; ++i)
        {
            const int n = sorted[i] + 12 * o;
            if (n <= 127 && heldSeqLen < (int) heldSeq.size())
                heldSeq[(size_t) heldSeqLen++] = n;
        }

    builtHeldOctaves = p.arpOctaves;
}

int ChipEngine::nextIndex (int idx, int& dir, int len, ArpPattern pattern, std::uint32_t& rng) noexcept
{
    if (len <= 1)
        return 0;

    idx = std::clamp (idx, 0, len - 1);

    switch (pattern)
    {
        case ArpPattern::Up:   return (idx + 1) % len;
        case ArpPattern::Down: return (idx - 1 + len) % len;
        case ArpPattern::UpDown:
        {
            int n = idx + dir;
            if (n >= len) { dir = -1; n = len - 2; }
            else if (n < 0) { dir = 1; n = 1; }
            return n;
        }
        case ArpPattern::Random:
        {
            rng = rng * 1664525u + 1013904223u;
            int n = (int) ((rng >> 16) % (std::uint32_t) len);
            if (n == idx)
                n = (n + 1) % len;
            return n;
        }
        case ArpPattern::Once: return std::min (idx + 1, len - 1);
    }
    return 0;
}

//==============================================================================
ChipEngine::Voice& ChipEngine::allocateVoice (int note, bool drum)
{
    // Console modes only have a few channels for notes (drums keep the whole kit)
    const auto first = voices.begin();
    const auto last = drum ? voices.end() : voices.begin() + getVoiceLimit();

    // 1. The same note (or same drum) is still sounding -> retrigger that voice
    for (auto it = first; it != last; ++it)
        if (it->active && it->isDrum == drum && (drum ? it->drumType == note % 12 : it->note == note))
            return *it;

    // 2. A free voice
    for (auto it = first; it != last; ++it)
        if (! it->active)
            return *it;

    // 3. Steal the oldest voice whose key is already released...
    Voice* best = nullptr;
    for (auto it = first; it != last; ++it)
        if (! it->held && (best == nullptr || it->age < best->age))
            best = &*it;

    if (best != nullptr)
        return *best;

    // 4. ...or simply the oldest voice
    for (auto it = first; it != last; ++it)
        if (best == nullptr || it->age < best->age)
            best = &*it;

    return *best;
}

void ChipEngine::startVoice (Voice& v, int note, float velocity)
{
    const bool wasActive = v.active && ! v.isDrum;

    v.active = true;
    v.held = true;
    v.isDrum = false;
    v.note = note;
    v.velocity = velocityToGain (velocity);
    v.age = ++ageCounter;

    v.targetPitch = (float) note;
    if (glideOn && lastNote >= 0)
        v.pitch = wasActive ? v.pitch : (float) lastNote;   // glide in from the last note
    else
        v.pitch = (float) note;

    v.t = 0.0;
    v.ctrlCountdown = 0;
    if (! wasActive)
    {
        v.phase = 0.0;
        v.env = 0.0f;
        v.gate = 0.0f;
        v.lfsr = 1;
        v.noiseClockPhase = 0.0;
    }
    v.lfoPhase = 0.25;
    v.pwmPhase = 0.0;
    v.stage = Stage::Attack;
    if (p.wave == Wave::Voice)
        restartVoiceSample (v);

    v.arpCounter = 0.0;
    v.arpDir = 1;
    v.arpIndex = (p.arpPattern == ArpPattern::Down) ? chordSeqLens[(size_t) chordShapeFor (note)] - 1 : 0;
    v.rng = 0x9E3779B9u * (std::uint32_t) (note + 1) + (std::uint32_t) ageCounter;

    v.choking = false;
    v.chokeGain = 1.0f;
    restartMacros (v);
    lastNote = note;

    setPan (v, p.wide ? (nextSideRight ? 1 : -1) : 0);
    nextSideRight = ! nextSideRight;
}

void ChipEngine::retrigger (Voice& v, int note)
{
    v.targetPitch = (float) note;
    v.note = note;
    if (! glideOn)
        v.pitch = (float) note;
    v.t = 0.0;
    v.ctrlCountdown = 0;
    v.lfoPhase = 0.25;
    v.stage = Stage::Attack;   // restart from the current level -> no click
    if (p.wave == Wave::Voice)
        restartVoiceSample (v);
    v.held = true;
    restartMacros (v);
    lastNote = note;
}

// Kept out of the per-sample code: it only runs 60 times a second
#if defined (_MSC_VER)
 #define BB_NOINLINE __declspec (noinline)
#else
 #define BB_NOINLINE __attribute__ ((noinline))
#endif

BB_NOINLINE void ChipEngine::nextMacroFrame (Voice& v) noexcept
{
    v.macroCounter -= macroStepSamples;
    if (v.macroFrame < 100000)
        ++v.macroFrame;
    applyMacroFrame (v);
}

// Macros start from step 0 on every new note (like FamiTracker)
void ChipEngine::restartMacros (Voice& v) noexcept
{
    v.macroFrame = 0;
    v.macroCounter = 0.0;
    applyMacroFrame (v);
}

void ChipEngine::applyMacroFrame (Voice& v) noexcept
{
    const auto& m = p.macros;
    v.macroVolume = m[kMacroVolume].isOn() ? (float) std::clamp (m[kMacroVolume].valueAt (v.macroFrame), 0, 15) / 15.0f : 1.0f;
    v.macroNotes  = (m[kMacroArp].isOn()   ? (float) m[kMacroArp].valueAt (v.macroFrame) : 0.0f)
                  + (m[kMacroPitch].isOn() ? (float) m[kMacroPitch].valueAt (v.macroFrame) * 0.125f : 0.0f);
    v.macroDuty   = m[kMacroDuty].isOn() ? std::clamp (m[kMacroDuty].valueAt (v.macroFrame), 0, 3) : -1;
}

void ChipEngine::startDrum (Voice& v, int note, float velocity)
{
    const int type = note % 12;
    const auto& d = kDrums[type];

    // A closed / pedal hi-hat cuts off a ringing open hi-hat, like a real drummer
    if (d.chokesHat)
        for (auto& other : voices)
            if (other.active && other.isDrum && kDrums[other.drumType].isOpenHat)
                other.choking = true;

    v = Voice {};
    v.active = true;
    v.held = false;
    v.isDrum = true;
    v.drumType = type;
    v.note = note;
    // Drum kit sides: kick, snares, clap and mid tom in the middle; hi-hats and high tom
    // on the right; rim, low tom and crash on the left
    static constexpr int drumSides[12] = { 0, -1, 0, 0, 0, -1, 1, 0, 1, 1, 1, -1 };
    setPan (v, drumSides[type]);
    v.velocity = velocityToGain (velocity);
    v.age = ++ageCounter;

    v.dToneEnv  = d.toneWave  != 0 ? 1.0f : 0.0f;
    v.dNoiseEnv = d.noiseMode != 0 ? 1.0f : 0.0f;
    v.dSweepEnv = 1.0f;

    auto multiplierFor = [this] (float tau)
    {
        return tau > 0.0f ? (float) std::exp (-1.0 / (tau * sampleRate)) : 0.0f;
    };

    v.dToneMul  = multiplierFor (d.toneDecay);
    v.dNoiseMul = multiplierFor (d.noiseDecay);
    v.dSweepMul = multiplierFor (d.sweep);
}

void ChipEngine::releaseVoice (Voice& v) noexcept
{
    v.held = false;
    if (v.isDrum)
        return;   // drums are one-shots: they ring out on their own
    if (v.active && v.stage != Stage::Idle)
        v.stage = Stage::Release;
}

void ChipEngine::releaseAllTones() noexcept
{
    for (auto& v : voices)
        if (v.active && ! v.isDrum)
            releaseVoice (v);
}

//==============================================================================
bool ChipEngine::render (float* out, int numSamples) noexcept
{
    if (isIdle())
        return false;   // nothing playing: costs nothing

    for (int i = 0; i < numSamples; ++i)
    {
        if (heldArpRunning)
            tickHeldArp();

        float sum = 0.0f;
        for (auto& v : voices)
            if (v.active)
                sum += v.isDrum ? renderDrum (v) : renderTone (v);

        out[i] += sum;
    }
    return true;
}

bool ChipEngine::render (float* left, float* right, int numSamples) noexcept
{
    if (isIdle())
        return false;

    for (int i = 0; i < numSamples; ++i)
    {
        if (heldArpRunning)
            tickHeldArp();

        float sumL = 0.0f, sumR = 0.0f;
        for (auto& v : voices)
            if (v.active)
            {
                const float s = v.isDrum ? renderDrum (v) : renderTone (v);
                sumL += s * v.panL;
                sumR += s * v.panR;
            }

        left[i] += sumL;
        right[i] += sumR;
    }
    return true;
}

void ChipEngine::setPan (Voice& v, int side) const noexcept
{
    // Off to one side but not "hard" (that tires your ears on headphones); same loudness as the middle
    static constexpr float near = 1.3f, far = 0.5f;
    const int s = p.wide ? side : 0;
    v.panL = s < 0 ? near : (s > 0 ? far : 1.0f);
    v.panR = s > 0 ? near : (s < 0 ? far : 1.0f);
}

void ChipEngine::tickHeldArp() noexcept
{
    heldArpCounter += 1.0;
    if (heldArpCounter < arpStepSamples)
        return;

    heldArpCounter -= arpStepSamples;

    if (heldSeqLen <= 0)
        return;

    auto& v = voices[0];
    if (! v.active || v.isDrum)
    {
        heldArpRunning = false;
        return;
    }

    const int old = std::min (heldArpPos, heldSeqLen - 1);
    heldArpPos = nextIndex (old, heldArpDir, heldSeqLen, p.arpPattern, heldRng);

    if (heldArpPos != old || p.arpPattern != ArpPattern::Once)
        retrigger (v, heldSeq[(size_t) heldArpPos]);
}

float ChipEngine::renderTone (Voice& v) noexcept
{
    // --- Volume envelope (linear, like the chips' volume counters) ---
    switch (v.stage)
    {
        case Stage::Attack:
            v.env += attackStep;
            if (v.env >= 1.0f) { v.env = 1.0f; v.stage = Stage::Decay; }
            break;
        case Stage::Decay:
            v.env -= decayStep;
            if (v.env <= p.sustain) { v.env = p.sustain; v.stage = Stage::Sustain; }
            break;
        case Stage::Sustain:
            v.env = p.sustain;
            break;
        case Stage::Release:
            v.env -= releaseStep;
            if (v.env <= 0.0f)
            {
                v.env = 0.0f;
                v.stage = Stage::Idle;
                v.active = false;
                return 0.0f;
            }
            break;
        case Stage::Idle:
            v.active = false;
            return 0.0f;
    }

    // --- Pitch: glide / sweep / vibrato at control rate, arpeggio per sample ---
    if (--v.ctrlCountdown <= 0)
    {
        v.ctrlCountdown = kControlInterval;
        updateModulation (v);
    }

    float note = v.ctrlNote;

    // Instrument macros: step on 60 times a second
    if (anyMacroOn)
    {
        v.macroCounter += 1.0;
        if (v.macroCounter >= macroStepSamples)
            nextMacroFrame (v);
        note += v.macroNotes;
    }

    // Chord arpeggio: flip through the chord notes, without restarting the note
    if (p.arpMode == ArpMode::Chord)
    {
        const auto shape = (size_t) chordShapeFor (v.note);
        const auto& seq = chordSeqs[shape];
        const int len = chordSeqLens[shape];
        v.arpCounter += 1.0;
        if (v.arpCounter >= arpStepSamples)
        {
            v.arpCounter -= arpStepSamples;
            v.arpIndex = nextIndex (v.arpIndex, v.arpDir, len, p.arpPattern, v.rng);
        }
        note += (float) seq[(size_t) std::clamp (v.arpIndex, 0, len - 1)];
    }

    // The exponential note -> frequency conversion only runs when the pitch changes
    if (note != v.cachedNote)
    {
        v.cachedNote = note;
        v.hz = chipFrequency (p.console, p.wave, noteToHz (note));
        v.inc = std::min (v.hz * (float) invSr, 0.45f);
    }

    const float s = oscillate (v);
    v.t += invSr;

    // The common case (no console limits, no macros) skips the chip-volume logic
    const bool plain = p.console == Console::Modern && ! anyMacroOn;
    return s * (plain ? v.env * v.velocity : chipLevel (v)) * kVoiceGain;
}

void ChipEngine::updateModulation (Voice& v) noexcept
{
    if (glideOn)
        v.pitch += (v.targetPitch - v.pitch) * glideCoeff;
    else
        v.pitch = v.targetPitch;

    float note = v.pitch + pitchBend;
    const auto t = (float) v.t;

    // Pitch sweep (+ = a falling sound, - = a rising one)
    if (p.sweepAmt != 0.0f)
    {
        switch (p.sweepMode)
        {
            case SweepMode::Slide:     // start sweepAmt semitones away and slide into the note
                if (t < p.sweepTime)
                {
                    const float x = 1.0f - t / p.sweepTime;
                    note += p.sweepAmt * x * x;
                }
                break;
            case SweepMode::Forever:   // sweepAmt semitones every sweepTime seconds, until the note ends
                note -= p.sweepAmt * t / p.sweepTime;
                if ((note < 0.0f || note > 120.0f) && v.stage != Stage::Release)
                {
                    // Out of range: like the NES, the channel simply goes quiet (a quick fade, no click)
                    v.stage = Stage::Release;
                    v.env = std::min (v.env, 0.02f);
                }
                break;
            case SweepMode::Bounce:    // away and back again, over and over
            {
                const float x = std::fmod (t / p.sweepTime, 2.0f);
                note -= p.sweepAmt * (x < 1.0f ? x : 2.0f - x);
                break;
            }
        }
        note = std::clamp (note, -12.0f, 135.0f);   // stays below 20 kHz however far it slides
    }

    // Vibrato: waits vibDelay seconds, then fades in quickly. Mod wheel adds more.
    float vib = modWheel;
    if (p.vibDepth > 0.0f && t > p.vibDelay)
        vib += p.vibDepth * std::min (1.0f, (t - p.vibDelay) * 12.0f);

    if (vib > 0.0f)
    {
        const double before = v.lfoPhase;
        v.lfoPhase += p.vibRate * invSr * kControlInterval;
        if (v.lfoPhase >= 1.0)
            v.lfoPhase -= 1.0;

        switch (p.vibShape)
        {
            case VibShape::Smooth:
                note += vib * triLfo (v.lfoPhase);
                break;
            case VibShape::Trill:    // the note, then up by twice the depth (VIBRATO 1.0 = a whole tone)
                note += v.lfoPhase < 0.5 ? 0.0f : 2.0f * vib;
                break;
            case VibShape::Random:   // a new random height every half wobble
                if ((before < 0.5) != (v.lfoPhase < 0.5))
                {
                    v.vibRng = v.vibRng * 1664525u + 1013904223u;
                    v.vibHold = (float) (v.vibRng >> 8) * (2.0f / 16777216.0f) - 1.0f;
                }
                note += vib * v.vibHold;
                break;
        }
    }

    v.ctrlNote = note;
}

float ChipEngine::oscillate (Voice& v) noexcept
{
    const float dt = v.inc;
    const float hz = v.hz;
    float out = 0.0f;

    switch (p.wave)
    {
        case Wave::Square:
        case Wave::Pulse25:
        case Wave::Pulse12:
        {
            static constexpr float macroDuties[4] = { 0.125f, 0.25f, 0.5f, 0.75f };
            float duty = v.macroDuty >= 0 ? macroDuties[v.macroDuty]
                                          : (p.wave == Wave::Square ? 0.5f : (p.wave == Wave::Pulse25 ? 0.25f : 0.125f));

            if (p.pwm > 0.0f)
            {
                v.pwmPhase += kPwmRate * invSr;
                if (v.pwmPhase >= 1.0)
                    v.pwmPhase -= 1.0;
                duty += p.pwm * 0.4f * (0.5f + 0.5f * triLfo (v.pwmPhase));
            }

            duty = std::clamp (duty, 0.02f, 0.98f);
            const auto ph = (float) v.phase;

            out = ph < duty ? 1.0f : -1.0f;
            out += polyBlep (ph, dt);                 // rising edge
            float ph2 = ph - duty;
            if (ph2 < 0.0f)
                ph2 += 1.0f;
            out -= polyBlep (ph2, dt);                // falling edge
            out -= (2.0f * duty - 1.0f);              // remove DC offset of narrow pulses
            break;
        }

        case Wave::Triangle:
            out = steppedTriangle (v.phase);
            break;

        case Wave::Saw:
        {
            const auto ph = (float) v.phase;
            out = 2.0f * ph - 1.0f - polyBlep (ph, dt);
            break;
        }

        // Noise pitch follows the key: clock = note frequency x 32
        case Wave::Noise: out = noiseStep (v, noiseClock (hz * 32.0), false); break;

        // Short-mode LFSR repeats every 93 steps -> clock at 93 x frequency = in tune
        case Wave::Metal: out = noiseStep (v, noiseClock (hz * 93.0), true); break;

        // Your own drawing, played back step by step like the Game Boy (no smoothing: that's the charm)
        case Wave::Custom:
            out = (float) p.customWave[(size_t) ((int) (v.phase * kCustomWaveSteps) & (kCustomWaveSteps - 1))] / 7.5f - 1.0f;
            break;

        case Wave::Voice:
            return playVoice (v);   // (moves through the recording itself, no looping phase)

        case Wave::Drums: break;
    }

    v.phase += dt;
    if (v.phase >= 1.0)
        v.phase -= 1.0;

    return out;
}

float ChipEngine::playVoice (Voice& v) noexcept
{
    const int length = p.voice != nullptr ? p.voice->getLength() : 0;
    const int index = (int) v.phase;   // VOICE: phase counts samples of the recording
    if (index >= length)
    {
        // The recording is over, so the note is too (a quick fade, no click)
        if (v.stage != Stage::Release)
        {
            v.stage = Stage::Release;
            v.env = std::min (v.env, 0.02f);
        }
        return v.voiceOut;
    }

    if (index != v.voiceIndex)
    {
        v.voiceIndex = index;
        const float x = p.voice->at (index);
        switch (p.console)
        {
            case Console::NES:       // DPCM: a 1-bit delta - each sample the level can only step up or down
                v.dpcm = std::clamp (v.dpcm + (x > v.dpcm ? 1.0f / 32.0f : -1.0f / 32.0f), -1.0f, 1.0f);
                v.voiceOut = v.dpcm;
                break;
            case Console::GameBoy:   // 4-bit samples (the wave channel / the C64 volume trick)
            case Console::C64:
                v.voiceOut = std::round ((x + 1.0f) * 7.5f) / 7.5f - 1.0f;
                break;
            case Console::Modern:
                v.voiceOut = x;
                break;
        }
    }

    // Middle C plays the recording at its own speed; other keys faster or slower
    constexpr double middleC = 261.6255653;
    v.phase += (double) v.hz / middleC * (VoiceSample::rate * invSr);
    return v.voiceOut;
}

// NES-style noise: a 15-bit linear-feedback shift register.
// Long mode taps bit 1 (period 32767 -> hiss), short mode taps bit 6
// (period 93 -> buzzy metallic tone).
float ChipEngine::noiseStep (Voice& v, double clockHz, bool shortMode) noexcept
{
    v.noiseClockPhase += clockHz * invSr;
    auto steps = (int) v.noiseClockPhase;

    if (steps > 0)
    {
        v.noiseClockPhase -= steps;
        steps = std::min (steps, 64);
        const int tap = shortMode ? 6 : 1;

        for (int k = 0; k < steps; ++k)
        {
            const std::uint32_t bit = (v.lfsr ^ (v.lfsr >> tap)) & 1u;
            v.lfsr = (v.lfsr >> 1) | (bit << 14);
        }
    }

    return (v.lfsr & 1u) ? -1.0f : 1.0f;
}

float ChipEngine::renderDrum (Voice& v) noexcept
{
    const auto& d = kDrums[v.drumType];
    float s = 0.0f;

    if (d.toneWave != 0)
    {
        const float f = d.f1 + (d.f0 - d.f1) * v.dSweepEnv;   // pitch drops from f0 to f1
        v.dSweepEnv *= v.dSweepMul;

        const float tone = d.toneWave == 1 ? steppedTriangle (v.phase) : (v.phase < 0.5 ? 1.0f : -1.0f);
        v.phase += f * invSr;
        if (v.phase >= 1.0)
            v.phase -= 1.0;

        s += tone * v.dToneEnv * d.toneLevel;
        v.dToneEnv *= v.dToneMul;
    }

    if (d.noiseMode != 0)
    {
        const float nz = noiseStep (v, d.noiseClock, d.noiseMode == 2);
        float amp = v.dNoiseEnv;

        if (d.clap && v.t < 0.032)   // a clap is a few quick bursts of noise
            amp *= (std::fmod (v.t, 0.0105) < 0.005) ? 1.0f : 0.15f;

        s += nz * amp * d.noiseLevel;
        v.dNoiseEnv *= v.dNoiseMul;
    }

    if (v.choking)
        v.chokeGain *= chokeMul;

    s *= v.chokeGain;
    v.t += invSr;

    const bool toneDone  = d.toneWave == 0  || v.dToneEnv  < kDrumEndLevel;
    const bool noiseDone = d.noiseMode == 0 || v.dNoiseEnv < kDrumEndLevel;
    if ((toneDone && noiseDone) || v.chokeGain < kDrumEndLevel)
        v.active = false;

    return s * v.velocity * kVoiceGain;
}
} // namespace bb
