#include "Parameters.h"

namespace bb
{
juce::StringArray waveNames()       { return { "Square", "Pulse 25%", "Pulse 12.5%", "Triangle", "Saw", "Noise", "Metal Noise", "Drum Kit", "Draw" }; }
juce::StringArray arpModeNames()    { return { "Off", "Chord", "Notes" }; }
juce::StringArray arpChordNames()   { return { "Major", "Minor", "Power", "Octave", "Sus4", "Major 7", "Minor 7", "Diminished", "Fourth (Coin)", "Smart" }; }
juce::StringArray arpPatternNames() { return { "Up", "Down", "Up & Down", "Random", "Once" }; }
juce::StringArray arpSpeedNames()   { return { "1/8", "1/16", "1/16 T", "1/32", "1/64", "15 Hz", "30 Hz", "60 Hz" }; }
juce::StringArray echoTimeNames()   { return { "1/16", "1/8", "1/8 D", "1/4", "1/4 D", "1/2" }; }
juce::StringArray voiceModeNames()  { return { "Poly", "Mono" }; }
juce::StringArray keyNames()        { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
juce::StringArray scaleNames()      { return { "Off", "Major", "Minor", "Major Penta", "Minor Penta", "Blues", "Dorian", "Harm. Minor" }; }
juce::StringArray filterTypeNames() { return { "Off", "Low Pass", "Band Pass", "High Pass" }; }
juce::StringArray consoleNames()    { return { "Modern", "NES", "Game Boy", "C64" }; }

namespace
{
using juce::String;

String secondsText (float v, int)
{
    if (v < 1.0f)
        return String (juce::roundToInt (v * 1000.0f)) + " ms";
    return String (v, 2) + " s";
}

float secondsFromText (const String& t)
{
    const auto v = t.getFloatValue();
    return t.containsIgnoreCase ("ms") ? v / 1000.0f : v;
}

String percentText (float v, int)      { return String (juce::roundToInt (v * 100.0f)) + "%"; }
float percentFromText (const String& t) { return t.getFloatValue() / 100.0f; }

String semitoneText (float v, int)
{
    const auto st = juce::roundToInt (v);
    return (st > 0 ? "+" : "") + String (st) + " st";
}

String fineSemitoneText (float v, int)  { return String (v, 2) + " st"; }
String hzText (float v, int)            { return String (v, 1) + " Hz"; }
String hzOrKhzText (float v, int)       { return v < 1000.0f ? String (juce::roundToInt (v)) + " Hz" : String (v / 1000.0f, 1) + " kHz"; }

String glideText (float v, int)
{
    return v < 0.001f ? String ("Off") : secondsText (v, 0);
}

String rateText (float v, int)
{
    if (v >= 44000.0f)
        return "Clean";
    return String (v / 1000.0f, 1) + " kHz";
}

String dbText (float v, int) { return String (v, 1) + " dB"; }

using FloatAttrs = juce::AudioParameterFloatAttributes;
using IntAttrs   = juce::AudioParameterIntAttributes;
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // version = the BitBuddy release that added the parameter (hosts use it to keep their order stable):
    // 1 = BitBuddy 1.0, 2 = BitBuddy 1.1
    auto addFloat = [&layout] (const char* id, const char* name, NormalisableRange<float> range, float def,
                               std::function<String (float, int)> toText,
                               std::function<float (const String&)> fromText = nullptr, int version = 1)
    {
        auto attrs = FloatAttrs().withStringFromValueFunction (std::move (toText));
        if (fromText != nullptr)
            attrs = attrs.withValueFromStringFunction (std::move (fromText));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, version }, name, range, def, attrs));
    };

    auto addChoice = [&layout] (const char* id, const char* name, const StringArray& choices, int def, int version = 1)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, version }, name, choices, def));
    };

    // 1. Sound source
    addChoice (ids::wave, "Wave", waveNames(), 0);

    // 2. Volume envelope (ADSR)
    addFloat (ids::attack,  "Attack",  { 0.001f, 2.0f, 0.0f, 0.3f },  0.003f, secondsText, secondsFromText);
    addFloat (ids::decay,   "Decay",   { 0.01f,  3.0f, 0.0f, 0.35f }, 0.35f,  secondsText, secondsFromText);
    addFloat (ids::sustain, "Sustain", { 0.0f,   1.0f },              0.7f,   percentText, percentFromText);
    addFloat (ids::release, "Release", { 0.005f, 3.0f, 0.0f, 0.35f }, 0.12f,  secondsText, secondsFromText);

    // 3. Arpeggiator ("magic chords")
    addChoice (ids::arpMode,    "Arp Mode",    arpModeNames(),    0);
    addChoice (ids::arpChord,   "Arp Chord",   arpChordNames(),   0);
    addChoice (ids::arpPattern, "Arp Pattern", arpPatternNames(), 0);
    addChoice (ids::arpSpeed,   "Arp Speed",   arpSpeedNames(),   3);
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::arpOctaves, 1 }, "Arp Octaves", 1, 3, 1,
                                                     IntAttrs().withStringFromValueFunction ([] (int v, int)
                                                     { return String (v) + (v == 1 ? " oct" : " octs"); })));

    // 4. Movement
    addFloat (ids::sweepAmt,  "Sweep",       { -48.0f, 48.0f, 1.0f },       0.0f,  semitoneText);
    addFloat (ids::sweepTime, "Sweep Time",  { 0.01f, 2.0f, 0.0f, 0.4f },   0.15f, secondsText, secondsFromText);
    addFloat (ids::vibDepth,  "Vibrato",     { 0.0f, 2.0f, 0.0f, 0.6f },    0.0f,  fineSemitoneText);
    addFloat (ids::vibRate,   "Vibrato Rate",{ 1.0f, 15.0f },               6.0f,  hzText);
    addFloat (ids::vibDelay,  "Vibrato Delay",{ 0.0f, 1.5f, 0.0f, 0.6f },   0.2f,  secondsText, secondsFromText);
    addFloat (ids::pwm,       "PWM",         { 0.0f, 1.0f },                0.0f,  percentText, percentFromText);
    addFloat (ids::glide,     "Glide",       { 0.0f, 1.0f, 0.0f, 0.4f },    0.0f,  glideText, secondsFromText);
    addChoice (ids::voiceMode, "Voice Mode", voiceModeNames(), 0);

    // 5. Crunch + echo + output
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ids::bits, 1 }, "Bits", 2, 16, 8,
                                                     IntAttrs().withStringFromValueFunction ([] (int v, int)
                                                     { return String (v) + "-bit"; })));
    addFloat (ids::rate,         "Sample Rate",   { 1000.0f, 48000.0f, 0.0f, 0.35f }, 48000.0f, rateText);
    addFloat (ids::echoMix,      "Echo",          { 0.0f, 1.0f },  0.0f,  percentText, percentFromText);
    addChoice (ids::echoTime,    "Echo Time",     echoTimeNames(), 2);
    addFloat (ids::echoFeedback, "Echo Repeats",  { 0.0f, 0.9f },  0.35f, percentText, percentFromText);
    addFloat (ids::volume,       "Volume",        { -40.0f, 6.0f, 0.1f }, -3.0f, dbText);

    // 6. Play (added in 1.1): Scale Lock
    addChoice (ids::scaleKey, "Key",   keyNames(),   0, 2);
    addChoice (ids::scale,    "Scale", scaleNames(), 0, 2);

    // 7. C64 filter (added in 1.1)
    addChoice  (ids::filterType,   "Filter",     filterTypeNames(), 0, 2);
    addFloat (ids::filterCutoff, "Cutoff",     { 60.0f, 12000.0f, 0.0f, 0.3f }, 1500.0f, hzOrKhzText, nullptr, 2);
    addFloat (ids::filterReso,   "Resonance",  { 0.0f, 1.0f }, 0.35f, percentText, percentFromText, 2);
    addFloat (ids::filterEnv,    "Filter Env", { 0.0f, 1.0f }, 0.0f,  percentText, percentFromText, 2);

    // 8. Console mode (added in 1.1): sound like a real NES / Game Boy / C64
    addChoice (ids::console, "Console", consoleNames(), 0, 2);

    return layout;
}
} // namespace bb
