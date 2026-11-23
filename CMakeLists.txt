cmake_minimum_required(VERSION 3.22)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # iPhone / iPad (configure with -G Xcode -DCMAKE_SYSTEM_NAME=iOS)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Oldest supported iOS")
else()
    # macOS: one "universal" build for Apple Silicon + Intel Macs (ignored elsewhere)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Oldest supported macOS")
endif()

project(BitBuddy VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the C++ runtime statically so the plugin works on PCs that don't have
# the Visual C++ redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(external/JUCE)

# Embedded assets (pixel font)
juce_add_binary_data(BitBuddyData SOURCES assets/PressStart2P-Regular.ttf)

set(BITBUDDY_SOURCES
    src/ChipEngine.cpp
    src/JamBand.cpp
    src/Parameters.cpp
    src/Presets.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Recorder.cpp
    src/RetroLookAndFeel.cpp)

# JUCE features BitBuddy never uses. Leaving them out keeps the binaries small.
set(BITBUDDY_JUCE_OPTIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_FLAC=0
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0)

# ---------------------------------------------------------------------------
# The plugin: VST3 (most DAWs) + Standalone app, plus AU on Mac (Logic, GarageBand)
# and AUv3 on iPhone / iPad (GarageBand iOS, AUM, Cubasis). Android: see android/.
# ---------------------------------------------------------------------------
if(IOS)
    set(BITBUDDY_FORMATS AUv3 Standalone)
elseif(APPLE)
    set(BITBUDDY_FORMATS VST3 Standalone AU)
else()
    set(BITBUDDY_FORMATS VST3 Standalone)
endif()

juce_add_plugin(BitBuddy
    COMPANY_NAME "BitBuddy Labs"
    BUNDLE_ID com.bitbuddylabs.bitbuddy
    PLUGIN_MANUFACTURER_CODE Bbdl
    PLUGIN_CODE Bbd1
    FORMATS ${BITBUDDY_FORMATS}
    PRODUCT_NAME "BitBuddy"
    DESCRIPTION "Easy chiptune / 8-bit synth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    ICON_BIG assets/icon.png
    # iPhone / iPad app settings (ignored on desktop)
    REQUIRES_FULL_SCREEN TRUE
    STATUS_BAR_HIDDEN TRUE
    FILE_SHARING_ENABLED TRUE   # recordings show up in the Files app
    IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
    IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight)

target_sources(BitBuddy PRIVATE ${BITBUDDY_SOURCES})
target_compile_definitions(BitBuddy PUBLIC ${BITBUDDY_JUCE_OPTIONS} JUCE_VST3_CAN_REPLACE_VST2=0
    $<$<BOOL:${IOS}>:JUCE_CONTENT_SHARING=1>)   # SHARE button for recordings on iPhone / iPad

target_link_libraries(BitBuddy
    PRIVATE
        BitBuddyData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags        # link-time optimisation: smaller, faster code
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Test host (tests/TestHost.cpp): loads the built VST3 like a DAW does, runs
# the automated tests and benchmark, renders the demo song and UI snapshots.
# (Desktop only.)
# ---------------------------------------------------------------------------
if(IOS)
    return()
endif()

juce_add_console_app(BitBuddyTestHost PRODUCT_NAME "BitBuddyTestHost")

target_sources(BitBuddyTestHost PRIVATE tests/TestHost.cpp ${BITBUDDY_SOURCES})
target_compile_definitions(BitBuddyTestHost PRIVATE ${BITBUDDY_JUCE_OPTIONS}
    JUCE_PLUGINHOST_VST3=1
    $<$<BOOL:${APPLE}>:JUCE_PLUGINHOST_AU=1>
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(BitBuddyTestHost
    PRIVATE
        BitBuddyData
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags)
