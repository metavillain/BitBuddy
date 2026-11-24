#pragma once

#include <memory>

namespace bb
{
/** The phone's motion sensor (accelerometer), for the phone tricks (added in 1.2):
    SHAKE for a new random sound, TILT for wobble. Phones only: on computers
    isAvailable() is false and nothing happens.

    Android: a Java SensorEventListener (built at run time by JUCE);
    iOS: CoreMotion (Motion_ios.mm). */
class Motion
{
public:
    Motion();
    ~Motion();

    /** True on a phone or tablet that has an accelerometer. */
    static bool isAvailable();

    void start();
    void stop();

    /** The latest reading in g (1 g = gravity), gravity included. False until the first one. */
    bool read (float& x, float& y, float& z) const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};
} // namespace bb
