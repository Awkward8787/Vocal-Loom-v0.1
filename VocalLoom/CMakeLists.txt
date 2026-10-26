cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
project(VocalLoom VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(JUCE_DIR "" CACHE PATH "Optional JUCE 8.0.12 checkout")
if(JUCE_DIR)
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850 GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(JUCE)
endif()
juce_add_plugin(VocalLoom
    COMPANY_NAME "Anthony Jones" BUNDLE_ID "com.anthonyjones.vocalloom"
    PLUGIN_MANUFACTURER_CODE AnJo PLUGIN_CODE VcLm
    PRODUCT_NAME "Vocal Loom" DESCRIPTION "Live pitch correction and vocal synthesis"
    FORMATS AU VST3 Standalone
    IS_SYNTH FALSE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    VST3_CATEGORIES Fx "Pitch Shift" Vocoder
    COPY_PLUGIN_AFTER_BUILD FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Vocal Loom uses your microphone for live vocal processing.")
juce_generate_juce_header(VocalLoom)
target_sources(VocalLoom PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(VocalLoom PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(VocalLoom PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
enable_testing()
add_executable(VocalLoomDSPTests Tests/DSPTests.cpp)
target_include_directories(VocalLoomDSPTests PRIVATE Source)
target_compile_options(VocalLoomDSPTests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME DSP COMMAND VocalLoomDSPTests "${CMAKE_CURRENT_LIST_DIR}/../AudioExamples")
add_executable(VocalLoomHostTests Tests/HostTests.cpp)
target_include_directories(VocalLoomHostTests PRIVATE Source "$<TARGET_PROPERTY:VocalLoom,INCLUDE_DIRECTORIES>")
target_compile_definitions(VocalLoomHostTests PRIVATE "$<TARGET_PROPERTY:VocalLoom,COMPILE_DEFINITIONS>")
target_link_libraries(VocalLoomHostTests PRIVATE VocalLoom "-framework CoreFoundation")
add_test(NAME ProcessorAndUI COMMAND VocalLoomHostTests "${CMAKE_CURRENT_LIST_DIR}/../Preview")
add_executable(VocalLoomAUFileSmoke Tests/AUFileSmoke.cpp)
target_link_libraries(VocalLoomAUFileSmoke PRIVATE "-framework AudioToolbox" "-framework CoreFoundation")
add_dependencies(VocalLoomAUFileSmoke VocalLoom_AU)
add_test(NAME AUBundle COMMAND VocalLoomAUFileSmoke "$<TARGET_BUNDLE_DIR:VocalLoom_AU>")
