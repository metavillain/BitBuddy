// SOUND CODES (added in 1.2): a whole sound as a short line of text you can send
// to a friend, e.g. "BB1-AQlIZXJvIExlYWQf...". Paste it into BitBuddy and you
// have exactly the same sound.
//
// What's inside (before it is turned into text):
//   1 byte   format (1)
//   1 + n    the sound's name (UTF-8, up to 24 characters)
//   1 + 2*k  k parameters, each 0..65535 (in the fixed order of kCodeParams, so
//            newer versions can add parameters at the end and still read old codes)
//   1 byte   extras: bit 0 = a DRAW wave follows (16 bytes, 2 steps per byte),
//                    bit 1 = macros follow (count, then lane, length, loop, values)
//   2 bytes  checksum (so a mistyped code is refused instead of making a strange sound)
// Your song's settings (Key, Scale, volume) are never in a code: the sound fits your song.

#include "PluginProcessor.h"
#include "Parameters.h"
#include "Presets.h"

namespace
{
constexpr const char* kPrefix = "BB1-";

// Format 1's parameter order. Never reorder: only add at the end.
const char* const kCodeParams[] = {
    bb::ids::wave, bb::ids::attack, bb::ids::decay, bb::ids::sustain, bb::ids::release,
    bb::ids::arpMode, bb::ids::arpChord, bb::ids::arpPattern, bb::ids::arpSpeed, bb::ids::arpOctaves,
    bb::ids::sweepAmt, bb::ids::sweepTime, bb::ids::vibDepth, bb::ids::vibRate, bb::ids::vibDelay,
    bb::ids::pwm, bb::ids::glide, bb::ids::voiceMode, bb::ids::bits, bb::ids::rate,
    bb::ids::echoMix, bb::ids::echoTime, bb::ids::echoFeedback,
    bb::ids::filterType, bb::ids::filterCutoff, bb::ids::filterReso, bb::ids::filterEnv,
    bb::ids::console, bb::ids::sweepMode, bb::ids::vibShape, bb::ids::stereo,
};
constexpr int kNumCodeParams = (int) (sizeof (kCodeParams) / sizeof (kCodeParams[0]));

std::uint16_t checksum (const juce::MemoryBlock& data, size_t length)
{
    std::uint32_t a = 1, b = 0;   // Fletcher-style: catches swapped and changed characters
    for (size_t i = 0; i < length; ++i)
    {
        a = (a + (std::uint8_t) data[i]) % 251;
        b = (b + a) % 251;
    }
    return (std::uint16_t) ((b << 8) | a);
}

// Base64 with '-' and '_' (safe in messages and links), no '=' padding
juce::String toCodeText (const juce::MemoryBlock& data)
{
    return juce::Base64::toBase64 (data.getData(), data.getSize())
               .replaceCharacters ("+/", "-_").trimCharactersAtEnd ("=");
}

bool fromCodeText (juce::String text, juce::MemoryBlock& data)
{
    text = text.replaceCharacters ("-_", "+/");
    while (text.length() % 4 != 0)
        text << "=";
    juce::MemoryOutputStream out (data, false);
    return juce::Base64::convertFromBase64 (out, text);
}
} // namespace

juce::String BitBuddyProcessor::getSoundCode()
{
    juce::MemoryOutputStream out;
    out.writeByte (1);

    const auto name = getDisplayPresetName().trimCharactersAtEnd (" *").upToFirstOccurrenceOf (" (code)", false, false)
                          .substring (0, 24);
    out.writeByte ((char) name.getNumBytesAsUTF8());
    out.write (name.toRawUTF8(), name.getNumBytesAsUTF8());

    out.writeByte ((char) kNumCodeParams);
    for (auto* id : kCodeParams)
    {
        const auto* p = apvts.getParameter (id);
        out.writeShort ((short) (std::uint16_t) juce::roundToInt ((p != nullptr ? p->getValue() : 0.0f) * 65535.0f));
    }

    const bool drawing = juce::roundToInt (pWave->load()) == (int) bb::Wave::Custom;
    const bool hasMacros = anyMacroOn();
    out.writeByte ((char) ((drawing ? 1 : 0) | (hasMacros ? 2 : 0)));
    if (drawing)
        for (int i = 0; i < bb::kCustomWaveSteps; i += 2)
            out.writeByte ((char) ((getCustomWaveStep (i) << 4) | getCustomWaveStep (i + 1)));
    if (hasMacros)
    {
        int lanesOn = 0;
        for (int lane = 0; lane < bb::kNumMacroLanes; ++lane)
            lanesOn += getMacro (lane).isOn() ? 1 : 0;
        out.writeByte ((char) lanesOn);
        for (int lane = 0; lane < bb::kNumMacroLanes; ++lane)
        {
            const auto m = getMacro (lane);
            if (! m.isOn())
                continue;
            out.writeByte ((char) lane);
            out.writeByte ((char) m.length);
            out.writeByte ((char) m.loop);
            for (int i = 0; i < m.length; ++i)
                out.writeByte ((char) m.values[(size_t) i]);
        }
    }

    juce::MemoryBlock data (out.getData(), out.getDataSize());
    const auto sum = checksum (data, data.getSize());
    const std::uint8_t tail[2] = { (std::uint8_t) (sum >> 8), (std::uint8_t) (sum & 0xff) };
    data.append (tail, 2);
    return kPrefix + toCodeText (data);
}

namespace
{
struct DecodedCode
{
    juce::String name;
    std::vector<float> values;
    bool hasDrawing = false;
    bb::CustomWave drawing {};
    std::array<bb::Macro, bb::kNumMacroLanes> macros {};
};

BitBuddyProcessor::CodeCheck decodeSoundCode (const juce::String& text, DecodedCode& code)
{
    using Check = BitBuddyProcessor::CodeCheck;

    // The code can be anywhere in the text ("try my sound: BB1-..."), even broken over lines
    const int start = text.indexOf (kPrefix);
    if (start < 0)
        return Check::noCode;
    juce::String body;
    for (auto c : text.substring (start + (int) std::strlen (kPrefix)))
    {
        if (juce::CharacterFunctions::isLetterOrDigit (c) || c == '-' || c == '_')
            body << juce::String::charToString (c);
        else if (! juce::CharacterFunctions::isWhitespace (c))
            break;
    }

    juce::MemoryBlock data;
    if (body.length() < 8 || ! fromCodeText (body, data) || data.getSize() < 6)
        return Check::broken;
    const size_t length = data.getSize() - 2;
    const auto expected = (std::uint16_t) (((std::uint8_t) data[length] << 8) | (std::uint8_t) data[length + 1]);
    if (checksum (data, length) != expected)
        return Check::broken;

    juce::MemoryInputStream in (data.getData(), length, false);
    if (in.readByte() != 1)
        return Check::tooNew;   // a code from a newer BitBuddy

    const int nameBytes = (std::uint8_t) in.readByte();
    juce::MemoryBlock nameData;
    in.readIntoMemoryBlock (nameData, nameBytes);
    code.name = juce::String::fromUTF8 ((const char*) nameData.getData(), (int) nameData.getSize());

    const int count = (std::uint8_t) in.readByte();
    for (int i = 0; i < count && ! in.isExhausted(); ++i)
        code.values.push_back ((float) (std::uint16_t) in.readShort() / 65535.0f);
    if ((int) code.values.size() != count)
        return Check::broken;

    const int extras = (std::uint8_t) in.readByte();
    code.hasDrawing = (extras & 1) != 0;
    if (code.hasDrawing)
        for (int i = 0; i < bb::kCustomWaveSteps; i += 2)
        {
            const auto b = (std::uint8_t) in.readByte();
            code.drawing[(size_t) i] = (std::uint8_t) (b >> 4);
            code.drawing[(size_t) i + 1] = (std::uint8_t) (b & 15);
        }
    if ((extras & 2) != 0)
    {
        const int lanes = (std::uint8_t) in.readByte();
        for (int l = 0; l < lanes && ! in.isExhausted(); ++l)
        {
            const int lane = (std::uint8_t) in.readByte();
            bb::Macro m;
            m.length = juce::jlimit (0, bb::kMacroSteps, (int) (std::uint8_t) in.readByte());
            m.loop = (std::int8_t) in.readByte();
            for (int i = 0; i < m.length; ++i)
                m.values[(size_t) i] = (std::int8_t) in.readByte();
            for (int i = juce::jmax (1, m.length); i < bb::kMacroSteps; ++i)   // like macroFromString: the rest repeat the last step
                m.values[(size_t) i] = m.values[(size_t) i - 1];
            if (juce::isPositiveAndBelow (lane, bb::kNumMacroLanes))
                code.macros[(size_t) lane] = m;
        }
    }
    if (code.name.isEmpty())
        code.name = "Shared sound";
    return Check::ok;
}
} // namespace

BitBuddyProcessor::CodeCheck BitBuddyProcessor::checkSoundCode (const juce::String& text, juce::String& soundName)
{
    DecodedCode code;
    const auto result = decodeSoundCode (text, code);
    soundName = code.name;
    return result;
}

bool BitBuddyProcessor::loadSoundCode (const juce::String& text)
{
    DecodedCode code;
    if (decodeSoundCode (text, code) != CodeCheck::ok)
        return false;

    // Everything checks out: reset to defaults (keeping your song's settings), then apply
    bb::applyValues (apvts, {});
    for (int i = 0; i < juce::jmin ((int) code.values.size(), kNumCodeParams); ++i)
        if (auto* p = apvts.getParameter (kCodeParams[i]))
            p->setValueNotifyingHost (code.values[(size_t) i]);
    if (code.hasDrawing)
        setCustomWave (code.drawing);
    for (int lane = 0; lane < bb::kNumMacroLanes; ++lane)
        setMacro (lane, code.macros[(size_t) lane]);
    updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));

    customName = code.name + " (code)";
    userPresetLoaded = false;
    return true;
}
