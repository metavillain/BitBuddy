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
#include "../src/Presets.h"
#include "../src/GameSounds.h"
#include "../src/SongPlayer.h"
#include <set>

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
static int programIndex (AudioProcessor& p, const String& name)
{
    for (int i = 0; i < p.getNumPrograms(); ++i)
        if (p.getProgramName (i) == name)
            return i;
    std::cout << "  !! program not found: " << name << "\n";
    return 0;
}

static bool selectProgram (AudioProcessor& p, const String& name)
{
    const int i = programIndex (p, name);
    if (p.getProgramName (i) != name)
        return false;
    if (p.getCurrentProgram() == i)
        p.setCurrentProgram ((i + 1) % p.getNumPrograms());
    p.setCurrentProgram (i);
    return true;
}

static bool writePng (const Image& image, const File& file)
{
    file.deleteFile();
    FileOutputStream out (file);
    const bool ok = out.openedOk() && PNGImageFormat().writeImageToStream (image, out);
    std::cout << "  wrote " << file.getFullPathName() << "\n";
    return ok;
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

constexpr float kLastWave = (float) (bb::kNumWaves - 1);   // wave index -> 0..1: index / kLastWave
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

// Runs BitBuddy's own processor with no incoming MIDI (it plays whatever its note player, on-screen
// keyboard or hum tracker sends), optionally calling `during` once, `callAt` seconds in.
static AudioBuffer<float> runOwn (BitBuddyProcessor& proc, double seconds, double sr,
                                  double callAt = -1.0, const std::function<void()>& during = nullptr)
{
    proc.prepareToPlay (sr, 512);
    AudioBuffer<float> result (2, (int) (seconds * sr)), blk (2, 512);
    result.clear();
    for (int pos = 0; pos + 512 <= result.getNumSamples(); pos += 512)
    {
        if (during != nullptr && callAt >= 0.0 && pos <= (int) (callAt * sr) && (int) (callAt * sr) < pos + 512)
            during();
        MidiBuffer midi;
        blk.clear();
        proc.processBlock (blk, midi);
        for (int ch = 0; ch < 2; ++ch)
            result.copyFrom (ch, pos, blk, ch, 0, 512);
    }
    return result;
}

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
    const int ownParams = std::make_unique<BitBuddyProcessor>()->getParameters().size();   // every knob + switch BitBuddy has
    const int formatExtras = plugin->getPluginDescription().pluginFormatName == "VST3" ? 2 : 0;   // VST3 adds bypass + program
    res.check (automatable == ownParams + formatExtras, "Automatable parameters in the DAW",
               String (automatable) + " of " + String (ownParams) + " (+" + String (plugin->getParameters().size() - automatable) + " hidden MIDI-CC slots)");
    res.check (plugin->getNumPrograms() == (int) bb::factoryPresets().size() && plugin->getNumPrograms() >= 31, "Factory presets exposed to the DAW", String (plugin->getNumPrograms()));

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
        { "Square",      0.0f / kLastWave, 69 }, { "Pulse 25%", 1.0f / kLastWave, 57 }, { "Pulse 12.5%", 2.0f / kLastWave, 72 },
        { "Triangle",    3.0f / kLastWave, 45 }, { "Saw",       4.0f / kLastWave, 64 }, { "Metal Noise", 6.0f / kLastWave, 69 },
        { "Draw (sine)", 8.0f / kLastWave, 69 },
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
            setParam (*plugin, "Wave", 3.0f / kLastWave);
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

    // ---- 4e. 1.2: sweep modes, vibrato shapes, stereo -------------------------
    std::cout << "\n  -- sweep modes, vibrato shapes, stereo --\n";
    {
        // Straight on the engine: one held note, mono
        auto engineNote = [&] (const bb::EngineParams& ep, int note, double holdSec, double totalSec)
        {
            bb::ChipEngine e;
            e.prepare (sr);
            e.setParams (ep);
            AudioBuffer<float> b (1, (int) (totalSec * sr));
            b.clear();
            const int hold = (int) (holdSec * sr);
            e.noteOn (note, 0.8f);
            e.render (b.getWritePointer (0), hold);
            e.noteOff (note);
            e.render (b.getWritePointer (0) + hold, b.getNumSamples() - hold);
            return b;
        };
        auto noteAt = [&] (const AudioBuffer<float>& b, double sec, int length = 2048)
        {
            const double hz = pitchAt (b, sec - 0.5 * length / sr, length, sr, 30.0);
            return hz > 0.0 ? 69.0 + 12.0 * std::log2 (hz / 440.0) : -1.0;
        };

        bb::EngineParams ep;
        ep.wave = bb::Wave::Square;
        ep.sustain = 1.0f;
        ep.sweepAmt = 12.0f;
        ep.sweepTime = 0.25f;

        ep.sweepMode = bb::SweepMode::Slide;
        const double slideEnd = noteAt (engineNote (ep, 72, 1.0, 1.1), 0.5);
        ep.sweepMode = bb::SweepMode::Forever;
        const auto forever = engineNote (ep, 72, 1.0, 1.1);
        const double f1 = noteAt (forever, 0.25), f2 = noteAt (forever, 0.5);   // 72 - 12 per 0.25 s
        res.check (std::abs (slideEnd - 72.0) < 0.2 && std::abs (f1 - 60.0) < 0.6 && std::abs (f2 - 48.0) < 0.6,
                   "SWEEP FOREVER: keeps falling 12 semitones every 0.25 s (SLIDE stops)",
                   "slide " + String (slideEnd, 1) + ", forever " + String (f1, 1) + " -> " + String (f2, 1));

        // Rising forever: past the top it goes quiet (like the NES), even with the key held
        ep.sweepAmt = -24.0f;
        ep.sweepTime = 0.1f;
        const auto rising = engineNote (ep, 72, 1.0, 1.1);
        const float early = peakOf (rising, 0.02, 0.1, sr), late = peakOf (rising, 0.4, 0.9, sr);
        res.check (early > 0.1f && late < 1.0e-4f, "SWEEP FOREVER rising: goes quiet past the top (no whistle)",
                   String (Decibels::gainToDecibels (early), 1) + " dB -> " + String (Decibels::gainToDecibels (late), 1) + " dB");

        ep.sweepAmt = 12.0f;
        ep.sweepTime = 0.25f;
        ep.sweepMode = bb::SweepMode::Bounce;
        const auto bounce = engineNote (ep, 72, 1.2, 1.3);
        const double b1 = noteAt (bounce, 0.25, 1024), b2 = noteAt (bounce, 0.5, 1024), b3 = noteAt (bounce, 0.75, 1024);
        res.check (std::abs (b1 - 60.0) < 1.0 && std::abs (b2 - 72.0) < 1.0 && std::abs (b3 - 60.0) < 1.0,
                   "SWEEP BOUNCE: down and back up, over and over (siren)",
                   String (b1, 1) + " / " + String (b2, 1) + " / " + String (b3, 1));

        // Vibrato shapes: TRILL only ever plays the note and 2 semitones up; RANDOM wanders
        ep.sweepAmt = 0.0f;
        ep.vibDepth = 1.0f;
        ep.vibRate = 4.0f;
        ep.vibDelay = 0.0f;
        auto notesSeen = [&] (const AudioBuffer<float>& b)
        {
            std::vector<double> seen;
            for (double t = 0.2; t < 1.4; t += 0.02)
                seen.push_back (noteAt (b, t, 512));
            return seen;
        };
        ep.vibShape = bb::VibShape::Trill;
        int low = 0, high = 0, between = 0;
        for (double n : notesSeen (engineNote (ep, 69, 1.5, 1.5)))
            (std::abs (n - 69.0) < 0.3 ? low : (std::abs (n - 71.0) < 0.3 ? high : between))++;
        res.check (low > 10 && high > 10 && between <= 6, "VIB TRILL: jumps between A and B (VIBRATO 1.0 = 2 semitones)",
                   String (low) + " x A, " + String (high) + " x B, " + String (between) + " in between (edges)");

        ep.vibShape = bb::VibShape::Random;
        const auto wobbly = notesSeen (engineNote (ep, 69, 1.5, 1.5));
        const auto [mn, mx] = std::minmax_element (wobbly.begin(), wobbly.end());
        res.check (*mx - *mn > 0.5 && *mn > 67.7 && *mx < 70.3, "VIB RANDOM: wanders, but stays within VIBRATO",
                   "range " + String (*mn, 2) + " .. " + String (*mx, 2));

        // STEREO through the plugin (Wide = next side for each note, Swing = moves once a bar)
        auto balance = [&] (const AudioBuffer<float>& b, double from, double to)   // > 1 = louder on the left
        {
            const int s0 = (int) (from * sr), n = (int) ((to - from) * sr);
            return (double) b.getRMSLevel (0, s0, n) / jmax (1.0e-9, (double) b.getRMSLevel (1, s0, n));
        };
        selectProgram (*plugin, "Classic Square");
        setParam (*plugin, "Stereo", 0.0f);
        const auto middle = renderNotes (*plugin, { { 0.0, 0.4, 60, 0.8f }, { 0.5, 0.4, 64, 0.8f } }, 1.0, sr);
        setParam (*plugin, "Stereo", 0.5f);
        const auto wideOut = renderNotes (*plugin, { { 0.0, 0.4, 60, 0.8f }, { 0.5, 0.4, 64, 0.8f } }, 1.0, sr);
        const double m1 = balance (middle, 0.05, 0.35), w1 = balance (wideOut, 0.05, 0.35), w2 = balance (wideOut, 0.55, 0.85);
        res.check (std::abs (m1 - 1.0) < 1.0e-6 && ((w1 > 2.0 && w2 < 0.5) || (w1 < 0.5 && w2 > 2.0)),
                   "STEREO WIDE: one note on each side (MIDDLE: both equal)",
                   "middle L/R " + String (m1, 2) + ", wide " + String (w1, 2) + " then " + String (w2, 2));

        selectProgram (*plugin, "Drum Kit");
        setParam (*plugin, "Stereo", 0.5f);
        const auto kit = renderNotes (*plugin, { { 0.0, 0.2, 36, 1.0f }, { 0.4, 0.2, 42, 1.0f } }, 0.8, sr);
        const double kick = balance (kit, 0.0, 0.2), hat = balance (kit, 0.4, 0.45);
        res.check (std::abs (kick - 1.0) < 0.01 && hat < 0.5, "STEREO WIDE drums: kick in the middle, hi-hat on the right",
                   "kick L/R " + String (kick, 2) + ", hat " + String (hat, 2));

        selectProgram (*plugin, "Classic Square");
        setParam (*plugin, "Stereo", 1.0f);   // Swing, at the test's 120 BPM: one bar = 2 s
        const auto swing = renderNotes (*plugin, { { 0.0, 2.1, 60, 0.8f } }, 2.2, sr);
        const double s1 = balance (swing, 0.4, 0.6), s2 = balance (swing, 1.4, 1.6);
        res.check (s1 < 0.3 && s2 > 3.0, "STEREO SWING: right, then left, once a bar",
                   "L/R at 0.5 s " + String (s1, 2) + ", at 1.5 s " + String (s2, 2));
        setParam (*plugin, "Stereo", 0.0f);
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
        setParam (*plugin, "Wave", 3.0f / kLastWave);
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
        auto procPtr = std::make_unique<BitBuddyProcessor>();   // (big: on the heap, like a DAW does)
        auto& proc = *procPtr;
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
        // Switches (choice parameters) may hop a whole setting; only knobs are held to "a little"
        StringArray switches;
        for (auto* p : proc.getParameters())
            if (auto* choice = dynamic_cast<AudioParameterChoice*> (p))
                switches.add (choice->getParameterID());
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
        proc.selectPreset (programIndex (proc, "Game Boy Wave"));
        const bool presetDrawingLoaded = BitBuddyProcessor::customWaveToString (proc.getCustomWave()) == "89BCDEEFFFEDCB98AC864211234688A6";

        // Renders one held note through BitBuddy's own processor (macros + drawings aren't host parameters)
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

        // Play the saw drawing at A4
        proc.selectPreset (0);
        set ("wave", 8.0f);
        proc.setCustomWave (saw);
        const double hz = pitchAt (renderDirect (69, 1.0), 0.3, 8192, sr, 40.0);

        // ---- MACROS ----

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
        proc.selectPreset (programIndex (proc, "Macro Chord Blip"));
        const bool presetMacros = proc.getMacro (bb::kMacroArp).length == 3 && proc.getMacro (bb::kMacroVolume).length == 12;
        res.check (macroSaved && clearedByPreset && presetMacros, "MACROS: saved with project, cleared by plain presets, loaded by macro presets",
                   String (macroSaved ? "saved" : "LOST") + ", " + (clearedByPreset ? "cleared" : "STILL ON") + ", "
                       + (presetMacros ? "preset ok" : "PRESET WRONG"));

        // ---- JAM band: plays in your key, every chord root right, nothing broken ----
        {
            auto bassNoteAt = [&] (int key, bb::Scale scale, int style, double fromSec)
            {
                bb::JamBand band;
                band.prepare (sr);
                band.setSong (key, scale);
                band.setStyle (style);
                band.setTempo (bb::jamStyleTempo (style));
                band.setTrackLevels (0.0f, 1.0f, 0.0f);   // bass only
                band.start();
                AudioBuffer<float> out (1, (int) (sr * (fromSec + 0.4)));
                for (int i = 0; i < out.getNumSamples(); ++i)
                    out.setSample (0, i, band.nextSample());
                return 69.0 + 12.0 * std::log2 (pitchAt (out, fromSec, 8192, sr, 40.0) / 440.0);
            };
            // Adventure in C major: bar 1 = C (bass C2), bar 2 = G (G2); A minor: bar 1 = A (A2)
            const double c = bassNoteAt (0, bb::Scale::Major, 0, 0.02);
            const double g = bassNoteAt (0, bb::Scale::Major, 0, 2.02);
            const double a = bassNoteAt (9, bb::Scale::Minor, 0, 0.02);
            res.check (std::abs (c - 36) < 0.3 && std::abs (g - 43) < 0.3 && std::abs (a - 45) < 0.3,
                       "JAM: bass follows the song (C major: C then G; A minor: A)",
                       String (c, 1) + " " + String (g, 1) + " " + String (a, 1));

            // Every style, full band, 8 s: finite, audible, never clipping
            String levels;
            bool bandOk = true;
            for (int style = 0; style < bb::kNumJamStyles; ++style)
            {
                bb::JamBand band;
                band.prepare (sr);
                band.setSong (2, bb::Scale::Dorian);
                band.setStyle (style);
                band.setTempo (bb::jamStyleTempo (style));
                band.start();
                float peakBand = 0.0f;
                bool finite = true;
                for (int i = 0; i < (int) (sr * 8.0); ++i)
                {
                    const float x = band.nextSample();
                    finite = finite && std::isfinite (x);
                    peakBand = jmax (peakBand, std::abs (x));
                }
                band.stop();
                for (int i = 0; i < (int) (sr * 5.0); ++i)   // a crash cymbal needs ~4 s to fade out
                    band.nextSample();
                bandOk = bandOk && finite && peakBand > 0.05f && peakBand < 0.98f && ! band.isSounding();
                levels << bb::jamStyleName (style) << " " << String (Decibels::gainToDecibels (peakBand), 1) << " dB  ";
            }
            res.check (bandOk, "JAM: all 4 styles play, stay below 0 dBFS, stop cleanly", levels);
        }

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

    // ---- 12. 1.2: game sounds (sound FX, SAVE WAV, jingles, note player) -----------
    std::cout << "\n  -- game sounds --\n";
    {
        auto procPtr = std::make_unique<BitBuddyProcessor>();   // (big: on the heap, like a DAW does)
        auto& proc = *procPtr;
        auto get = [&] (const char* id) { auto* p = proc.apvts.getParameter (id); return p->convertFrom0to1 (p->getValue()); };

        // Runs BitBuddy's own processor (whatever its note player plays), optionally calling `during` at a time
        auto run = [&] (double seconds, double callAt = -1.0, std::function<void()> during = nullptr)
        {
            return runOwn (proc, seconds, sr, callAt, during);
        };

        // Every kind makes a sound that fits it, plays straight away, then goes quiet
        String detail;
        bool allOk = true;
        for (int type = 0; type < bb::kNumSfx; ++type)
        {
            proc.makeSoundEffect (type);
            const auto out = run (5.0);   // (echo repeats can take a few seconds to die away)
            const float peak = peakOf (out, 0.0, 0.5, sr), tail = peakOf (out, 4.9, 5.0, sr);
            const float sweep = get ("sweepAmt");
            const bool fits = type == (int) bb::Sfx::Jump  ? sweep < 0.0f
                            : type == (int) bb::Sfx::Laser ? sweep >= 24.0f
                            : type == (int) bb::Sfx::Coin  ? roundToInt (get ("arpMode")) == 1
                            : type == (int) bb::Sfx::Bomb  ? roundToInt (get ("sweepMode")) == 1 && sweep > 0.0f
                            : type == (int) bb::Sfx::Boom  ? roundToInt (get ("wave")) == 5
                            : true;
            const bool named = proc.getDisplayPresetName().startsWithIgnoreCase (String (bb::sfxName (type)).substring (0, 3));
            const bool ok = allFinite (out) && peak > 0.02f && peak < 1.0f && tail < 1.0e-4f && fits && named;
            allOk = allOk && ok;
            detail << bb::sfxName (type) << (ok ? String (" ok  ")
                                                : " FAILED (peak " + String (peak, 3) + ", tail " + String (tail, 6)
                                                      + (fits ? "" : ", wrong kind") + (named ? "" : ", name") + ")  ");
        }
        res.check (allOk, "SOUND FX: all 8 kinds sound right, play at once, then stop", detail);

        // Every tap is a new sound
        proc.makeSoundEffect ((int) bb::Sfx::Laser);
        const auto first = std::make_tuple (get ("wave"), get ("sweepAmt"), get ("sweepTime"), get ("decay"));
        proc.makeSoundEffect ((int) bb::Sfx::Laser);
        const auto second = std::make_tuple (get ("wave"), get ("sweepAmt"), get ("sweepTime"), get ("decay"));
        res.check (first != second, "SOUND FX: tapping again makes a different one", proc.getDisplayPresetName());

        // SAVE WAV: just the sound, from its first moment until it has died away
        proc.makeSoundEffect ((int) bb::Sfx::Coin);
        const auto wavFile = File::getSpecialLocation (File::tempDirectory).getChildFile ("bitbuddy-test-sfx.wav");
        const bool saved = proc.renderSoundToWav (wavFile);
        double wavSeconds = 0.0;
        float wavPeak = 0.0f, wavStart = 0.0f, wavEnd = 1.0f;
        {
            WavAudioFormat wav;
            if (auto reader = std::unique_ptr<AudioFormatReader> (wav.createReaderFor (wavFile.createInputStream().release(), true)))
            {
                AudioBuffer<float> b (2, (int) reader->lengthInSamples);
                reader->read (&b, 0, b.getNumSamples(), 0, true, true);
                wavSeconds = (double) b.getNumSamples() / reader->sampleRate;
                wavPeak = b.getMagnitude (0, b.getNumSamples());
                wavStart = b.getMagnitude (0, jmin (b.getNumSamples(), 441));   // the first 10 ms
                wavEnd = b.getMagnitude (b.getNumSamples() - 441, 441);         // the last 10 ms
            }
        }
        wavFile.deleteFile();
        res.check (saved && wavSeconds > 0.2 && wavSeconds < 3.0 && wavPeak > 0.05f && wavStart > 0.01f && wavEnd < 1.0e-3f,
                   "SAVE WAV: the sound effect, starting at once, ending when it fades",
                   String (wavSeconds, 2) + " s, peak " + String (wavPeak, 2) + ", start " + String (wavStart, 3)
                       + ", end " + String (wavEnd, 5));

        // Jingles: in your key + scale, the right shape, and they play
        Random rng (42);
        bool inKey = true, shapes = true;
        for (int i = 0; i < 40; ++i)
        {
            const auto type = (bb::Jingle) (i % bb::kNumJingles);
            const auto notes = bb::makeJingle (type, 9, bb::Scale::Minor, 140.0, rng);   // A minor
            for (const auto& n : notes)
                inKey = inKey && bb::inScale (n.note, 9, bb::Scale::Minor) && n.start >= 0.0 && n.start < 4.0;
            const int firstNote = notes.front().note, lastNote = notes.back().note;
            if (type == bb::Jingle::GameOver) shapes = shapes && lastNote < firstNote;   // goes down
            if (type == bb::Jingle::GotItem)  shapes = shapes && lastNote > firstNote;   // goes up
            shapes = shapes && notes.size() >= 4;
        }
        proc.selectPreset (0);
        int played = 0;
        for (int type = 0; type < bb::kNumJingles; ++type)
        {
            proc.playJingle (type);
            const auto out = run (4.5);
            played += (peakOf (out, 0.0, 1.0, sr) > 0.05f && peakOf (out, 4.3, 4.5, sr) < 1.0e-4f && ! proc.notePlayer.isPlaying()) ? 1 : 0;
        }
        res.check (inKey && shapes && played == bb::kNumJingles, "JINGLES: in your KEY + SCALE, right shape, all 4 play and end",
                   String (inKey ? "in key" : "OUT OF KEY") + ", " + (shapes ? "shapes ok" : "SHAPE WRONG") + ", "
                       + String (played) + "/4 played");

        // Stopping (or starting something else) never leaves a note hanging
        proc.selectPreset (programIndex (proc, "Classic Square"));   // short release, no echo
        proc.notePlayer.play ({ { 0.0, 3.0, 60, 0.8f }, { 0.0, 3.0, 64, 0.8f } });
        const auto stopped = run (2.0, 0.5, [&] { proc.notePlayer.stop(); });
        proc.notePlayer.play ({ { 0.0, 3.0, 60, 0.8f } });
        const auto replaced = run (2.0, 0.5, [&] { proc.notePlayer.play ({ { 0.0, 0.1, 72, 0.8f } }); });
        res.check (peakOf (stopped, 0.3, 0.5, sr) > 0.05f && peakOf (stopped, 1.8, 2.0, sr) < 1.0e-4f
                       && peakOf (replaced, 1.8, 2.0, sr) < 1.0e-4f,
                   "NOTE PLAYER: stop / replace releases every note (none stuck)",
                   "after stop " + String (Decibels::gainToDecibels (peakOf (stopped, 1.8, 2.0, sr)), 1) + " dB, after replace "
                       + String (Decibels::gainToDecibels (peakOf (replaced, 1.8, 2.0, sr)), 1) + " dB");
    }

    // ---- 13. 1.2: sound codes ---------------------------------------------------
    std::cout << "\n  -- sound codes --\n";
    {
        auto minePtr = std::make_unique<BitBuddyProcessor>(), friendsPtr = std::make_unique<BitBuddyProcessor>();
        auto& mine = *minePtr;
        auto& friends = *friendsPtr;
        auto setOn = [] (BitBuddyProcessor& p, const char* id, float v) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
        auto soundOf = [] (BitBuddyProcessor& p)
        {
            std::map<String, float> values;
            for (auto* prm : p.getParameters())
                if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
                    if (! bb::isSongSetting (rp->getParameterID()))
                        values[rp->getParameterID()] = rp->getValue();
            return values;
        };

        // My sound: a preset, mutated, with a drawing and a macro
        mine.selectPreset (programIndex (mine, "Hero Lead"));
        mine.mutate();
        setOn (mine, "wave", 8.0f);
        bb::CustomWave drawing {};
        for (int i = 0; i < bb::kCustomWaveSteps; ++i)
            drawing[(size_t) i] = (std::uint8_t) ((i * 7) % 16);
        mine.setCustomWave (drawing);
        bb::Macro arp;
        BitBuddyProcessor::macroFromString ("3|0|0,4,7", arp);
        mine.setMacro (bb::kMacroArp, arp);
        const auto code = mine.getSoundCode();

        // My friend plays in another key, scale and volume - those must stay theirs
        setOn (friends, "scaleKey", 9.0f);
        setOn (friends, "scale", 2.0f);
        setOn (friends, "volume", -10.0f);
        const auto friendSong = std::make_tuple (friends.apvts.getParameter ("scaleKey")->getValue(),
                                                 friends.apvts.getParameter ("scale")->getValue(),
                                                 friends.apvts.getParameter ("volume")->getValue());

        // Pasted from a chat message, broken over two lines
        const auto message = "hey try this one!  " + code.substring (0, 40) + "\n" + code.substring (40) + "  :)";
        const bool loaded = friends.loadSoundCode (message);
        bool same = true;
        const auto a = soundOf (mine), b = soundOf (friends);
        for (const auto& [id, v] : a)
            same = same && std::abs (b.at (id) - v) <= 1.0f / 65535.0f;
        const bool extras = friends.getCustomWave() == drawing
                         && BitBuddyProcessor::macroToString (friends.getMacro (bb::kMacroArp)) == BitBuddyProcessor::macroToString (arp)
                         && ! friends.getMacro (bb::kMacroVolume).isOn();
        const bool songKept = friendSong == std::make_tuple (friends.apvts.getParameter ("scaleKey")->getValue(),
                                                             friends.apvts.getParameter ("scale")->getValue(),
                                                             friends.apvts.getParameter ("volume")->getValue());
        res.check (loaded && same && extras && songKept && code.startsWith ("BB1-") && code.length() < 200,
                   "SOUND CODE: same sound on another BitBuddy (drawing + macros too), their Key/Scale/volume stay",
                   String (code.length()) + " characters, " + (same ? "knobs same" : "KNOBS DIFFER") + ", "
                       + (extras ? "drawing + macros same" : "EXTRAS DIFFER: wave " + BitBuddyProcessor::customWaveToString (friends.getCustomWave())
                                                               + " vs " + BitBuddyProcessor::customWaveToString (drawing) + ", arp "
                                                               + BitBuddyProcessor::macroToString (friends.getMacro (bb::kMacroArp)) + " vs "
                                                               + BitBuddyProcessor::macroToString (arp)) + ", " + (songKept ? "song kept" : "SONG CHANGED")
                       + ", name \"" + friends.getDisplayPresetName() + "\"");

        // A mistyped code (or any other text) is refused and changes nothing
        auto typo = code;
        const int at = code.length() / 2;
        typo = typo.replaceSection (at, 1, typo[at] == 'A' ? "B" : "A");
        auto freshPtr = std::make_unique<BitBuddyProcessor>();   // (big: on the heap, like a DAW does)
        auto& fresh = *freshPtr;
        const auto before = soundOf (fresh);
        const bool refused = ! fresh.loadSoundCode (typo) && ! fresh.loadSoundCode ("BB1-hello") && ! fresh.loadSoundCode ("no code here")
                          && soundOf (fresh) == before;
        res.check (refused, "SOUND CODE: a mistyped code is refused (the sound stays as it was)", typo.substring (0, 24) + "...");
    }

    // ---- 14. 1.2: your voice (VOICE wave) + HUM ----------------------------------
    std::cout << "\n  -- your voice + hum --\n";
    {
        // A pretend voice: a buzzy tone with harmonics, a little vibrato and breath noise
        auto sing = [] (std::initializer_list<std::pair<double, double>> notes /* hz (0 = quiet), seconds */, double rate)
        {
            double total = 0.0;
            for (auto& n : notes) total += n.second;
            AudioBuffer<float> b (1, (int) (total * rate));
            b.clear();
            Random noise (7);
            double phase = 0.0, t = 0.0;
            int pos = 0;
            for (auto& [hz, seconds] : notes)
                for (int i = 0; i < (int) (seconds * rate); ++i, ++pos, t += 1.0 / rate)
                {
                    if (hz <= 0.0) { b.setSample (0, pos, 0.002f * (noise.nextFloat() - 0.5f)); continue; }
                    phase += hz * (1.0 + 0.008 * std::sin (2.0 * MathConstants<double>::pi * 5.0 * t)) / rate;
                    float x = 0.0f;
                    for (int h = 1; h <= 6; ++h)
                        x += (float) (std::sin (2.0 * MathConstants<double>::pi * h * phase) / h);
                    b.setSample (0, pos, 0.25f * x + 0.01f * (noise.nextFloat() - 0.5f));
                }
            return b;
        };

        // HUM: the pitch finder is accurate across a voice's range...
        bool accurate = true;
        String pitchDetail;
        for (double hz : { 98.0, 196.0, 261.63, 440.0, 784.0 })
        {
            const auto b = sing ({ { hz, 0.1 } }, 11025.0);
            const float found = bb::HumTracker::detectPitch (b.getReadPointer (0) + 200, 512, 11025.0);
            accurate = accurate && found > 0.0f && std::abs (centsOff (found, hz)) < 15.0;
            pitchDetail << String (hz, 0) << "->" << String (found, 1) << " ";
        }
        res.check (accurate, "HUM: finds the pitch of a voice (98 Hz .. 784 Hz, within 15 cents)", pitchDetail);

        // ...and a sung A3, E4, then quiet becomes exactly: A3 on, A3 off + E4 on, E4 off
        bb::HumTracker tracker;
        const auto song = sing ({ { 220.0, 0.6 }, { 329.63, 0.6 }, { 0.0, 0.4 } }, 48000.0);
        std::vector<std::tuple<int, bool, double>> heard;
        for (int pos = 0; pos < song.getNumSamples(); pos += 480)
        {
            tracker.process (song.getReadPointer (0) + pos, jmin (480, song.getNumSamples() - pos), 48000.0);
            tracker.popEvents ([&] (int note, bool on, float) { heard.emplace_back (note, on, pos / 48000.0); });
        }
        String humDetail;
        for (auto& [note, on, at] : heard)
            humDetail << (on ? "+" : "-") << MidiMessage::getMidiNoteName (note, true, true, 4) << "@" << String (at, 2) << " ";
        const bool humOk = heard.size() == 4 && std::get<0> (heard[0]) == 57 && std::get<1> (heard[0]) && std::get<2> (heard[0]) < 0.1
                        && std::get<0> (heard[1]) == 57 && ! std::get<1> (heard[1])
                        && std::get<0> (heard[2]) == 64 && std::get<1> (heard[2]) && std::abs (std::get<2> (heard[2]) - 0.6) < 0.1
                        && std::get<0> (heard[3]) == 64 && ! std::get<1> (heard[3]);
        res.check (humOk, "HUM: a sung A3 then E4 becomes notes A3, E4 (quick, steady, and they end)", humDetail);

        // VOICE: a recording is trimmed, evened out, and plays at its own pitch on middle C
        auto procPtr = std::make_unique<BitBuddyProcessor>();   // (big: on the heap, like a DAW does)
        auto& proc = *procPtr;
        const auto recording = sing ({ { 0.0, 0.5 }, { 440.0, 1.0 }, { 0.0, 0.5 } }, 48000.0);
        const bool made = proc.setVoiceRecording (recording, 48000.0);
        const double voiceSeconds = proc.getVoiceSample().getLength() / bb::VoiceSample::rate;
        auto* wave = proc.apvts.getParameter ("wave");
        proc.selectPreset (0);
        wave->setValueNotifyingHost (wave->convertTo0to1 ((float) bb::Wave::Voice));
        proc.apvts.getParameter ("sustain")->setValueNotifyingHost (1.0f);
        auto play = [&] (int note, double holdSec, double seconds)
        {
            proc.notePlayer.play ({ { 0.0, holdSec, note, 0.9f } });
            return runOwn (proc, seconds, sr);
        };
        const auto atC = play (60, 3.0, 3.0), atC5 = play (72, 3.0, 3.0);
        const double ownPitch = pitchAt (atC, 0.3, 4096, sr), octaveUp = pitchAt (atC5, 0.1, 4096, sr);
        const float during = peakOf (atC, 0.2, 0.8, sr), after = peakOf (atC, 1.5, 3.0, sr);
        res.check (made && voiceSeconds > 0.95 && voiceSeconds < 1.2 && std::abs (centsOff (ownPitch, 440.0)) < 15.0
                       && std::abs (centsOff (octaveUp, 880.0)) < 15.0 && during > 0.1f && after < 1.0e-4f,
                   "VOICE: trimmed to the sound, own pitch on middle C, octave up on C5, ends with the recording",
                   String (voiceSeconds, 2) + " s, C4 " + String (ownPitch, 1) + " Hz, C5 " + String (octaveUp, 1)
                       + " Hz, after the end " + String (Decibels::gainToDecibels (after), 1) + " dB (key still held)");

        // Consoles: Game Boy = 4-bit (16 levels), NES = 1-bit delta steps (so it sounds different again)
        auto engineVoice = [&] (bb::Console console)
        {
            bb::ChipEngine e;
            e.prepare (sr);
            bb::EngineParams ep;
            ep.wave = bb::Wave::Voice;
            ep.voice = &proc.getVoiceSample();
            ep.console = console;
            ep.attack = 0.001f;
            ep.sustain = 1.0f;
            e.setParams (ep);
            std::vector<float> out (8192, 0.0f);
            e.noteOn (60, 1.0f);
            e.render (out.data(), (int) out.size());
            return std::vector<float> (out.begin() + 2000, out.end());   // after the attack
        };
        const auto modern = engineVoice (bb::Console::Modern), gameBoy = engineVoice (bb::Console::GameBoy), nes = engineVoice (bb::Console::NES);
        std::set<int> gbLevels, modernLevels;
        for (auto x : gameBoy) gbLevels.insert (roundToInt (x * 10000.0f));
        for (auto x : modern)  modernLevels.insert (roundToInt (x * 10000.0f));
        double nesDiff = 0.0;
        for (size_t i = 0; i < nes.size(); ++i)
            nesDiff += std::abs (nes[i] - modern[i]);
        res.check (gbLevels.size() <= 16 && modernLevels.size() > 40 && nesDiff / (double) nes.size() > 0.01,
                   "VOICE + CHIP: Game Boy plays it in 16 levels, NES as 1-bit delta steps",
                   String ((int) gbLevels.size()) + " GB levels (Modern " + String ((int) modernLevels.size()) + "), NES differs by "
                       + String (nesDiff / (double) nes.size(), 3));

        // The voice is kept with your project; factory presets leave it alone; silence is refused
        MemoryBlock projectState;
        proc.getStateInformation (projectState);
        auto reopenedPtr = std::make_unique<BitBuddyProcessor>();   // (big: on the heap, like a DAW does)
        auto& reopened = *reopenedPtr;
        reopened.setStateInformation (projectState.getData(), (int) projectState.getSize());
        const bool keptInProject = reopened.getVoiceSample().get() == proc.getVoiceSample().get();
        reopened.selectPreset (programIndex (reopened, "Hero Lead"));
        const bool keptByPreset = reopened.getVoiceSample().get() == proc.getVoiceSample().get();
        AudioBuffer<float> silence (1, 48000);
        silence.clear();
        const bool silentRefused = ! reopened.setVoiceRecording (silence, 48000.0) && keptByPreset;
        auto freshPtr = std::make_unique<BitBuddyProcessor>();   // (big: on the heap, like a DAW does)
        auto& fresh = *freshPtr;
        const bool hasDefault = fresh.getVoiceSample().getLength() > 8000;
        res.check (keptInProject && keptByPreset && silentRefused && hasDefault,
                   "VOICE: saved with the project, presets keep it, silence refused, 'aah' out of the box",
                   String (keptInProject ? "project ok" : "PROJECT LOST") + ", " + (keptByPreset ? "preset kept it" : "PRESET REPLACED IT")
                       + ", " + (silentRefused ? "silence refused" : "SILENCE ACCEPTED") + ", default "
                       + String (fresh.getVoiceSample().getLength() / bb::VoiceSample::rate, 2) + " s");
    }

    // ---- 15. 1.2: MIDI songs ------------------------------------------------------
    std::cout << "\n  -- midi songs --\n";
    {
        // A little 4-part song: melody, bass, pads and drums (120 BPM, 480 ticks per beat)
        MidiFile file;
        file.setTicksPerQuarterNote (480);
        MidiMessageSequence tempo;
        tempo.addEvent (MidiMessage::tempoMetaEvent (500000), 0);
        file.addTrack (tempo);
        std::vector<std::array<int, 3>> melody, bass, pads, drums;
        for (int i = 0; i < 16; ++i) melody.push_back ({ i * 240, 200, 72 + (i % 4) * 2 + (i == 5 ? 1 : 0) });   // (one C#)
        for (int i = 0; i < 8; ++i)  bass.push_back ({ i * 480, 400, 36 + (i % 2) * 7 });
        for (int i = 0; i < 4; ++i)  for (int n : { 60, 64, 67 }) pads.push_back ({ i * 960, 900, n });
        for (int i = 0; i < 16; ++i) drums.push_back ({ i * 240, 60, i % 4 == 0 ? 36 : (i % 4 == 2 ? 38 : 42) });
        auto add = [&] (const String& name, int ch, const std::vector<std::array<int, 3>>& v /* tick, length, note */)
        {
            MidiMessageSequence seq;
            seq.addEvent (MidiMessage::textMetaEvent (3, name), 0);
            for (auto& n : v)
            {
                seq.addEvent (MidiMessage::noteOn (ch, n[2], 0.8f), n[0]);
                seq.addEvent (MidiMessage::noteOff (ch, n[2]), n[0] + n[1]);
            }
            seq.updateMatchedPairs();
            file.addTrack (seq);
        };
        add ("Pads", 3, pads);      // (in a jumbled order: the arranger has to work out who is who)
        add ("Drums", 10, drums);
        add ("Melody", 1, melody);
        add ("Bass", 2, bass);

        const auto song = bb::arrangeMidiSong (file, "Test Song");
        int kitOk = 0;
        for (auto& n : song.notes)
            if (n.channel == bb::SongBand::drumsChannel)
                kitOk += (n.note == 36 || n.note == 38 || n.note == 42) ? 1 : 0;
        res.check (song.lead == 16 && song.bass == 8 && song.chords == 12 && song.drums == 16 && kitOk == 16
                       && std::abs (song.seconds - 4.0) < 0.1 && song.parts.contains ("Melody") && song.parts.contains ("Bass"),
                   "MIDI SONG: melody -> your sound, lowest part -> triangle bass, rest -> chords, ch. 10 -> drum kit",
                   "lead " + String (song.lead) + ", bass " + String (song.bass) + ", chords " + String (song.chords) + ", drums "
                       + String (song.drums) + " (" + String (kitOk) + " mapped), " + String (song.seconds, 2) + " s | " + song.parts);

        // It plays: band + melody together, then everything stops
        auto procPtr = std::make_unique<BitBuddyProcessor>();   // (big: on the heap, like a DAW does)
        auto& proc = *procPtr;
        proc.selectPreset (0);
        auto run = [&] (double seconds) { return runOwn (proc, seconds, sr); };
        proc.notePlayer.play (song.notes);
        const auto out = run (5.0);
        const float playingPeak = peakOf (out, 0.5, 3.5, sr), endPeak = peakOf (out, 4.8, 5.0, sr);
        res.check (allFinite (out) && playingPeak > 0.1f && playingPeak < 1.0f && endPeak < 1.0e-4f && ! proc.notePlayer.isPlaying(),
                   "MIDI SONG: plays (band + melody), stays below 0 dBFS, then stops",
                   "peak " + String (playingPeak, 2) + ", after the end " + String (Decibels::gainToDecibels (endPeak), 1) + " dB");

        // Songs have their own key: the melody skips Scale Lock (your own keys still snap)
        proc.apvts.getParameter ("scale")->setValueNotifyingHost (proc.apvts.getParameter ("scale")->convertTo0to1 (1.0f));   // C major
        proc.notePlayer.play ({ { 0.0, 1.0, 61, 0.9f, bb::SongBand::leadChannel } });
        const double songCSharp = pitchAt (run (1.0), 0.3, 8192, sr);
        proc.notePlayer.play ({ { 0.0, 1.0, 61, 0.9f, 1 } });
        const double keyCSharp = pitchAt (run (1.0), 0.3, 8192, sr);
        proc.apvts.getParameter ("scale")->setValueNotifyingHost (0.0f);
        res.check (std::abs (centsOff (songCSharp, midiToHz (61))) < 10.0 && std::abs (centsOff (keyCSharp, midiToHz (60))) < 10.0,
                   "MIDI SONG: the melody ignores Scale Lock (a played C# still snaps to C)",
                   "song C# " + String (songCSharp, 1) + " Hz, played C# " + String (keyCSharp, 1) + " Hz");
    }

    // ---- 16. 1.2: slide-to-bend + tilt wobble (what the keyboard / phone send) -------
    std::cout << "\n  -- slide to bend, tilt to wobble --\n";
    {
        auto procPtr = std::make_unique<BitBuddyProcessor>();   // (big: on the heap, like a DAW does)
        auto& proc = *procPtr;
        proc.selectPreset (0);
        auto playA = [&]
        {
            proc.notePlayer.play ({ { 0.0, 1.5, 69, 0.8f } });
            return runOwn (proc, 1.5, sr);
        };
        proc.setTouchBend (2.0f);
        const double bent = pitchAt (playA(), 0.3, 8192, sr);
        proc.setTouchBend (0.0f);
        proc.setTiltWobble (1.0f);
        const auto wobbly = playA();
        proc.setTiltWobble (0.0f);
        double lo = 1.0e9, hi = 0.0;
        for (double t = 0.3; t < 1.2; t += 0.02)
        {
            const double hz = pitchAt (wobbly, t, 1024, sr);
            if (hz > 0.0) { lo = jmin (lo, hz); hi = jmax (hi, hz); }
        }
        const double straight = pitchAt (playA(), 0.3, 8192, sr);
        res.check (std::abs (centsOff (bent, midiToHz (71))) < 10.0 && centsOff (hi, lo) > 30.0
                       && std::abs (centsOff (straight, 440.0)) < 10.0,
                   "SLIDE bends 2 semitones; TILT adds wobble; both let go cleanly",
                   "bent " + String (bent, 1) + " Hz, wobble " + String (lo, 1) + ".." + String (hi, 1) + " Hz, after " + String (straight, 1) + " Hz");
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
    AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Standalone);   // the app: JAM + REC visible
    auto procPtr = std::make_unique<BitBuddyProcessor>();   // (big: on the heap, like a DAW does)
    auto& proc = *procPtr;
    proc.prepareToPlay (44100.0, 512);

    // key: 0 = C ... 11 = B;  scale: 0 = Scale Lock off, 4 = minor pentatonic
    struct Shot { const char* preset; std::vector<int> notes; const char* file; bool help; int key = 0, scale = 0;
                  const char* click = nullptr;   // a button to press before the picture
                  int theme = 0; };
    const Shot shots[] = {
        { "Hero Lead",       { 69 },         "ui-hero-lead.png",   false },
        { "Major Chord Arp", { 60 },         "ui-chord-arp.png",   false },
        { "Smart Chords",    { 62 },         "ui-scale-lock.png",  false, 9, 4 },   // A minor pentatonic
        { "C64 Squelch Bass", { 40 },        "ui-filter.png",      false },
        { "Game Boy Wave",   { 69 },         "ui-draw-wave.png",   false },
        { "Macro Stutter Lead", { 69 },      "ui-macros.png",      false, 0, 0, "MACROS" },
        { "Real NES Lead",   { 69 },         "ui-theme-gameboy.png", false, 0, 0, nullptr, 1 },
        { "C64 Squelch Bass", { 40 },        "ui-theme-amber.png", false, 0, 0, nullptr, 2 },
        { "Real NES Lead",   { 69 },         "ui-theme-nes.png",   false, 0, 0, nullptr, 3 },
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

        bb::applyTheme (shot.theme);   // (doesn't touch your saved theme)
        std::unique_ptr<AudioProcessorEditor> editor (proc.createEditor());
        editor->setVisible (true);
        if (shot.help)
            if (auto* b = findButton (*editor, "? HELP"))
                b->triggerClick();
        if (shot.click != nullptr)
            if (auto* b = findButton (*editor, shot.click))
                b->triggerClick();

        MessageManager::getInstance()->runDispatchLoopUntil (400);   // let timers update the UI

        writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), outDir.getChildFile (shot.file));

        MidiBuffer off;
        for (int n : shot.notes)
            off.addEvent (MidiMessage::noteOff (1, n), 0);
        buf.clear();
        proc.processBlock (buf, off);
    }

    // A custom layout: two boxes hidden, PLAY ALONG moved to the top
    {
        bb::applyTheme (0);
        selectProgram (proc, "Hero Lead");
        std::unique_ptr<AudioProcessorEditor> editor (proc.createEditor());
        editor->setVisible (true);
        if (auto* panel = findComponent<bb::MainPanel> (*editor))
        {
            panel->setBoxVisible ("movement", false);
            panel->setBoxVisible ("crunch", false);
            panel->moveBox ("play", 0);
            MessageManager::getInstance()->runDispatchLoopUntil (400);
            writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), outDir.getChildFile ("ui-custom-layout.png"));
            panel->resetLayout();
        }
    }

    // A phone held sideways: top of the page, and scrolled to the bottom
    {
        selectProgram (proc, "Smart Chords");
        std::unique_ptr<AudioProcessorEditor> editor (proc.createEditor());
        editor->setVisible (true);
        editor->setSize (914, 411);
        MessageManager::getInstance()->runDispatchLoopUntil (400);
        writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), outDir.getChildFile ("ui-phone.png"));
        if (auto* viewport = findComponent<Viewport> (*editor))
        {
            viewport->setViewPosition (0, viewport->getViewedComponent()->getHeight());
            MessageManager::getInstance()->runDispatchLoopUntil (200);
            writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), outDir.getChildFile ("ui-phone-scrolled.png"));
        }
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

    AudioProcessor::setTypeOfNextNewPlugin (AudioProcessor::wrapperType_Standalone);   // the app: JAM + REC visible
    auto procPtr = std::make_unique<BitBuddyProcessor>();   // (big: on the heap, like a DAW does)
    auto& proc = *procPtr;
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

    // 1b. Boxes: hide, move, reset - and the window reshapes to fit
    if (auto* panel = findComponent<bb::MainPanel> (*editor))
    {
        auto settle = [] { MessageManager::getInstance()->runDispatchLoopUntil (250); };
        auto boxVisible = [&] (const String& id)
        {
            auto* b = findComponent<bb::Box> (*panel, [&] (bb::Box& box) { return box.getId() == id; });
            return b != nullptr && b->isVisible();
        };
        const int fullHeight = panel->getDesignHeight();

        // The "x" on the first box hides it (a real button press)
        if (auto* hideFirst = findButton (*editor, "HIDE"))
            hideFirst->triggerClick();
        settle();
        const bool hiddenByButton = ! boxVisible ("sound") && ! panel->isBoxVisible ("sound");
        panel->setBoxVisible ("sound", true);

        panel->setBoxVisible ("filter", false);
        panel->setBoxVisible ("play", false);
        settle();
        const int shortHeight = panel->getDesignHeight();
        // (the test window's own title bar skews its outer size, so check the shape the editor asks for)
        const double windowRatio = editor->getConstrainer() != nullptr ? editor->getConstrainer()->getFixedAspectRatio() : 0.0;
        const bool savedHidden = bb::loadSetting ("boxHidden", {}).contains ("filter");

        panel->moveBox ("crunch", 0);
        settle();
        auto* crunch = findComponent<bb::Box> (*panel, [] (bb::Box& box) { return box.getId() == "crunch"; });
        const bool movedFirst = panel->getBoxOrder()[0] == "crunch" && crunch != nullptr && crunch->getPosition() == Point<int> (10, 64);

        // VIEW > Show dice buttons: hides every dice, remembered for next time
        auto visibleDice = [&]
        {
            int n = 0;
            forEachComponent (*panel, [&] (Component& c) { n += (dynamic_cast<bb::DiceButton*> (&c) != nullptr && c.isVisible()) ? 1 : 0; });
            return n;
        };
        const int diceBefore = visibleDice();
        panel->setDiceShown (false);
        const bool diceHidden = visibleDice() == 0 && bb::loadSetting ("showDice", "1") == "0";
        panel->setDiceShown (true);
        const bool diceBack = visibleDice() == diceBefore && diceBefore >= 6;
        res.check (diceHidden && diceBack, "VIEW > Show dice buttons: hides all dice, saved, comes back",
                   String (diceBefore) + " dice -> " + (diceHidden ? "0 (saved)" : "STILL SHOWN") + " -> " + String (visibleDice()));

        panel->resetLayout();
        settle();
        const bool reset = panel->getDesignHeight() == fullHeight && boxVisible ("filter") && boxVisible ("play")
                        && panel->getBoxOrder()[0] == "sound";

        res.check (fullHeight == bb::MainPanel::fullHeight && hiddenByButton && shortHeight == fullHeight - 118   // the filter + play row
                       && std::abs (windowRatio - 1000.0 / shortHeight) < 0.01
                       && savedHidden && movedFirst && reset,
                   "Boxes: hide (x button + VIEW), move, reset; window reshapes; layout saved",
                   "height " + String (fullHeight) + " -> " + String (shortHeight) + ", window ratio " + String (windowRatio, 3)
                       + " (" + String (editor->getWidth()) + "x" + String (editor->getHeight()) + ")"
                       + (hiddenByButton ? ", x ok" : ", X FAILED") + (savedHidden ? ", saved" : ", NOT SAVED")
                       + (movedFirst ? ", moved" : ", MOVE FAILED") + (reset ? ", reset" : ", RESET FAILED"));
    }
    else
    {
        res.check (false, "Boxes: hide, move, reset", "MainPanel not found");
    }

    // 1c. Phone held sideways (914 x 411): keyboard pinned to the bottom, the rest scrolls
    if (SystemStats::getEnvironmentVariable ("BB_SKIP_PHONE_TEST", {}).isEmpty())
    {
        auto settle = [] { MessageManager::getInstance()->runDispatchLoopUntil (250); };
        const auto desktopSize = editor->getBounds();
        if (auto* constrainer = editor->getConstrainer())   // a phone screen has no window-shape lock
            constrainer->setFixedAspectRatio (0.0);
        editor->setResizeLimits (100, 100, 4000, 4000);
        editor->setSize (914, 411);
        settle();
        auto* viewport = findComponent<Viewport> (*editor);
        auto* strip = findComponent<bb::BottomStrip> (*editor);
        const bool scrolling = viewport != nullptr && viewport->isVisible()
                            && viewport->getViewedComponent()->getHeight() > viewport->getHeight();
        const auto stripArea = strip != nullptr ? editor->getLocalArea (strip, strip->getLocalBounds()) : Rectangle<int>();
        const bool pinned = strip != nullptr && strip->getParentComponent() == editor.get()
                         && std::abs (stripArea.getBottom() - 411) <= 1 && stripArea.getWidth() >= 912;
        auto* knob = findComponent<Slider> (*editor);
        const bool knobsTurn = knob != nullptr && knob->getParentComponent() != nullptr
                            && knob->getParentComponent()->getViewportIgnoreDragFlag();

        editor->setBounds (desktopSize);
        settle();
        MessageManager::getInstance()->runDispatchLoopUntil (1000);
        const bool backToNormal = (viewport == nullptr || ! viewport->isVisible()) && strip != nullptr
                               && dynamic_cast<bb::MainPanel*> (strip->getParentComponent()) != nullptr;
        res.check (scrolling && pinned && knobsTurn && backToNormal,
                   "Phone screen: keyboard pinned, boxes scroll, knobs still turn; desktop size restores",
                   String (scrolling ? "scrolls" : "NO SCROLL") + ", strip " + stripArea.toString()
                       + (knobsTurn ? ", knobs ok" : ", KNOBS SCROLL") + (backToNormal ? ", restored" : ", NOT RESTORED"));
    }

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
        { "COIN button",           findButton (*editor, "COIN") },
        { "LEVEL CLEAR button",    findButton (*editor, "LEVEL CLEAR") },
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

        if (String (what).contains ("COIN"))
            res.check (proc.getDisplayPresetName().startsWith ("Coin #"), "Simulated click on COIN made a new coin sound",
                       proc.getDisplayPresetName());

        if (String (what).contains ("SAW"))   // prove the simulated clicks really land
            res.check (roundToInt (proc.apvts.getRawParameterValue ("wave")->load()) == 4,
                       "Simulated click selected the SAW wave",
                       "wave " + String (proc.apvts.getRawParameterValue ("wave")->load()) + ", button at "
                           + editor->getLocalArea (target, target->getLocalBounds()).toString() + " in editor "
                           + editor->getBounds().toString() + ", component there: "
                           + [&]
                             {
                                 auto* hit = editor->getComponentAt (editor->getLocalPoint (target, target->getLocalBounds().getCentre()));
                                 return hit != nullptr ? String (typeid (*hit).name()) + " '" + hit->getName() + "'" : String ("none");
                             }());
    }

    // 2b. Slide up on a key: that note keeps playing and bends up; letting go bends it back
    {
        auto& kb = *piano;
        const auto start = Point<float> (kb.getWidth() * 0.5f, kb.getHeight() * 0.9f);
        const auto from = editor->getLocalPoint (&kb, start);
        const auto to = editor->getLocalPoint (&kb, start.translated (0.0f, -kb.getHeight() * 0.45f));
        auto t = Time::currentTimeMillis();
        auto mouse = [&] (Point<float> p, bool down)
        {
            peer->handleMouseEvent (MouseInputSource::InputSourceType::mouse, p,
                                    down ? ModifierKeys (ModifierKeys::leftButtonModifier) : ModifierKeys(),
                                    MouseInputSource::defaultPressure, MouseInputSource::defaultOrientation, t += 20);
        };
        auto notesOn = [&]
        {
            Array<int> on;
            for (int n = 0; n < 128; ++n)
                if (proc.keyboardState.isNoteOn (1, n))
                    on.add (n);
            return on;
        };
        mouse (from, true);
        const auto firstNotes = notesOn();
        for (int i = 1; i <= 6; ++i)
            mouse (from + (to - from) * ((float) i / 6.0f), true);
        MessageManager::getInstance()->runDispatchLoopUntil (100);
        const float bendHeld = proc.getTouchBend();
        const auto heldNotes = notesOn();
        mouse (to, false);
        MessageManager::getInstance()->runDispatchLoopUntil (100);
        res.check (firstNotes.size() == 1 && heldNotes == firstNotes && bendHeld > 1.0f && proc.getTouchBend() == 0.0f
                       && notesOn().isEmpty(),
                   "Slide up on a key: same note keeps playing, bends up, back in tune on release",
                   "note " + (firstNotes.isEmpty() ? String ("none") : MidiMessage::getMidiNoteName (firstNotes[0], true, true, 4))
                       + (heldNotes == firstNotes ? " held" : " CHANGED") + ", bend " + String (bendHeld, 2) + " st -> "
                       + String (proc.getTouchBend(), 2));
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

    return writePng (img, file) ? 0 : 1;
}

//==============================================================================
int main (int argc, char* argv[])
{
    ScopedJuceInitialiser_GUI juceInit;

    // Tests change themes and box layouts: keep the real BitBuddy settings untouched
    const auto testSettings = File::getSpecialLocation (File::tempDirectory).getChildFile ("BitBuddyTests.settings");
    testSettings.deleteFile();
    bb::useSettingsFile (testSettings);

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
