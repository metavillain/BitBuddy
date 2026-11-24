#include "VoiceSample.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace bb
{
void VoiceSample::set (const std::vector<std::int8_t>& samples) noexcept
{
    const int n = juce::jmin ((int) samples.size(), maxLength);
    length.store (0, std::memory_order_release);   // silent while it changes
    for (int i = 0; i < n; ++i)
        data[(size_t) i].store (samples[(size_t) i], std::memory_order_relaxed);
    length.store (n, std::memory_order_release);
    ++version;
}

std::vector<std::int8_t> VoiceSample::get() const
{
    std::vector<std::int8_t> out ((size_t) getLength());
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = data[i].load (std::memory_order_relaxed);
    return out;
}

std::vector<std::int8_t> makeVoiceSample (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    const int numIn = audio.getNumSamples();
    if (numIn <= 0 || audio.getNumChannels() <= 0 || sampleRate <= 0.0)
        return {};

    // 1. Mono, with a gentle low-pass so nothing above 8 kHz folds back as hiss
    const double cutoff = 7000.0;
    const float a = (float) std::exp (-juce::MathConstants<double>::twoPi * cutoff / sampleRate);
    std::vector<float> mono ((size_t) numIn);
    float y1 = 0.0f, y2 = 0.0f;
    for (int i = 0; i < numIn; ++i)
    {
        float x = 0.0f;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            x += audio.getSample (ch, i);
        x /= (float) audio.getNumChannels();
        y1 = x + a * (y1 - x);   // two one-pole stages
        y2 = y1 + a * (y2 - y1);
        mono[(size_t) i] = y2;
    }

    // 2. Down to 16 kHz (linear interpolation is plenty after the low-pass)
    const double step = sampleRate / VoiceSample::rate;
    std::vector<float> resampled;
    resampled.reserve ((size_t) (numIn / step) + 1);
    for (double pos = 0.0; pos < (double) (numIn - 1); pos += step)
    {
        const int i = (int) pos;
        const float frac = (float) (pos - i);
        resampled.push_back (mono[(size_t) i] + (mono[(size_t) i + 1] - mono[(size_t) i]) * frac);
    }

    // 3. Trim the quiet start and end (below 1/30 of the loudest moment)
    float peak = 0.0f;
    for (auto x : resampled)
        peak = juce::jmax (peak, std::abs (x));
    if (peak < 1.0e-3f)
        return {};   // just silence (is the microphone on?)

    const float threshold = peak / 30.0f;
    size_t first = 0, last = resampled.size();
    while (first < last && std::abs (resampled[first]) < threshold)
        ++first;
    while (last > first && std::abs (resampled[last - 1]) < threshold)
        --last;
    first = first > 160 ? first - 160 : 0;                     // keep 10 ms before the sound starts
    last = juce::jmin (resampled.size(), last + 800);          // ...and 50 ms after it ends
    last = juce::jmin (last, first + (size_t) VoiceSample::maxLength);

    // 4. As loud as possible without clipping, then 8 bits; short fades so it never clicks
    std::vector<std::int8_t> out;
    out.reserve (last - first);
    const float gain = 0.95f / peak;
    const size_t fade = 80;   // 5 ms
    for (size_t i = first; i < last; ++i)
    {
        float g = gain;
        const size_t fromStart = i - first, toEnd = last - 1 - i;
        if (fromStart < fade) g *= (float) fromStart / fade;
        if (toEnd < fade)     g *= (float) toEnd / fade;
        out.push_back ((std::int8_t) juce::jlimit (-127, 127, juce::roundToInt (resampled[i] * g * 127.0f)));
    }
    return out;
}

namespace
{
std::vector<std::int8_t> makeDefaultVoice()
{
    // A sung "aah" on middle C: harmonics of 261.6 Hz, shaped by the three "formants"
    // of an a-vowel (the resonances of your mouth), with a little vibrato and a fade.
    constexpr double f0 = 261.6256, seconds = 1.2;
    constexpr double formants[3][3] = { { 730.0, 90.0, 1.0 }, { 1090.0, 110.0, 0.5 }, { 2440.0, 170.0, 0.25 } };
    const int n = (int) (seconds * VoiceSample::rate);
    std::vector<float> wave ((size_t) n, 0.0f);

    for (int h = 1; f0 * h < 7000.0; ++h)
    {
        const double hz = f0 * h;
        double gain = 0.0;
        for (const auto& f : formants)   // each formant: a bump around its centre
        {
            const double d = (hz - f[0]) / f[1];
            gain += f[2] / (1.0 + d * d);
        }
        gain /= std::sqrt ((double) h);
        double phase = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / VoiceSample::rate;
            const double vibrato = 1.0 + 0.006 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t) * juce::jmin (1.0, t * 3.0);
            phase += juce::MathConstants<double>::twoPi * hz * vibrato / VoiceSample::rate;
            wave[(size_t) i] += (float) (gain * std::sin (phase));
        }
    }

    float peak = 0.0f;
    for (auto x : wave)
        peak = juce::jmax (peak, std::abs (x));
    std::vector<std::int8_t> out ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / VoiceSample::rate;
        const double env = juce::jmin (1.0, t / 0.04) * juce::jmin (1.0, (seconds - t) / 0.3);
        out[(size_t) i] = (std::int8_t) juce::roundToInt (wave[(size_t) i] / peak * 120.0f * (float) env);
    }
    return out;
}
} // namespace

std::vector<std::int8_t> defaultVoiceSample()
{
    static const auto voice = makeDefaultVoice();   // made once, the first time it's needed
    return voice;
}

juce::String voiceSampleToString (const std::vector<std::int8_t>& samples)
{
    return juce::Base64::toBase64 (samples.data(), samples.size());
}

bool voiceSampleFromString (const juce::String& text, std::vector<std::int8_t>& samples)
{
    if (text.isEmpty())
        return false;
    juce::MemoryOutputStream out;
    if (! juce::Base64::convertFromBase64 (out, text) || out.getDataSize() == 0
        || out.getDataSize() > (size_t) VoiceSample::maxLength)
        return false;
    const auto* bytes = static_cast<const std::int8_t*> (out.getData());
    samples.assign (bytes, bytes + out.getDataSize());
    return true;
}
} // namespace bb
