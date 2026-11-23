#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
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
};

const std::vector<Preset>& factoryPresets();

/** Settings that belong to your song, not to a sound: presets, SURPRISE ME!
    and MUTATE leave them alone (volume, Key, Scale). */
bool isSongSetting (const juce::String& paramId);

/** Resets everything (except the song settings) to defaults, then applies the preset. */
void applyPreset (juce::AudioProcessorValueTreeState&, int index);
} // namespace bb
