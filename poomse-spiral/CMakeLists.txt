cmake_minimum_required(VERSION 3.22)
project(PoomseSpiral VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

option(PS_BUILD_DSP_TESTS "Build the JUCE-free DSP tests" ON)
if(PS_BUILD_DSP_TESTS)
  enable_testing()
  add_executable(PoomseSpiralDspTest tests/dsp_test.cpp)
  add_test(NAME dsp COMMAND PoomseSpiralDspTest)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
  set(PS_FORMATS AU Standalone)
else()
  set(PS_FORMATS Standalone)
endif()

# An EFFECT (aufx): a pitch-shifting delay.
juce_add_plugin(PoomseSpiral
  COMPANY_NAME "Benoit"
  PLUGIN_MANUFACTURER_CODE Benp
  PLUGIN_CODE Spl1
  FORMATS ${PS_FORMATS}
  PRODUCT_NAME "Poomse Spiral"
  AU_MAIN_TYPE kAudioUnitType_Effect
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD ${APPLE})

target_sources(PoomseSpiral PRIVATE
  Source/PluginProcessor.cpp Source/PluginProcessor.h
  Source/PluginEditor.cpp Source/PluginEditor.h
  Source/Look.h
  Source/Dsp/Engine.h)

target_compile_definitions(PoomseSpiral PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PoomseSpiral PRIVATE
  juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
