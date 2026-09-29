cmake_minimum_required(VERSION 3.24)
project(SimpleWindowsRecorder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(vendor/JUCE)

juce_add_gui_app(SimpleRecorder
    PRODUCT_NAME "Simple Recorder"
    COMPANY_NAME "Simple Windows Recorder"
    BUNDLE_ID "org.simplewindowsrecorder.prototype")

target_sources(SimpleRecorder PRIVATE src/Main.cpp)

target_compile_definitions(SimpleRecorder PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:SimpleRecorder,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:SimpleRecorder,JUCE_VERSION>"
    JUCE_ASIO=1)

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
