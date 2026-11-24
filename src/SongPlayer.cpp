#include "SongPlayer.h"
#include "JamBand.h"

#include <map>

namespace bb
{
int gmDrumToKit (int gm) noexcept
{
    switch (gm)
    {
        case 35: case 36:                     return 0;    // kicks
        case 37: case 56: case 75: case 76:   return 1;    // side stick, cowbell, claves, woodblock -> rim
        case 38:                              return 2;    // snare
        case 39:                              return 3;    // clap
        case 40:                              return 4;    // snare 2
        case 41: case 43:                     return 5;    // low toms
        case 42: case 54: case 70:            return 6;    // closed hat, tambourine, maracas
        case 45: case 47:                     return 7;    // mid toms
        case 44:                              return 8;    // pedal hat
        case 48: case 50:                     return 9;    // high toms
        case 46: case 51: case 53: case 59:   return 10;   // open hat, rides
        case 49: case 52: case 55: case 57:   return 11;   // crashes, china, splash
        default:                              return 1;    // other percussion: a rim click
    }
}

SongArrangement arrangeMidiSong (juce::InputStream& stream, const juce::String& name)
{
    juce::MidiFile file;
    if (! file.readFrom (stream))
        return {};
    return arrangeMidiSong (file, name);
}

SongArrangement arrangeMidiSong (const juce::MidiFile& source, const juce::String& name)
{
    juce::MidiFile file (source);
    file.convertTimestampTicksToSeconds();

    // 1. Every note, grouped by (track, channel): one group = one part of the song
    struct Part { std::vector<NotePlayer::Note> notes; double pitchSum = 0.0; juce::String title; bool isDrums = false; };
    std::map<std::pair<int, int>, Part> parts;
    for (int t = 0; t < file.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence track (*file.getTrack (t));
        track.updateMatchedPairs();
        juce::String title;
        for (int i = 0; i < track.getNumEvents(); ++i)
        {
            const auto& m = track.getEventPointer (i)->message;
            if (m.isTrackNameEvent() && title.isEmpty())
                title = m.getTextFromTextMetaEvent().trim();
            if (! m.isNoteOn())
                continue;
            const double start = m.getTimeStamp();
            const double end = track.getEventPointer (i)->noteOffObject != nullptr
                                   ? track.getEventPointer (i)->noteOffObject->message.getTimeStamp() : start + 0.25;
            auto& part = parts[{ t, m.getChannel() }];
            part.isDrums = m.getChannel() == 10;
            part.title = title;
            part.notes.push_back ({ start, juce::jmax (0.02, end - start), m.getNoteNumber(), m.getFloatVelocity() });
            part.pitchSum += m.getNoteNumber();
        }
    }

    // 2. Who plays what: bass = the lowest part, melody = the highest busy part, the rest = chords
    auto average = [] (const Part& p) { return p.pitchSum / juce::jmax<size_t> (1, p.notes.size()); };
    size_t total = 0;
    for (auto& [key, part] : parts)
        total += part.isDrums ? 0 : part.notes.size();
    const Part* bassPart = nullptr;
    const Part* leadPart = nullptr;
    for (auto& [key, part] : parts)
        if (! part.isDrums && average (part) < 55.0 && (bassPart == nullptr || average (part) < average (*bassPart)))
            bassPart = &part;
    for (auto& [key, part] : parts)
    {
        const bool busy = part.notes.size() >= juce::jmax<size_t> (8, total / 10);
        if (! part.isDrums && &part != bassPart && busy && (leadPart == nullptr || average (part) > average (*leadPart)))
            leadPart = &part;
    }
    if (leadPart == nullptr)   // a song with only one or two small parts: the biggest one leads
        for (auto& [key, part] : parts)
            if (! part.isDrums && &part != bassPart && (leadPart == nullptr || part.notes.size() > leadPart->notes.size()))
                leadPart = &part;

    // 3. Out to the chip channels
    SongArrangement song;
    song.name = name;
    int chordParts = 0;
    for (auto& [key, part] : parts)
    {
        const int channel = part.isDrums ? SongBand::drumsChannel
                          : &part == leadPart ? SongBand::leadChannel
                          : &part == bassPart ? SongBand::bassChannel
                                              : SongBand::chordsChannel;
        for (auto n : part.notes)
        {
            n.channel = channel;
            if (part.isDrums)
                n.note = 36 + gmDrumToKit (n.note);
            else if (channel == SongBand::bassChannel)
                while (n.note > 52) n.note -= 12;   // the triangle sits low, like on the NES
            song.notes.push_back (n);
            song.seconds = juce::jmax (song.seconds, n.start + n.length);
        }
        (part.isDrums ? song.drums : channel == SongBand::leadChannel ? song.lead
                                   : channel == SongBand::bassChannel ? song.bass : song.chords) += (int) part.notes.size();
        chordParts += channel == SongBand::chordsChannel ? 1 : 0;
    }

    auto describe = [] (const Part* p) { return p == nullptr ? juce::String ("-") : p->title.isNotEmpty() ? p->title : juce::String ("a part"); };
    song.parts = "MELODY (your sound): " + describe (leadPart) + "   BASS (triangle): " + describe (bassPart)
               + "   CHORDS (pulse): " + juce::String (chordParts) + (chordParts == 1 ? " part" : " parts")
               + "   DRUMS: " + (song.drums > 0 ? "yes" : "none");
    return song;
}

//==============================================================================
void SongBand::prepare (double sampleRate)
{
    for (auto* e : { &bass, &chords, &drums })
        e->prepare (sampleRate);

    bass.setParams (bandBassSound());   // the same triangle bass as the JAM band

    EngineParams c;
    c.wave = Wave::Pulse25;
    c.attack = 0.002f; c.decay = 0.4f; c.sustain = 0.45f; c.release = 0.12f;
    chords.setParams (c);

    EngineParams d;
    d.wave = Wave::Drums;
    drums.setParams (d);
}

ChipEngine* SongBand::engineFor (int channel) noexcept
{
    switch (channel)
    {
        case bassChannel:   return &bass;
        case chordsChannel: return &chords;
        case drumsChannel:  return &drums;
        default:            return nullptr;
    }
}

bool SongBand::render (float* out, int numSamples, const juce::MidiBuffer& events) noexcept
{
    bool any = false;
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end <= pos)
            return;
        for (auto* e : { &bass, &chords, &drums })
            any = e->render (out + pos, end - pos) || any;
        pos = end;
    };

    for (const auto meta : events)
    {
        renderTo (juce::jlimit (0, numSamples, meta.samplePosition));
        const auto m = meta.getMessage();
        if (auto* e = engineFor (m.getChannel()))
        {
            if (m.isNoteOn())
                e->noteOn (m.getNoteNumber(), m.getFloatVelocity() * 0.8f);
            else if (m.isNoteOff())
                e->noteOff (m.getNoteNumber());
        }
    }
    renderTo (numSamples);
    return any;
}

bool SongBand::isSounding() const noexcept
{
    return ! (bass.isIdle() && chords.isIdle() && drums.isIdle());
}

void SongBand::allNotesOff() noexcept
{
    for (auto* e : { &bass, &chords, &drums })
        e->allNotesOff (false);
}
} // namespace bb
