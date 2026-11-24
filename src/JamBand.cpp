#include "JamBand.h"

#include <algorithm>
#include <cmath>

namespace bb
{
namespace
{
struct Style
{
    const char* name;
    double tempo;
    int progression[4];       // chord on each bar, as a degree of the scale (0 = home chord)
    const char* kick;         // 16 steps per bar: x = hit
    const char* snare;
    const char* hat;          // x = closed hat, o = open hat
    const char* bassline;     // R = root, O = octave up, F = fifth, - = hold, . = rest
};

// Degrees: Adventure I V vi IV, Dungeon i iv i v, Racing I IV V IV, Chill I vi IV V
const Style kStyles[kNumJamStyles] = {
    { "Adventure", 120.0, { 0, 4, 5, 3 },
      "x.......x.x.....", "....x.......x...", "x.x.x.x.x.x.x.xo", "R.R.R.O.R.R.R.O." },
    { "Dungeon",   96.0,  { 0, 3, 0, 4 },
      "x.........x.....", "........x.......", "x...x...x...x...", "R-----..F-----.." },
    { "Racing",    152.0, { 0, 3, 4, 3 },
      "x...x...x...x...", "....x.......x...", "xxoxxxoxxxoxxxox", "RORORORORORORORO" },
    { "Chill",     88.0,  { 0, 5, 3, 4 },
      "x......x..x.....", "....x.......x...", "x.x.x.xox.x.x.x.", "R-----.R..F---.." },
};

// The drum kit's notes (see ChipEngine: every C is a kick...)
constexpr int kKick = 36, kSnare = 38, kClosedHat = 42, kOpenHat = 46, kCrash = 47;
} // namespace

const char* jamStyleName (int s)  { return kStyles[std::clamp (s, 0, kNumJamStyles - 1)].name; }
double jamStyleTempo (int s)      { return kStyles[std::clamp (s, 0, kNumJamStyles - 1)].tempo; }

void JamBand::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    for (auto* e : { &drums, &bass, &chords })
        e->prepare (sampleRate);
    configured = false;
    configureEngines();
    setTempo (bpm);
    playing = false;
    step = -1;
    chunkPos = chunkLength = kChunk;
}

void JamBand::configureEngines() noexcept
{
    EngineParams d;
    d.wave = Wave::Drums;
    drums.setParams (d);

    bass.setParams (bandBassSound());

    EngineParams c;
    c.wave = Wave::Pulse25;
    c.attack = 0.002f; c.decay = 0.4f; c.sustain = 0.55f; c.release = 0.15f;
    c.arpMode = ArpMode::Chord;
    c.smartChords = true;                  // the chord that belongs to each note of your scale
    c.scaleKey = key;
    c.scale = harmonyScale (scale == Scale::Off ? Scale::Major : scale);   // 7-note scale: every chord root is in it
    c.arpStepSeconds = 1.0 / 30.0;         // the classic 30 Hz chip chord
    c.vibDepth = 0.08f; c.vibDelay = 0.3f;
    chords.setParams (c);
    configured = true;
}

void JamBand::setSong (int newKey, Scale newScale) noexcept
{
    newKey = std::clamp (newKey, 0, 11);
    if (newKey != key || newScale != scale || ! configured)
    {
        key = newKey;
        scale = newScale;
        configureEngines();
    }
}

void JamBand::setStyle (int newStyle) noexcept { style = std::clamp (newStyle, 0, kNumJamStyles - 1); }

void JamBand::setTempo (double newBpm) noexcept
{
    bpm = std::clamp (newBpm, 40.0, 240.0);
    samplesPerStep = sampleRate * 60.0 / bpm / 4.0;   // 16th notes
}

void JamBand::setTrackLevels (float d, float b, float c) noexcept
{
    drumLevel = d; bassLevel = b; chordLevel = c;
}

void JamBand::start() noexcept
{
    if (playing)
        return;
    playing = true;
    step = -1;
    stepCounter = samplesPerStep;   // play step 0 straight away
}

void JamBand::stop() noexcept
{
    if (! playing)
        return;
    playing = false;
    for (auto* e : { &drums, &bass, &chords })
        e->allNotesOff (false);
    bassNote = chordNote = -1;
    bassOffStep = -1;
}

bool JamBand::isSounding() const noexcept
{
    return playing || ! drums.isIdle() || ! bass.isIdle() || ! chords.isIdle();
}

int JamBand::chordRoot (int bar) const noexcept
{
    // The scale the chords are built from (pentatonic / blues borrow from their parent scale)
    const int mask = scaleMask (harmonyScale (scale == Scale::Off ? Scale::Major : scale));
    int notes[12] {}, count = 0;
    for (int pc = 0; pc < 12; ++pc)
        if ((mask >> pc) & 1)
            notes[count++] = pc;
    const int degree = kStyles[style].progression[bar & 3] % std::max (1, count);
    return (key + notes[degree]) % 12;
}

void JamBand::playStep() noexcept
{
    step = (step + 1) % 64;
    const int bar = step / 16, s = step % 16;
    const auto& st = kStyles[style];

    // --- drums (one-shots: note-off straight after keeps the key bookkeeping tidy) ---
    auto hit = [this] (int note, float velocity)
    {
        drums.noteOn (note, velocity);
        drums.noteOff (note);
    };
    if (step == 0)
        hit (kCrash, 0.6f);
    if (st.kick[s] == 'x')
        hit (kKick, 1.0f);
    if (st.snare[s] == 'x')
        hit (kSnare, 0.85f);
    if (st.hat[s] == 'x')
        hit (kClosedHat, s % 4 == 0 ? 0.6f : 0.4f);
    else if (st.hat[s] == 'o')
        hit (kOpenHat, 0.45f);

    // --- bass ---
    const int root = chordRoot (bar);
    if (bassNote >= 0 && (step == bassOffStep || st.bassline[s] != '-'))
    {
        bass.noteOff (bassNote);
        bassNote = -1;
    }
    const char b = st.bassline[s];
    if (b == 'R' || b == 'O' || b == 'F')
    {
        bassNote = 36 + root + (b == 'O' ? 12 : 0) + (b == 'F' ? 7 : 0);
        bass.noteOn (bassNote, 0.9f);
        int length = 1;
        while (s + length < 16 && st.bassline[s + length] == '-')
            ++length;
        bassOffStep = (step + length) % 64;
    }

    // --- chords: one held chord per bar ---
    if (s == 0)
    {
        if (chordNote >= 0)
            chords.noteOff (chordNote);
        chordNote = 60 + root - (root > 7 ? 12 : 0);   // keep chords around middle C
        chords.noteOn (chordNote, 0.7f);
    }
    else if (s == 15 && chordNote >= 0)
    {
        chords.noteOff (chordNote);   // a short gap before the next chord
        chordNote = -1;
    }
}

void JamBand::renderChunk() noexcept
{
    chunk.fill (0.0f);
    chunkLength = kChunk;
    chunkPos = 0;

    // Split the chunk at step boundaries so every hit lands on the right sample
    int pos = 0;
    while (pos < kChunk)
    {
        if (playing && stepCounter >= samplesPerStep)
        {
            stepCounter -= samplesPerStep;
            playStep();
        }
        int n = kChunk - pos;
        if (playing)
            n = std::clamp ((int) std::ceil (samplesPerStep - stepCounter), 1, n);

        float d[kChunk] {}, b[kChunk] {}, c[kChunk] {};
        drums.render (d, n);
        bass.render (b, n);
        chords.render (c, n);
        for (int i = 0; i < n; ++i)
            chunk[(size_t) (pos + i)] = 0.5f * (drumLevel * d[i] + bassLevel * 1.1f * b[i] + chordLevel * 0.5f * c[i]);   // headroom for YOUR playing

        if (playing)
            stepCounter += n;
        pos += n;
    }
}
} // namespace bb
