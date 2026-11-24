#include "MicInput.h"

// Only the BitBuddy app is built with BB_STANDALONE_HOLDER=1: it can reach the
// app's audio device through JUCE's standalone "plugin holder".
#if BB_STANDALONE_HOLDER
 #include <juce_audio_processors/juce_audio_processors.h>
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_gui_extra/juce_gui_extra.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace bb
{
namespace
{
juce::AudioDeviceManager* appDeviceManager()
{
   #if BB_STANDALONE_HOLDER
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        return &holder->deviceManager;
   #endif
    return nullptr;
}
} // namespace

MicInput::~MicInput()
{
    stop();
}

bool MicInput::isAvailable()
{
    return pretendAvailable || appDeviceManager() != nullptr;
}

void MicInput::start (AudioFn fn, DoneFn done)
{
    stop();
    manager = appDeviceManager();
    if (manager == nullptr)
    {
        done (false, "The microphone works in the BitBuddy app (in a DAW, record with the DAW).");
        return;
    }
    onAudio = std::move (fn);

    // Phones ask "Allow BitBuddy to record audio?" the first time (computers answer yes here;
    // a Mac asks when the microphone opens)
    juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio,
                                       [safe = juce::WeakReference<MicInput> (this), done] (bool granted)
    {
        if (safe == nullptr)
            return;
        if (! granted)
            done (false, "BitBuddy isn't allowed to use the microphone - you can allow it in your settings.");
        else
            safe->open (done);
    });
}

void MicInput::open (DoneFn done)
{
    auto setup = manager->getAudioDeviceSetup();
    previousSetup = setup;

    if (setup.inputDeviceName.isEmpty())
        if (auto* type = manager->getCurrentDeviceTypeObject())
        {
            const auto names = type->getDeviceNames (true);
            const int index = type->getDefaultDeviceIndex (true);
            if (juce::isPositiveAndBelow (index, names.size()))
                setup.inputDeviceName = names[index];
        }
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, 2, true);   // the first two inputs (a phone's mic is just one)

    const auto error = manager->setAudioDeviceSetup (setup, true);
    auto* device = manager->getCurrentAudioDevice();
    if (error.isNotEmpty() || device == nullptr || device->getActiveInputChannels().isZero())
    {
        manager->setAudioDeviceSetup (*previousSetup, true);
        previousSetup.reset();
        done (false, "BitBuddy couldn't find a microphone. Is one plugged in and switched on?"
                     + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));
        return;
    }

    mono.assign (16384, 0.0f);
    deviceRate = device->getCurrentSampleRate();
    manager->addAudioCallback (this);
    running = true;
    done (true, {});
}

void MicInput::stop()
{
    if (manager == nullptr)
        return;
    if (running)
        manager->removeAudioCallback (this);   // (waits for the device thread to let go)
    running = false;
    level = 0.0f;
    if (previousSetup.has_value())
        manager->setAudioDeviceSetup (*previousSetup, true);   // microphone off again
    previousSetup.reset();
}

void MicInput::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device != nullptr)
        deviceRate = device->getCurrentSampleRate();
}

void MicInput::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs,
                                                 int numOutputs, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputs; ++ch)   // the app's own sound is added by the device manager
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    int used = 0;
    for (int ch = 0; ch < numInputs; ++ch)
        used += inputs[ch] != nullptr ? 1 : 0;
    if (used == 0 || onAudio == nullptr)
        return;

    float peak = 0.0f;
    for (int start = 0; start < numSamples; start += (int) mono.size())
    {
        const int n = juce::jmin ((int) mono.size(), numSamples - start);
        juce::FloatVectorOperations::clear (mono.data(), n);
        for (int ch = 0; ch < numInputs; ++ch)
            if (inputs[ch] != nullptr)
                juce::FloatVectorOperations::addWithMultiply (mono.data(), inputs[ch] + start, 1.0f / (float) used, n);
        for (int i = 0; i < n; ++i)
            peak = juce::jmax (peak, std::abs (mono[(size_t) i]));
        onAudio (mono.data(), n, deviceRate.load());
    }
    level = peak;
}
} // namespace bb
