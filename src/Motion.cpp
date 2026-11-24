// The Android (and "no sensor here") side of bb::Motion. iOS: Motion_ios.mm.
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1   // (must come before the first JUCE header)
#include <juce_core/juce_core.h>

#include "Motion.h"

#include <atomic>
#include <cstring>

namespace bb
{
#if JUCE_ANDROID

namespace
{
struct Reading
{
    std::atomic<float> x { 0.0f }, y { 0.0f }, z { 0.0f };
    std::atomic<bool> valid { false };
};

bool clearJavaException (JNIEnv* env)
{
    if (! env->ExceptionCheck())
        return false;
    env->ExceptionClear();
    return true;
}

// Android calls this object's onSensorChanged() with every new reading. JUCE turns it into
// a real Java SensorEventListener; Java may keep it alive a little longer than Motion, so
// it shares the reading instead of pointing at Motion.
class SensorListener : public juce::AndroidInterfaceImplementer
{
public:
    explicit SensorListener (std::shared_ptr<Reading> r) : reading (std::move (r)) {}

    jobject invoke (jobject proxy, jobject method, jobjectArray args) override
    {
        auto* env = juce::getEnv();
        auto methodClass = env->GetObjectClass (method);
        auto nameObject = (jstring) env->CallObjectMethod (method, env->GetMethodID (methodClass, "getName", "()Ljava/lang/String;"));
        const char* chars = nameObject != nullptr ? env->GetStringUTFChars (nameObject, nullptr) : nullptr;
        const bool changed = chars != nullptr && std::strcmp (chars, "onSensorChanged") == 0;
        const bool accuracy = chars != nullptr && std::strcmp (chars, "onAccuracyChanged") == 0;
        if (chars != nullptr)
            env->ReleaseStringUTFChars (nameObject, chars);
        env->DeleteLocalRef (nameObject);
        env->DeleteLocalRef (methodClass);

        if (changed && args != nullptr && env->GetArrayLength (args) > 0)
        {
            auto event = env->GetObjectArrayElement (args, 0);
            auto eventClass = env->GetObjectClass (event);
            auto values = (jfloatArray) env->GetObjectField (event, env->GetFieldID (eventClass, "values", "[F"));
            float v[3] {};
            if (values != nullptr && env->GetArrayLength (values) >= 3)
            {
                env->GetFloatArrayRegion (values, 0, 3, v);
                constexpr float g = 9.80665f;   // m/s^2 -> g
                reading->x = v[0] / g;
                reading->y = v[1] / g;
                reading->z = v[2] / g;
                reading->valid = true;
            }
            env->DeleteLocalRef (values);
            env->DeleteLocalRef (eventClass);
            env->DeleteLocalRef (event);
            return nullptr;
        }
        if (accuracy)
            return nullptr;
        return AndroidInterfaceImplementer::invoke (proxy, method, args);   // equals, hashCode, toString
    }

private:
    std::shared_ptr<Reading> reading;
};
} // namespace

struct Motion::Impl
{
    std::shared_ptr<Reading> reading = std::make_shared<Reading>();
    juce::GlobalRef manager, listener;

    static juce::LocalRef<jobject> sensorManager (JNIEnv* env)
    {
        auto context = juce::getAppContext();
        if (context.get() == nullptr)
            return {};
        auto contextClass = juce::LocalRef<jclass> (env->FindClass ("android/content/Context"));
        auto name = juce::LocalRef<jstring> (env->NewStringUTF ("sensor"));
        auto m = env->CallObjectMethod (context.get(), env->GetMethodID (contextClass.get(), "getSystemService",
                                                                           "(Ljava/lang/String;)Ljava/lang/Object;"), name.get());
        if (clearJavaException (env))
            return {};
        return juce::LocalRef<jobject> (m);
    }

    static juce::LocalRef<jobject> accelerometer (JNIEnv* env, jobject manager)
    {
        auto managerClass = juce::LocalRef<jclass> (env->FindClass ("android/hardware/SensorManager"));
        auto s = env->CallObjectMethod (manager, env->GetMethodID (managerClass.get(), "getDefaultSensor",
                                                                     "(I)Landroid/hardware/Sensor;"), 1 /* TYPE_ACCELEROMETER */);
        if (clearJavaException (env))
            return {};
        return juce::LocalRef<jobject> (s);
    }

    void start()
    {
        if (listener.get() != nullptr)
            return;
        auto* env = juce::getEnv();
        auto m = sensorManager (env);
        if (m.get() == nullptr)
            return;
        auto sensor = accelerometer (env, m.get());
        if (sensor.get() == nullptr)
            return;

        auto l = juce::CreateJavaInterface (new SensorListener (reading), "android/hardware/SensorEventListener");
        auto managerClass = juce::LocalRef<jclass> (env->FindClass ("android/hardware/SensorManager"));
        env->CallBooleanMethod (m.get(), env->GetMethodID (managerClass.get(), "registerListener",
                                                            "(Landroid/hardware/SensorEventListener;Landroid/hardware/Sensor;I)Z"),
                                l.get(), sensor.get(), 1 /* SENSOR_DELAY_GAME: ~50 readings a second */);
        if (clearJavaException (env))
            return;
        manager = juce::GlobalRef (m);
        listener = juce::GlobalRef (l);
    }

    void stop()
    {
        if (listener.get() == nullptr)
            return;
        auto* env = juce::getEnv();
        auto managerClass = juce::LocalRef<jclass> (env->FindClass ("android/hardware/SensorManager"));
        env->CallVoidMethod (manager.get(), env->GetMethodID (managerClass.get(), "unregisterListener",
                                                               "(Landroid/hardware/SensorEventListener;)V"), listener.get());
        clearJavaException (env);
        listener.clear();
        manager.clear();
        reading->valid = false;
    }
};

Motion::Motion() : impl (std::make_unique<Impl>()) {}
Motion::~Motion() { impl->stop(); }

bool Motion::isAvailable()
{
    auto* env = juce::getEnv();
    auto m = Impl::sensorManager (env);
    return m.get() != nullptr && Impl::accelerometer (env, m.get()).get() != nullptr;
}

void Motion::start() { impl->start(); }
void Motion::stop()  { impl->stop(); }

bool Motion::read (float& x, float& y, float& z) const
{
    const auto& r = *impl->reading;
    if (! r.valid.load())
        return false;
    x = r.x.load();
    y = r.y.load();
    z = r.z.load();
    return true;
}

#elif ! JUCE_IOS

// Computers: no motion sensor
struct Motion::Impl {};
Motion::Motion() = default;
Motion::~Motion() = default;
bool Motion::isAvailable() { return false; }
void Motion::start() {}
void Motion::stop() {}
bool Motion::read (float&, float&, float&) const { return false; }

#endif
} // namespace bb
