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

// The Commodore 64's SID chip had a resonant filter - the "squelchy", rubbery
// sound of C64 basses. This is a 12 dB/octave state-variable filter (low,
// band or high pass) with an envelope that opens it on every new note and
// lets it close again: turn ENV up and RESO up for that classic "wow".
class SidFilter
{
public:
    enum Mode { Off = 0, LowPass, BandPass, HighPass };

    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        reset();
        smoothedCutoff = cutoffHz;
    }

    void reset() noexcept
    {
        ic1 = ic2 = 0.0f;
        env = 0.0f;
        countdown = 0;
    }

    void setParams (int newMode, float newCutoffHz, float resonance01, float envAmount01, float envDecaySeconds) noexcept
    {
        mode = std::clamp (newMode, 0, 3);
        cutoffHz = std::clamp (newCutoffHz, 20.0f, 20000.0f);
        // k = 1/Q: 2 (no resonance) down to 0.06 (almost whistling, like a SID turned all the way up)
        k = 2.0f - 1.94f * std::clamp (resonance01, 0.0f, 1.0f);
        envOctaves = 5.0f * std::clamp (envAmount01, 0.0f, 1.0f);
        envMul = (float) std::exp (-(double) kControlInterval / (std::max (0.01, (double) envDecaySeconds) * 0.5 * sr));
        makeup = 1.0f - 0.35f * std::clamp (resonance01, 0.0f, 1.0f);   // resonance gets loud: compensate a little
    }

    bool isOn() const noexcept { return mode != Off; }

    /** A new note: snap the envelope open. */
    void trigger() noexcept { env = 1.0f; countdown = 0; }

    float process (float x) noexcept
    {
        if (--countdown <= 0)
        {
            countdown = kControlInterval;
            updateCoefficients();
        }

        // Topology-preserving state-variable filter (stays stable even when the cutoff moves fast)
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        float y;
        switch (mode)
        {
            case LowPass:  y = v2; break;
            case BandPass: y = k * v1; break;   // peak level 1, whatever the resonance
            case HighPass: y = x - k * v1 - v2; break;
            default:       return x;
        }
        return y * makeup;
    }

    /** Analogue response at a frequency, for drawing the curve (1 = unchanged). */
    static float magnitudeAt (int mode, float hz, float cutoff, float resonance01) noexcept
    {
        if (mode == Off)
            return 1.0f;
        const float kk = 2.0f - 1.94f * std::clamp (resonance01, 0.0f, 1.0f);
        const float w = hz / cutoff;
        const float denom = std::sqrt ((1.0f - w * w) * (1.0f - w * w) + kk * kk * w * w);
        switch (mode)
        {
            case LowPass:  return 1.0f / denom;
            case BandPass: return kk * w / denom;
            default:       return w * w / denom;
        }
    }

private:
    static constexpr int kControlInterval = 16;

    void updateCoefficients() noexcept
    {
        smoothedCutoff += (cutoffHz - smoothedCutoff) * 0.15f;   // no zipper noise when you turn the knob
        float hz = smoothedCutoff;
        if (envOctaves > 0.0f && env > 1.0e-4f)
        {
            hz *= std::exp2 (envOctaves * env);
            env *= envMul;
        }
        hz = std::min (hz, (float) (sr * 0.45));

        const float g = (float) std::tan (3.14159265358979 * hz / sr);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    double sr = 44100.0;
    int mode = Off, countdown = 0;
    float cutoffHz = 2000.0f, smoothedCutoff = 2000.0f, k = 2.0f;
    float envOctaves = 0.0f, envMul = 0.99f, env = 0.0f, makeup = 1.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
};

// The tone of each console's output circuit: the NES trims deep bass and the
// very top, the Game Boy sounds a bit muffled, the C64 a touch soft.
class ConsoleTone
{
public:
    void prepare (double sampleRate) noexcept { sr = sampleRate; reset(); }
    void reset() noexcept { hpX = hpY = lpY = 0.0f; }

    /** 0 Modern (untouched), 1 NES, 2 Game Boy, 3 C64 */
    void setConsole (int console) noexcept
    {
        float hpHz = 0.0f, lpHz = 0.0f;
        switch (console)
        {
            case 1: hpHz = 90.0f; lpHz = 14000.0f; break;
            case 2: hpHz = 30.0f; lpHz = 9000.0f;  break;
            case 3: hpHz = 20.0f; lpHz = 12000.0f; break;
            default: break;
        }
        on = console != 0;
        hpCoef = hpHz > 0.0f ? (float) std::exp (-6.283185307 * hpHz / sr) : 1.0f;
        lpCoef = lpHz > 0.0f ? (float) (1.0 - std::exp (-6.283185307 * std::min ((double) lpHz, sr * 0.45) / sr)) : 1.0f;
    }

    float process (float x) noexcept
    {
        if (! on)
            return x;
        const float hp = hpCoef * (hpY + x - hpX);   // one-pole high-pass
        hpX = x;
        hpY = hp;
        lpY += lpCoef * (hp - lpY);                   // one-pole low-pass
        return lpY;
    }

private:
    double sr = 44100.0;
    bool on = false;
    float hpCoef = 1.0f, lpCoef = 1.0f, hpX = 0.0f, hpY = 0.0f, lpY = 0.0f;
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

    /** inL / inR: the dry sound (the same value twice for a mono sound). */
    void process (float inL, float inR, float mix, float feedback, float& outL, float& outR) noexcept
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

        left[(size_t) writePos]  = 0.5f * (inL + inR) + echoR * feedback;   // L -> R -> L ...
        right[(size_t) writePos] = echoL * feedback;

        if (++writePos >= size)
            writePos = 0;

        outL = inL + echoL * mix;
        outR = inR + echoR * mix;
    }

private:
    double sr = 44100.0;
    int size = 1, writePos = 0;
    float delay = 1.0f, target = 1.0f;
    std::vector<float> left, right;
};
//==============================================================================
/** Everything one channel goes through before the echo: DC blocker -> C64 filter ->
    console tone -> crunch. A mono sound uses one; STEREO "Wide" uses one per side. */
struct ChannelFx
{
    DcBlocker dc;
    SidFilter filter;
    ConsoleTone tone;
    Crusher crusher;

    void prepare (double sampleRate)
    {
        filter.prepare (sampleRate);
        tone.prepare (sampleRate);
        crusher.prepare (sampleRate);
        reset();
    }

    void reset() noexcept
    {
        dc.reset();
        filter.reset();
        tone.reset();
        crusher.reset();
    }

    float process (float x, bool useFilter) noexcept
    {
        x = dc.process (x);
        if (useFilter)
            x = filter.process (x);
        return crusher.process (tone.process (x));
    }
};
} // namespace bb
