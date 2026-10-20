cmake_minimum_required(VERSION 3.22)

# The one place the version is set (Semantic Versioning, see README "Versioning"). JUCE takes
# the plugin's version from here; cmake/Version.cmake adds the dev suffix for display.
project(mnKord VERSION 1.4.1)
include(cmake/Version.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.1
)
FetchContent_MakeAvailable(juce)

juce_add_plugin(mnKord
    COMPANY_NAME "MNAudio"
    PLUGIN_MANUFACTURER_CODE MnKr
    PLUGIN_CODE MnKd
    FORMATS VST3 Standalone
    IS_MIDI_EFFECT TRUE
    # VST3 has no MIDI-effect category and JUCE defaults non-synths to "Fx", which hosts
    # (e.g. Bitwig) load as an audio effect. As an instrument it sits on an instrument track,
    # receives MIDI, and its MIDI output feeds the next device in the chain.
    VST3_CATEGORIES Instrument
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    PRODUCT_NAME "mnKord"
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(mnKord)

juce_add_binary_data(mnKordFonts
    SOURCES
        Resources/Fonts/Montserrat.ttf
        Resources/Fonts/RobotoCondensed.ttf
)

target_sources(mnKord PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/NoteName.cpp
    Source/ChordSymbol.cpp
)

target_include_directories(mnKord PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_compile_definitions(mnKord
    PUBLIC
        JucePlugin_Name="mnKord"
        JUCE_VST3_CAN_REPLACE_VST2=0
        # Not used by the plugin; on Linux they would also need WebKitGTK and libcurl to build.
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

# Source files are UTF-8 without a BOM. Without this, MSVC interprets narrow string literals
# using the active Windows codepage instead of UTF-8, corrupting any non-ASCII character
# embedded in a string literal (e.g. "…" or "·") into mojibake at runtime.
if(MSVC)
    target_compile_options(mnKord PRIVATE /utf-8)
endif()

target_link_libraries(mnKord
    PRIVATE
        mnKordFonts
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_gui_extra
)

# Console tests for the preset library, project state, chord symbols and velocity layers:
#   cmake -S . -B build-tests -DMNKORD_BUILD_TESTS=ON
#   cmake --build build-tests --config Release --target mnKordTests
#   build-tests/Release/mnKordTests.exe
option(MNKORD_BUILD_TESTS "Build the console tests (mnKordTests)" OFF)
if(MNKORD_BUILD_TESTS)
    add_executable(mnKordTests Tests/LibraryTests.cpp)
    target_include_directories(mnKordTests PRIVATE
        Source
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${CMAKE_CURRENT_BINARY_DIR}/mnKord_artefacts/JuceLibraryCode"
        "${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_mnKordFonts/JuceLibraryCode")
    if(MSVC)
        target_compile_options(mnKordTests PRIVATE /utf-8)
        # The tests keep several processors on the stack; the default 1 MB overflows in Release.
        target_link_options(mnKordTests PRIVATE /STACK:8388608)
    endif()
    target_link_libraries(mnKordTests PRIVATE
        mnKord
        mnKordFonts
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_gui_extra)
endif()
