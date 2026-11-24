#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <bitset>
#include <vector>

namespace bb
{
/** Plays a list of notes into BitBuddy, sample-accurately: sound-effect previews,
    jingles and MIDI songs (added in 1.2).

    The UI thread hands over a whole list with play(); the audio thread picks it up
    at the start of its next block. Lists are swapped, never copied or freed on the
    audio thread, so playing never allocates memory there. */
class NotePlayer
{
public:
    struct Note
    {
        double start, length;   // seconds
        int note;               // MIDI note number
        float velocity;         // 0..1
        int channel = 1;        // 1 = your sound, 16 = your sound without Scale Lock, 2..15 = the song band
    };

    /** UI thread: plays these notes, starting now (whatever was playing stops). */
    void play (const std::vector<Note>& notes);

    /** UI thread: stops (sounding notes get their note-offs). */
    void stop();

    bool isPlaying() const noexcept     { return playing.load(); }
    double getPosition() const noexcept { return positionSeconds.load(); }   // seconds into the list
    double getLength() const noexcept   { return lengthSeconds.load(); }

    /** Audio thread: adds this block's note-ons and note-offs. Channels 1 and 16 go to
        `main`, the others to `band` (dropped when band is nullptr). */
    void addEvents (juce::MidiBuffer& main, juce::MidiBuffer* band, int numSamples, double sampleRate) noexcept;

private:
    struct Event { double time; int note; float velocity; int channel; bool on; };

    void releaseAll (juce::MidiBuffer& main, juce::MidiBuffer* band) noexcept;

    juce::SpinLock lock;
    std::vector<Event> pending, active;   // pending: handed over by the UI; active: playing (audio thread)
    bool hasPending = false, stopRequested = false;

    size_t next = 0;           // next event in `active`
    double posSamples = 0.0;   // samples played of `active`
    std::array<std::bitset<128>, 16> sounding {};   // notes that got a note-on but no note-off yet

    std::atomic<bool> playing { false };
    std::atomic<double> positionSeconds { 0.0 }, lengthSeconds { 0.0 };
};
} // namespace bb
