cmake_minimum_required(VERSION 3.24)
project(SimpleWindowsRecorder VERSION 0.8.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(vendor/JUCE)

juce_add_gui_app(SimpleRecorder
    PRODUCT_NAME "Take One"
    ICON_BIG "assets/take-one.svg"
    COMPANY_NAME "Simple Windows Recorder"
    BUNDLE_ID "org.simplewindowsrecorder.prototype")

target_sources(SimpleRecorder PRIVATE
    src/App.cpp src/Diagnostics.cpp src/StudioComponent.cpp src/SessionEngine.cpp
    src/ProjectStore.cpp src/AudioPreferences.cpp src/CalibrationPanel.cpp
    src/TrackEffects.cpp src/TrackReverb.cpp src/InputAnalysis.cpp tests/FeedbackTests.cpp tests/EffectTests.cpp tests/ReverbTests.cpp
    tests/EngineTests.cpp tests/ProjectTests.cpp)

target_compile_definitions(SimpleRecorder PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_CATCH_UNHANDLED_EXCEPTIONS=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SimpleRecorder,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SimpleRecorder,JUCE_VERSION>"
    JUCE_ASIO=1)

if (MSVC)
    target_compile_options(SimpleRecorder PRIVATE /utf-8)
endif()

target_link_libraries(SimpleRecorder PRIVATE
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

juce_generate_juce_header(SimpleRecorder)

enable_testing()
add_test(NAME recorder-regressions COMMAND SimpleRecorder --self-test)
