#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Every knob / switch in BitBuddy is a "parameter". Parameters are what your
// DAW can see, save with your project and automate (draw curves for).
namespace bb::ids
{
inline constexpr const char* wave         = "wave";
inline constexpr const char* attack       = "attack";
inline constexpr const char* decay        = "decay";
inline constexpr const char* sustain      = "sustain";
inline constexpr const char* release      = "release";
inline constexpr const char* arpMode      = "arpMode";
inline constexpr const char* arpChord     = "arpChord";
inline constexpr const char* arpPattern   = "arpPattern";
inline constexpr const char* arpSpeed     = "arpSpeed";
inline constexpr const char* arpOctaves   = "arpOctaves";
inline constexpr const char* sweepAmt     = "sweepAmt";
inline constexpr const char* sweepTime    = "sweepTime";
inline constexpr const char* vibDepth     = "vibDepth";
inline constexpr const char* vibRate      = "vibRate";
inline constexpr const char* vibDelay     = "vibDelay";
inline constexpr const char* pwm          = "pwm";
inline constexpr const char* glide        = "glide";
inline constexpr const char* voiceMode    = "voiceMode";
inline constexpr const char* bits         = "bits";
inline constexpr const char* rate         = "rate";
inline constexpr const char* echoMix      = "echoMix";
inline constexpr const char* echoTime     = "echoTime";
inline constexpr const char* echoFeedback = "echoFeedback";
inline constexpr const char* volume       = "volume";
inline constexpr const char* scaleKey     = "scaleKey";
inline constexpr const char* scale        = "scale";
} // namespace bb::ids

namespace bb
{
juce::StringArray waveNames();
juce::StringArray arpModeNames();
juce::StringArray arpChordNames();
juce::StringArray arpPatternNames();
juce::StringArray arpSpeedNames();
juce::StringArray echoTimeNames();
juce::StringArray voiceModeNames();
juce::StringArray keyNames();
juce::StringArray scaleNames();

/** Chord type index of "Smart" (follows Key + Scale) in arpChordNames(). */
inline constexpr int kSmartChordIndex = 9;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace bb
