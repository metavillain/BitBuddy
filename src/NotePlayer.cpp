#include "NotePlayer.h"

#include <algorithm>

namespace bb
{
void NotePlayer::play (const std::vector<Note>& notes)
{
    std::vector<Event> events;
    events.reserve (notes.size() * 2);
    double length = 0.0;
    for (const auto& n : notes)
    {
        if (n.note < 0 || n.note > 127 || n.length <= 0.0)
            continue;
        const int channel = juce::jlimit (1, 16, n.channel);
        events.push_back ({ n.start, n.note, n.velocity, channel, true });
        events.push_back ({ n.start + n.length, n.note, 0.0f, channel, false });
        length = std::max (length, n.start + n.length);
    }

    // In time order; at the same moment note-offs come first (so a repeated note restarts)
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b)
    {
        if (a.time != b.time)
            return a.time < b.time;
        return ! a.on && b.on;
    });

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        pending.swap (events);   // the old list (if any) is freed here, on the UI thread
        hasPending = true;
        stopRequested = false;
    }
    lengthSeconds = length;
}

void NotePlayer::stop()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    hasPending = false;
    stopRequested = true;
}

void NotePlayer::releaseAll (juce::MidiBuffer& main, juce::MidiBuffer* band) noexcept
{
    for (int ch = 0; ch < 16; ++ch)
    {
        auto* dest = (ch == 0 || ch == 15) ? &main : band;
        if (dest != nullptr && sounding[(size_t) ch].any())
            for (int note = 0; note < 128; ++note)
                if (sounding[(size_t) ch][(size_t) note])
                    dest->addEvent (juce::MidiMessage::noteOff (ch + 1, note), 0);
        sounding[(size_t) ch].reset();
    }
}

void NotePlayer::addEvents (juce::MidiBuffer& main, juce::MidiBuffer* band, int numSamples, double sampleRate) noexcept
{
    {
        // Never wait for the UI here: if it is just handing over a list, pick it up next block
        const juce::SpinLock::ScopedTryLockType tl (lock);
        if (tl.isLocked() && (hasPending || stopRequested))
        {
            releaseAll (main, band);
            if (hasPending)
                active.swap (pending);   // the old list goes back to the UI side to be freed
            else
                active.clear();          // (keeps its memory: nothing is freed on the audio thread)
            hasPending = stopRequested = false;
            next = 0;
            posSamples = 0.0;
            playing = ! active.empty();
        }
    }

    if (! playing.load (std::memory_order_relaxed))
        return;

    const double blockEnd = posSamples + numSamples;
    for (; next < active.size(); ++next)
    {
        const auto& e = active[next];
        const double at = e.time * sampleRate;
        if (at >= blockEnd)
            break;

        const int offset = juce::jlimit (0, numSamples - 1, (int) (at - posSamples));
        const auto ch = (size_t) (e.channel - 1);
        auto* dest = (e.channel == 1 || e.channel == 16) ? &main : band;
        if (e.on)
        {
            if (dest != nullptr)
                dest->addEvent (juce::MidiMessage::noteOn (e.channel, e.note, e.velocity), offset);
            sounding[ch].set ((size_t) e.note);
        }
        else if (sounding[ch][(size_t) e.note])
        {
            if (dest != nullptr)
                dest->addEvent (juce::MidiMessage::noteOff (e.channel, e.note), offset);
            sounding[ch].reset ((size_t) e.note);
        }
    }

    posSamples = blockEnd;
    positionSeconds = posSamples / sampleRate;
    if (next >= active.size())
        playing = false;
}
} // namespace bb
