#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace bb
{
/** The microphone, for the BitBuddy app's REC VOICE and HUM (added in 1.2).

    BitBuddy itself has no audio input (DAWs see a plain instrument), so in the app
    this borrows the app's own audio device: it switches the device's input on
    while you record or hum, and back off afterwards (so phones don't keep
    showing the "microphone in use" light). In a DAW it is simply not available. */
class MicInput : private juce::AudioIODeviceCallback
{
public:
    /** Called on the audio device's thread with every block of microphone audio (mono). */
    using AudioFn = std::function<void (const float* samples, int numSamples, double sampleRate)>;
    using DoneFn = std::function<void (bool ok, const juce::String& problem)>;

    MicInput() = default;
    ~MicInput() override;

    /** True in the BitBuddy app (not when BitBuddy is a plugin in a DAW). */
    static bool isAvailable();

    /** Asks for permission the first time (phones), switches the microphone on and
        starts calling onAudio. `done` is called on the message thread. */
    void start (AudioFn onAudio, DoneFn done);
    void stop();
    bool isRunning() const noexcept   { return running; }
    double getSampleRate() const noexcept { return deviceRate.load(); }
    float getLevel() const noexcept   { return level.load(); }   // loudness of the last block, 0..1

private:
    void open (DoneFn done);
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs,
                                           int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

    juce::AudioDeviceManager* manager = nullptr;
    std::optional<juce::AudioDeviceManager::AudioDeviceSetup> previousSetup;
    AudioFn onAudio;                  // set before the callback is added, read only by the device thread
    std::vector<float> mono;          // scratch space for one block
    std::atomic<double> deviceRate { 44100.0 };
    std::atomic<float> level { 0.0f };
    bool running = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (MicInput)
    JUCE_DECLARE_NON_COPYABLE (MicInput)
};
} // namespace bb
