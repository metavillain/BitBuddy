#include "Presets.h"
#include "Parameters.h"

namespace bb
{
// Choice indices, for readability below
namespace w   { constexpr float square = 0, pulse25 = 1, pulse12 = 2, triangle = 3, saw = 4, noise = 5, metal = 6, drums = 7, draw = 8; }
namespace am  { constexpr float off = 0, chord = 1, notes = 2; }
namespace ch  { constexpr float major = 0, minor = 1, power = 2, octave = 3, sus4 = 4, maj7 = 5, min7 = 6, dim = 7, fourth = 8, smart = 9; }
namespace pat { constexpr float up = 0, down = 1, upDown = 2, random = 3, once = 4; }
namespace sp  { constexpr float e8 = 0, s16 = 1, s16t = 2, t32 = 3, s64 = 4, hz15 = 5, hz30 = 6, hz60 = 7; }
namespace et  { constexpr float s16 = 0, e8 = 1, e8d = 2, q4 = 3, q4d = 4, h2 = 5; }
namespace swm { constexpr float slide = 0, forever = 1, bounce = 2; }
namespace vs  { constexpr float smooth = 0, trill = 1, random = 2; }
namespace st  { constexpr float middle = 0, wide = 1, swing = 2; }
constexpr float mono = 1.0f;

const std::vector<Preset>& factoryPresets()
{
    using namespace ids;

    static const std::vector<Preset> presets = {
        { "Classic Square", "The basic 8-bit sound. Play anything!",
          { { wave, w::square } } },

        { "Hero Lead", "A heroic NES-style lead. Hold long notes to hear the vibrato bloom.",
          { { wave, w::pulse25 }, { attack, 0.004f }, { decay, 0.5f }, { sustain, 0.65f }, { release, 0.15f },
            { vibDepth, 0.35f }, { vibRate, 6.0f }, { vibDelay, 0.22f },
            { echoMix, 0.25f }, { echoTime, et::e8d }, { echoFeedback, 0.3f } } },

        { "Real NES Lead", "Sounds like the real chip: 2 notes at once, 16 volume steps, NES tuning. Play a melody!",
          { { console, 1.0f }, { wave, w::pulse25 }, { attack, 0.001f }, { decay, 0.7f }, { sustain, 0.5f }, { release, 0.12f },
            { vibDepth, 0.3f }, { vibRate, 6.5f }, { vibDelay, 0.25f },
            { echoMix, 0.2f }, { echoTime, et::e8d }, { echoFeedback, 0.25f } } },

        { "Thin Lead", "Very thin 12.5% pulse - cuts through any mix.",
          { { wave, w::pulse12 }, { decay, 0.3f }, { sustain, 0.7f }, { release, 0.1f },
            { vibDepth, 0.25f }, { vibRate, 7.0f }, { vibDelay, 0.3f }, { echoMix, 0.2f } } },

        { "Trill Lead", "Hold a note: after a moment it trills like a game's title-screen melody.",
          { { wave, w::pulse25 }, { decay, 0.4f }, { sustain, 0.7f }, { release, 0.1f },
            { vibShape, vs::trill }, { vibDepth, 0.5f }, { vibRate, 9.0f }, { vibDelay, 0.3f },
            { echoMix, 0.2f }, { echoTime, et::e8d } } },

        { "Triangle Bass", "The classic NES bass. Play low notes, one at a time.",
          { { wave, w::triangle }, { attack, 0.001f }, { decay, 0.25f }, { sustain, 0.85f }, { release, 0.04f },
            { voiceMode, mono }, { glide, 0.02f } } },

        { "Punchy Bass", "Square bass with a little pitch 'thump' on every note.",
          { { wave, w::square }, { attack, 0.001f }, { decay, 0.18f }, { sustain, 0.45f }, { release, 0.05f },
            { sweepAmt, 5.0f }, { sweepTime, 0.04f }, { voiceMode, mono } } },

        { "C64 Squelch Bass", "Rubbery Commodore 64 bass: the filter 'wows' on every note. Play low!",
          { { wave, w::saw }, { attack, 0.001f }, { decay, 0.35f }, { sustain, 0.6f }, { release, 0.08f },
            { voiceMode, mono }, { glide, 0.03f },
            { filterType, 1.0f }, { filterCutoff, 260.0f }, { filterReso, 0.72f }, { filterEnv, 0.65f } } },

        { "Major Chord Arp", "Hold ONE key - you get a whole happy chord!",
          { { wave, w::pulse25 }, { arpMode, am::chord }, { arpChord, ch::major }, { arpSpeed, sp::hz30 },
            { sustain, 0.8f }, { release, 0.15f }, { echoMix, 0.15f } } },

        { "Minor Chord Arp", "Hold ONE key - you get a whole sad / serious chord.",
          { { wave, w::square }, { arpMode, am::chord }, { arpChord, ch::minor }, { arpSpeed, sp::hz30 },
            { sustain, 0.8f }, { release, 0.15f }, { echoMix, 0.15f } } },

        { "Smart Chords", "Pick a KEY + SCALE below, then hold ONE key at a time: always the right chord!",
          { { wave, w::pulse25 }, { arpMode, am::chord }, { arpChord, ch::smart }, { arpSpeed, sp::hz30 },
            { sustain, 0.8f }, { release, 0.2f }, { echoMix, 0.2f }, { echoTime, et::e8d } } },

        { "Macro Chord Blip", "FamiTracker-style: an ARP macro flips 0-4-7 at 60 Hz. Open MACROS on the SCREEN to see it!",
          { { wave, w::pulse12 }, { sustain, 1.0f }, { release, 0.1f }, { echoMix, 0.15f } },
          nullptr,
          { "12|-1|15,15,14,13,12,11,10,9,8,7,6,5", "3|0|0,4,7", nullptr, nullptr } },

        { "Macro Stutter Lead", "VOL + DUTY macros: each note stutters like a fake echo and its tone shifts.",
          { { wave, w::square }, { sustain, 1.0f }, { release, 0.15f }, { vibDepth, 0.25f }, { vibDelay, 0.35f } },
          nullptr,
          { "20|-1|15,15,13,11,0,0,11,10,9,0,0,8,7,6,0,0,6,6,6,6", nullptr, "6|-1|-6,-4,-2,-1,0,0", "6|-1|0,0,1,1,2,2" } },

        { "Power Arp", "Rock power chords, bouncing across two octaves.",
          { { wave, w::pulse12 }, { arpMode, am::chord }, { arpChord, ch::power }, { arpOctaves, 2.0f },
            { arpPattern, pat::upDown }, { arpSpeed, sp::t32 }, { echoMix, 0.2f } } },

        { "Stereo Arp", "Use headphones! Each chord note jumps to the other ear, like a Game Boy.",
          { { wave, w::square }, { arpMode, am::notes }, { arpPattern, pat::up }, { arpSpeed, sp::s16 },
            { arpOctaves, 2.0f }, { stereo, st::wide }, { decay, 0.2f }, { sustain, 0.4f }, { release, 0.1f },
            { echoMix, 0.3f }, { echoTime, et::e8d }, { echoFeedback, 0.3f } } },

        { "Note Arpeggiator", "Hold 3 or 4 keys and it plays them as a sparkling pattern.",
          { { wave, w::pulse12 }, { arpMode, am::notes }, { arpPattern, pat::upDown }, { arpSpeed, sp::s16 },
            { arpOctaves, 2.0f }, { attack, 0.002f }, { decay, 0.3f }, { sustain, 0.6f }, { release, 0.1f },
            { echoMix, 0.3f }, { echoFeedback, 0.35f } } },

        { "PWM Strings", "Lush, chorus-y chords. Hold chords down slowly.",
          { { wave, w::square }, { pwm, 0.7f }, { attack, 0.25f }, { decay, 0.6f }, { sustain, 0.8f }, { release, 0.5f },
            { vibDepth, 0.12f }, { vibRate, 5.0f }, { vibDelay, 0.4f }, { echoMix, 0.3f } } },

        { "Game Boy Wave", "A hand-drawn wave, like the Game Boy's wave channel. Pick DRAW and redraw it on the SCREEN!",
          { { wave, w::draw }, { attack, 0.002f }, { decay, 0.6f }, { sustain, 0.55f }, { release, 0.2f },
            { vibDepth, 0.2f }, { vibRate, 5.5f }, { vibDelay, 0.25f }, { echoMix, 0.2f }, { echoTime, et::e8d } },
          "89BCDEEFFFEDCB98AC864211234688A6" },

        { "Spooky Triangle", "Haunted-castle vibes. Slow melodies work best.",
          { { wave, w::triangle }, { attack, 0.05f }, { decay, 0.5f }, { sustain, 0.8f }, { release, 0.7f },
            { vibDepth, 0.6f }, { vibRate, 4.5f }, { vibDelay, 0.0f },
            { echoMix, 0.45f }, { echoTime, et::q4 }, { echoFeedback, 0.45f } } },

        { "Metal Bells", "Metallic noise tuned to your keys - bells and robots.",
          { { wave, w::metal }, { attack, 0.001f }, { decay, 0.9f }, { sustain, 0.0f }, { release, 0.6f },
            { echoMix, 0.35f }, { echoTime, et::e8d } } },

        { "Lo-Fi Saw Buzz", "Gritty 4-bit saw with glide. Play one note at a time.",
          { { wave, w::saw }, { bits, 4.0f }, { rate, 6000.0f }, { voiceMode, mono }, { glide, 0.08f },
            { attack, 0.001f }, { decay, 0.4f }, { sustain, 0.7f }, { release, 0.1f } } },

        { "Drum Kit", "Every C is a kick, D a snare, F# a hi-hat, A# an open hat.",
          { { wave, w::drums } } },

        { "Crunchy Drums", "The drum kit, extra crispy.",
          { { wave, w::drums }, { bits, 5.0f }, { rate, 9000.0f } } },

        { "Coin Pickup", "Tap a high note (around B5) - ka-ching!",
          { { wave, w::square }, { arpMode, am::chord }, { arpChord, ch::fourth }, { arpPattern, pat::once },
            { arpSpeed, sp::hz15 }, { attack, 0.001f }, { decay, 0.6f }, { sustain, 0.0f }, { release, 0.1f } } },

        { "Jump", "Tap a note - boing! Higher notes = smaller jumps.",
          { { wave, w::square }, { sweepAmt, -10.0f }, { sweepTime, 0.2f },
            { attack, 0.001f }, { decay, 0.3f }, { sustain, 0.0f }, { release, 0.05f }, { voiceMode, mono } } },

        { "Laser Zap", "Pew pew! Starts high and drops down to the note.",
          { { wave, w::pulse25 }, { sweepAmt, 36.0f }, { sweepTime, 0.25f },
            { attack, 0.001f }, { decay, 0.35f }, { sustain, 0.0f }, { release, 0.05f } } },

        { "Power Up", "Tap one note - a rising sparkle of notes.",
          { { wave, w::square }, { arpMode, am::chord }, { arpChord, ch::major }, { arpOctaves, 3.0f },
            { arpPattern, pat::once }, { arpSpeed, sp::hz30 },
            { attack, 0.001f }, { decay, 1.0f }, { sustain, 0.2f }, { release, 0.2f }, { echoMix, 0.2f } } },

        { "Falling Bomb", "Hold a HIGH note... and let go when it lands!",
          { { wave, w::square }, { sweepMode, swm::forever }, { sweepAmt, 12.0f }, { sweepTime, 1.2f },
            { attack, 0.001f }, { sustain, 1.0f }, { release, 0.05f }, { vibDepth, 0.15f }, { vibRate, 12.0f },
            { vibDelay, 0.0f } } },

        { "Siren", "Hold a note. Wee-woo, wee-woo!",
          { { wave, w::pulse25 }, { sweepMode, swm::bounce }, { sweepAmt, -7.0f }, { sweepTime, 0.45f },
            { attack, 0.01f }, { sustain, 1.0f }, { release, 0.1f }, { stereo, st::swing } } },

        { "Explosion", "Play LOW notes for a big boom.",
          { { wave, w::noise }, { sweepAmt, 12.0f }, { sweepTime, 0.8f },
            { attack, 0.001f }, { decay, 1.2f }, { sustain, 0.0f }, { release, 0.4f },
            { bits, 6.0f }, { rate, 11025.0f } } },

        { "Wind & Waves", "Hold a note and listen to the wind blow.",
          { { wave, w::noise }, { attack, 0.8f }, { decay, 1.0f }, { sustain, 0.6f }, { release, 1.5f },
            { vibDepth, 2.0f }, { vibRate, 1.0f }, { vibDelay, 0.0f } } },
    };

    return presets;
}

bool isSongSetting (const juce::String& paramId)
{
    return paramId == ids::volume || paramId == ids::scaleKey || paramId == ids::scale;
}

void applyPreset (juce::AudioProcessorValueTreeState& apvts, int index)
{
    const auto& presets = factoryPresets();
    if (index < 0 || index >= (int) presets.size())
        return;

    // 1. Reset every parameter to its default - except the ones that belong to
    //    your song rather than to the sound: volume and the Key / Scale you play in
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! isSongSetting (rp->getParameterID()))
                rp->setValueNotifyingHost (rp->getDefaultValue());

    // 2. Apply this preset's values
    for (const auto& [id, value] : presets[(size_t) index].values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
}
} // namespace bb
