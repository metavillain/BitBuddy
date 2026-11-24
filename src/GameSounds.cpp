#include "GameSounds.h"
#include "Parameters.h"

namespace bb
{
namespace
{
// Choice indices (see Parameters.cpp)
namespace w   { constexpr float square = 0, pulse25 = 1, pulse12 = 2, triangle = 3, saw = 4, noise = 5; }
namespace ch  { constexpr float major = 0, octave = 3, sus4 = 4, maj7 = 5, fourth = 8; }
namespace pat { constexpr float up = 0, once = 4; }
namespace sp  { constexpr float hz15 = 5, hz30 = 6, hz60 = 7; }
constexpr float chordArp = 1.0f, forever = 1.0f;

struct Roll
{
    juce::Random& rng;
    float range (float lo, float hi) const { return lo + rng.nextFloat() * (hi - lo); }
    int between (int lo, int hi) const     { return lo + rng.nextInt (hi - lo + 1); }
    bool chance (float p) const            { return rng.nextFloat() < p; }
    float pick (std::initializer_list<float> options) const
    {
        return *(options.begin() + rng.nextInt ((int) options.size()));
    }
};
} // namespace

const char* sfxName (int index)
{
    static const char* names[kNumSfx] = { "COIN", "JUMP", "LASER", "HIT", "BOOM", "POWER-UP", "BLIP", "BOMB" };
    return names[juce::jlimit (0, kNumSfx - 1, index)];
}

const char* sfxHint (int index)
{
    static const char* hints[kNumSfx] = {
        "COIN: a new 'ba-ding!' pickup sound each time you tap.",
        "JUMP: a quick rising 'boing'.",
        "LASER: a 'pew!' that drops from high to low.",
        "HIT: a crunchy 'ouch' for when the hero gets hurt.",
        "BOOM: an explosion - big, noisy and crunchy.",
        "POWER-UP: a rising sparkle of notes.",
        "BLIP: a tiny menu 'tick' or text-box beep.",
        "BOMB: a whistle that keeps falling... hold the key, let go when it lands!",
    };
    return hints[juce::jlimit (0, kNumSfx - 1, index)];
}

SoundEffect makeSoundEffect (Sfx type, juce::Random& rng)
{
    using namespace ids;
    const Roll r { rng };
    SoundEffect fx;
    auto& v = fx.values;

    switch (type)
    {
        case Sfx::Coin:      // two quick notes a fourth (or a chord) apart, ringing out
            v = { { wave, r.pick ({ w::square, w::pulse25, w::pulse12 }) },
                  { arpMode, chordArp }, { arpChord, r.pick ({ ch::fourth, ch::fourth, ch::major }) },
                  { arpPattern, pat::once }, { arpSpeed, sp::hz15 },
                  { attack, 0.001f }, { decay, r.range (0.35f, 0.6f) }, { sustain, 0.0f }, { release, r.range (0.1f, 0.25f) },
                  { echoMix, r.chance (0.5f) ? r.range (0.1f, 0.2f) : 0.0f } };
            fx.note = r.between (79, 88);
            fx.length = 0.45;
            break;

        case Sfx::Jump:      // rises into the note
            v = { { wave, r.pick ({ w::square, w::pulse25 }) },
                  { sweepAmt, -(float) r.between (8, 16) }, { sweepTime, r.range (0.12f, 0.25f) },
                  { attack, 0.001f }, { decay, r.range (0.25f, 0.4f) }, { sustain, 0.0f }, { release, 0.05f } };
            fx.note = r.between (60, 69);
            fx.length = 0.3;
            break;

        case Sfx::Laser:     // drops a long way, fast
            v = { { wave, r.pick ({ w::pulse12, w::pulse25, w::saw, w::square }) },
                  { sweepAmt, (float) r.between (24, 48) }, { sweepTime, r.range (0.12f, 0.3f) },
                  { attack, 0.001f }, { decay, r.range (0.2f, 0.35f) }, { sustain, 0.0f }, { release, 0.05f },
                  { echoMix, r.chance (0.3f) ? 0.15f : 0.0f } };
            fx.note = r.between (64, 84);
            fx.length = 0.3;
            break;

        case Sfx::Hit:       // a short, crunchy drop
            v = { { wave, r.pick ({ w::noise, w::noise, w::square }) },
                  { sweepAmt, (float) r.between (12, 30) }, { sweepTime, r.range (0.05f, 0.12f) },
                  { attack, 0.001f }, { decay, r.range (0.1f, 0.25f) }, { sustain, 0.0f }, { release, 0.03f },
                  { bits, (float) r.between (4, 7) }, { rate, r.range (8000.0f, 16000.0f) } };
            fx.note = r.between (45, 60);
            fx.length = 0.15;
            break;

        case Sfx::Boom:      // low noise, falling, long and crunchy
            v = { { wave, w::noise }, { sweepAmt, (float) r.between (6, 18) }, { sweepTime, r.range (0.5f, 1.0f) },
                  { attack, 0.001f }, { decay, r.range (0.8f, 1.5f) }, { sustain, 0.0f }, { release, r.range (0.3f, 0.5f) },
                  { bits, (float) r.between (4, 7) }, { rate, r.range (6000.0f, 12000.0f) },
                  { echoMix, r.chance (0.4f) ? r.range (0.1f, 0.25f) : 0.0f } };
            fx.note = r.between (36, 48);
            fx.length = 0.8;
            break;

        case Sfx::PowerUp:   // a fast rising arpeggio, sometimes climbing as it plays
        {
            const bool climb = r.chance (0.5f);
            v = { { wave, r.pick ({ w::square, w::pulse25, w::pulse12 }) },
                  { arpMode, chordArp }, { arpChord, r.pick ({ ch::major, ch::maj7, ch::sus4, ch::octave }) },
                  { arpOctaves, (float) r.between (2, 3) }, { arpPattern, r.pick ({ pat::up, pat::once }) },
                  { arpSpeed, r.pick ({ sp::hz30, sp::hz60 }) },
                  { attack, 0.001f }, { decay, r.range (0.6f, 1.0f) }, { sustain, r.range (0.0f, 0.3f) },
                  { release, r.range (0.1f, 0.2f) },
                  { sweepMode, climb ? forever : 0.0f }, { sweepAmt, climb ? -(float) r.between (2, 5) : 0.0f },
                  { sweepTime, 0.6f } };
            fx.note = r.between (60, 67);
            fx.length = 0.6;
            break;
        }

        case Sfx::Blip:      // tiny and high
            v = { { wave, r.pick ({ w::square, w::pulse12, w::pulse25, w::triangle }) },
                  { attack, 0.001f }, { decay, r.range (0.04f, 0.1f) }, { sustain, 0.0f }, { release, r.range (0.02f, 0.05f) } };
            fx.note = r.between (72, 96);
            fx.length = 0.06;
            break;

        case Sfx::Bomb:      // a whistle that never stops falling (SWEEP FOREVER)
            v = { { wave, r.pick ({ w::square, w::triangle, w::pulse25 }) },
                  { sweepMode, forever }, { sweepAmt, (float) r.between (8, 14) }, { sweepTime, r.range (0.8f, 1.4f) },
                  { attack, 0.001f }, { sustain, 1.0f }, { release, r.range (0.05f, 0.15f) },
                  { vibDepth, r.range (0.1f, 0.2f) }, { vibRate, r.range (10.0f, 14.0f) }, { vibDelay, 0.0f } };
            fx.note = r.between (84, 96);
            fx.length = r.range (1.6f, 2.4f);
            break;
    }
    return fx;
}

//==============================================================================
const char* jingleName (int index)
{
    static const char* names[kNumJingles] = { "LEVEL CLEAR", "GAME OVER", "GOT ITEM", "FANFARE" };
    return names[juce::jlimit (0, kNumJingles - 1, index)];
}

const char* jingleHint (int index)
{
    static const char* hints[kNumJingles] = {
        "LEVEL CLEAR: a happy little 'you did it!' tune - new each time, in your KEY + SCALE, played with your sound.",
        "GAME OVER: a sad 'wah wah wahhh' that slows down at the end.",
        "GOT ITEM: a quick sparkly run up to a high note.",
        "FANFARE: 'ta-ta-ta TAAA!' - for the start of a level or a boss.",
    };
    return hints[juce::jlimit (0, kNumJingles - 1, index)];
}

namespace
{
// One note of a jingle: a step of the scale (0 = the key's note, 7 = an octave up)
// and when / how long it plays, in beats.
struct Step { int step; float start, length; };
using Tune = std::vector<Step>;

const std::vector<Tune>& tunesFor (Jingle j)
{
    static const std::vector<Tune> levelClear = {
        { { 0, 0, .25f }, { 2, .25f, .25f }, { 4, .5f, .25f }, { 7, .75f, .25f }, { 4, 1, .25f }, { 7, 1.25f, .25f },
          { 9, 1.5f, .5f }, { 7, 2, 1.5f }, { 4, 2, 1.5f } },
        { { 4, 0, .33f }, { 4, .33f, .33f }, { 4, .67f, .33f }, { 7, 1, .5f }, { 6, 1.5f, .25f }, { 7, 1.75f, 1.5f },
          { 4, 1.75f, 1.5f } },
        { { 0, 0, .25f }, { 1, .25f, .25f }, { 2, .5f, .25f }, { 3, .75f, .25f }, { 4, 1, .25f }, { 5, 1.25f, .25f },
          { 6, 1.5f, .25f }, { 7, 1.75f, 1.5f }, { 2, 1.75f, 1.5f } },
    };
    static const std::vector<Tune> gameOver = {
        { { 4, 0, .5f }, { 3, .5f, .5f }, { 2, 1, .6f }, { 1, 1.6f, .8f }, { 0, 2.4f, 2 } },
        { { 7, 0, .75f }, { 6, .75f, .75f }, { 5, 1.5f, .9f }, { 4, 2.4f, 2.4f } },
        { { 2, 0, .25f }, { 1, .25f, .25f }, { 0, .5f, .5f }, { -3, 1, .6f }, { -5, 1.6f, .8f }, { -7, 2.4f, 2 } },
    };
    static const std::vector<Tune> gotItem = {
        { { 0, 0, .125f }, { 2, .125f, .125f }, { 4, .25f, .125f }, { 7, .375f, .125f }, { 9, .5f, .125f },
          { 11, .625f, .125f }, { 14, .75f, 1 } },
        { { 4, 0, .125f }, { 7, .125f, .125f }, { 11, .25f, .125f }, { 14, .375f, .375f }, { 11, .75f, .125f },
          { 14, .875f, 1 } },
        { { 0, 0, .25f }, { 4, .25f, .25f }, { 7, .5f, .25f }, { 11, .75f, .25f }, { 14, 1, 1 }, { 11, 1, 1 } },
    };
    static const std::vector<Tune> fanfare = {
        { { 0, 0, .33f }, { 0, .33f, .33f }, { 0, .67f, .33f }, { 4, 1, 1 }, { 3, 2, .33f }, { 3, 2.33f, .33f },
          { 3, 2.67f, .33f }, { 7, 3, 2 }, { 4, 3, 2 } },
        { { 4, 0, .5f }, { 4, .5f, .25f }, { 4, .75f, .25f }, { 7, 1, 1.5f }, { 2, 1, 1.5f } },
        { { 0, 0, .25f }, { 0, .25f, .25f }, { 4, .5f, .5f }, { 3, 1, .25f }, { 3, 1.25f, .25f }, { 5, 1.5f, .5f },
          { 4, 2, .25f }, { 6, 2.25f, .25f }, { 7, 2.5f, 1.5f }, { 4, 2.5f, 1.5f } },
    };
    switch (j)
    {
        case Jingle::GameOver: return gameOver;
        case Jingle::GotItem:  return gotItem;
        case Jingle::Fanfare:  return fanfare;
        case Jingle::LevelClear:
        default:               return levelClear;
    }
}
} // namespace

std::vector<NotePlayer::Note> makeJingle (Jingle type, int key, Scale scale, double bpm, juce::Random& rng)
{
    // The 7 notes of the scale the song is in (pentatonic / blues / Off: their 7-note "parent")
    const int mask = scaleMask (harmonyScale (scale));
    int pcs[12] {}, count = 0;
    for (int pc = 0; pc < 12; ++pc)
        if ((mask >> pc) & 1)
            pcs[count++] = pc;

    const int root = 60 + juce::jlimit (0, 11, key) - (key > 6 ? 12 : 0);   // around middle C
    auto noteFor = [&] (int step)
    {
        const int octave = step >= 0 ? step / count : -((-step + count - 1) / count);
        return root + 12 * octave + pcs[step - octave * count];
    };

    // Jingles sound best quick: fold the song tempo into 120..180 BPM, then wobble it a little
    while (bpm < 120.0) bpm *= 2.0;
    while (bpm > 180.0) bpm *= 0.5;
    const double beat = 60.0 / (bpm * (0.92 + 0.16 * rng.nextDouble()));

    const auto& tunes = tunesFor (type);
    const auto& tune = tunes[(size_t) rng.nextInt ((int) tunes.size())];
    const int shift = rng.nextInt (4) == 0 ? (type == Jingle::GameOver ? -count : count) : 0;   // sometimes an octave away

    std::vector<NotePlayer::Note> notes;
    for (const auto& s : tune)
        notes.push_back ({ s.start * beat, s.length * beat * 0.95, juce::jlimit (0, 127, noteFor (s.step + shift)),
                           s.start == tune.back().start ? 0.75f : 0.9f });
    return notes;
}
} // namespace bb
