cmake_minimum_required(VERSION 3.22)

project(BitBuddy VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the C++ runtime statically so the plugin works on PCs that don't have
# the Visual C++ redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(external/JUCE)

# ---------------------------------------------------------------------------
# Embedded assets (pixel font)
# ---------------------------------------------------------------------------
juce_add_binary_data(BitBuddyData
    SOURCES assets/PressStart2P-Regular.ttf)

set(BITBUDDY_SOURCES
    src/ChipEngine.cpp
    src/Parameters.cpp
    src/Presets.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/RetroLookAndFeel.cpp)

# ---------------------------------------------------------------------------
# The plugin: VST3 (for DAWs) + Standalone (runs on its own)
# ---------------------------------------------------------------------------
juce_add_plugin(BitBuddy
    COMPANY_NAME "BitBuddy Labs"
    BUNDLE_ID com.bitbuddylabs.bitbuddy
    PLUGIN_MANUFACTURER_CODE Bbdl
    PLUGIN_CODE Bbd1
    FORMATS VST3 Standalone
    PRODUCT_NAME "BitBuddy"
    DESCRIPTION "Easy chiptune / 8-bit synth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

target_sources(BitBuddy PRIVATE ${BITBUDDY_SOURCES})

target_compile_definitions(BitBuddy PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(BitBuddy
    PRIVATE
        BitBuddyData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Test host: loads the built VST3 like a DAW does, renders audio offline,
# measures it, renders the demo song, and snapshots the UI to PNG.
# ---------------------------------------------------------------------------
juce_add_console_app(BitBuddyTestHost PRODUCT_NAME "BitBuddyTestHost")

target_sources(BitBuddyTestHost PRIVATE tools/TestHost.cpp ${BITBUDDY_SOURCES})

target_compile_definitions(BitBuddyTestHost PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(BitBuddyTestHost
    PRIVATE
        BitBuddyData
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags)
