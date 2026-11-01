cmake_minimum_required(VERSION 3.22)
project(PoomsePad VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

option(PP_BUILD_DSP_TESTS "Build the JUCE-free DSP tests" ON)
if(PP_BUILD_DSP_TESTS)
  enable_testing()
  add_executable(PoomsePadDspTest tests/dsp_test.cpp)
  add_test(NAME dsp COMMAND PoomsePadDspTest)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
  set(PP_FORMATS AU Standalone)
else()
  set(PP_FORMATS Standalone) # AU only exists on macOS; Standalone lets other OSes compile-check the code
endif()

# An audio EFFECT (aufx): the sound of the track goes through it.
juce_add_plugin(PoomsePad
  COMPANY_NAME "Benoit"
  PLUGIN_MANUFACTURER_CODE Benp
  PLUGIN_CODE Pad1
  FORMATS ${PP_FORMATS}
  PRODUCT_NAME "Poomse Pad"
  AU_MAIN_TYPE kAudioUnitType_Effect
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD ${APPLE})

target_sources(PoomsePad PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginProcessor.h
  Source/PluginEditor.cpp
  Source/PluginEditor.h
  Source/Look.h
  Source/Dsp/Rng.h
  Source/Dsp/Fft.h
  Source/Dsp/SpectralPad.h
  Source/Dsp/PadReverb.h
  Source/Dsp/ModMatrix.h
  Source/Dsp/Modulators.h
  Source/Dsp/Util.h)

target_compile_definitions(PoomsePad PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PoomsePad PRIVATE
  juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
