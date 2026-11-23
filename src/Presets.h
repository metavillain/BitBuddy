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
};

const std::vector<Preset>& factoryPresets();

/** Resets everything (except volume) to defaults, then applies the preset. */
void applyPreset (juce::AudioProcessorValueTreeState&, int index);
} // namespace bb
