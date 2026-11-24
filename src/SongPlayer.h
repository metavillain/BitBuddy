#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "ChipEngine.h"
#include "NotePlayer.h"

// =============================================================================
//  MIDI SONGS (added in 1.2, BitBuddy app): play any MIDI file the chiptune way.
//
//  Like a real NES tune, every part of the song gets one chip channel:
//    * the melody          -> your sound (BitBuddy's own engine, with all your settings)
//    * the lowest part     -> a triangle bass
//    * everything else     -> a pulse-wave "chords" channel
//    * drums (MIDI ch. 10) -> BitBuddy's drum kit
// =============================================================================
namespace bb
{
struct SongArrangement
{
    std::vector<NotePlayer::Note> notes;   // ready for the NotePlayer (channels below)
    juce::String name, parts;              // parts: who plays what, in words
    double seconds = 0.0;
    int lead = 0, bass = 0, chords = 0, drums = 0;   // notes per part
};

/** Reads a MIDI file and shares its parts out over the chip channels. No notes = not a usable song. */
SongArrangement arrangeMidiSong (const juce::MidiFile&, const juce::String& name);
SongArrangement arrangeMidiSong (juce::InputStream&, const juce::String& name);

/** A General MIDI drum (35 = kick, 38 = snare, 42 = hi-hat...) -> BitBuddy's kit (0 = C kick .. 11 = B crash). */
int gmDrumToKit (int gmNote) noexcept;

/** The song's backing band: three more chip engines. */
class SongBand
{
public:
    static constexpr int leadChannel = 16;   // the melody: your sound, without Scale Lock (songs have their own key)
    static constexpr int bassChannel = 2, chordsChannel = 3, drumsChannel = 4;

    void prepare (double sampleRate);

    /** Adds the band into `out` (mono), playing `events` at their exact sample positions.
        Returns false (and adds nothing) when the band is silent. */
    bool render (float* out, int numSamples, const juce::MidiBuffer& events) noexcept;
    bool isSounding() const noexcept;
    void allNotesOff() noexcept;

private:
    ChipEngine* engineFor (int channel) noexcept;
    ChipEngine bass, chords, drums;
};
} // namespace bb
