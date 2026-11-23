#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <array>
#include <atomic>

namespace bb
{
// =============================================================================
//  RECORD button (BitBuddy app): saves what you play as a WAV file (the sound)
//  plus a MIDI file (the notes, to open in any music program).
//
//  The audio thread hands its samples to a background thread that writes the
//  file, so recording never causes glitches.
// =============================================================================
class Recorder : private juce::Timer
{
public:
    Recorder();
    ~Recorder() override;

    /** Music/BitBuddy Recordings on computers, the app's documents folder on phones. */
    static juce::File getRecordingsFolder();

    /** Message thread. Returns false if the files couldn't be created. */
    bool start (double sampleRate);
    /** Message thread: closes the WAV and writes the MIDI file. */
    void stop();

    bool isRecording() const noexcept { return recording.load(); }
    double getSecondsRecorded() const noexcept { return (double) samplesWritten.load() / sampleRate; }
    juce::File getWavFile() const { return wavFile; }
    juce::File getMidiFile() const { return midiFile; }

    // --- audio thread ---
    /** Note on / off and sustain pedal, at a position in the current block. Call before writeAudio(). */
    void addMidi (const juce::MidiMessage&, int samplePositionInBlock) noexcept;
    void writeAudio (const float* const* channels, int numSamples) noexcept;

private:
    void timerCallback() override { drainMidi(); }
    void drainMidi();

    juce::TimeSliceThread writerThread { "BitBuddy recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::CriticalSection writerLock;
    juce::AudioFormatWriter::ThreadedWriter* activeWriter = nullptr;

    std::atomic<bool> recording { false };
    std::atomic<juce::int64> samplesWritten { 0 };
    double sampleRate = 44100.0;

    struct NoteEvent { juce::int64 sample; juce::uint8 data[3]; };
    static constexpr int fifoSize = 4096;
    juce::AbstractFifo fifo { fifoSize };
    std::array<NoteEvent, fifoSize> fifoEvents {};
    juce::MidiMessageSequence sequence;

    juce::File wavFile, midiFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Recorder)
};
} // namespace bb
