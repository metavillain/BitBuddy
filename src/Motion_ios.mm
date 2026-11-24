// The iPhone / iPad side of bb::Motion: CoreMotion's accelerometer, read when asked.
#include "Motion.h"

#import <CoreMotion/CoreMotion.h>

namespace bb
{
struct Motion::Impl
{
    CMMotionManager* manager = nil;
};

Motion::Motion() : impl (std::make_unique<Impl>()) {}

Motion::~Motion()
{
    stop();
   #if ! __has_feature (objc_arc)
    [impl->manager release];
   #endif
    impl->manager = nil;
}

bool Motion::isAvailable()
{
    static const bool available = []
    {
        CMMotionManager* m = [[CMMotionManager alloc] init];
        const bool result = m.accelerometerAvailable;
       #if ! __has_feature (objc_arc)
        [m release];
       #endif
        return result;
    }();
    return available;
}

void Motion::start()
{
    if (impl->manager == nil)
        impl->manager = [[CMMotionManager alloc] init];
    if (impl->manager.accelerometerAvailable && ! impl->manager.accelerometerActive)
    {
        impl->manager.accelerometerUpdateInterval = 1.0 / 50.0;
        [impl->manager startAccelerometerUpdates];
    }
}

void Motion::stop()
{
    if (impl->manager != nil && impl->manager.accelerometerActive)
        [impl->manager stopAccelerometerUpdates];
}

bool Motion::read (float& x, float& y, float& z) const
{
    CMAccelerometerData* data = impl->manager != nil ? impl->manager.accelerometerData : nil;
    if (data == nil)
        return false;
    x = (float) data.acceleration.x;   // already in g
    y = (float) data.acceleration.y;
    z = (float) data.acceleration.z;
    return true;
}
} // namespace bb
