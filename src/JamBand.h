#pragma once

#include "ChipEngine.h"

// =============================================================================
//  JAM MODE: a little chiptune band (drums, bass, chords) that plays in your
//  KEY and SCALE, so you can make up a melody on top. It's three more copies of
//  BitBuddy's own chip engine, driven by a 16-step-per-bar sequencer.
// =============================================================================
namespace bb
{
constexpr int kNumJamStyles = 4;
const char* jamStyleName (int style);      // "Adventure", "Dungeon", "Racing", "Chill"
double jamStyleTempo (int style);          // its natural tempo (BPM)

class JamBand
{
public:
    void prepare (double sampleRate);

    void setSong (int key, Scale scale) noexcept;   // follow your KEY + SCALE
    void setStyle (int style) noexcept;
    void setTempo (double bpm) noexcept;
    void setTrackLevels (float drums, float bass, float chords) noexcept;   // 0..1 (tests mute tracks)

    void start() noexcept;
    void stop() noexcept;                    // notes ring out, then silence
    bool isPlaying() const noexcept { return playing; }
    bool isSounding() const noexcept;        // playing, or still ringing out

    /** The next mono sample of the band. Cheap: renders in small chunks. */
    float nextSample() noexcept
    {
        if (chunkPos >= chunkLength)
            renderChunk();
        return chunk[(size_t) chunkPos++];
    }

private:
    void renderChunk() noexcept;
    void playStep() noexcept;
    void configureEngines() noexcept;
    int chordRoot (int bar) const noexcept;         // pitch class (0..11) of this bar's chord

    ChipEngine drums, bass, chords;
    double sampleRate = 44100.0, bpm = 120.0, samplesPerStep = 5512.5, stepCounter = 0.0;
    int style = 0, key = 0, step = -1;
    Scale scale = Scale::Major;
    bool playing = false, configured = false;
    float drumLevel = 1.0f, bassLevel = 1.0f, chordLevel = 1.0f;

    int bassNote = -1, bassOffStep = -1, chordNote = -1;

    static constexpr int kChunk = 32;
    std::array<float, kChunk> chunk {};
    int chunkPos = kChunk, chunkLength = kChunk;
};
} // namespace bb
