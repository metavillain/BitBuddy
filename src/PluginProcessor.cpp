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
    pFilterType   = get (bb::ids::filterType);
    pFilterCutoff = get (bb::ids::filterCutoff);
    pFilterReso   = get (bb::ids::filterReso);
    pFilterEnv    = get (bb::ids::filterEnv);
    pConsole      = get (bb::ids::console);
    pSweepMode    = get (bb::ids::sweepMode);
    pVibShape     = get (bb::ids::vibShape);
    pStereo       = get (bb::ids::stereo);

    // Any knob that moves marks the engine settings as out of date
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.addParameterListener (rp->getParameterID(), this);

    setCustomWave (bb::defaultCustomWave());
    clearMacros();
}

BitBuddyProcessor::~BitBuddyProcessor()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (rp->getParameterID(), this);
}

//==============================================================================
int BitBuddyProcessor::getCustomWaveStep (int index) const noexcept
{
    return customWave[(size_t) (index & (bb::kCustomWaveSteps - 1))].load (std::memory_order_relaxed);
}

void BitBuddyProcessor::setCustomWaveStep (int index, int value) noexcept
{
    if (juce::isPositiveAndBelow (index, bb::kCustomWaveSteps))
    {
        customWave[(size_t) index].store ((std::uint8_t) juce::jlimit (0, 15, value), std::memory_order_relaxed);
        paramsDirty = true;
    }
}

void BitBuddyProcessor::setCustomWave (const bb::CustomWave& w) noexcept
{
    for (int i = 0; i < bb::kCustomWaveSteps; ++i)
        setCustomWaveStep (i, w[(size_t) i]);
}

bb::CustomWave BitBuddyProcessor::getCustomWave() const noexcept
{
    bb::CustomWave w {};
    for (int i = 0; i < bb::kCustomWaveSteps; ++i)
        w[(size_t) i] = (std::uint8_t) getCustomWaveStep (i);
    return w;
}

void BitBuddyProcessor::customWaveEdited()
{
    markRemixed();
    updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
}

juce::String BitBuddyProcessor::customWaveToString (const bb::CustomWave& w)
{
    juce::String s;
    for (auto v : w)
        s << juce::String::toHexString ((int) (v & 15)).toUpperCase();
    return s;
}

//==============================================================================
bb::Macro BitBuddyProcessor::getMacro (int lane) const noexcept
{
    bb::Macro m;
    if (! juce::isPositiveAndBelow (lane, bb::kNumMacroLanes))
        return m;
    const auto& s = macros[(size_t) lane];
    m.length = s.length.load (std::memory_order_relaxed);
    m.loop = s.loop.load (std::memory_order_relaxed);
    for (int i = 0; i < bb::kMacroSteps; ++i)
        m.values[(size_t) i] = s.values[(size_t) i].load (std::memory_order_relaxed);
    return m;
}

void BitBuddyProcessor::setMacro (int lane, const bb::Macro& m) noexcept
{
    if (! juce::isPositiveAndBelow (lane, bb::kNumMacroLanes))
        return;
    auto& s = macros[(size_t) lane];
    for (int i = 0; i < bb::kMacroSteps; ++i)
        setMacroStep (lane, i, m.values[(size_t) i]);
    const int length = juce::jlimit (0, bb::kMacroSteps, m.length);
    s.loop.store (m.loop >= 0 && m.loop < length ? m.loop : -1, std::memory_order_relaxed);
    s.length.store (length, std::memory_order_relaxed);
    paramsDirty = true;
}

void BitBuddyProcessor::setMacroStep (int lane, int step, int value) noexcept
{
    if (juce::isPositiveAndBelow (lane, bb::kNumMacroLanes) && juce::isPositiveAndBelow (step, bb::kMacroSteps))
    {
        const auto& info = bb::kMacroLanes[lane];
        macros[(size_t) lane].values[(size_t) step].store ((std::int8_t) juce::jlimit (info.minValue, info.maxValue, value),
                                                           std::memory_order_relaxed);
        paramsDirty = true;
    }
}

void BitBuddyProcessor::clearMacros() noexcept
{
    for (int lane = 0; lane < bb::kNumMacroLanes; ++lane)
    {
        bb::Macro m;
        m.values.fill ((std::int8_t) bb::kMacroLanes[lane].defaultValue);
        setMacro (lane, m);
    }
}

bool BitBuddyProcessor::anyMacroOn() const noexcept
{
    for (auto& s : macros)
        if (s.length.load (std::memory_order_relaxed) > 0)
            return true;
    return false;
}

void BitBuddyProcessor::macrosEdited()
{
    markRemixed();
    updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
}

juce::String BitBuddyProcessor::macroToString (const bb::Macro& m)
{
    juce::StringArray values;
    for (auto v : m.values)
        values.add (juce::String ((int) v));
    return juce::String (m.length) + "|" + juce::String (m.loop) + "|" + values.joinIntoString (",");
}

bool BitBuddyProcessor::macroFromString (const juce::String& s, bb::Macro& m)
{
    const auto parts = juce::StringArray::fromTokens (s, "|", {});
    if (parts.size() != 3)
        return false;
    const auto values = juce::StringArray::fromTokens (parts[2], ",", {});
    if (values.isEmpty() || values.size() > bb::kMacroSteps)
        return false;

    m = {};
    m.length = juce::jlimit (0, bb::kMacroSteps, parts[0].getIntValue());
    m.loop = parts[1].getIntValue();
    for (int i = 0; i < values.size(); ++i)
        m.values[(size_t) i] = (std::int8_t) juce::jlimit (-128, 127, values[i].getIntValue());
    for (int i = values.size(); i < bb::kMacroSteps; ++i)
        m.values[(size_t) i] = m.values[(size_t) values.size() - 1];   // short lists: repeat the last value
    return true;
}

// The parts of a sound that aren't knobs - your drawn wave and the macros -
// saved the same way in projects, your presets and the factory presets.
void BitBuddyProcessor::writeExtras (const std::function<void (const juce::String& key, const juce::String& value)>& set) const
{
    set ("customWave", customWaveToString (getCustomWave()));
    for (int lane = 0; lane < bb::kNumMacroLanes; ++lane)
        set ("macro" + juce::String (lane), macroToString (getMacro (lane)));
}

void BitBuddyProcessor::readExtras (const std::function<juce::String (const juce::String& key)>& get, bool keepWaveIfMissing)
{
    bb::CustomWave w {};
    if (customWaveFromString (get ("customWave"), w))
        setCustomWave (w);
    else if (! keepWaveIfMissing)
        setCustomWave (bb::defaultCustomWave());

    clearMacros();   // lanes that aren't mentioned are off
    for (int lane = 0; lane < bb::kNumMacroLanes; ++lane)
    {
        bb::Macro m;
        if (macroFromString (get ("macro" + juce::String (lane)), m))
            setMacro (lane, m);
    }
}

bool BitBuddyProcessor::customWaveFromString (const juce::String& s, bb::CustomWave& w)
{
    if (s.length() != bb::kCustomWaveSteps || ! s.containsOnly ("0123456789abcdefABCDEF"))
        return false;
    for (int i = 0; i < bb::kCustomWaveSteps; ++i)
        w[(size_t) i] = (std::uint8_t) juce::CharacterFunctions::getHexDigitValue (s[i]);
    return true;
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
    for (auto& f : fx)
        f.prepare (sampleRate);
    echo.prepare (sampleRate);
    jam.prepare (sampleRate);
    effectsIdle = true;
    echoWasOn = filterWasOn = wideWasOn = false;
    swingPhase = 0.0;
    quietSamples = 0;

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pVolume->load()));

    keyboardState.reset();
    lastTempo = hostTempo();
    paramsDirty = false;
    updateEngineParams (lastTempo);
}

// Tempo from the DAW (falls back to 120 BPM in the standalone app)
double BitBuddyProcessor::hostTempo() const
{
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                return juce::jlimit (20.0, 999.0, *b);
    return 120.0;
}

void BitBuddyProcessor::updateEngineParams (double bpm)
{
    const double secondsPerBeat = 60.0 / bpm;
    auto idx = idxOf;

    bb::EngineParams ep;
    ep.wave       = (bb::Wave) idx (pWave, bb::kNumWaves - 1);
    for (int i = 0; i < bb::kCustomWaveSteps; ++i)
        ep.customWave[(size_t) i] = customWave[(size_t) i].load (std::memory_order_relaxed);
    for (int lane = 0; lane < bb::kNumMacroLanes; ++lane)
        ep.macros[(size_t) lane] = getMacro (lane);
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
    ep.console   = (bb::Console) idx (pConsole, 3);
    ep.sweepMode = (bb::SweepMode) idx (pSweepMode, 2);
    ep.vibShape  = (bb::VibShape) idx (pVibShape, 2);
    stereoMode   = idx (pStereo, 2);
    ep.wide      = stereoMode == 1;
    engine.setParams (ep);
    swingStep = 1.0 / (4.0 * secondsPerBeat * (getSampleRate() > 0.0 ? getSampleRate() : 44100.0));   // one bar: side to side and back
    for (auto& f : fx)
    {
        f.tone.setConsole ((int) ep.console);
        f.crusher.setParams ((int) std::lround (pBits->load()), pRate->load());
        f.filter.setParams (idx (pFilterType, 3), pFilterCutoff->load(), pFilterReso->load(), pFilterEnv->load(),
                            pDecay->load());   // the filter envelope closes over the same DECAY time as the volume
    }

    static constexpr double echoBeats[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
    echo.setTimeSeconds (juce::jmin (2.4, echoBeats[idx (pEchoTime, 5)] * secondsPerBeat));
    echoMix      = pEchoMix->load();
    echoFeedback = juce::jlimit (0.0f, 0.95f, pEchoFeedback->load());

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (pVolume->load()));
}

int BitBuddyProcessor::idxOf (std::atomic<float>* p, int maxIndex) noexcept
{
    return juce::jlimit (0, maxIndex, (int) std::lround (p->load()));
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

    // Re-read the settings only when a knob, drawing, macro or the DAW tempo changed
    const double tempo = hostTempo();
    if (paramsDirty.exchange (false) || tempo != lastTempo)
    {
        lastTempo = tempo;
        updateEngineParams (tempo);
    }

    // 1. Synth voices -> channel 0 (STEREO Wide: channels 0 + 1), split at each MIDI
    //    event for sample accuracy
    float* mono = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    const bool wide = stereoMode == 1 && right != nullptr;
    bool engineActive = false;
    int pos = 0;
    auto renderUntil = [&] (int end)
    {
        if (end <= pos)
            return;
        engineActive = (wide ? engine.render (mono + pos, right + pos, end - pos)
                             : engine.render (mono + pos, end - pos)) || engineActive;
        pos = end;
    };
    numFilterTriggers = 0;
    for (const auto meta : midi)
    {
        renderUntil (juce::jlimit (0, numSamples, meta.samplePosition));
        const int eventPos = pos;
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && numFilterTriggers < (int) filterTriggers.size())
            filterTriggers[(size_t) numFilterTriggers++] = eventPos;   // the filter envelope opens right here
        recorder.addMidi (msg, eventPos);
        handleMidi (msg);
    }
    renderUntil (numSamples);

    midi.clear();

    // The JAM band follows your Key + Scale (it's mixed in after your effects, below)
    jam.setSong (idxOf (pScaleKey, 11), (bb::Scale) idxOf (pScale, bb::kNumScales - 1));
    jam.setStyle (jamStyle.load());
    jam.setTempo (jamTempo.load());
    if (jamOn.load() != jam.isPlaying())
        jamOn.load() ? jam.start() : jam.stop();
    const bool useJam = jam.isSounding();

    // 2. Nothing playing and every effect tail has died away? The cleared buffer
    //    is already the right answer, so an idle BitBuddy costs (almost) nothing.
    if (! engineActive && effectsIdle && ! useJam)
    {
        outputGain.skip (numSamples);
        scope.pushSilence (numSamples);
        writeRecording (buffer, numSamples);   // keep the recording's timeline running
        return;
    }
    const bool wasIdle = effectsIdle;
    effectsIdle = false;
    scope.markActive();

    // Echo set to 0% is skipped entirely (and starts clean when turned back up)
    const bool useEcho = echoMix > 0.0f;
    if (useEcho && ! echoWasOn)
        echo.clear();
    echoWasOn = useEcho;

    const bool useFilter = fx[0].filter.isOn();
    if (useFilter && ! filterWasOn)
        for (auto& f : fx)
            f.filter.reset();
    filterWasOn = useFilter;

    if (wide && ! wideWasOn)
        fx[1] = fx[0];   // the right side starts where the left one is (no jump)
    wideWasOn = wide;
    const bool swing = stereoMode == 2;
    if (swing)
    {
        // Locked to the bars while the DAW plays; otherwise it starts in the middle when you start playing
        bool synced = false;
        if (auto* ph = getPlayHead())
            if (auto info = ph->getPosition())
                if (auto ppq = info->getPpqPosition(); ppq.hasValue() && info->getIsPlaying())
                {
                    swingPhase = std::fmod (*ppq / 4.0, 1.0);
                    synced = true;
                }
        if (! synced && wasIdle)
            swingPhase = 0.0;
    }

    // 3. Effects: DC blocker -> C64 filter -> console -> crunch -> echo -> stereo -> volume
    float peak = 0.0f;
    int nextTrigger = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        while (useFilter && nextTrigger < numFilterTriggers && filterTriggers[(size_t) nextTrigger] <= i)
        {
            fx[0].filter.trigger();
            fx[1].filter.trigger();
            ++nextTrigger;
        }
        const float xl = fx[0].process (mono[i], useFilter);
        const float xr = wide ? fx[1].process (right[i], useFilter) : xl;

        const float g = outputGain.getNextValue();
        scope.push (0.5f * (xl + xr) * g);

        float l = xl, r = xr;
        if (useEcho)
            echo.process (xl, xr, echoMix, echoFeedback, l, r);

        if (swing)
        {
            // Equal loudness at every position: sides at 80%, through the middle twice a bar
            swingPhase += swingStep;
            if (swingPhase >= 1.0)
                swingPhase -= 1.0;
            const float pan = 0.8f * (float) std::sin (juce::MathConstants<double>::twoPi * swingPhase);
            const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            l *= juce::MathConstants<float>::sqrt2 * std::cos (angle);
            r *= juce::MathConstants<float>::sqrt2 * std::sin (angle);
        }

        l *= g;
        r *= g;
        if (useJam)
        {
            const float band = jam.nextSample();
            l += band;
            r += band;
        }

        l = softLimit (l);   // gentle safety limiter: big chords round off instead of crackling
        r = softLimit (r);
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
        for (auto& f : fx)
            f.reset();
        echo.clear();
    }

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    writeRecording (buffer, numSamples);
}

void BitBuddyProcessor::writeRecording (const juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    if (! recorder.isRecording())
        return;
    const float* left = buffer.getReadPointer (0);
    const float* channels[2] = { left, buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : left };
    recorder.writeAudio (channels, numSamples);
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
    userPresetLoaded = false;
    bb::applyPreset (apvts, index);

    // Presets that use DRAW bring their own drawing (others leave yours alone);
    // macros are part of a sound, so a preset without them switches them off
    const auto& preset = bb::factoryPresets()[(size_t) index];
    readExtras ([&preset] (const juce::String& key) -> juce::String
    {
        const char* text = nullptr;
        if (key == "customWave")
            text = preset.customWave;
        else if (const int lane = key.getTrailingIntValue(); juce::isPositiveAndBelow (lane, bb::kNumMacroLanes))
            text = preset.macros[(size_t) lane];
        return text != nullptr ? juce::String (text) : juce::String();
    }, true);
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
    if (userPresetLoaded)
        return "One of your own presets.";
    if (customName.endsWithChar ('*'))
        return "Remixed! Press SAVE to keep it as one of your presets.";
    if (customName.isNotEmpty())
        return "A random sound - tweak it, roll again, or SAVE it!";
    return bb::factoryPresets()[(size_t) currentProgram].tip;
}

void BitBuddyProcessor::setParamValue (const char* id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

// Random but musical settings for one part of BitBuddy
void BitBuddyProcessor::rollSection (Section section, juce::Random& rng)
{
    auto set = [this] (const char* id, float value) { setParamValue (id, value); };
    auto pick = [&rng] (std::initializer_list<float> options)
    {
        const auto i = rng.nextInt ((int) options.size());
        return *(options.begin() + i);
    };
    auto range = [&rng] (float lo, float hi) { return lo + rng.nextFloat() * (hi - lo); };
    auto chance = [&rng] (float probability) { return rng.nextFloat() < probability; };

    switch (section)
    {
        case Section::Sound:
            set (bb::ids::wave, chance (0.06f) ? 7.0f : (float) rng.nextInt (7));
            set (bb::ids::console, chance (0.7f) ? 0.0f : (float) (1 + rng.nextInt (3)));
            break;

        case Section::Shape:
            set (bb::ids::attack,  pick ({ 0.001f, 0.003f, 0.01f, 0.05f, 0.2f }));
            set (bb::ids::decay,   range (0.05f, 1.0f));
            set (bb::ids::sustain, chance (0.3f) ? 0.0f : range (0.3f, 1.0f));
            set (bb::ids::release, range (0.02f, 0.6f));
            break;

        case Section::Chords:
        {
            const float arp = rng.nextFloat();
            set (bb::ids::arpMode,    arp < 0.4f ? 0.0f : (arp < 0.8f ? 1.0f : 2.0f));
            set (bb::ids::arpChord,   (float) rng.nextInt (bb::kSmartChordIndex + 1));
            set (bb::ids::arpPattern, (float) rng.nextInt (5));
            set (bb::ids::arpSpeed,   (float) rng.nextInt (8));
            set (bb::ids::arpOctaves, (float) (1 + rng.nextInt (2)));
            break;
        }

        case Section::Movement:
        {
            set (bb::ids::vibDepth, chance (0.5f) ? range (0.1f, 0.6f) : 0.0f);
            set (bb::ids::vibRate,  range (4.0f, 8.0f));
            set (bb::ids::vibDelay, range (0.0f, 0.4f));

            const bool sweep = chance (0.25f);
            set (bb::ids::sweepAmt,  sweep ? (float) ((chance (0.5f) ? 1 : -1) * (1 + rng.nextInt (24))) : 0.0f);
            set (bb::ids::sweepTime, range (0.05f, 0.4f));
            const float sweepMode = rng.nextFloat(), vibShape = rng.nextFloat();
            set (bb::ids::sweepMode, sweepMode < 0.8f ? 0.0f : (sweepMode < 0.9f ? 1.0f : 2.0f));
            set (bb::ids::vibShape,  vibShape < 0.75f ? 0.0f : (vibShape < 0.9f ? 1.0f : 2.0f));

            set (bb::ids::pwm,       chance (0.3f) ? range (0.2f, 0.8f) : 0.0f);
            set (bb::ids::glide,     chance (0.2f) ? range (0.02f, 0.15f) : 0.0f);
            set (bb::ids::voiceMode, chance (0.25f) ? 1.0f : 0.0f);
            break;
        }

        case Section::Crunch:
        {
            set (bb::ids::bits, chance (0.5f) ? 8.0f : (float) (4 + rng.nextInt (13)));
            set (bb::ids::rate, chance (0.7f) ? 48000.0f : range (4000.0f, 22050.0f));
            const float stereo = rng.nextFloat();
            set (bb::ids::stereo, stereo < 0.6f ? 0.0f : (stereo < 0.9f ? 1.0f : 2.0f));

            const bool useEcho = chance (0.5f);
            set (bb::ids::echoMix,      useEcho ? range (0.15f, 0.4f) : 0.0f);
            set (bb::ids::echoTime,     (float) rng.nextInt (6));
            set (bb::ids::echoFeedback, range (0.2f, 0.5f));
            break;
        }

        case Section::Filter:
        {
            const float type = rng.nextFloat();
            set (bb::ids::filterType,   type < 0.3f ? 0.0f : (type < 0.75f ? 1.0f : (type < 0.9f ? 2.0f : 3.0f)));
            set (bb::ids::filterCutoff, 400.0f * std::pow (2.0f, range (0.0f, 4.0f)));   // 400 Hz .. 6.4 kHz
            set (bb::ids::filterReso,   range (0.1f, 0.85f));
            set (bb::ids::filterEnv,    chance (0.5f) ? range (0.2f, 0.8f) : 0.0f);
            break;
        }
    }
}

void BitBuddyProcessor::randomize()
{
    juce::Random rng;
    for (auto s : { Section::Sound, Section::Shape, Section::Chords, Section::Movement, Section::Crunch, Section::Filter })
        rollSection (s, rng);

    clearMacros();   // a fresh sound starts without macros
    customName = "Surprise #" + juce::String (++randomCount);
    userPresetLoaded = false;
}

void BitBuddyProcessor::rollSection (Section section)
{
    juce::Random rng;
    rollSection (section, rng);
    markRemixed();
}

void BitBuddyProcessor::markRemixed()
{
    const auto base = getDisplayPresetName();
    if (! base.endsWithChar ('*'))
        customName = base + " *";
    userPresetLoaded = false;
}

void BitBuddyProcessor::mutate()
{
    // Every knob moves a little (about +/- 12% of its range); now and then a
    // switch flips to a neighbouring setting. The waveform, Key and Scale stay.
    juce::Random rng;
    for (auto* p : getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || bb::isSongSetting (rp->getParameterID()) || rp->getParameterID() == bb::ids::wave)
            continue;

        const float now = rp->getValue();
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (rp))
        {
            if (rng.nextFloat() < 0.15f)
            {
                const int n = choice->choices.size();
                const int index = juce::jlimit (0, n - 1, choice->getIndex() + (rng.nextBool() ? 1 : -1));
                rp->setValueNotifyingHost (rp->convertTo0to1 ((float) index));
            }
            continue;
        }

        const float nudge = (rng.nextFloat() + rng.nextFloat() - 1.0f) * 0.12f;   // gentle, mostly small
        rp->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, now + nudge));
    }
    markRemixed();
}

//==============================================================================
juce::File BitBuddyProcessor::getUserPresetFolder()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support/BitBuddy/Presets");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("BitBuddy/Presets");
   #endif
}

juce::StringArray BitBuddyProcessor::getUserPresetNames()
{
    juce::StringArray names;
    for (const auto& f : getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.bbpreset"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool BitBuddyProcessor::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim()).substring (0, 40).trim();
    if (name.isEmpty())
        return false;

    juce::XmlElement xml ("BitBuddyPreset");
    xml.setAttribute ("name", name);
    writeExtras ([&xml] (const juce::String& key, const juce::String& value) { xml.setAttribute (key, value); });
   #ifdef JucePlugin_VersionString
    xml.setAttribute ("version", JucePlugin_VersionString);
   #endif
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! bb::isSongSetting (rp->getParameterID()))
            {
                auto* e = xml.createNewChildElement ("PARAM");
                e->setAttribute ("id", rp->getParameterID());
                e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
            }

    auto folder = getUserPresetFolder();
    if (! folder.createDirectory())
        return false;
    if (! xml.writeTo (folder.getChildFile (name + ".bbpreset")))
        return false;

    customName = name;
    userPresetLoaded = true;
    return true;
}

bool BitBuddyProcessor::loadUserPreset (const juce::String& name)
{
    const auto xml = juce::XmlDocument::parse (getUserPresetFolder().getChildFile (name + ".bbpreset"));
    if (xml == nullptr || ! xml->hasTagName ("BitBuddyPreset"))
        return false;

    // Anything the file doesn't mention (e.g. made with an older BitBuddy) goes back to its default
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! bb::isSongSetting (rp->getParameterID()))
                rp->setValueNotifyingHost (rp->getDefaultValue());

    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
    {
        const auto id = e->getStringAttribute ("id");
        if (bb::isSongSetting (id))
            continue;
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 ((float) e->getDoubleAttribute ("value")));
    }

    readExtras ([&xml] (const juce::String& key) { return xml->getStringAttribute (key); }, true);

    customName = name;
    userPresetLoaded = true;
    return true;
}

bool BitBuddyProcessor::deleteUserPreset (const juce::String& name)
{
    // To the recycle bin / trash where there is one (phones have none)
    const auto file = getUserPresetFolder().getChildFile (name + ".bbpreset");
    return file.moveToTrash() || file.deleteFile();
}

//==============================================================================
void BitBuddyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("customName", customName, nullptr);
    state.setProperty ("userPreset", userPresetLoaded, nullptr);
    writeExtras ([&state] (const juce::String& key, const juce::String& value) { state.setProperty (key, value, nullptr); });
    state.setProperty ("jamStyle", jamStyle.load(), nullptr);   // (the band always starts stopped)
    state.setProperty ("jamTempo", jamTempo.load(), nullptr);

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
            userPresetLoaded = (bool) tree.getProperty ("userPreset", false);
            readExtras ([&tree] (const juce::String& key) { return tree.getProperty (juce::Identifier (key), "").toString(); }, false);
            setJamStyle ((int) tree.getProperty ("jamStyle", 0));
            setJamTempo ((float) (double) tree.getProperty ("jamTempo", bb::jamStyleTempo (jamStyle)));
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
