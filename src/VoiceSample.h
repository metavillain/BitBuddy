#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace juce { template <typename> class AudioBuffer; class String; }

namespace bb
{
/** YOUR VOICE ON THE NES (added in 1.2): a short recording that the VOICE wave
    plays like an old console's sample channel (the NES "DPCM" channel played
    drum hits and voices like this).

    Stored the way old consoles did: 16,000 samples a second, 8 bits each, at
    most 3 seconds. It plays at its own pitch on middle C (note 60); other keys
    play it faster or slower, like a tape. The UI writes it, the audio thread
    reads it (each sample is an atomic, so a new recording never tears). */
struct VoiceSample
{
    static constexpr double rate = 16000.0;
    static constexpr int maxLength = 48000;   // 3 seconds
    static constexpr int rootNote = 60;

    /** UI thread: replaces the sample (the engine hears silence while it changes). */
    void set (const std::vector<std::int8_t>& samples) noexcept;
    std::vector<std::int8_t> get() const;

    int getLength() const noexcept               { return length.load (std::memory_order_acquire); }
    float at (int index) const noexcept          { return (float) data[(size_t) index].load (std::memory_order_relaxed) * (1.0f / 127.0f); }
    int getVersion() const noexcept              { return version.load(); }   // changes with every new sample (for the UI)

private:
    std::array<std::atomic<std::int8_t>, maxLength> data {};
    std::atomic<int> length { 0 }, version { 0 };
};

/** Turns any recording into a voice sample: mono, 16 kHz, quiet start and end
    trimmed, loudness evened out, at most 3 seconds. Empty if it's all silence. */
std::vector<std::int8_t> makeVoiceSample (const juce::AudioBuffer<float>& audio, double sampleRate);

/** The voice you get until you record your own: a sung "aah" (made from scratch). */
std::vector<std::int8_t> defaultVoiceSample();

/** How voices are saved in projects and presets (base64 text). */
juce::String voiceSampleToString (const std::vector<std::int8_t>&);
bool voiceSampleFromString (const juce::String&, std::vector<std::int8_t>&);
} // namespace bb
