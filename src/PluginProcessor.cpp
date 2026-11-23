#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "Presets.h"

namespace
{
// Untouched below 0.8, then curves smoothly towards (never past) 0.98 (-0.18 dBFS)
inline float softLimit (float x) noexcept
{
    const float a = std::abs (x);
    if (a <= 0.8f)
        return x;
    return std::copysign (0.8f + 0.18f * std::tanh ((a - 0.8f) / 0.18f), x);
}
} // namespace

BitBuddyProcessor::BitBuddyProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BitBuddyState", bb::createParameterLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    pWave         = get (bb::ids::wave);
    pAttack       = get (bb::ids::attack);
    pDecay        = get (bb::ids::decay);
    pSustain      = get (bb::ids::sustain);
    pRelease      = get (bb::ids::release);
    pArpMode      = get (bb::ids::arpMode);
    pArpChord     = get (bb::ids::arpChord);
    pArpPattern   = get (bb::ids::arpPattern);
    pArpSpeed     = get (bb::ids::arpSpeed);
    pArpOctaves   = get (bb::ids::arpOctaves);
    pSweepAmt     = get (bb::ids::sweepAmt);
    pSweepTime    = get (bb::ids::sweepTime);
    pVibDepth     = get (bb::ids::vibDepth);
    pVibRate      = get (bb::ids::vibRate);
    pVibDelay     = get (bb::ids::vibDelay);
    pPwm          = get (bb::ids::pwm);
    pGlide        = get (bb::ids::glide);
    pVoiceMode    = get (bb::ids::voiceMode);
    pBits         = get (bb::ids::bits);
    pRate         = get (bb::ids::rate);
    pEchoMix      = get (bb::ids::echoMix);
    pEchoTime     = get (bb::ids::echoTime);
    pEchoFeedback = get (bb::ids::echoFeedback);
    pVolume       = get (bb::ids::volume);
    pScaleKey     = get (bb::ids::scaleKey);
    pScale        = get (bb::ids::scale);
}

//==============================================================================
bool BitBuddyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet().isDisabled();
}

void BitBuddyProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    crusher.prepare (sampleRate);
    echo.prepare (sampleRate);
    dcBlocker.reset();
    effectsIdle = true;
    echoWasOn = false;
    quietSamples = 0;

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pVolume->load()));

    keyboardState.reset();
    updateEngineParams();
}

void BitBuddyProcessor::updateEngineParams()
{
    // Tempo from the DAW (falls back to 120 BPM in the standalone app)
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;
    bpm = juce::jlimit (20.0, 999.0, bpm);
    const double secondsPerBeat = 60.0 / bpm;

    auto idx = [] (std::atomic<float>* p, int maxIndex) { return juce::jlimit (0, maxIndex, (int) std::lround (p->load())); };

    bb::EngineParams ep;
    ep.wave       = (bb::Wave) idx (pWave, 7);
    ep.attack     = pAttack->load();
    ep.decay      = pDecay->load();
    ep.sustain    = juce::jlimit (0.0f, 1.0f, pSustain->load());
    ep.release    = pRelease->load();
    ep.arpMode    = (bb::ArpMode) idx (pArpMode, 2);
    const int chord = idx (pArpChord, bb::kSmartChordIndex);
    ep.smartChords = chord == bb::kSmartChordIndex;
    ep.arpChord   = ep.smartChords ? 0 : chord;
    ep.scaleKey   = idx (pScaleKey, 11);
    ep.scale      = (bb::Scale) idx (pScale, bb::kNumScales - 1);
    ep.arpPattern = (bb::ArpPattern) idx (pArpPattern, 4);
    ep.arpOctaves = juce::jlimit (1, 3, (int) std::lround (pArpOctaves->load()));

    static constexpr double arpBeats[] = { 0.5, 0.25, 1.0 / 6.0, 0.125, 0.0625 };
    static constexpr double arpHz[]    = { 15.0, 30.0, 60.0 };
    const int speed = idx (pArpSpeed, 7);
    ep.arpStepSeconds = speed < 5 ? arpBeats[speed] * secondsPerBeat : 1.0 / arpHz[speed - 5];

    ep.sweepAmt  = pSweepAmt->load();
    ep.sweepTime = juce::jmax (0.01f, pSweepTime->load());
    ep.vibDepth  = pVibDepth->load();
    ep.vibRate   = pVibRate->load();
    ep.vibDelay  = pVibDelay->load();
    ep.pwm       = pPwm->load();
    ep.glide     = pGlide->load();
    ep.mono      = idx (pVoiceMode, 1) == 1;
    engine.setParams (ep);

    crusher.setParams ((int) std::lround (pBits->load()), pRate->load());

    static constexpr double echoBeats[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
    echo.setTimeSeconds (juce::jmin (2.4, echoBeats[idx (pEchoTime, 5)] * secondsPerBeat));
    echoMix      = pEchoMix->load();
    echoFeedback = juce::jlimit (0.0f, 0.95f, pEchoFeedback->load());

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (pVolume->load()));
}

void BitBuddyProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber());
    else if (m.isAllSoundOff())
        engine.allNotesOff (true);
    else if (m.isAllNotesOff())
        engine.allNotesOff (false);
    else if (m.isPitchWheel())
        engine.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f);   // +/- 2 semitones
    else if (m.isController())
    {
        if (m.getControllerNumber() == 1)        // mod wheel -> extra vibrato
            engine.setModWheel ((float) m.getControllerValue() / 127.0f);
        else if (m.getControllerNumber() == 64)  // sustain pedal
            engine.setSustainPedal (m.getControllerValue() >= 64);
    }
}

void BitBuddyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    buffer.clear();

    if (numSamples == 0 || numChannels == 0)
        return;

    // Merge notes played on the on-screen keyboard
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);
    updateEngineParams();

    // 1. Synth voices -> channel 0, split at each MIDI event for sample accuracy
    float* mono = buffer.getWritePointer (0);
    bool engineActive = false;
    int pos = 0;
    for (const auto meta : midi)
    {
        const int eventPos = juce::jlimit (0, numSamples, meta.samplePosition);
        if (eventPos > pos)
        {
            engineActive = engine.render (mono + pos, eventPos - pos) || engineActive;
            pos = eventPos;
        }
        handleMidi (meta.getMessage());
    }
    if (pos < numSamples)
        engineActive = engine.render (mono + pos, numSamples - pos) || engineActive;

    midi.clear();

    // 2. Nothing playing and every effect tail has died away? The cleared buffer
    //    is already the right answer, so an idle BitBuddy costs (almost) nothing.
    if (! engineActive && effectsIdle)
    {
        outputGain.skip (numSamples);
        scope.pushSilence (numSamples);
        return;
    }
    effectsIdle = false;
    scope.markActive();

    // Echo set to 0% is skipped entirely (and starts clean when turned back up)
    const bool useEcho = echoMix > 0.0f;
    if (useEcho && ! echoWasOn)
        echo.clear();
    echoWasOn = useEcho;

    // 3. Effects: DC blocker -> crunch -> echo -> volume
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float x = dcBlocker.process (mono[i]);
        x = crusher.process (x);

        const float g = outputGain.getNextValue();
        scope.push (x * g);

        float l = x, r = x;
        if (useEcho)
            echo.process (x, echoMix, echoFeedback, l, r);

        l = softLimit (l * g);   // gentle safety limiter: big chords round off instead of crackling
        r = softLimit (r * g);
        peak = juce::jmax (peak, std::abs (l), std::abs (r));

        if (right != nullptr)
        {
            mono[i] = l;
            right[i] = r;
        }
        else
        {
            mono[i] = 0.5f * (l + r);
        }
    }

    // 4. Go idle once nothing has played for longer than the echo can still answer
    //    (echoes can arrive after a silent gap, so a single quiet block isn't enough)
    if (! engineActive && peak < 1.0e-6f)
        quietSamples += numSamples;
    else
        quietSamples = 0;

    if (quietSamples > (useEcho ? echo.getMaxDelaySamples() : 0))
    {
        effectsIdle = true;
        quietSamples = 0;
        dcBlocker.reset();
        crusher.reset();
        echo.clear();
    }

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);
}

//==============================================================================
int BitBuddyProcessor::getNumPrograms()
{
    return (int) bb::factoryPresets().size();
}

void BitBuddyProcessor::setCurrentProgram (int index)
{
    // Hosts re-send the current program number while restoring a project.
    // Re-applying it here would wipe the user's restored tweaks, so ignore it.
    index = juce::jlimit (0, getNumPrograms() - 1, index);
    if (index == currentProgram)
        return;

    loadProgram (index);
}

void BitBuddyProcessor::loadProgram (int index)
{
    index = juce::jlimit (0, getNumPrograms() - 1, index);
    currentProgram = index;
    customName = {};
    bb::applyPreset (apvts, index);
}

const juce::String BitBuddyProcessor::getProgramName (int index)
{
    const auto& presets = bb::factoryPresets();
    if (index >= 0 && index < (int) presets.size())
        return presets[(size_t) index].name;
    return {};
}

void BitBuddyProcessor::selectPreset (int index)
{
    loadProgram (index);   // from our own UI: always (re)load, even the same preset
    updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true));
}

juce::String BitBuddyProcessor::getDisplayPresetName() const
{
    if (customName.isNotEmpty())
        return customName;
    return bb::factoryPresets()[(size_t) currentProgram].name;
}

juce::String BitBuddyProcessor::getPresetTip() const
{
    if (customName.isNotEmpty())
        return "A random sound - tweak it, or roll again!";
    return bb::factoryPresets()[(size_t) currentProgram].tip;
}

void BitBuddyProcessor::randomize()
{
    juce::Random rng;

    auto set = [this] (const char* id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    auto pick = [&rng] (std::initializer_list<float> options)
    {
        const auto i = rng.nextInt ((int) options.size());
        return *(options.begin() + i);
    };
    auto range = [&rng] (float lo, float hi) { return lo + rng.nextFloat() * (hi - lo); };
    auto chance = [&rng] (float probability) { return rng.nextFloat() < probability; };

    set (bb::ids::wave, chance (0.06f) ? 7.0f : (float) rng.nextInt (7));

    set (bb::ids::attack,  pick ({ 0.001f, 0.003f, 0.01f, 0.05f, 0.2f }));
    set (bb::ids::decay,   range (0.05f, 1.0f));
    set (bb::ids::sustain, chance (0.3f) ? 0.0f : range (0.3f, 1.0f));
    set (bb::ids::release, range (0.02f, 0.6f));

    const float arp = rng.nextFloat();
    set (bb::ids::arpMode,    arp < 0.4f ? 0.0f : (arp < 0.8f ? 1.0f : 2.0f));
    set (bb::ids::arpChord,   (float) rng.nextInt (bb::kSmartChordIndex + 1));
    set (bb::ids::arpPattern, (float) rng.nextInt (5));
    set (bb::ids::arpSpeed,   (float) rng.nextInt (8));
    set (bb::ids::arpOctaves, (float) (1 + rng.nextInt (2)));

    set (bb::ids::vibDepth, chance (0.5f) ? range (0.1f, 0.6f) : 0.0f);
    set (bb::ids::vibRate,  range (4.0f, 8.0f));
    set (bb::ids::vibDelay, range (0.0f, 0.4f));

    const bool sweep = chance (0.25f);
    set (bb::ids::sweepAmt,  sweep ? (float) ((chance (0.5f) ? 1 : -1) * (1 + rng.nextInt (24))) : 0.0f);
    set (bb::ids::sweepTime, range (0.05f, 0.4f));

    set (bb::ids::pwm,       chance (0.3f) ? range (0.2f, 0.8f) : 0.0f);
    set (bb::ids::glide,     chance (0.2f) ? range (0.02f, 0.15f) : 0.0f);
    set (bb::ids::voiceMode, chance (0.25f) ? 1.0f : 0.0f);

    set (bb::ids::bits, chance (0.5f) ? 8.0f : (float) (4 + rng.nextInt (13)));
    set (bb::ids::rate, chance (0.7f) ? 48000.0f : range (4000.0f, 22050.0f));

    const bool useEcho = chance (0.5f);
    set (bb::ids::echoMix,      useEcho ? range (0.15f, 0.4f) : 0.0f);
    set (bb::ids::echoTime,     (float) rng.nextInt (6));
    set (bb::ids::echoFeedback, range (0.2f, 0.5f));

    customName = "Surprise #" + juce::String (++randomCount);
}

//==============================================================================
void BitBuddyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("customName", customName, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BitBuddyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));
            customName = tree.getProperty ("customName", "").toString();
            apvts.replaceState (tree);
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* BitBuddyProcessor::createEditor()
{
    return new BitBuddyEditor (*this);
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BitBuddyProcessor();
}
