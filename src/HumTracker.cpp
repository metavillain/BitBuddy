#include "HumTracker.h"

#include <algorithm>
#include <cmath>

namespace bb
{
HumTracker::HumTracker() = default;

float HumTracker::detectPitch (const float* x, int n, double sampleRate) noexcept
{
    // YIN: d(tau) = sum (x[i] - x[i + tau])^2, normalised by its running mean; the first
    // dip below 0.15 is one cycle of the voice. Voices: about 70 Hz .. 1000 Hz.
    const int minTau = std::max (2, (int) (sampleRate / 1000.0));
    const int maxTau = std::min (n / 2, (int) (sampleRate / 70.0));
    if (maxTau <= minTau + 2)
        return 0.0f;

    float d[400] {};
    const int count = std::min (maxTau + 2, 400);
    const int len = n - count;
    float runningSum = 0.0f;
    int best = -1;
    for (int tau = 1; tau < count; ++tau)
    {
        float sum = 0.0f;
        for (int i = 0; i < len; ++i)
        {
            const float diff = x[i] - x[i + tau];
            sum += diff * diff;
        }
        runningSum += sum;
        d[tau] = runningSum > 0.0f ? sum * (float) tau / runningSum : 1.0f;
    }
    for (int tau = minTau; tau < count - 1; ++tau)
        if (d[tau] < 0.15f)
        {
            while (tau + 1 < count - 1 && d[tau + 1] < d[tau])   // walk down to the bottom of the dip
                ++tau;
            best = tau;
            break;
        }
    if (best < 0)
        return 0.0f;

    // Between samples: a parabola through the dip and its neighbours
    const float a = d[best - 1], b = d[best], c = d[best + 1];
    const float denom = a - 2.0f * b + c;
    const float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f;
    return (float) (sampleRate / ((float) best + std::clamp (shift, -0.5f, 0.5f)));
}

void HumTracker::process (const float* samples, int numSamples, double sampleRate) noexcept
{
    // Thin out to ~11 kHz by averaging (which also smooths away hiss)
    const int wanted = std::max (1, (int) std::lround (sampleRate / 11025.0));
    if (wanted != decimation)
    {
        decimation = wanted;
        decimCount = 0;
        decimSum = 0.0f;
    }
    rate = sampleRate / decimation;

    for (int i = 0; i < numSamples; ++i)
    {
        decimSum += samples[i];
        if (++decimCount < decimation)
            continue;
        const float x = decimSum / (float) decimation;
        decimCount = 0;
        decimSum = 0.0f;

        ring[(size_t) ringPos] = x;
        ringPos = (ringPos + 1) % window;
        loudness = std::max (std::abs (x), loudness * 0.9995f);
        if (++sinceHop >= hop)
        {
            sinceHop = 0;
            analyse();
        }
    }
}

void HumTracker::analyse() noexcept
{
    std::array<float, window> x {};
    float energy = 0.0f;
    for (int i = 0; i < window; ++i)
    {
        x[(size_t) i] = ring[(size_t) ((ringPos + i) % window)];
        energy += x[(size_t) i] * x[(size_t) i];
    }
    const float rms = std::sqrt (energy / window);

    // Too quiet (below about -45 dB): not singing
    const float hz = rms > 0.006f ? detectPitch (x.data(), window, rate) : 0.0f;
    lastPitch = hz;

    if (hz <= 0.0f)
    {
        voicedHops = 0;
        candidate = -1;
        if (currentNote >= 0 && ++quietHops >= 3)   // ~35 ms of quiet: the note ends
        {
            push (currentNote, false, 0.0f);
            currentNote = -1;
        }
        return;
    }

    quietHops = 0;
    const float exact = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    const int note = std::clamp ((int) std::lround (exact), 24, 108);
    const float velocity = std::clamp (0.4f + rms * 4.0f, 0.4f, 1.0f);

    if (currentNote < 0)
    {
        // A new note once the voice has been clear for 2 analyses (~25 ms)
        if (++voicedHops >= 2)
        {
            currentNote = note;
            push (note, true, velocity);
        }
        return;
    }

    // Stay on the note through little wobbles; move when you clearly sing another one
    if (std::abs (exact - (float) currentNote) < 0.7f)
    {
        candidate = -1;
        return;
    }
    if (note != candidate)
    {
        candidate = note;
        candidateHops = 0;
    }
    if (++candidateHops >= 2)
    {
        push (currentNote, false, 0.0f);
        currentNote = note;
        push (note, true, velocity);
        candidate = -1;
    }
}

void HumTracker::reset() noexcept
{
    if (currentNote >= 0)
        push (currentNote, false, 0.0f);
    currentNote = candidate = -1;
    voicedHops = quietHops = candidateHops = 0;
    lastPitch = 0.0f;
}

void HumTracker::push (int note, bool on, float velocity) noexcept
{
    const int w = writePos.load (std::memory_order_relaxed);
    const int next = (w + 1) % queueSize;
    if (next == readPos.load (std::memory_order_acquire))
        return;   // full (BitBuddy isn't playing audio?) - drop it
    queue[(size_t) w] = { note, on, velocity };
    writePos.store (next, std::memory_order_release);
}

} // namespace bb
