#pragma once

// How clearly can you still hear the words? A compact version of STOI ("short-time
// objective intelligibility", Taal et al., IEEE TASLP 2011), the standard score for this.
//
// It compares how the loudness of 15 speech bands (150 Hz .. 4.3 kHz) rises and falls
// over 0.4-second windows in the clean voice and in what BitBuddy plays. Roughly:
// 1.0 = the same words, 0.85+ = clear, 0.7 = you can follow it with some effort,
// below 0.5 = mostly mush.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace bbtest
{
constexpr double stoiRate = 10000.0;

/** Windowed-sinc resampler (only for tests: slow but clean). */
inline std::vector<float> resampleTo (const float* x, int n, double fromRate, double toRate)
{
    constexpr double pi = 3.14159265358979323846;
    const double ratio = fromRate / toRate;                            // input samples per output sample
    const double fc = 0.45 * std::min (fromRate, toRate) / fromRate;   // cutoff, cycles per input sample
    const int width = (int) std::ceil (10.0 / (2.0 * fc));
    const int outN = n > 1 ? (int) std::floor ((n - 1) / ratio) + 1 : 0;
    std::vector<float> y ((size_t) outN, 0.0f);
    for (int i = 0; i < outN; ++i)
    {
        const double t = i * ratio;
        const int k0 = std::max (0, (int) std::ceil (t - width)), k1 = std::min (n - 1, (int) std::floor (t + width));
        double sum = 0.0;
        for (int k = k0; k <= k1; ++k)
        {
            const double d = t - k, a = 2.0 * fc * d;
            const double sinc = std::abs (a) < 1.0e-9 ? 1.0 : std::sin (pi * a) / (pi * a);
            sum += x[k] * 2.0 * fc * sinc * (0.5 + 0.5 * std::cos (pi * d / width));
        }
        y[(size_t) i] = (float) sum;
    }
    return y;
}

namespace detail
{
inline void fft512 (std::array<std::complex<double>, 512>& a)
{
    constexpr int n = 512;
    for (int i = 1, j = 0; i < n; ++i)   // bit reversal
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[(size_t) i], a[(size_t) j]);
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.14159265358979323846 / len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (int i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (int k = 0; k < len / 2; ++k)
            {
                const auto u = a[(size_t) (i + k)], v = a[(size_t) (i + k + len / 2)] * w;
                a[(size_t) (i + k)] = u + v;
                a[(size_t) (i + k + len / 2)] = u - v;
                w *= wl;
            }
        }
    }
}

using BandFrame = std::array<double, 15>;

/** One-third octave band levels of every 25.6 ms frame (hop 12.8 ms). */
inline std::vector<BandFrame> bandFrames (const std::vector<float>& x, std::vector<double>& frameDb)
{
    constexpr int frame = 256, hop = 128, nfft = 512;
    constexpr double pi = 3.14159265358979323846;
    static const auto bandBins = []
    {
        std::array<std::pair<int, int>, 15> bins {};
        for (int j = 0; j < 15; ++j)
        {
            const double lo = 150.0 * std::pow (2.0, (2.0 * j - 1.0) / 6.0), hi = 150.0 * std::pow (2.0, (2.0 * j + 1.0) / 6.0);
            bins[(size_t) j] = { (int) std::lround (lo * nfft / stoiRate), (int) std::lround (hi * nfft / stoiRate) };
        }
        return bins;
    }();

    std::vector<BandFrame> out;
    frameDb.clear();
    for (size_t start = 0; start + frame <= x.size(); start += hop)
    {
        std::array<std::complex<double>, nfft> a {};
        double energy = 0.0;
        for (int i = 0; i < frame; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * pi * (i + 1) / (frame + 1));
            const double s = x[start + (size_t) i] * w;
            a[(size_t) i] = s;
            energy += s * s;
        }
        frameDb.push_back (10.0 * std::log10 (energy + 1.0e-20));
        fft512 (a);
        BandFrame bands {};
        for (int j = 0; j < 15; ++j)
        {
            double sum = 0.0;
            for (int k = bandBins[(size_t) j].first; k < bandBins[(size_t) j].second; ++k)
                sum += std::norm (a[(size_t) k]);
            bands[(size_t) j] = std::sqrt (sum);
        }
        out.push_back (bands);
    }
    return out;
}
} // namespace detail

/** The score (about 0..1) of `processed` (played at `processedRate`) against the clean
    voice (at `cleanRate`). Delays (-maxEarlyMs..40 ms) are lined up first. */
inline double speechClarity (const std::vector<float>& clean, double cleanRate,
                             const std::vector<float>& processed, double processedRate, int maxEarlyMs = 0)
{
    const auto x = resampleTo (clean.data(), (int) clean.size(), cleanRate, stoiRate);
    auto y = resampleTo (processed.data(), (int) processed.size(), processedRate, stoiRate);
    if (x.size() < 2000)
        return 0.0;

    // Line up: the delay where the two match best
    int bestLag = 0;
    double best = -1.0e30;
    for (int lag = -maxEarlyMs * 10; lag <= 400; lag += 2)
    {
        double dot = 0.0, yy = 1.0e-20;
        for (size_t i = 0; i < x.size(); i += 2)
        {
            const auto j = (std::ptrdiff_t) i + lag;
            if (j < 0 || j >= (std::ptrdiff_t) y.size())
                continue;
            dot += x[i] * y[(size_t) j];
            yy += y[(size_t) j] * y[(size_t) j];
        }
        const double score = dot / std::sqrt (yy);
        if (score > best)
        {
            best = score;
            bestLag = lag;
        }
    }
    std::vector<float> aligned (x.size(), 0.0f);
    for (size_t i = 0; i < x.size(); ++i)
        if (const auto j = (std::ptrdiff_t) i + bestLag; j >= 0 && j < (std::ptrdiff_t) y.size())
            aligned[i] = y[(size_t) j];

    std::vector<double> xDb, yDb;
    const auto xb = detail::bandFrames (x, xDb), yb = detail::bandFrames (aligned, yDb);

    // Leave out the silent moments (40 dB below the loudest frame of the clean voice)
    const double loudest = *std::max_element (xDb.begin(), xDb.end());
    std::vector<detail::BandFrame> xs, ys;
    for (size_t i = 0; i < xb.size(); ++i)
        if (xDb[i] > loudest - 40.0)
        {
            xs.push_back (xb[i]);
            ys.push_back (yb[i]);
        }

    constexpr int segment = 30;   // 384 ms
    const double clip = 1.0 + std::pow (10.0, 15.0 / 20.0);
    double total = 0.0;
    int count = 0;
    for (int m = segment; m <= (int) xs.size(); ++m)
        for (int j = 0; j < 15; ++j)
        {
            std::array<double, segment> a {}, b {};
            double ea = 0.0, eb = 0.0;
            for (int k = 0; k < segment; ++k)
            {
                a[(size_t) k] = xs[(size_t) (m - segment + k)][(size_t) j];
                b[(size_t) k] = ys[(size_t) (m - segment + k)][(size_t) j];
                ea += a[(size_t) k] * a[(size_t) k];
                eb += b[(size_t) k] * b[(size_t) k];
            }
            const double alpha = std::sqrt (ea / (eb + 1.0e-20));
            double ma = 0.0, mb = 0.0;
            for (int k = 0; k < segment; ++k)
            {
                b[(size_t) k] = std::min (alpha * b[(size_t) k], clip * a[(size_t) k]);
                ma += a[(size_t) k];
                mb += b[(size_t) k];
            }
            ma /= segment;
            mb /= segment;
            double num = 0.0, da = 0.0, db = 0.0;
            for (int k = 0; k < segment; ++k)
            {
                num += (a[(size_t) k] - ma) * (b[(size_t) k] - mb);
                da += (a[(size_t) k] - ma) * (a[(size_t) k] - ma);
                db += (b[(size_t) k] - mb) * (b[(size_t) k] - mb);
            }
            total += num / (std::sqrt (da * db) + 1.0e-20);
            ++count;
        }
    return count > 0 ? total / count : 0.0;
}
} // namespace bbtest
