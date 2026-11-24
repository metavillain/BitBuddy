#pragma once

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

#include "NotePlayer.h"
#include "Scales.h"

// =============================================================================
//  GAME SOUNDS (added in 1.2)
//
//  * Sound effect generator: one tap makes a brand new COIN, JUMP, LASER...
//    Each recipe picks random settings inside the range that makes that kind of
//    sound, so every tap is different but always recognisable.
//  * Jingles: short tunes for the big moments of a game (level clear, game
//    over...), written fresh each time in your KEY and SCALE.
// =============================================================================
namespace bb
{
enum class Sfx : int { Coin = 0, Jump, Laser, Hit, Boom, PowerUp, Blip, Bomb };
constexpr int kNumSfx = 8;
const char* sfxName (int index);   // "COIN", "JUMP"...
const char* sfxHint (int index);   // one line for the help bar

struct SoundEffect
{
    std::vector<std::pair<const char*, float>> values;   // parameter id -> real value (the rest: defaults)
    int note = 72;                                       // the note that plays it
    double length = 0.3;                                 // how long that note is held (seconds)
};

SoundEffect makeSoundEffect (Sfx, juce::Random&);

enum class Jingle : int { LevelClear = 0, GameOver, GotItem, Fanfare };
constexpr int kNumJingles = 4;
const char* jingleName (int index);   // "LEVEL CLEAR"...
const char* jingleHint (int index);

/** A new jingle in this key + scale (Off = major). Melody notes and the odd chord
    note, all on channel 1 (your sound). */
std::vector<NotePlayer::Note> makeJingle (Jingle, int key, Scale, double bpm, juce::Random&);
} // namespace bb
