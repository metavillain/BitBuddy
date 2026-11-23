#include "Recorder.h"

namespace bb
{
Recorder::Recorder()
{
    writerThread.startThread();
}

Recorder::~Recorder()
{
    stop();
    writerThread.stopThread (2000);
}

juce::File Recorder::getRecordingsFolder()
{
   #if JUCE_IOS || JUCE_ANDROID
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("BitBuddy Recordings");
   #else
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("BitBuddy Recordings");
   #endif
}

bool Recorder::start (double newSampleRate)
{
    stop();

    auto folder = getRecordingsFolder();
    if (! folder.createDirectory())
        return false;

    const auto name = "BitBuddy " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    wavFile = folder.getNonexistentChildFile (name, ".wav");
    midiFile = wavFile.withFileExtension ("mid");
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    std::unique_ptr<juce::OutputStream> stream = wavFile.createOutputStream();
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate)
                                                                                 .withNumChannels (2)
                                                                                 .withBitsPerSample (24));
    if (writer == nullptr)
        return false;

    sequence.clear();
    fifo.reset();
    samplesWritten = 0;
    threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 32768);
    {
        const juce::ScopedLock sl (writerLock);
        activeWriter = threadedWriter.get();
    }
    recording = true;
    startTimerHz (10);
    return true;
}

void Recorder::stop()
{
    if (! recording.exchange (false))
        return;

    stopTimer();
    {
        const juce::ScopedLock sl (writerLock);
        activeWriter = nullptr;
    }
    threadedWriter.reset();   // flushes and closes the WAV
    drainMidi();

    // MIDI file: 960 ticks per beat at 120 BPM -> 1920 ticks per second
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0.0);
    track.addEvent (juce::MidiMessage::textMetaEvent (3, "BitBuddy"), 0.0);
    for (auto* e : sequence)
    {
        auto m = e->message;
        m.setTimeStamp (m.getTimeStamp() * 1920.0);
        track.addEvent (m);
    }
    track.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (960);
    file.addTrack (track);
    midiFile.deleteFile();
    if (auto out = midiFile.createOutputStream())
        file.writeTo (*out);
}

void Recorder::addMidi (const juce::MidiMessage& m, int samplePositionInBlock) noexcept
{
    if (! recording.load (std::memory_order_relaxed))
        return;
    if (! (m.isNoteOnOrOff() || m.isSustainPedalOn() || m.isSustainPedalOff()) || m.getRawDataSize() > 3)
        return;

    const auto scope = fifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 < 1)
        return;   // full: drop the event rather than block the audio thread

    auto& e = fifoEvents[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
    e.sample = samplesWritten.load (std::memory_order_relaxed) + samplePositionInBlock;
    const auto* raw = m.getRawData();
    for (int i = 0; i < 3; ++i)
        e.data[i] = i < m.getRawDataSize() ? raw[i] : 0;
}

void Recorder::writeAudio (const float* const* channels, int numSamples) noexcept
{
    if (! recording.load (std::memory_order_relaxed))
        return;

    const juce::ScopedLock sl (writerLock);
    if (activeWriter != nullptr && activeWriter->write (channels, numSamples))
        samplesWritten += numSamples;
}

void Recorder::drainMidi()
{
    const auto scope = fifo.read (fifo.getNumReady());
    auto take = [this] (int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& e = fifoEvents[(size_t) i];
            const auto msg = juce::MidiMessage (e.data[0], e.data[1], e.data[2], (double) e.sample / sampleRate);
            sequence.addEvent (msg);
        }
    };
    take (scope.startIndex1, scope.blockSize1);
    take (scope.startIndex2, scope.blockSize2);
}
} // namespace bb
