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
#include <iostream>

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

static AudioBuffer<float> renderNotes (AudioPluginInstance& plugin, const std::vector<NoteEvent>& notes, double seconds,
                                       double sr = 44100.0, int block = 512, double bpm = 120.0)
{
    TestPlayHead ph;
    ph.bpm = bpm;
    ph.sampleRate = sr;
    plugin.setPlayHead (&ph);
    plugin.setNonRealtime (true);
    plugin.prepareToPlay (sr, block);
    auto result = render (plugin, toEvents (notes, sr), seconds, sr, block, ph);
    plugin.releaseResources();
    plugin.setPlayHead (nullptr);
    return result;
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
    res.check (automatable >= 24 && automatable <= 26, "Automatable parameters in the DAW",
               String (automatable) + " (+" + String (plugin->getParameters().size() - automatable) + " hidden MIDI-CC slots)");
    res.check (plugin->getNumPrograms() == 21, "Factory presets exposed to the DAW", String (plugin->getNumPrograms()));

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
        { "Square",      0.0f / 7.0f, 69 }, { "Pulse 25%", 1.0f / 7.0f, 57 }, { "Pulse 12.5%", 2.0f / 7.0f, 72 },
        { "Triangle",    3.0f / 7.0f, 45 }, { "Saw",       4.0f / 7.0f, 64 }, { "Metal Noise", 6.0f / 7.0f, 69 },
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
        TestPlayHead ph;
        plugin->setPlayHead (&ph);
        plugin->prepareToPlay (sr, 512);
        std::vector<RawEvent> ev { { 0, MidiMessage::pitchWheel (1, 16383) }, { 0, MidiMessage::noteOn (1, 69, 0.8f) },
                                   { (int) sr, MidiMessage::noteOff (1, 69) } };
        auto out = render (*plugin, ev, 1.2, sr, 512, ph);
        plugin->releaseResources();
        plugin->setPlayHead (nullptr);
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
        TestPlayHead ph;
        plugin->setPlayHead (&ph);
        plugin->prepareToPlay (sr, 512);
        std::vector<RawEvent> ev;
        for (int n : { 60, 64, 67, 71 })
            ev.push_back ({ 0, MidiMessage::noteOn (1, n, 0.8f) });
        ev.push_back ({ (int) sr, MidiMessage::allSoundOff (1) });
        auto out = render (*plugin, ev, 2.0, sr, 512, ph);
        plugin->releaseResources();
        plugin->setPlayHead (nullptr);
        const float tail = peakOf (out, 1.1, 2.0, sr);
        res.check (tail < 1.0e-3f, "All-sound-off silences held notes", String (Decibels::gainToDecibels (tail, -120.0f), 1) + " dB after");
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
        setParam (*plugin, "Wave", 3.0f / 7.0f);
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
        const auto t0 = std::chrono::steady_clock::now();
        auto out = renderNotes (*plugin, notes, 60.0, sr);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double factor = 60.0 / secs;
        res.check (factor > 20.0 && allFinite (out), "CPU: 8-voice chords for 60 s",
                   String (factor, 0) + "x faster than real time (" + String (100.0 / factor, 2) + "% of one core)");
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

    struct Track { const char* name; const char* preset; const Part* part; float gain; const char* sfxPreset; };
    const Track tracks[] = {
        { "Lead",   "Hero Lead",        &lead,   1.00f, nullptr },
        { "Chords", "Note Arpeggiator", &chords, 0.50f, nullptr },
        { "Bass",   "Triangle Bass",    &bass,   1.00f, nullptr },
        { "Drums",  "Drum Kit",         &drums,  0.85f, nullptr },
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
static Button* findButton (Component& root, const String& text)
{
    if (auto* b = dynamic_cast<Button*> (&root))
        if (b->getButtonText() == text)
            return b;
    for (auto* child : root.getChildren())
        if (auto* found = findButton (*child, text))
            return found;
    return nullptr;
}

static int runSnapshot (const File& outDir)
{
    outDir.createDirectory();
    BitBuddyProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    struct Shot { const char* preset; std::vector<int> notes; const char* file; bool help; };
    const Shot shots[] = {
        { "Hero Lead",       { 69 },         "ui-hero-lead.png",  false },
        { "Major Chord Arp", { 60 },         "ui-chord-arp.png",  false },
        { "Drum Kit",        { 36, 42 },     "ui-drum-kit.png",   false },
        { "Classic Square",  {},             "ui-help.png",       true  },
    };

    for (const auto& shot : shots)
    {
        selectProgram (proc, shot.preset);

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

    std::cout << "usage:\n  BitBuddyTestHost --test <plugin.vst3>\n"
                 "  BitBuddyTestHost --demo <plugin.vst3> <outDir>\n"
                 "  BitBuddyTestHost --snapshot <outDir>\n";
    return 2;
}
