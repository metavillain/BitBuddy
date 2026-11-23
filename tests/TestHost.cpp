// BitBuddy test host
//
//   BitBuddyTestHost --test     <BitBuddy.vst3>            automated audio tests
//   BitBuddyTestHost --demo     <BitBuddy.vst3> <outDir>   render demo song (WAV + MIDI)
//   BitBuddyTestHost --snapshot <outDir>                   render the UI to PNG files
//
// --test and --demo load the real VST3 binary exactly like a DAW does.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"

#include <chrono>
#include <functional>
#include <iostream>
#include <typeinfo>

using namespace juce;

//==============================================================================
struct TestPlayHead : public AudioPlayHead
{
    double bpm = 120.0, sampleRate = 44100.0;
    int64 samplePos = 0;

    Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setTimeInSamples (samplePos);
        info.setTimeInSeconds ((double) samplePos / sampleRate);
        info.setPpqPosition ((double) samplePos / sampleRate * bpm / 60.0);
        info.setTimeSignature (TimeSignature { 4, 4 });
        info.setIsPlaying (true);
        return info;
    }
};

struct NoteEvent { double start, length; int note; float velocity; };
struct RawEvent { int pos; MidiMessage msg; };

static AudioPluginFormatManager& formatManager()
{
    static AudioPluginFormatManager fm;
    static bool initialised = false;
    if (! initialised)
    {
        addDefaultFormatsToManager (fm);
        initialised = true;
    }
    return fm;
}

static std::unique_ptr<AudioPluginInstance> loadPlugin (const String& path, double sr, int block)
{
    OwnedArray<PluginDescription> types;
    for (auto* f : formatManager().getFormats())
        f->findAllTypesForFile (types, path);

    if (types.isEmpty())
    {
        std::cout << "  !! no plugin found in " << path << "\n";
        return {};
    }

    String error;
    auto instance = formatManager().createPluginInstance (*types[0], sr, block, error);
    if (instance == nullptr)
        std::cout << "  !! failed to load: " << error << "\n";
    return instance;
}

static AudioProcessorParameter* findParam (AudioProcessor& p, const String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (100) == name)
            return prm;
    return nullptr;
}

static void setParam (AudioProcessor& p, const String& name, float normalised)
{
    if (auto* prm = findParam (p, name))
        prm->setValueNotifyingHost (normalised);
    else
        std::cout << "  !! parameter not found: " << name << "\n";
}

// Loads a preset by name. Hosts (and BitBuddy) ignore a program change to the
// program that is already active, so switch away first to force a clean reload.
static bool selectProgram (AudioProcessor& p, const String& name)
{
    for (int i = 0; i < p.getNumPrograms(); ++i)
        if (p.getProgramName (i) == name)
        {
            if (p.getCurrentProgram() == i)
                p.setCurrentProgram ((i + 1) % p.getNumPrograms());
            p.setCurrentProgram (i);
            return true;
        }
    std::cout << "  !! program not found: " << name << "\n";
    return false;
}

static AudioBuffer<float> render (AudioProcessor& plugin, std::vector<RawEvent> events, double seconds,
                                  double sr, int block, TestPlayHead& playHead)
{
    std::stable_sort (events.begin(), events.end(), [] (const RawEvent& a, const RawEvent& b)
    {
        if (a.pos != b.pos)
            return a.pos < b.pos;
        return a.msg.isNoteOff() && ! b.msg.isNoteOff();   // note-offs first at the same time
    });

    const int total = (int) std::ceil (seconds * sr);
    AudioBuffer<float> out (2, total);
    out.clear();

    AudioBuffer<float> buf (juce::jmax (2, plugin.getTotalNumOutputChannels()), block);
    MidiBuffer midi;
    size_t ei = 0;

    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buf.setSize (buf.getNumChannels(), n, false, false, true);
        buf.clear();
        midi.clear();

        while (ei < events.size() && events[ei].pos < pos + n)
        {
            midi.addEvent (events[ei].msg, std::max (0, events[ei].pos - pos));
            ++ei;
        }

        playHead.samplePos = pos;
        plugin.processBlock (buf, midi);

        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, buf, ch, 0, n);
    }
    return out;
}

static std::vector<RawEvent> toEvents (const std::vector<NoteEvent>& notes, double sr)
{
    std::vector<RawEvent> ev;
    for (auto& n : notes)
    {
        ev.push_back ({ (int) std::lround (n.start * sr), MidiMessage::noteOn (1, n.note, n.velocity) });
        ev.push_back ({ (int) std::lround ((n.start + n.length) * sr), MidiMessage::noteOff (1, n.note) });
    }
    return ev;
}

// Prepares the plugin like a DAW pressing Play, renders the events, then stops.
static AudioBuffer<float> renderEvents (AudioPluginInstance& plugin, std::vector<RawEvent> events, double seconds,
                                        double sr = 44100.0, int block = 512, double bpm = 120.0)
{
    TestPlayHead ph;
    ph.bpm = bpm;
    ph.sampleRate = sr;
    plugin.setPlayHead (&ph);
    plugin.setNonRealtime (true);
    plugin.prepareToPlay (sr, block);
    auto result = render (plugin, std::move (events), seconds, sr, block, ph);
    plugin.releaseResources();
    plugin.setPlayHead (nullptr);
    return result;
}

static AudioBuffer<float> renderNotes (AudioPluginInstance& plugin, const std::vector<NoteEvent>& notes, double seconds,
                                       double sr = 44100.0, int block = 512, double bpm = 120.0)
{
    return renderEvents (plugin, toEvents (notes, sr), seconds, sr, block, bpm);
}

static double secondsToRender (AudioPluginInstance& plugin, const std::vector<NoteEvent>& notes, double seconds)
{
    const auto t0 = std::chrono::steady_clock::now();
    renderNotes (plugin, notes, seconds);
    return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
}

//==============================================================================
static float peakOf (const AudioBuffer<float>& b, double fromSec, double toSec, double sr)
{
    const int s0 = jlimit (0, b.getNumSamples(), (int) (fromSec * sr));
    const int s1 = jlimit (0, b.getNumSamples(), (int) (toSec * sr));
    float peak = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = s0; i < s1; ++i)
            peak = jmax (peak, std::abs (b.getSample (ch, i)));
    return peak;
}

static bool allFinite (const AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}

// YIN pitch detector (de Cheveigne & Kawahara, 2002). Takes the first dip of the
// cumulative-mean-normalised difference below a threshold, which avoids the
// octave errors a plain autocorrelation makes on smooth or jittery waveforms.
static double detectPitch (const float* x, int n, double sr, double minHz = 50.0, double maxHz = 2000.0)
{
    const int minLag = jmax (2, (int) (sr / maxHz));
    const int maxLag = jmin (n / 2, (int) (sr / minHz));
    const int window = n - maxLag - 2;
    if (window <= 0)
        return 0.0;

    std::vector<double> cmnd ((size_t) maxLag + 2, 1.0);
    double running = 0.0;

    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        double d = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const double diff = (double) x[i] - (double) x[i + tau];
            d += diff * diff;
        }
        running += d;
        cmnd[(size_t) tau] = running > 0.0 ? d * tau / running : 1.0;
    }

    int tau = -1;
    for (int t = minLag; t <= maxLag; ++t)
    {
        if (cmnd[(size_t) t] < 0.3)
        {
            while (t + 1 <= maxLag && cmnd[(size_t) t + 1] < cmnd[(size_t) t])
                ++t;
            tau = t;
            break;
        }
    }

    if (tau < 0)   // no clear dip: fall back to the global minimum
    {
        double best = 1e9;
        for (int t = minLag; t <= maxLag; ++t)
            if (cmnd[(size_t) t] < best)
            {
                best = cmnd[(size_t) t];
                tau = t;
            }
    }

    const double y0 = cmnd[(size_t) tau - 1], y1 = cmnd[(size_t) tau], y2 = cmnd[(size_t) tau + 1];
    const double denom = y0 - 2.0 * y1 + y2;
    const double offset = std::abs (denom) > 1e-12 ? 0.5 * (y0 - y2) / denom : 0.0;
    return sr / ((double) tau + jlimit (-0.5, 0.5, offset));
}

static double pitchAt (const AudioBuffer<float>& b, double fromSec, int length, double sr, double minHz = 50.0)
{
    const int s0 = (int) (fromSec * sr);
    if (s0 + length > b.getNumSamples())
        return 0.0;
    return detectPitch (b.getReadPointer (0) + s0, length, sr, minHz);
}

static double midiToHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }
static double centsOff (double measured, double expected) { return 1200.0 * std::log2 (measured / expected); }

//==============================================================================
struct Results
{
    int passed = 0, failed = 0;

    void check (bool ok, const String& name, const String& detail)
    {
        (ok ? passed : failed)++;
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << name.paddedRight (' ', 44) << detail << "\n";
    }
};

static int runTests (const String& path)
{
    constexpr double sr = 44100.0;
    Results res;
    std::cout << "\n=== BitBuddy automated tests ===\nplugin: " << path << "\n\n";

    auto plugin = loadPlugin (path, sr, 512);
    if (plugin == nullptr)
        return 1;

    // ---- 1. Basic description ---------------------------------------------
    res.check (plugin->getName() == "BitBuddy", "Plugin name", plugin->getName());
    res.check (plugin->acceptsMidi(), "Accepts MIDI (instrument)", plugin->acceptsMidi() ? "yes" : "no");
    res.check (plugin->getTotalNumOutputChannels() == 2, "Stereo output", String (plugin->getTotalNumOutputChannels()) + " ch");
    int automatable = 0;
    for (auto* prm : plugin->getParameters())
        automatable += prm->isAutomatable() ? 1 : 0;
    res.check (automatable >= 31 && automatable <= 33, "Automatable parameters in the DAW",
               String (automatable) + " (+" + String (plugin->getParameters().size() - automatable) + " hidden MIDI-CC slots)");
    res.check (plugin->getNumPrograms() == 27, "Factory presets exposed to the DAW", String (plugin->getNumPrograms()));

    // ---- 2. Every preset makes sound, stays finite, and goes silent --------
    std::cout << "\n  -- every preset: sound / no NaN / no clipping / no stuck notes --\n";
    for (int i = 0; i < plugin->getNumPrograms(); ++i)
    {
        plugin->setCurrentProgram (i);
        const auto name = plugin->getProgramName (i);

        std::vector<NoteEvent> notes;
        if (name.contains ("Drum"))
            notes = { { 0.0, 0.2, 36, 1.0f }, { 0.25, 0.2, 38, 0.9f }, { 0.5, 0.2, 42, 0.7f }, { 0.75, 0.2, 46, 0.7f } };
        else if (name.contains ("Note Arp") || name.contains ("Strings"))
            notes = { { 0.0, 1.0, 60, 0.8f }, { 0.0, 1.0, 64, 0.8f }, { 0.0, 1.0, 67, 0.8f } };
        else if (name.contains ("Coin"))
            notes = { { 0.0, 0.5, 83, 0.9f } };
        else if (name.contains ("Explosion") || name.contains ("Bass"))
            notes = { { 0.0, 1.0, 40, 0.9f } };
        else
            notes = { { 0.0, 1.0, 60, 0.8f } };

        auto out = renderNotes (*plugin, notes, 9.0, sr);
        const float peak = peakOf (out, 0.0, 2.0, sr);
        const float tail = peakOf (out, 8.5, 9.0, sr);
        const bool ok = allFinite (out) && peak > 0.005f && peak <= 1.0f && tail < 1.0e-3f;
        res.check (ok, "Preset " + String (i).paddedLeft ('0', 2) + " " + name,
                   "peak " + String (Decibels::gainToDecibels (peak), 1) + " dB, tail "
                       + String (Decibels::gainToDecibels (tail, -120.0f), 1) + " dB");
    }

    // ---- 3. Tuning --------------------------------------------------------
    std::cout << "\n  -- tuning (autocorrelation pitch, tolerance +/- 8 cents) --\n";
    struct PitchCase { const char* wave; float waveNorm; int note; };
    const PitchCase pitchCases[] = {
        // Wave has 9 choices (0..8): normalised value = index / 8
        { "Square",      0.0f / 8.0f, 69 }, { "Pulse 25%", 1.0f / 8.0f, 57 }, { "Pulse 12.5%", 2.0f / 8.0f, 72 },
        { "Triangle",    3.0f / 8.0f, 45 }, { "Saw",       4.0f / 8.0f, 64 }, { "Metal Noise", 6.0f / 8.0f, 69 },
        { "Draw (sine)", 8.0f / 8.0f, 69 },
    };
    for (const auto& pc : pitchCases)
    {
        selectProgram (*plugin, "Classic Square");
        setParam (*plugin, "Wave", pc.waveNorm);
        auto out = renderNotes (*plugin, { { 0.0, 1.0, pc.note, 0.8f } }, 1.2, sr);
        const double hz = pitchAt (out, 0.3, 8192, sr, 40.0);
        const double cents = hz > 0 ? centsOff (hz, midiToHz (pc.note)) : 9999.0;
        res.check (std::abs (cents) < 8.0, String ("Tuning: ") + pc.wave + " note " + String (pc.note),
                   String (hz, 2) + " Hz (expected " + String (midiToHz (pc.note), 2) + ", " + String (cents, 2) + " cents)");
    }

    // Other sample rates & block sizes
    const std::pair<double, int> configs[] = { { 48000.0, 64 }, { 96000.0, 1024 }, { 22050.0, 37 } };
    for (auto [rate, block] : configs)
    {
        selectProgram (*plugin, "Classic Square");
        auto out = renderNotes (*plugin, { { 0.0, 1.0, 69, 0.8f } }, 1.2, rate, block);
        const double hz = pitchAt (out, 0.3, 8192, rate, 40.0);
        res.check (allFinite (out) && std::abs (centsOff (hz, 440.0)) < 8.0,
                   "A4 at " + String (rate / 1000.0, 2) + " kHz, block " + String (block), String (hz, 2) + " Hz");
    }

    // Pitch bend +2 semitones
    {
        selectProgram (*plugin, "Classic Square");
        auto out = renderEvents (*plugin, { { 0, MidiMessage::pitchWheel (1, 16383) },
                                            { 0, MidiMessage::noteOn (1, 69, 0.8f) },
                                            { (int) sr, MidiMessage::noteOff (1, 69) } }, 1.2, sr);
        const double hz = pitchAt (out, 0.3, 8192, sr, 40.0);
        res.check (std::abs (centsOff (hz, midiToHz (71.0))) < 10.0, "Pitch bend up 2 semitones", String (hz, 2) + " Hz (expected 493.88)");
    }

    // ---- 4. Arpeggiators ---------------------------------------------------
    std::cout << "\n  -- arpeggiators --\n";
    {
        selectProgram (*plugin, "Major Chord Arp");
        setParam (*plugin, "Arp Speed", 5.0f / 7.0f);    // 15 Hz: one step = 2940 samples
        setParam (*plugin, "Echo", 0.0f);
        auto out = renderNotes (*plugin, { { 0.0, 1.0, 60, 0.8f } }, 1.0, sr);
        String seq;
        bool ok = true;
        const int expected[] = { 0, 4, 7, 0, 4, 7 };
        for (int step = 0; step < 6; ++step)
        {
            const double hz = pitchAt (out, (step * 2940 + 400) / sr, 2048, sr, 100.0);
            const double semis = 12.0 * std::log2 (hz / midiToHz (60));
            seq << String (semis, 1) << " ";
            ok = ok && std::abs (semis - expected[step]) < 0.3;
        }
        res.check (ok, "CHORD arp: one key -> C E G C E G", "semitones: " + seq);
    }
    {
        selectProgram (*plugin, "Note Arpeggiator");
        setParam (*plugin, "Echo", 0.0f);
        // 1/16 at 120 BPM = 0.125 s per step; Up & Down over 2 octaves of C E G
        auto out = renderNotes (*plugin, { { 0.0, 2.0, 60, 0.8f }, { 0.0, 2.0, 64, 0.8f }, { 0.0, 2.0, 67, 0.8f } }, 2.0, sr);
        const int expected[] = { 60, 64, 67, 72, 76, 79, 76, 72, 67, 64 };
        String seq;
        bool ok = true;
        for (int step = 0; step < 10; ++step)
        {
            const double hz = pitchAt (out, step * 0.125 + 0.02, 2048, sr, 100.0);
            const double note = 69.0 + 12.0 * std::log2 (hz / 440.0);
            seq << String (juce::roundToInt (note)) << " ";
            ok = ok && std::abs (note - expected[step]) < 0.3;
        }
        res.check (ok, "NOTES arp: C E G held, up & down, 2 octaves", "notes: " + seq);
    }

    // ---- 4b. Scale Lock + Smart Chords ----------------------------------------
    std::cout << "\n  -- scale lock + smart chords --\n";
    auto setScale = [&] (int key, int scale)
    {
        setParam (*plugin, "Key", (float) key / 11.0f);
        setParam (*plugin, "Scale", (float) scale / 7.0f);
    };
    auto noteHeard = [&] (const AudioBuffer<float>& out, double atSec)
    {
        const double hz = pitchAt (out, atSec, 8192, sr, 40.0);
        return hz > 0 ? 69.0 + 12.0 * std::log2 (hz / 440.0) : -1.0;
    };
    {
        selectProgram (*plugin, "Classic Square");
        setScale (0, 1);   // C major
        String detail;
        bool ok = true;
        // C# -> C, F# -> F (ties go down), D# -> D, B stays B
        for (auto [key, expected] : { std::pair { 61, 60 }, { 66, 65 }, { 63, 62 }, { 71, 71 } })
        {
            const double heard = noteHeard (renderNotes (*plugin, { { 0.0, 1.0, key, 0.8f } }, 1.2, sr), 0.3);
            ok = ok && std::abs (heard - expected) < 0.2;
            detail << key << "->" << String (heard, 1) << " ";
        }
        res.check (ok, "SCALE LOCK (C major): black keys snap to scale notes", detail);

        // A minor pentatonic: A C D E G only
        setScale (9, 4);
        const double heardB = noteHeard (renderNotes (*plugin, { { 0.0, 1.0, 71, 0.8f } }, 1.2, sr), 0.3);  // B -> C (1 away; A is 2)
        const double heardF = noteHeard (renderNotes (*plugin, { { 0.0, 1.0, 65, 0.8f } }, 1.2, sr), 0.3);  // F -> E
        res.check (std::abs (heardB - 72) < 0.2 && std::abs (heardF - 64) < 0.2, "SCALE LOCK (A minor penta): B -> C, F -> E",
                   String (heardB, 1) + ", " + String (heardF, 1));
    }
    {
        // Two keys that snap to the same note: it must sound until BOTH are released
        selectProgram (*plugin, "Classic Square");
        setScale (0, 1);
        auto out = renderEvents (*plugin, { { 0, MidiMessage::noteOn (1, 61, 0.8f) },              // C# -> C
                                            { (int) (0.1 * sr), MidiMessage::noteOn (1, 60, 0.8f) },  // C
                                            { (int) (0.5 * sr), MidiMessage::noteOff (1, 61) },
                                            { (int) (1.0 * sr), MidiMessage::noteOff (1, 60) } }, 2.0, sr);
        const float held = peakOf (out, 0.6, 0.95, sr);
        const float tail = peakOf (out, 1.5, 2.0, sr);
        res.check (held > 0.05f && tail < 1.0e-3f, "SCALE LOCK: shared note holds until both keys are up",
                   "held " + String (Decibels::gainToDecibels (held), 1) + " dB, tail "
                       + String (Decibels::gainToDecibels (tail, -120.0f), 1) + " dB");
    }
    {
        // Changing the scale while a key is held must not leave a stuck note
        selectProgram (*plugin, "Classic Square");
        setScale (0, 1);
        TestPlayHead ph;
        ph.sampleRate = sr;
        plugin->setPlayHead (&ph);
        plugin->setNonRealtime (true);
        plugin->prepareToPlay (sr, 512);
        render (*plugin, { { 0, MidiMessage::noteOn (1, 61, 0.8f) } }, 0.5, sr, 512, ph);   // sounds C
        setScale (0, 0);                                                                  // Scale Lock off
        render (*plugin, {}, 0.05, sr, 512, ph);
        auto out = render (*plugin, { { 0, MidiMessage::noteOff (1, 61) } }, 1.5, sr, 512, ph);
        plugin->releaseResources();
        plugin->setPlayHead (nullptr);
        const float tail = peakOf (out, 1.0, 1.5, sr);
        res.check (tail < 1.0e-3f, "SCALE LOCK: changing scale while holding a key -> no stuck note",
                   "tail " + String (Decibels::gainToDecibels (tail, -120.0f), 1) + " dB");
    }
    {
        // SMART chords in C major: D -> D minor (0 3 7), B -> B diminished (0 3 6), F -> F major (0 4 7)
        String detail;
        bool ok = true;
        for (auto [key, third, fifth] : { std::tuple { 62, 3, 7 }, { 71, 3, 6 }, { 65, 4, 7 } })
        {
            selectProgram (*plugin, "Smart Chords");
            setScale (0, 1);
            setParam (*plugin, "Arp Speed", 5.0f / 7.0f);   // 15 Hz: 2940 samples per step
            setParam (*plugin, "Echo", 0.0f);
            auto out = renderNotes (*plugin, { { 0.0, 1.0, key, 0.8f } }, 1.0, sr);
            const int expected[] = { 0, third, fifth };
            for (int step = 0; step < 3; ++step)
            {
                const double hz = pitchAt (out, (step * 2940 + 400) / sr, 2048, sr, 100.0);
                const double semis = 12.0 * std::log2 (hz / midiToHz (key));
                ok = ok && std::abs (semis - expected[step]) < 0.3;
                detail << String (semis, 1) << " ";
            }
            detail << "| ";
        }
        res.check (ok, "SMART chords (C major): Dm, Bdim, F", detail);
    }
    setScale (0, 0);   // leave Scale Lock off for the rest of the tests

    // ---- 4d. Console modes --------------------------------------------------
    std::cout << "\n  -- console modes --\n";
    {
        // NES pitch registers: A6 (1760 Hz) can only be 1747.8 Hz on a real NES (period 63)
        selectProgram (*plugin, "Real NES Lead");
        setParam (*plugin, "Vibrato", 0.0f);
        setParam (*plugin, "Echo", 0.0f);
        const double nesHz = pitchAt (renderNotes (*plugin, { { 0.0, 1.0, 93, 0.8f } }, 1.0, sr), 0.2, 8192, sr, 100.0);
        const double expected = 1789773.0 / (16.0 * 64.0);
        res.check (std::abs (centsOff (nesHz, expected)) < 4.0 && std::abs (centsOff (nesHz, 1760.0)) > 8.0,
                   "NES mode: A6 plays at the NES's own pitch (1747.8 Hz)", String (nesHz, 2) + " Hz");

        // Notes at once: NES 2, C64 3, Modern 8
        String detail;
        bool ok = true;
        for (auto [console, expectVoices] : { std::pair { bb::Console::NES, 2 }, { bb::Console::GameBoy, 2 },
                                              { bb::Console::C64, 3 }, { bb::Console::Modern, 5 } })
        {
            bb::ChipEngine e;
            e.prepare (sr);
            bb::EngineParams ep;
            ep.console = console;
            e.setParams (ep);
            float buf[256] {};
            for (int n : { 60, 64, 67, 71, 74 })
            {
                e.noteOn (n, 0.8f);
                e.render (buf, 256);
            }
            ok = ok && e.getNumActiveVoices() == expectVoices;
            detail << e.getNumActiveVoices() << " ";
        }
        res.check (ok, "Console note limits: NES 2, Game Boy 2, C64 3, Modern all 5", "voices: " + detail);

        // NES triangle has no volume control: it doesn't fade while you hold the key
        auto holdRatio = [&] (int console)
        {
            selectProgram (*plugin, "Classic Square");
            setParam (*plugin, "Wave", 3.0f / 8.0f);
            setParam (*plugin, "Console", (float) console / 3.0f);
            setParam (*plugin, "Decay", 0.3f);       // ~0.1 s decay (normalised value)...
            setParam (*plugin, "Sustain", 0.1f);     // ...down to a quiet sustain
            const auto out = renderNotes (*plugin, { { 0.0, 1.0, 57, 0.8f } }, 1.0, sr);
            return (double) out.getRMSLevel (0, (int) (0.6 * sr), (int) (0.2 * sr))
                 / jmax (1.0e-9, (double) out.getRMSLevel (0, (int) (0.01 * sr), (int) (0.04 * sr)));
        };
        const double nesHold = holdRatio (1), modernHold = holdRatio (0);
        res.check (nesHold > 0.8 && modernHold < 0.5, "NES triangle: on/off only (no fade), Modern fades",
                   "held level NES " + String (roundToInt (nesHold * 100.0)) + "%, Modern " + String (roundToInt (modernHold * 100.0)) + "%");
        setParam (*plugin, "Console", 0.0f);
    }

    // ---- 4c. C64 filter -----------------------------------------------------
    std::cout << "\n  -- C64 filter --\n";
    {
        // "Fizz" = energy of the sample-to-sample differences: high notes and harmonics
        auto fizz = [&] (const AudioBuffer<float>& b, double from, double to)
        {
            const int s0 = jmax (1, (int) (from * sr)), s1 = jmin (b.getNumSamples(), (int) (to * sr));
            const float* x = b.getReadPointer (0);
            double sum = 0.0;
            for (int i = s0; i < s1; ++i)
                sum += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
            return sum / jmax (1, s1 - s0);
        };
        auto rms = [&] (const AudioBuffer<float>& b, double from, double to)
        {
            const int s0 = (int) (from * sr), s1 = jmin (b.getNumSamples(), (int) (to * sr));
            return (double) b.getRMSLevel (0, s0, s1 - s0);
        };
        const NormalisableRange<float> cutoffRange (60.0f, 12000.0f, 0.0f, 0.3f);
        auto setFilter = [&] (int type, float cutoffHz, float reso, float env)
        {
            setParam (*plugin, "Filter", (float) type / 3.0f);
            setParam (*plugin, "Cutoff", cutoffRange.convertTo0to1 (cutoffHz));
            setParam (*plugin, "Resonance", reso);
            setParam (*plugin, "Filter Env", env);
        };

        selectProgram (*plugin, "Classic Square");
        const auto dry = renderNotes (*plugin, { { 0.0, 1.0, 69, 0.8f } }, 1.0, sr);
        setFilter (1, 600.0f, 0.0f, 0.0f);
        const auto low = renderNotes (*plugin, { { 0.0, 1.0, 69, 0.8f } }, 1.0, sr);
        const double lowRatio = fizz (low, 0.2, 0.8) / fizz (dry, 0.2, 0.8);
        res.check (lowRatio < 0.15, "FILTER LOW 600 Hz on A4 square: fizz cut", "fizz " + String (lowRatio * 100.0, 1) + "% of dry");

        selectProgram (*plugin, "Classic Square");
        const auto dryBass = renderNotes (*plugin, { { 0.0, 1.0, 45, 0.8f } }, 1.0, sr);
        setFilter (3, 2000.0f, 0.0f, 0.0f);
        const auto high = renderNotes (*plugin, { { 0.0, 1.0, 45, 0.8f } }, 1.0, sr);
        const double highRatio = rms (high, 0.2, 0.8) / rms (dryBass, 0.2, 0.8);
        res.check (highRatio < 0.4, "FILTER HIGH 2 kHz on A2 square: bass cut", "level " + String (highRatio * 100.0, 1) + "% of dry");

        // Envelope: each note opens the filter, then it closes -> bright start, darker tail
        selectProgram (*plugin, "C64 Squelch Bass");
        setParam (*plugin, "Echo", 0.0f);
        const auto squelch = renderNotes (*plugin, { { 0.0, 1.2, 40, 0.9f }, { 1.3, 0.8, 47, 0.9f } }, 2.6, sr);
        const double opened = fizz (squelch, 0.005, 0.06), closed = fizz (squelch, 0.8, 1.1);
        const double reopened = fizz (squelch, 1.305, 1.36);
        const float squelchPeak = peakOf (squelch, 0.0, 2.6, sr);
        res.check (allFinite (squelch) && opened > 3.0 * closed && reopened > 3.0 * closed && squelchPeak < 1.0f,
                   "FILTER ENV: opens on each note, then closes",
                   "start/tail fizz x" + String (opened / jmax (1e-12, closed), 1) + ", 2nd note x"
                       + String (reopened / jmax (1e-12, closed), 1) + ", peak " + String (squelchPeak, 3));

        // Worst case: max resonance, cutoff swept by the envelope, loud chord -> no blow-ups
        selectProgram (*plugin, "PWM Strings");
        setParam (*plugin, "Volume", 1.0f);
        setFilter (2, 300.0f, 1.0f, 1.0f);
        std::vector<NoteEvent> chord;
        for (int n : { 36, 48, 55, 60, 64, 67, 72, 84 })
            chord.push_back ({ 0.0, 1.5, n, 1.0f });
        const auto wild = renderNotes (*plugin, chord, 2.0, sr);
        const float wildPeak = peakOf (wild, 0.0, 2.0, sr);
        res.check (allFinite (wild) && wildPeak < 1.0f, "FILTER max resonance + env + 8 notes: stable, below 0 dBFS",
                   "peak " + String (wildPeak, 4));
    }

    // ---- 5. Mono + legato glide -----------------------------------------------
    {
        selectProgram (*plugin, "Triangle Bass");
        auto out = renderNotes (*plugin, { { 0.0, 1.0, 45, 0.9f }, { 0.5, 1.0, 52, 0.9f } }, 1.5, sr);
        const double first = pitchAt (out, 0.2, 8192, sr, 40.0);
        const double second = pitchAt (out, 1.0, 8192, sr, 40.0);
        res.check (std::abs (centsOff (first, midiToHz (45))) < 10.0 && std::abs (centsOff (second, midiToHz (52))) < 10.0,
                   "MONO legato: A2 then E3", String (first, 1) + " Hz -> " + String (second, 1) + " Hz");
    }

    // ---- 6. Drum kit: all 12 drums --------------------------------------------
    std::cout << "\n  -- drum kit --\n";
    {
        const char* drumNames[12] = { "Kick", "Rim", "Snare", "Clap", "Snare 2", "Low tom", "Closed hat",
                                      "Mid tom", "Pedal hat", "High tom", "Open hat", "Crash" };
        selectProgram (*plugin, "Drum Kit");
        String detail;
        bool ok = true;
        for (int d = 0; d < 12; ++d)
        {
            auto out = renderNotes (*plugin, { { 0.0, 0.1, 36 + d, 1.0f } }, 5.0, sr);
            const float peak = peakOf (out, 0.0, 0.5, sr);
            const float tail = peakOf (out, 4.5, 5.0, sr);
            const bool drumOk = allFinite (out) && peak > 0.02f && tail < 1.0e-3f;
            ok = ok && drumOk;
            if (! drumOk)
                detail << drumNames[d] << " (peak " << String (peak, 3) << ", tail " << String (tail, 5) << ") ";
        }
        res.check (ok, "All 12 drums sound and decay to silence", ok ? String ("12/12 ok") : detail);
    }

    // ---- 7. All-notes-off / panic ---------------------------------------------
    {
        selectProgram (*plugin, "PWM Strings");
        setParam (*plugin, "Echo", 0.0f);
        std::vector<RawEvent> ev;
        for (int n : { 60, 64, 67, 71 })
            ev.push_back ({ 0, MidiMessage::noteOn (1, n, 0.8f) });
        ev.push_back ({ (int) sr, MidiMessage::allSoundOff (1) });
        auto out = renderEvents (*plugin, ev, 2.0, sr);
        const float tail = peakOf (out, 1.1, 2.0, sr);
        res.check (tail < 1.0e-3f, "All-sound-off silences held notes", String (Decibels::gainToDecibels (tail, -120.0f), 1) + " dB after");
    }

    // Echo tails must keep ringing after the notes stop. There's a silent gap
    // before the first echo, which idle detection must not mistake for "done".
    {
        selectProgram (*plugin, "Hero Lead");   // echo 25%, 1/8 dotted = 0.375 s at 120 BPM
        auto out = renderNotes (*plugin, { { 0.0, 0.2, 72, 0.9f } }, 2.0, sr);
        const float gap = peakOf (out, 0.36, 0.37, sr);
        const float firstEcho = peakOf (out, 0.45, 0.75, sr);
        res.check (firstEcho > Decibels::decibelsToGain (-45.0f), "Echo survives the silent gap after a note",
                   "gap " + String (Decibels::gainToDecibels (gap, -120.0f), 0) + " dB, echo "
                       + String (Decibels::gainToDecibels (firstEcho, -120.0f), 1) + " dB");
    }

    // ---- 8. Big chords: the soft limiter must stop hard clipping --------------
    {
        selectProgram (*plugin, "PWM Strings");
        setParam (*plugin, "Volume", 1.0f);   // +6 dB: worst case
        std::vector<NoteEvent> chord;
        for (int n : { 48, 52, 55, 60, 64, 67, 71, 74 })
            chord.push_back ({ 0.0, 2.0, n, 1.0f });
        auto out = renderNotes (*plugin, chord, 2.5, sr);
        const float peak = peakOf (out, 0.0, 2.5, sr);
        res.check (allFinite (out) && peak < 1.0f, "8-note chord at max volume stays below 0 dBFS",
                   "peak " + String (peak, 4));
    }

    // ---- 9. State save / restore (what the DAW does with your project) --------
    // Tweak a preset, save, load something else, restore: the tweaks must survive.
    {
        // A DAW keeps processing audio between your clicks; while "playing", VST3
        // delivers knob changes with the next audio block, so do the same here.
        auto pump = [&] { renderNotes (*plugin, {}, 0.05, sr); };

        selectProgram (*plugin, "Hero Lead");
        pump();
        setParam (*plugin, "Wave", 3.0f / 8.0f);
        setParam (*plugin, "Attack", 0.42f);
        setParam (*plugin, "Echo", 0.33f);
        pump();
        MemoryBlock saved;
        plugin->getStateInformation (saved);
        const float wantWave = findParam (*plugin, "Wave")->getValue();
        const float wantAttack = findParam (*plugin, "Attack")->getValue();
        const float wantEcho = findParam (*plugin, "Echo")->getValue();

        selectProgram (*plugin, "Drum Kit");
        pump();
        setParam (*plugin, "Attack", 0.9f);
        pump();
        plugin->setStateInformation (saved.getData(), (int) saved.getSize());
        pump();

        const float gotWave = findParam (*plugin, "Wave")->getValue();
        const float gotAttack = findParam (*plugin, "Attack")->getValue();
        const float gotEcho = findParam (*plugin, "Echo")->getValue();
        const bool ok = std::abs (gotWave - wantWave) < 1e-4f && std::abs (gotAttack - wantAttack) < 1e-4f
                     && std::abs (gotEcho - wantEcho) < 1e-4f
                     && plugin->getProgramName (plugin->getCurrentProgram()) == "Hero Lead";
        res.check (ok, "Tweaked preset survives save + reload",
                   String ((int) saved.getSize()) + " bytes; wave " + String (gotWave, 3) + "/" + String (wantWave, 3)
                       + " attack " + String (gotAttack, 3) + "/" + String (wantAttack, 3)
                       + " echo " + String (gotEcho, 3) + "/" + String (wantEcho, 3)
                       + ", program " + plugin->getProgramName (plugin->getCurrentProgram()));
    }

    // ---- 10. Performance -----------------------------------------------------
    {
        selectProgram (*plugin, "PWM Strings");
        std::vector<NoteEvent> notes;
        for (double t = 0.0; t < 60.0; t += 2.0)
            for (int n : { 48, 52, 55, 60, 64, 67, 71, 74 })
                notes.push_back ({ t, 1.9, n, 0.8f });
        const double factor = 60.0 / secondsToRender (*plugin, notes, 60.0);
        res.check (factor > 20.0, "CPU: 8-voice chords for 60 s",
                   String (roundToInt (factor)) + "x faster than real time (" + String (100.0 / factor, 2) + "% of one core)");
    }
    {
        selectProgram (*plugin, "Hero Lead");
        const double factor = 60.0 / secondsToRender (*plugin, {}, 60.0);
        res.check (factor > 2000.0, "CPU: idle (loaded, no notes) for 60 s",
                   String (roundToInt (factor)) + "x faster than real time (" + String (100.0 / factor, 3) + "% of one core)");
    }

    // ---- 11. My Presets, dice, MUTATE (BitBuddy's own processor, as the UI uses it) ----
    std::cout << "\n  -- my presets, dice, mutate --\n";
    {
        BitBuddyProcessor proc;
        auto get = [&] (const char* id) { auto* p = proc.apvts.getParameter (id); return p->convertFrom0to1 (p->getValue()); };
        auto set = [&] (const char* id, float v) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        auto snapshot = [&]
        {
            std::map<String, float> values;
            for (auto* p : proc.getParameters())
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
                    values[rp->getParameterID()] = rp->getValue();
            return values;
        };

        // Save -> change everything -> load: the sound comes back, your Key/Scale stay
        const String name = "zz BitBuddy test " + String (Time::currentTimeMillis());
        proc.selectPreset (1);
        set ("attack", 0.5f); set ("echoMix", 0.42f); set ("wave", 4.0f); set ("scale", 2.0f);
        const bool saved = proc.saveUserPreset (name);
        proc.selectPreset (0);
        set ("scale", 5.0f);
        const bool loaded = proc.loadUserPreset (name);
        const bool listed = BitBuddyProcessor::getUserPresetNames().contains (name);
        const bool ok = saved && loaded && listed && std::abs (get ("attack") - 0.5f) < 0.01f
                     && std::abs (get ("echoMix") - 0.42f) < 0.01f && juce::roundToInt (get ("wave")) == 4
                     && juce::roundToInt (get ("scale")) == 5 && proc.isUserPresetLoaded() && proc.getDisplayPresetName() == name;
        res.check (ok, "MY PRESETS: save, reload (Key/Scale untouched), listed",
                   String (saved ? "saved" : "NOT saved") + ", " + (loaded ? "loaded" : "NOT loaded") + ", attack "
                       + String (get ("attack"), 2) + ", echo " + String (get ("echoMix"), 2) + ", scale " + String (get ("scale"), 0));
        const bool deleted = BitBuddyProcessor::getUserPresetFolder().getChildFile (name + ".bbpreset").deleteFile()
                          && ! BitBuddyProcessor::getUserPresetNames().contains (name);
        res.check (deleted, "MY PRESETS: test preset removed again", BitBuddyProcessor::getUserPresetFolder().getFullPathName());

        // Dice on SHAPE IT: only the envelope changes
        proc.selectPreset (1);
        set ("scale", 3.0f);
        const auto before = snapshot();
        proc.rollSection (BitBuddyProcessor::Section::Shape);
        proc.rollSection (BitBuddyProcessor::Section::Shape);
        const auto after = snapshot();
        StringArray changed;
        for (auto& [id, v] : before)
            if (std::abs (after.at (id) - v) > 1.0e-6f)
                changed.add (id);
        const StringArray shapeIds { "attack", "decay", "sustain", "release" };
        bool onlyShape = ! changed.isEmpty();
        for (auto& id : changed)
            onlyShape = onlyShape && shapeIds.contains (id);
        res.check (onlyShape && proc.getDisplayPresetName().endsWithChar ('*'), "DICE (shape): only the envelope changes, name gets *",
                   changed.joinIntoString (",") + " | " + proc.getDisplayPresetName());

        // MUTATE: many small changes; wave, Key, Scale and volume stay
        proc.selectPreset (1);
        const auto m0 = snapshot();
        for (int i = 0; i < 3; ++i)
            proc.mutate();
        const auto m1 = snapshot();
        int moved = 0;
        float biggest = 0.0f;
        const StringArray switches { "arpMode", "arpChord", "arpPattern", "arpSpeed", "echoTime", "voiceMode" };
        for (auto& [id, v] : m0)
            if (std::abs (m1.at (id) - v) > 1.0e-6f)
            {
                ++moved;
                if (! switches.contains (id))
                    biggest = jmax (biggest, std::abs (m1.at (id) - v));
            }
        const bool kept = m0.at ("wave") == m1.at ("wave") && m0.at ("scaleKey") == m1.at ("scaleKey")
                       && m0.at ("scale") == m1.at ("scale") && m0.at ("volume") == m1.at ("volume");
        res.check (kept && moved >= 8 && biggest <= 0.37f, "MUTATE: nudges many knobs a little, keeps wave/Key/Scale/volume",
                   String (moved) + " params moved, biggest knob change " + String (roundToInt (biggest * 100.0f)) + "% (3 mutates)");

        // DRAW: your drawing is saved with the project and in your presets, and plays in tune
        bb::CustomWave saw {};
        for (int i = 0; i < bb::kCustomWaveSteps; ++i)
            saw[(size_t) i] = (std::uint8_t) (i / 2);
        proc.selectPreset (0);
        set ("wave", 8.0f);
        proc.setCustomWave (saw);
        MemoryBlock project;
        proc.getStateInformation (project);
        proc.setCustomWave (bb::defaultCustomWave());
        proc.setStateInformation (project.getData(), (int) project.getSize());
        const bool projectOk = proc.getCustomWave() == saw;

        const String drawName = "zz BitBuddy draw test " + String (Time::currentTimeMillis());
        proc.saveUserPreset (drawName);
        proc.setCustomWave (bb::defaultCustomWave());
        proc.loadUserPreset (drawName);
        const bool presetOk = proc.getCustomWave() == saw;
        BitBuddyProcessor::getUserPresetFolder().getChildFile (drawName + ".bbpreset").deleteFile();

        proc.selectPreset (0);
        const bool keptByPlainPreset = proc.getCustomWave() == saw;   // presets without a drawing leave yours alone
        int gameBoy = -1;
        for (int i = 0; i < proc.getNumPrograms(); ++i)
            if (proc.getProgramName (i) == "Game Boy Wave")
                gameBoy = i;
        proc.selectPreset (gameBoy);
        const bool presetDrawingLoaded = BitBuddyProcessor::customWaveToString (proc.getCustomWave()) == "89BCDEEFFFEDCB98AC864211234688A6";

        // Play the saw drawing at A4 through BitBuddy's own processor
        proc.selectPreset (0);
        set ("wave", 8.0f);
        proc.setCustomWave (saw);
        proc.prepareToPlay (sr, 512);
        AudioBuffer<float> out (2, (int) sr), block (2, 512);
        for (int pos = 0; pos + 512 <= out.getNumSamples(); pos += 512)
        {
            MidiBuffer midi;
            if (pos == 0)
                midi.addEvent (MidiMessage::noteOn (1, 69, 0.8f), 0);
            block.clear();
            proc.processBlock (block, midi);
            out.copyFrom (0, pos, block, 0, 0, 512);
        }
        const double hz = pitchAt (out, 0.3, 8192, sr, 40.0);
        // ---- MACROS ----
        auto renderDirect = [&] (int note, double seconds)
        {
            proc.prepareToPlay (sr, 512);
            AudioBuffer<float> result (2, (int) (seconds * sr)), blk (2, 512);
            result.clear();
            for (int pos = 0; pos + 512 <= result.getNumSamples(); pos += 512)
            {
                MidiBuffer midi;
                if (pos == 0)
                    midi.addEvent (MidiMessage::noteOn (1, note, 0.8f), 0);
                blk.clear();
                proc.processBlock (blk, midi);
                result.copyFrom (0, pos, blk, 0, 0, 512);
            }
            return result;
        };
        auto macro = [] (const char* text) { bb::Macro m; BitBuddyProcessor::macroFromString (text, m); return m; };

        // ARP: 6 steps (0.1 s) on the note, then 6 steps a fifth up, looping
        proc.selectPreset (0);
        set ("sustain", 1.0f);
        proc.setMacro (bb::kMacroArp, macro ("12|0|0,0,0,0,0,0,7,7,7,7,7,7"));
        const auto arpOut = renderDirect (57, 0.5);
        const double n1 = 69.0 + 12.0 * std::log2 (pitchAt (arpOut, 0.03, 2048, sr, 80.0) / 440.0);
        const double n2 = 69.0 + 12.0 * std::log2 (pitchAt (arpOut, 0.12, 2048, sr, 80.0) / 440.0);
        const double n3 = 69.0 + 12.0 * std::log2 (pitchAt (arpOut, 0.23, 2048, sr, 80.0) / 440.0);
        res.check (std::abs (n1 - 57) < 0.3 && std::abs (n2 - 64) < 0.3 && std::abs (n3 - 57) < 0.3,
                   "MACRO ARP: 0 then +7 then loops (60 steps/s)", String (n1, 1) + " " + String (n2, 1) + " " + String (n3, 1));

        // VOL: full for 6 steps, then 0 -> silent while the key is still held
        proc.clearMacros();
        proc.setMacro (bb::kMacroVolume, macro ("8|-1|15,15,15,15,15,15,0,0"));
        const auto volOut = renderDirect (57, 0.6);
        const float loud = peakOf (volOut, 0.02, 0.09, sr), quiet = peakOf (volOut, 0.4, 0.6, sr);
        res.check (loud > 0.05f && quiet < 0.005f, "MACRO VOL: note cut to silence after 0.1 s (key still held)",
                   String (Decibels::gainToDecibels (loud), 1) + " dB -> " + String (Decibels::gainToDecibels (quiet, -120.0f), 1) + " dB");

        // DUTY: 12.5% vs 50% pulse -> share of time the wave is "up"
        auto upShare = [&] (const char* duty)
        {
            proc.clearMacros();
            proc.setMacro (bb::kMacroDuty, macro (duty));
            const auto out = renderDirect (57, 0.4);
            int up = 0, n = 0;
            for (int i = (int) (0.1 * sr); i < (int) (0.35 * sr); ++i, ++n)
                up += out.getSample (0, i) > 0.0f ? 1 : 0;
            return (double) up / n;
        };
        const double narrow = upShare ("1|-1|0"), square = upShare ("1|-1|2");
        res.check (narrow < 0.25 && square > 0.4 && square < 0.6, "MACRO DUTY: 12.5% vs 50% pulse width",
                   "up " + String (roundToInt (narrow * 100.0)) + "% vs " + String (roundToInt (square * 100.0)) + "%");

        // Saved with the project + presets; presets without macros switch them off
        proc.clearMacros();
        proc.setMacro (bb::kMacroArp, macro ("3|0|0,4,7"));
        MemoryBlock macroProject;
        proc.getStateInformation (macroProject);
        proc.clearMacros();
        proc.setStateInformation (macroProject.getData(), (int) macroProject.getSize());
        const bool macroSaved = BitBuddyProcessor::macroToString (proc.getMacro (bb::kMacroArp)).startsWith ("3|0|0,4,7");
        proc.selectPreset (0);
        const bool clearedByPreset = ! proc.anyMacroOn();
        int blip = -1;
        for (int i = 0; i < proc.getNumPrograms(); ++i)
            if (proc.getProgramName (i) == "Macro Chord Blip")
                blip = i;
        proc.selectPreset (blip);
        const bool presetMacros = proc.getMacro (bb::kMacroArp).length == 3 && proc.getMacro (bb::kMacroVolume).length == 12;
        res.check (macroSaved && clearedByPreset && presetMacros, "MACROS: saved with project, cleared by plain presets, loaded by macro presets",
                   String (macroSaved ? "saved" : "LOST") + ", " + (clearedByPreset ? "cleared" : "STILL ON") + ", "
                       + (presetMacros ? "preset ok" : "PRESET WRONG"));

        // ---- RECORD: WAV + MIDI files ----
        {
            bb::Recorder rec;
            const bool started = rec.start (sr);
            AudioBuffer<float> tone (2, 512);
            for (int blockIndex = 0; blockIndex < (int) (sr / 512); ++blockIndex)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float s = 0.5f * std::sin (6.2831853f * 440.0f * (float) (blockIndex * 512 + i) / (float) sr);
                    tone.setSample (0, i, s);
                    tone.setSample (1, i, s);
                }
                if (blockIndex == 10)
                    rec.addMidi (MidiMessage::noteOn (1, 69, 0.8f), 100);
                if (blockIndex == 60)
                    rec.addMidi (MidiMessage::noteOff (1, 69), 0);
                rec.writeAudio (tone.getArrayOfReadPointers(), 512);
            }
            rec.stop();

            AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (rec.getWavFile()));
            const auto seconds = reader != nullptr ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
            float wavPeak = 0.0f;
            if (reader != nullptr)
            {
                Range<float> range[2];
                reader->readMaxLevels (0, reader->lengthInSamples, range, 2);
                wavPeak = range[0].getEnd();
            }
            reader.reset();

            MidiFile midiFile;
            FileInputStream midiIn (rec.getMidiFile());
            const bool midiRead = midiIn.openedOk() && midiFile.readFrom (midiIn);
            int noteOns = 0;
            double noteTime = -1.0;
            if (midiRead && midiFile.getNumTracks() > 0)
            {
                midiFile.convertTimestampTicksToSeconds();
                for (auto* e : *midiFile.getTrack (0))
                    if (e->message.isNoteOn())
                    {
                        ++noteOns;
                        noteTime = e->message.getTimeStamp();
                    }
            }
            const double expectedNoteTime = (10 * 512 + 100) / sr;
            res.check (started && std::abs (seconds - 512.0 * (int) (sr / 512) / sr) < 0.001 && std::abs (wavPeak - 0.5f) < 0.01f
                           && noteOns == 1 && std::abs (noteTime - expectedNoteTime) < 0.002,
                       "RECORD: WAV (length, level) + MIDI (note at the right time)",
                       String (seconds, 3) + " s, peak " + String (wavPeak, 3) + ", " + String (noteOns) + " note at "
                           + String (noteTime, 4) + " s (expected " + String (expectedNoteTime, 4) + ")");
            rec.getWavFile().deleteFile();
            rec.getMidiFile().deleteFile();
        }

        res.check (projectOk && presetOk && keptByPlainPreset && presetDrawingLoaded && std::abs (centsOff (hz, 440.0)) < 8.0,
                   "DRAW: drawing saved in project + preset, preset drawings load, plays in tune",
                   String (projectOk ? "project ok" : "PROJECT LOST") + ", " + (presetOk ? "preset ok" : "PRESET LOST") + ", "
                       + (keptByPlainPreset ? "kept" : "OVERWRITTEN") + ", " + (presetDrawingLoaded ? "GB ok" : "GB WRONG")
                       + ", A4 = " + String (hz, 2) + " Hz");
    }

    std::cout << "\n  " << res.passed << " passed, " << res.failed << " failed\n";
    return res.failed;
}

//==============================================================================
// Demo song: "Pixel Quest" - an original 17-bar tune at 140 BPM, Am F C G.
struct BeatNote { double beat, length; int note; float velocity; };
using Part = std::vector<BeatNote>;

static Part makeLead()
{
    struct M { int note, eighths; };   // note 0 = rest
    static const std::vector<std::vector<M>> bars = {
        { { 69, 1 }, { 72, 1 }, { 76, 2 }, { 81, 2 }, { 79, 1 }, { 76, 1 } },              // 5  Am
        { { 77, 2 }, { 76, 1 }, { 72, 1 }, { 69, 3 }, { 72, 1 } },                         // 6  F
        { { 67, 1 }, { 72, 1 }, { 76, 2 }, { 79, 2 }, { 76, 1 }, { 72, 1 } },              // 7  C
        { { 74, 3 }, { 71, 1 }, { 67, 2 }, { 0, 2 } },                                     // 8  G
        { { 69, 1 }, { 72, 1 }, { 76, 2 }, { 81, 2 }, { 83, 1 }, { 84, 1 } },              // 9  Am
        { { 81, 2 }, { 79, 1 }, { 77, 1 }, { 76, 2 }, { 72, 2 } },                         // 10 F
        { { 76, 1 }, { 79, 1 }, { 84, 2 }, { 83, 1 }, { 79, 1 }, { 76, 2 } },              // 11 C
        { { 74, 2 }, { 79, 2 }, { 74, 1 }, { 71, 1 }, { 67, 2 } },                         // 12 G
        { { 88, 2 }, { 86, 1 }, { 84, 1 }, { 83, 1 }, { 84, 1 }, { 81, 2 } },              // 13 Am
        { { 84, 2 }, { 81, 1 }, { 77, 1 }, { 81, 2 }, { 84, 2 } },                         // 14 F
        { { 79, 1 }, { 76, 1 }, { 79, 1 }, { 84, 1 }, { 88, 2 }, { 86, 1 }, { 84, 1 } },   // 15 C
        { { 83, 2 }, { 86, 2 }, { 79, 2 }, { 83, 1 }, { 86, 1 } },                         // 16 G
        { { 84, 8 } },                                                                     // 17 C
    };

    Part p;
    double beat = 16.0;   // bar 5
    for (const auto& bar : bars)
        for (const auto& m : bar)
        {
            if (m.note > 0)
                p.push_back ({ beat, m.eighths * 0.5 * 0.92, m.note, 0.85f });
            beat += m.eighths * 0.5;
        }
    return p;
}

static Part makeChords()
{
    static const int triads[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 60, 64 }, { 55, 59, 62 } };
    Part p;
    for (int bar = 0; bar < 16; ++bar)
        for (int k = 0; k < 3; ++k)
            p.push_back ({ bar * 4.0, 3.95, triads[bar % 4][k], 0.75f });
    for (int n : { 60, 64, 67 })
        p.push_back ({ 64.0, 4.0, n, 0.75f });
    return p;
}

static Part makeBass()
{
    static const int roots[4] = { 45, 41, 36, 43 };
    static const int pattern[8] = { 0, 0, 12, 0, 0, 12, 0, 12 };
    Part p;
    for (int bar = 2; bar < 16; ++bar)
        for (int e = 0; e < 8; ++e)
            p.push_back ({ bar * 4.0 + e * 0.5, 0.45, roots[bar % 4] + pattern[e], 0.9f });
    p.push_back ({ 64.0, 2.0, 36, 0.9f });
    return p;
}

static Part makeDrums()
{
    enum { kick = 36, snare = 38, hat = 42, openHat = 46, lowTom = 41, midTom = 43, highTom = 45, crash = 47 };
    Part p;
    auto hit = [&p] (int bar, int step, int note, float vel) { p.push_back ({ bar * 4.0 + step * 0.25, 0.2, note, vel }); };

    for (int bar = 2; bar < 4; ++bar)          // intro: hats only
        for (int s = 0; s < 16; s += 2)
            hit (bar, s, hat, s % 4 == 0 ? 0.6f : 0.45f);

    for (int bar = 4; bar < 16; ++bar)
    {
        const bool fill = (bar == 11 || bar == 15);
        hit (bar, 0, kick, 1.0f);
        hit (bar, 8, kick, 1.0f);
        if (bar % 2 == 1)
            hit (bar, 10, kick, 0.8f);
        hit (bar, 4, snare, 0.9f);

        if (fill)
        {
            hit (bar, 12, highTom, 0.9f);
            hit (bar, 13, highTom, 0.8f);
            hit (bar, 14, midTom, 0.9f);
            hit (bar, 15, lowTom, 1.0f);
        }
        else
        {
            hit (bar, 12, snare, 0.9f);
        }

        for (int s = 0; s < (fill ? 12 : 16); s += 2)
            hit (bar, s, (s == 14 && bar % 2 == 1) ? openHat : hat, s % 4 == 0 ? 0.6f : 0.45f);

        if (bar == 4 || bar == 12)
            hit (bar, 0, crash, 0.8f);
    }

    hit (16, 0, kick, 1.0f);
    hit (16, 0, crash, 0.9f);
    return p;
}

static Part makeSfx()
{
    return { { 14.0, 1.8, 72, 0.8f },     // power-up leading into the melody
             { 66.0, 0.5, 83, 0.9f } };   // coin at the very end
}

static void writeMidiFile (const File& file, const std::vector<std::pair<String, const Part*>>& tracks, double bpm)
{
    MidiFile mf;
    mf.setTicksPerQuarterNote (960);

    for (const auto& [name, part] : tracks)
    {
        MidiMessageSequence seq;
        seq.addEvent (MidiMessage::textMetaEvent (3, name), 0.0);
        seq.addEvent (MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)), 0.0);
        seq.addEvent (MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
        for (const auto& n : *part)
        {
            seq.addEvent (MidiMessage::noteOn (1, n.note, n.velocity), n.beat * 960.0);
            seq.addEvent (MidiMessage::noteOff (1, n.note), (n.beat + n.length) * 960.0);
        }
        seq.updateMatchedPairs();
        mf.addTrack (seq);
    }

    file.deleteFile();
    FileOutputStream out (file);
    mf.writeTo (out, 1);
}

static bool writeWav (const File& file, const AudioBuffer<float>& buffer, double sr)
{
    file.deleteFile();
    std::unique_ptr<OutputStream> stream = std::make_unique<FileOutputStream> (file);
    WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, AudioFormatWriterOptions {}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (16));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

static int runDemo (const String& path, const File& outDir)
{
    constexpr double sr = 44100.0, bpm = 140.0;
    outDir.createDirectory();

    const Part lead = makeLead(), chords = makeChords(), bass = makeBass(), drums = makeDrums(), sfx = makeSfx();

    struct Track { const char* preset; const Part* part; float gain; };
    const Track tracks[] = {
        { "Hero Lead",        &lead,   1.00f },
        { "Note Arpeggiator", &chords, 0.50f },
        { "Triangle Bass",    &bass,   1.00f },
        { "Drum Kit",         &drums,  0.85f },
    };

    const double seconds = 68.0 * 60.0 / bpm + 3.0;
    const int total = (int) std::ceil (seconds * sr);
    AudioBuffer<float> mix (2, total);
    mix.clear();

    auto renderPart = [&] (const String& preset, const Part& part, float gain)
    {
        auto plugin = loadPlugin (path, sr, 512);
        if (plugin == nullptr)
            return false;
        selectProgram (*plugin, preset);

        std::vector<NoteEvent> notes;
        for (const auto& n : part)
            notes.push_back ({ n.beat * 60.0 / bpm, n.length * 60.0 / bpm, n.note, n.velocity });

        auto out = renderNotes (*plugin, notes, seconds, sr, 512, bpm);
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, out, ch, 0, total, gain);
        std::cout << "  rendered " << preset << " (" << part.size() << " notes)\n";
        return true;
    };

    for (const auto& t : tracks)
        if (! renderPart (t.preset, *t.part, t.gain))
            return 1;

    // SFX track: one note each from two different presets
    if (! renderPart ("Power Up", Part { sfx[0] }, 0.45f) || ! renderPart ("Coin Pickup", Part { sfx[1] }, 0.45f))
        return 1;

    const float peak = mix.getMagnitude (0, total);
    if (peak > 0.0f)
        mix.applyGain (0.89f / peak);   // normalise to -1 dBFS

    const auto wavFile = outDir.getChildFile ("BitBuddy-Demo-PixelQuest.wav");
    const bool ok = writeWav (wavFile, mix, sr);
    std::cout << "  wrote " << wavFile.getFullPathName() << (ok ? "" : "  (FAILED)") << "\n";

    const auto midiDir = outDir.getChildFile ("midi");
    midiDir.createDirectory();
    writeMidiFile (midiDir.getChildFile ("1-Lead (Hero Lead).mid"),     { { "Lead", &lead } }, bpm);
    writeMidiFile (midiDir.getChildFile ("2-Chords (Note Arpeggiator).mid"), { { "Chords", &chords } }, bpm);
    writeMidiFile (midiDir.getChildFile ("3-Bass (Triangle Bass).mid"),  { { "Bass", &bass } }, bpm);
    writeMidiFile (midiDir.getChildFile ("4-Drums (Drum Kit).mid"),      { { "Drums", &drums } }, bpm);
    writeMidiFile (midiDir.getChildFile ("5-SFX (Power Up + Coin Pickup).mid"), { { "SFX", &sfx } }, bpm);
    writeMidiFile (midiDir.getChildFile ("PixelQuest-AllParts.mid"),
                   { { "Lead", &lead }, { "Chords", &chords }, { "Bass", &bass }, { "Drums", &drums }, { "SFX", &sfx } }, bpm);
    std::cout << "  wrote MIDI files to " << midiDir.getFullPathName() << "\n";
    return ok ? 0 : 1;
}

//==============================================================================
static void forEachComponent (Component& c, const std::function<void (Component&)>& fn)
{
    fn (c);
    for (auto* child : c.getChildren())
        forEachComponent (*child, fn);
}

template <typename T>
static T* findComponent (Component& root, const std::function<bool (T&)>& matches = nullptr)
{
    T* found = nullptr;
    forEachComponent (root, [&] (Component& c)
    {
        if (found == nullptr)
            if (auto* t = dynamic_cast<T*> (&c))
                if (matches == nullptr || matches (*t))
                    found = t;
    });
    return found;
}

static Button* findButton (Component& root, const String& text)
{
    return findComponent<Button> (root, [&] (Button& b) { return b.getButtonText() == text; });
}

static int runSnapshot (const File& outDir)
{
    outDir.createDirectory();
    BitBuddyProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    // key: 0 = C ... 11 = B;  scale: 0 = Scale Lock off, 4 = minor pentatonic
    struct Shot { const char* preset; std::vector<int> notes; const char* file; bool help; int key = 0, scale = 0;
                  const char* click = nullptr; };   // a button to press before the picture
    const Shot shots[] = {
        { "Hero Lead",       { 69 },         "ui-hero-lead.png",   false },
        { "Major Chord Arp", { 60 },         "ui-chord-arp.png",   false },
        { "Smart Chords",    { 62 },         "ui-scale-lock.png",  false, 9, 4 },   // A minor pentatonic
        { "C64 Squelch Bass", { 40 },        "ui-filter.png",      false },
        { "Game Boy Wave",   { 69 },         "ui-draw-wave.png",   false },
        { "Macro Stutter Lead", { 69 },      "ui-macros.png",      false, 0, 0, "MACROS" },
        { "Drum Kit",        { 36, 42 },     "ui-drum-kit.png",    false },
        { "Classic Square",  {},             "ui-help.png",        true  },
    };

    for (const auto& shot : shots)
    {
        selectProgram (proc, shot.preset);
        if (auto* p = proc.apvts.getParameter ("scaleKey")) p->setValueNotifyingHost (p->convertTo0to1 ((float) shot.key));
        if (auto* p = proc.apvts.getParameter ("scale"))    p->setValueNotifyingHost (p->convertTo0to1 ((float) shot.scale));

        // Play some audio so the scope has something to show
        AudioBuffer<float> buf (2, 512);
        for (int block = 0; block < 40; ++block)
        {
            MidiBuffer midi;
            if (block == 0)
                for (int n : shot.notes)
                    midi.addEvent (MidiMessage::noteOn (1, n, 0.8f), 0);
            buf.clear();
            proc.processBlock (buf, midi);
        }

        std::unique_ptr<AudioProcessorEditor> editor (proc.createEditor());
        editor->setVisible (true);
        if (shot.help)
            if (auto* b = findButton (*editor, "? HELP"))
                b->triggerClick();
        if (shot.click != nullptr)
            if (auto* b = findButton (*editor, shot.click))
                b->triggerClick();

        MessageManager::getInstance()->runDispatchLoopUntil (400);   // let timers update the UI

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = outDir.getChildFile (shot.file);
        file.deleteFile();
        FileOutputStream out (file);
        PNGImageFormat().writeImageToStream (image, out);
        std::cout << "  wrote " << file.getFullPathName() << "\n";

        MidiBuffer off;
        for (int n : shot.notes)
            off.addEvent (MidiMessage::noteOff (1, n), 0);
        buf.clear();
        proc.processBlock (buf, off);
    }
    return 0;
}

//==============================================================================
// CPU benchmark: how many times faster than real time each scenario renders.
static int runBench (const String& path)
{
    constexpr double sr = 44100.0, seconds = 60.0;
    auto plugin = loadPlugin (path, sr, 512);
    if (plugin == nullptr)
        return 1;

    auto everyBeat = [] (std::initializer_list<int> notes, double step, double length)
    {
        std::vector<NoteEvent> ev;
        for (double t = 0.0; t < seconds; t += step)
            for (int n : notes)
                ev.push_back ({ t, length, n, 0.8f });
        return ev;
    };

    struct Scenario { const char* name; const char* preset; std::vector<NoteEvent> notes; };
    const Scenario scenarios[] = {
        { "8-voice chords (PWM Strings)",  "PWM Strings",  everyBeat ({ 48, 52, 55, 60, 64, 67, 71, 74 }, 2.0, 1.9) },
        { "lead melody + echo (Hero Lead)", "Hero Lead",    everyBeat ({ 72 }, 0.25, 0.2) },
        { "drum pattern (Drum Kit)",        "Drum Kit",     everyBeat ({ 36, 42 }, 0.25, 0.1) },
        { "idle, no notes",                 "Hero Lead",    {} },
    };

    std::cout << "\n=== CPU benchmark (60 s of audio, best of 3) ===\n\n";
    for (const auto& s : scenarios)
    {
        selectProgram (*plugin, s.preset);
        double best = 1e9;
        for (int run = 0; run < 3; ++run)
            best = jmin (best, secondsToRender (*plugin, s.notes, seconds));
        std::cout << "  " << String (s.name).paddedRight (' ', 34) << String (roundToInt (seconds / best)).paddedLeft (' ', 7)
                  << "x real time  (" << String (100.0 * best / seconds, 3) << "% of one core)\n";
    }
    return 0;
}

//==============================================================================
// Opens the editor as a real window, clicks controls with simulated mouse
// events, and checks the computer keyboard stays on the piano.
static void clickOn (ComponentPeer& peer, Component& editor, Component& target)
{
    const auto pos = editor.getLocalPoint (&target, target.getLocalBounds().getCentre()).toFloat();
    const auto t = Time::currentTimeMillis();
    peer.handleMouseEvent (MouseInputSource::InputSourceType::mouse, pos, ModifierKeys (ModifierKeys::leftButtonModifier),
                           MouseInputSource::defaultPressure, MouseInputSource::defaultOrientation, t);
    peer.handleMouseEvent (MouseInputSource::InputSourceType::mouse, pos, ModifierKeys(),
                           MouseInputSource::defaultPressure, MouseInputSource::defaultOrientation, t + 60);
    MessageManager::getInstance()->runDispatchLoopUntil (200);
}

static int runFocusTest()
{
    Results res;
    std::cout << "\n=== Keyboard focus test ===\n\n";

    BitBuddyProcessor proc;
    proc.prepareToPlay (44100.0, 512);
    std::unique_ptr<AudioProcessorEditor> editor (proc.createEditor());
    editor->addToDesktop (ComponentPeer::windowHasTitleBar | ComponentPeer::windowAppearsOnTaskbar);
    editor->setTopLeftPosition (80, 80);
    editor->setVisible (true);
    editor->toFront (true);
    MessageManager::getInstance()->runDispatchLoopUntil (800);

    // 1. Structure: only the piano may ever take keyboard focus
    MidiKeyboardComponent* piano = nullptr;
    StringArray offenders;
    for (auto* child : editor->getChildren())
        forEachComponent (*child, [&] (Component& c)
        {
            if (auto* k = dynamic_cast<MidiKeyboardComponent*> (&c))
            {
                piano = k;
                return;
            }
            if (piano != nullptr && piano->isParentOf (&c))   // the piano's own (hidden) scroll arrows
                return;
            if (c.getWantsKeyboardFocus())
                offenders.add (typeid (c).name());
        });
    res.check (piano != nullptr && offenders.isEmpty(), "Only the piano can take keyboard focus",
               offenders.isEmpty() ? String ("ok") : offenders.joinIntoString (", "));

    auto* peer = editor->getPeer();
    if (peer == nullptr || piano == nullptr || ! peer->isFocused())
    {
        std::cout << "  SKIP  click tests: the test window could not become the active window\n";
        std::cout << "\n  " << res.passed << " passed, " << res.failed << " failed\n";
        return res.failed;
    }

    res.check (piano->hasKeyboardFocus (false), "Keys work as soon as the window opens", "");

    // 2. Click a selection of controls; the piano must keep the keys after each one
    const std::pair<const char*, Component*> targets[] = {
        { "wave button (SAW)",     findButton (*editor, "SAW") },
        { "knob (Attack)",         findComponent<Slider> (*editor) },
        { "MONO button",           findButton (*editor, "MONO") },
        { "next-preset button",    findButton (*editor, ">") },
        { "SURPRISE ME! button",   findButton (*editor, "SURPRISE ME!") },
    };

    for (const auto& [what, target] : targets)
    {
        if (target == nullptr)
        {
            res.check (false, String ("Click ") + what, "control not found");
            continue;
        }
        clickOn (*peer, *editor, *target);
        res.check (piano->hasKeyboardFocus (false), String ("Keys still work after clicking ") + what, "");

        if (String (what).contains ("SAW"))   // prove the simulated clicks really land
            res.check (roundToInt (proc.apvts.getRawParameterValue ("wave")->load()) == 4,
                       "Simulated click selected the SAW wave", "");
    }

    // 3. Open and close a drop-down menu: focus must come back by itself
    if (auto* combo = findComponent<ComboBox> (*editor))
    {
        clickOn (*peer, *editor, *combo);
        MessageManager::getInstance()->runDispatchLoopUntil (300);
        const bool menuOpened = ModalComponentManager::getInstance()->getNumModalComponents() > 0;
        PopupMenu::dismissAllActiveMenus();
        MessageManager::getInstance()->runDispatchLoopUntil (400);
        res.check (piano->hasKeyboardFocus (false), "Keys come back after a drop-down menu closes",
                   menuOpened ? "menu opened + closed" : "menu did not open");
    }

    editor->removeFromDesktop();
    std::cout << "\n  " << res.passed << " passed, " << res.failed << " failed\n";
    return res.failed;
}

//==============================================================================
// Draws the 1024 x 1024 app icon (used for the Windows / Mac / iOS / Android apps).
static int runIcon (const File& file)
{
    constexpr int size = 1024;
    Image img (Image::ARGB, size, size, true);
    {
        Graphics g (img);
        const Rectangle<float> area (0.0f, 0.0f, (float) size, (float) size);
        g.setColour (bb::pal::bg);
        g.fillRoundedRectangle (area.reduced (40.0f), 190.0f);
        g.setColour (bb::pal::yellow);
        g.drawRoundedRectangle (area.reduced (72.0f), 160.0f, 22.0f);

        // "BB" in chunky pixel letters with a drop shadow
        g.setFont (bb::pixelFont (330.0f));
        const juce::Colour letterColours[] = { bb::pal::orange, bb::pal::cyan };
        for (int i = 0; i < 2; ++i)
        {
            const Rectangle<int> cell (182 + i * 330, 200, 330, 400);
            g.setColour (bb::pal::purple);
            g.drawText ("B", cell.translated (22, 22), Justification::centred);
            g.setColour (letterColours[i]);
            g.drawText ("B", cell, Justification::centred);
        }

        // A pixel-art square wave underneath
        constexpr int px = 32, x0 = 176, x1 = 848, high = 680, low = 776, halfCycle = 4 * px;
        g.setColour (bb::pal::lime);
        int prevY = -1;
        for (int x = x0; x < x1; x += px)
        {
            const int y = (((x - x0) / halfCycle) % 2 == 0) ? high : low;
            if (prevY >= 0 && y != prevY)
                g.fillRect (x, high, px, low - high + px);
            g.fillRect (x, y, px, px);
            prevY = y;
        }
    }

    file.deleteFile();
    FileOutputStream out (file);
    const bool ok = out.openedOk() && PNGImageFormat().writeImageToStream (img, out);
    std::cout << "  wrote " << file.getFullPathName() << (ok ? "" : "  (FAILED)") << "\n";
    return ok ? 0 : 1;
}

//==============================================================================
int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI juceInit;

    StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    if (args.size() >= 2 && args[0] == "--test")
        return runTests (args[1]);
    if (args.size() >= 3 && args[0] == "--demo")
        return runDemo (args[1], File (args[2]));
    if (args.size() >= 2 && args[0] == "--snapshot")
        return runSnapshot (File (args[1]));
    if (args.size() >= 1 && args[0] == "--focus-test")
        return runFocusTest();
    if (args.size() >= 2 && args[0] == "--bench")
        return runBench (args[1]);
    if (args.size() >= 2 && args[0] == "--icon")
        return runIcon (File (args[1]));

    std::cout << "usage:\n  BitBuddyTestHost --test <plugin.vst3>\n"
                 "  BitBuddyTestHost --demo <plugin.vst3> <outDir>\n"
                 "  BitBuddyTestHost --snapshot <outDir>\n";
    return 2;
}
