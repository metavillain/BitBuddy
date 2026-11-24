#pragma once

#include <array>
#include <atomic>
#include <vector>

namespace bb
{
/** HUM TO PLAY (added in 1.2): listens to you humming or singing and turns it
    into notes for BitBuddy to play.

    How: the microphone audio is thinned out to ~11 kHz (a voice's own pitch is
    far below that), then every ~12 ms the "YIN" method looks for the delay at
    which the sound best repeats itself: that delay is one cycle of your voice,
    so 1 / delay is the pitch. A note starts when your voice is clear for ~25 ms,
    changes when you clearly move to another note, and stops when you go quiet.

    process() runs on the microphone's thread, popEvents() on BitBuddy's audio
    thread: events pass through a small lock-free queue. */
class HumTracker
{
public:
    HumTracker();

    /** Microphone thread: feed it audio (any sample rate). */
    void process (const float* samples, int numSamples, double sampleRate) noexcept;

    /** Ends the current note (e.g. when HUM is switched off). Microphone thread or UI
        thread - but not both at once. */
    void reset() noexcept;

    /** Audio thread: hands over the notes heard since last time: handle (note, on, velocity). */
    template <typename Handler>
    void popEvents (Handler&& handle) noexcept
    {
        int r = readPos.load (std::memory_order_relaxed);
        const int w = writePos.load (std::memory_order_acquire);
        while (r != w)
        {
            const auto& e = queue[(size_t) r];
            handle (e.note, e.on, e.velocity);
            r = (r + 1) % queueSize;
        }
        readPos.store (r, std::memory_order_release);
    }

    /** Pitch of the last analysis (Hz; 0 = no clear note), for the UI. */
    float getLastPitch() const noexcept { return lastPitch.load(); }

    /** For tests: the pitch (Hz) of a block of audio at ~11 kHz, 0 if unclear. */
    static float detectPitch (const float* x, int n, double sampleRate) noexcept;

private:
    void analyse() noexcept;
    void push (int note, bool on, float velocity) noexcept;

    static constexpr int window = 512, hop = 128;   // at the thinned-out rate (~46 ms / ~12 ms)
    std::array<float, window> ring {};
    int ringPos = 0, sinceHop = 0;
    double rate = 11025.0;
    int decimation = 4, decimCount = 0;
    float decimSum = 0.0f;

    int currentNote = -1, candidate = -1, candidateHops = 0, quietHops = 0, voicedHops = 0;
    float loudness = 0.0f;
    std::atomic<float> lastPitch { 0.0f };

    struct Event { int note; bool on; float velocity; };
    static constexpr int queueSize = 64;
    std::array<Event, queueSize> queue {};
    std::atomic<int> readPos { 0 }, writePos { 0 };
};
} // namespace bb
