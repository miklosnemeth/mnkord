cmake_minimum_required(VERSION 3.22)
project(mnKord VERSION 1.0.0)

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
    VENDOR_NAME "mnKord"
    PLUGIN_MANUFACTURER_CODE MnKr
    PLUGIN_CODE MnKd
    FORMATS VST3 Standalone
    IS_MIDI_EFFECT TRUE
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
)

target_compile_definitions(mnKord
    PUBLIC
        JucePlugin_Name="mnKord"
        JUCE_VST3_CAN_REPLACE_VST2=0
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
