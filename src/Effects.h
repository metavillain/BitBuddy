#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace bb
{
// Removes any constant offset ("DC") so the speakers swing evenly.
class DcBlocker
{
public:
    void reset() noexcept { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = x - x1 + 0.9995f * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float x1 = 0.0f, y1 = 0.0f;
};

// "Crunch": fewer bits = coarser volume steps; lower rate = grainier, aliased
// sound, like cheap old sound hardware.
class Crusher
{
public:
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        phase = 1.0;
        heldSample = 0.0f;
    }

    void setParams (int bits, float rateHz) noexcept
    {
        ratio = std::min (1.0, (double) rateHz / sr);
        // Quantise relative to a 0.5 full scale so the effect is clearly
        // audible at normal playing levels. 16 bits = off.
        quant = bits >= 16 ? 0.0f : (float) (1 << std::clamp (bits, 1, 15));
    }

    float process (float x) noexcept
    {
        if (ratio < 0.999)
        {
            phase += ratio;
            if (phase >= 1.0)
            {
                phase -= 1.0;
                heldSample = x;
            }
            x = heldSample;
        }

        if (quant > 0.0f)
            x = std::round (x * quant) / quant;

        return x;
    }

private:
    double sr = 44100.0, ratio = 1.0, phase = 1.0;
    float heldSample = 0.0f, quant = 0.0f;
};

// Echo that bounces between the left and right speaker.
class PingPongEcho
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        size = (int) (sr * 2.5) + 4;
        left.assign ((size_t) size, 0.0f);
        right.assign ((size_t) size, 0.0f);
        writePos = 0;
        delay = target = (float) (sr * 0.3);
    }

    void clear() noexcept
    {
        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
    }

    /** Longest possible echo delay. After this much silence the echo is empty. */
    int getMaxDelaySamples() const noexcept { return size; }

    void setTimeSeconds (double seconds) noexcept
    {
        target = (float) std::clamp (seconds * sr, 16.0, (double) size - 4.0);
    }

    void process (float in, float mix, float feedback, float& outL, float& outR) noexcept
    {
        delay += (target - delay) * 0.0007f;   // glide smoothly when the tempo changes

        float readPos = (float) writePos - delay;
        if (readPos < 0.0f)
            readPos += (float) size;

        const int i0 = std::min ((int) readPos, size - 1);
        const int i1 = (i0 + 1) % size;
        const float frac = readPos - (float) i0;

        const float echoL = left[(size_t) i0]  + (left[(size_t) i1]  - left[(size_t) i0])  * frac;
        const float echoR = right[(size_t) i0] + (right[(size_t) i1] - right[(size_t) i0]) * frac;

        left[(size_t) writePos]  = in + echoR * feedback;   // L -> R -> L ...
        right[(size_t) writePos] = echoL * feedback;

        if (++writePos >= size)
            writePos = 0;

        outL = in + echoL * mix;
        outR = in + echoR * mix;
    }

private:
    double sr = 44100.0;
    int size = 1, writePos = 0;
    float delay = 1.0f, target = 1.0f;
    std::vector<float> left, right;
};
} // namespace bb
