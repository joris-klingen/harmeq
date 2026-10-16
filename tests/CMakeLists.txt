FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(HarmEQTests
    ProcessorTests.cpp
    FilterTests.cpp
    PitchTrackingTests.cpp
    EditorTests.cpp
    DetectorTests.cpp)

target_link_libraries(HarmEQTests
    PRIVATE
        HarmEQ
        juce::juce_audio_utils
        juce::juce_dsp
        Catch2::Catch2WithMain)

include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(HarmEQTests)
