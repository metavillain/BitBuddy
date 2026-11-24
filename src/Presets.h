#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <utility>
#include <vector>

namespace bb
{
struct Preset
{
    const char* name;
    const char* tip;   // one-line playing tip shown in the UI
    std::vector<std::pair<const char*, float>> values;   // parameter id -> real value
    const char* customWave = nullptr;                     // DRAW presets: 32 hex digits (0-F)
    std::array<const char*, 4> macros {};                 // VOL, ARP, PITCH, DUTY: "length|loop|v,v,v..."
};

const std::vector<Preset>& factoryPresets();

/** Settings that belong to your song, not to a sound: presets, SURPRISE ME!
    and MUTATE leave them alone (volume, Key, Scale). */
bool isSongSetting (const juce::String& paramId);

/** Resets everything (except the song settings) to defaults, then applies the preset. */
void applyPreset (juce::AudioProcessorValueTreeState&, int index);

/** Same, for any list of values (parameter id -> real value): used by the
    sound effect generator. */
void applyValues (juce::AudioProcessorValueTreeState&, const std::vector<std::pair<const char*, float>>& values);
} // namespace bb
