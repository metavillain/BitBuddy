#pragma once

#include <initializer_list>

// =============================================================================
//  Musical scales for Scale Lock, Smart Chords, the keyboard display and Jam Mode.
//
//  A scale is a set of the 12 note names (C, C#, D ... B) that sound good
//  together. Scale Lock moves every key you press to the nearest note of the
//  chosen scale, so nothing you play can sound "wrong".
// =============================================================================
namespace bb
{
enum class Scale : int { Off = 0, Major, Minor, MajorPenta, MinorPenta, Blues, Dorian, HarmonicMinor };
constexpr int kNumScales = 8;

namespace detail
{
constexpr int bitsOf (std::initializer_list<int> semitones)
{
    int mask = 0;
    for (int s : semitones)
        mask |= 1 << s;
    return mask;
}

// Bit n = the note n semitones above the key is part of the scale
constexpr int kScaleMasks[kNumScales] = {
    0xFFF,                               // Off: every note
    bitsOf ({ 0, 2, 4, 5, 7, 9, 11 }),   // Major            (happy)
    bitsOf ({ 0, 2, 3, 5, 7, 8, 10 }),   // Minor            (sad, serious)
    bitsOf ({ 0, 2, 4, 7, 9 }),          // Major pentatonic (only 5 notes - can't miss)
    bitsOf ({ 0, 3, 5, 7, 10 }),         // Minor pentatonic (rock, blues)
    bitsOf ({ 0, 3, 5, 6, 7, 10 }),      // Blues
    bitsOf ({ 0, 2, 3, 5, 7, 9, 10 }),   // Dorian           (adventure, RPG towns)
    bitsOf ({ 0, 2, 3, 5, 7, 8, 11 }),   // Harmonic minor   (spooky castles)
};
} // namespace detail

/** Pitch class (0..11) of a note relative to the key. */
constexpr int degreeOf (int note, int key) noexcept { return ((note - key) % 12 + 12) % 12; }

constexpr int scaleMask (Scale s) noexcept
{
    const int i = (int) s;
    return detail::kScaleMasks[i >= 0 && i < kNumScales ? i : 0];
}

constexpr bool inScale (int note, int key, Scale s) noexcept
{
    return ((scaleMask (s) >> degreeOf (note, key)) & 1) != 0;
}

/** The nearest note of the scale (ties go down). Scale::Off returns the note unchanged. */
constexpr int snapToScale (int note, int key, Scale s) noexcept
{
    if (s == Scale::Off)
        return note;

    for (int distance = 0; distance <= 6; ++distance)
    {
        const int down = note - distance, up = note + distance;
        if (down >= 0 && inScale (down, key, s))
            return down;
        if (up <= 127 && inScale (up, key, s))
            return up;
    }
    return note;
}

/** The 7-note scale whose chords Smart Chords use. Pentatonic and blues scales
    borrow the chords of their "parent" scale; Off uses the key's major scale. */
constexpr Scale harmonyScale (Scale s) noexcept
{
    switch (s)
    {
        case Scale::Off:
        case Scale::MajorPenta:    return Scale::Major;
        case Scale::MinorPenta:
        case Scale::Blues:         return Scale::Minor;
        default:                   return s;
    }
}

/** The chord built on each degree of a 7-note scale, by stacking every other
    scale note: root + third + fifth. */
enum class Triad : int { Major = 0, Minor, Diminished, Augmented, Outside };

constexpr Triad triadOn (int degree, Scale s) noexcept
{
    const int mask = scaleMask (harmonyScale (s));
    int notes[12] {};
    int count = 0, index = -1;
    for (int pc = 0; pc < 12; ++pc)
        if ((mask >> pc) & 1)
        {
            if (pc == degree)
                index = count;
            notes[count++] = pc;
        }

    if (index < 0)
        return Triad::Outside;   // a note that isn't in the scale

    const int third = (notes[(index + 2) % count] - degree + 12) % 12;
    const int fifth = (notes[(index + 4) % count] - degree + 12) % 12;
    if (third == 3)
        return fifth == 6 ? Triad::Diminished : Triad::Minor;
    return fifth == 8 ? Triad::Augmented : Triad::Major;
}

// Compile-time sanity checks: C major has C, Dm, Em, F, G, Am, Bdim
static_assert (triadOn (0, Scale::Major) == Triad::Major);
static_assert (triadOn (2, Scale::Major) == Triad::Minor);
static_assert (triadOn (11, Scale::Major) == Triad::Diminished);
static_assert (triadOn (3, Scale::HarmonicMinor) == Triad::Augmented);
static_assert (snapToScale (61, 0, Scale::Major) == 60);   // C# -> C (ties go down)
static_assert (snapToScale (63, 0, Scale::Blues) == 63);   // Eb is in C blues
static_assert (snapToScale (62, 0, Scale::Blues) == 63);   // D -> Eb (nearest)
} // namespace bb
