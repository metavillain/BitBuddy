#include "ChipEngine.h"

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
};
constexpr int kNumChords = (int) (sizeof (kChords) / sizeof (kChords[0]));

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
    pedalDown = false;
    heldArpRunning = false;
    lastNote = -1;
    pitchBend = 0.0f;   // controllers start centred whenever playback restarts
    modWheel = 0.0f;
    chokeMul = (float) std::exp (-1.0 / (0.006 * sampleRate));

    paramsInitialised = false;
    builtChord = builtChordOctaves = builtHeldOctaves = -1;
    setParams (p);
}

void ChipEngine::setParams (const EngineParams& np)
{
    const bool modeChanged = paramsInitialised && (np.arpMode != p.arpMode || np.mono != p.mono);

    p = np;
    p.arpChord   = std::clamp (p.arpChord, 0, kNumChords - 1);
    p.arpOctaves = std::clamp (p.arpOctaves, 1, 3);
    paramsInitialised = true;

    const double sr = sampleRate;
    attackStep  = (float) (1.0 / (std::max (0.001f, p.attack)  * sr));
    decayStep   = (float) (1.0 / (std::max (0.001f, p.decay)   * sr));
    releaseStep = (float) (1.0 / (std::max (0.001f, p.release) * sr));
    glideOn     = p.glide >= 0.001f;
    glideCoeff  = glideOn ? (float) (1.0 - std::exp (-(double) kControlInterval / (p.glide * 0.25 * sr))) : 1.0f;
    arpStepSamples = std::max (16.0, p.arpStepSeconds * sr);

    if (p.arpChord != builtChord || p.arpOctaves != builtChordOctaves)
        rebuildChordSequence();

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
void ChipEngine::noteOn (int note, float velocity)
{
    if (note < 0 || note > 127)
        return;

    if (velocity <= 0.0f)
    {
        noteOff (note);
        return;
    }

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

void ChipEngine::noteOff (int note)
{
    if (note < 0 || note > 127)
        return;

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

void ChipEngine::rebuildChordSequence()
{
    const auto& c = kChords[p.arpChord];
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
        chordSeq[(size_t) i] = tmp[i];

    chordSeqLen = std::max (1, n);
    builtChord = p.arpChord;
    builtChordOctaves = p.arpOctaves;
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
    // 1. The same note (or same drum) is still sounding -> retrigger that voice
    for (auto& v : voices)
        if (v.active && v.isDrum == drum && (drum ? v.drumType == note % 12 : v.note == note))
            return v;

    // 2. A free voice
    for (auto& v : voices)
        if (! v.active)
            return v;

    // 3. Steal the oldest voice whose key is already released...
    Voice* best = nullptr;
    for (auto& v : voices)
        if (! v.held && (best == nullptr || v.age < best->age))
            best = &v;

    if (best != nullptr)
        return *best;

    // 4. ...or simply the oldest voice
    for (auto& v : voices)
        if (best == nullptr || v.age < best->age)
            best = &v;

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
        v.lfsr = 1;
        v.noiseClockPhase = 0.0;
    }
    v.lfoPhase = 0.25;
    v.pwmPhase = 0.0;
    v.stage = Stage::Attack;

    v.arpCounter = 0.0;
    v.arpDir = 1;
    v.arpIndex = (p.arpPattern == ArpPattern::Down) ? chordSeqLen - 1 : 0;
    v.rng = 0x9E3779B9u * (std::uint32_t) (note + 1) + (std::uint32_t) ageCounter;

    v.choking = false;
    v.chokeGain = 1.0f;
    lastNote = note;
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
    v.held = true;
    lastNote = note;
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

    // Chord arpeggio: flip through the chord notes, without restarting the note
    if (p.arpMode == ArpMode::Chord)
    {
        v.arpCounter += 1.0;
        if (v.arpCounter >= arpStepSamples)
        {
            v.arpCounter -= arpStepSamples;
            v.arpIndex = nextIndex (v.arpIndex, v.arpDir, chordSeqLen, p.arpPattern, v.rng);
        }
        note += (float) chordSeq[(size_t) std::clamp (v.arpIndex, 0, chordSeqLen - 1)];
    }

    // The exponential note -> frequency conversion only runs when the pitch changes
    if (note != v.cachedNote)
    {
        v.cachedNote = note;
        v.hz = noteToHz (note);
        v.inc = std::min (v.hz * (float) invSr, 0.45f);
    }

    const float s = oscillate (v);
    v.t += invSr;

    return s * v.env * v.velocity * kVoiceGain;
}

void ChipEngine::updateModulation (Voice& v) noexcept
{
    if (glideOn)
        v.pitch += (v.targetPitch - v.pitch) * glideCoeff;
    else
        v.pitch = v.targetPitch;

    float note = v.pitch + pitchBend;
    const auto t = (float) v.t;

    // Pitch sweep: start sweepAmt semitones away and slide into the note
    if (p.sweepAmt != 0.0f && t < p.sweepTime)
    {
        const float x = 1.0f - t / p.sweepTime;
        note += p.sweepAmt * x * x;
    }

    // Vibrato: waits vibDelay seconds, then fades in quickly. Mod wheel adds more.
    float vib = modWheel;
    if (p.vibDepth > 0.0f && t > p.vibDelay)
        vib += p.vibDepth * std::min (1.0f, (t - p.vibDelay) * 12.0f);

    if (vib > 0.0f)
    {
        v.lfoPhase += p.vibRate * invSr * kControlInterval;
        if (v.lfoPhase >= 1.0)
            v.lfoPhase -= 1.0;
        note += vib * triLfo (v.lfoPhase);
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
            float duty = p.wave == Wave::Square ? 0.5f : (p.wave == Wave::Pulse25 ? 0.25f : 0.125f);

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
        case Wave::Noise: out = noiseStep (v, hz * 32.0, false); break;

        // Short-mode LFSR repeats every 93 steps -> clock at 93 x frequency = in tune
        case Wave::Metal: out = noiseStep (v, hz * 93.0, true); break;

        case Wave::Drums: break;
    }

    v.phase += dt;
    if (v.phase >= 1.0)
        v.phase -= 1.0;

    return out;
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
