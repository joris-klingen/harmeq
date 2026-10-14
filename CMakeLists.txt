cmake_minimum_required(VERSION 3.22)

project(HarmEQ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

option(HARMEQ_BUILD_TESTS "Build unit tests" ON)
option(HARMEQ_COPY_PLUGIN "Install the built plugins into the system plugin folders" OFF)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(HarmEQ
    COMPANY_NAME "Joris Klingen"
    BUNDLE_ID "com.jorisklingen.harmeq"
    PLUGIN_MANUFACTURER_CODE Jokl
    PLUGIN_CODE Hreq
    PRODUCT_NAME "HarmEQ"
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${HARMEQ_COPY_PLUGIN}
    VST3_CATEGORIES Fx EQ)

target_sources(HarmEQ
    PRIVATE
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/Parameters.cpp
        source/dsp/EqBand.cpp
        source/dsp/EqEngine.cpp
        source/dsp/PitchTracker.cpp
        source/gui/BandStrip.cpp
        source/gui/Controls.cpp
        source/gui/HeaderBars.cpp
        source/gui/LookAndFeel.cpp
        source/gui/ResponseDisplay.cpp
        source/gui/SpectrumAnalyzer.cpp)

target_include_directories(HarmEQ PUBLIC source)

target_compile_definitions(HarmEQ
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(HarmEQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(HARMEQ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)

    # Renders the editor to a PNG (for reviewing the GUI without a host).
    add_executable(HarmEQScreenshot tools/Screenshot.cpp)
    target_link_libraries(HarmEQScreenshot PRIVATE HarmEQ juce::juce_audio_utils juce::juce_dsp)
endif()
