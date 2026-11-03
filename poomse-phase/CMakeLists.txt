cmake_minimum_required(VERSION 3.22)
project(PoomsePhase VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

option(PS_BUILD_DSP_TESTS "Build the JUCE-free DSP tests" ON)
if(PS_BUILD_DSP_TESTS)
  enable_testing()
  add_executable(PoomsePhaseDspTest tests/dsp_test.cpp)
  add_test(NAME dsp COMMAND PoomsePhaseDspTest)
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

# An INSTRUMENT (aumu): every key you hold makes cycles of different lengths that slip against each other (phasing).
juce_add_plugin(PoomsePhase
  COMPANY_NAME "Benoit"
  PLUGIN_MANUFACTURER_CODE Benp
  PLUGIN_CODE Phs1
  FORMATS ${PS_FORMATS}
  PRODUCT_NAME "Poomse Phase"
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD ${APPLE})

target_sources(PoomsePhase PRIVATE
  Source/PluginProcessor.cpp Source/PluginProcessor.h
  Source/PluginEditor.cpp Source/PluginEditor.h
  Source/Look.h
  Source/Dsp/Rng.h Source/Dsp/Kit.h Source/Dsp/Aiff.h Source/Dsp/Engine.h Source/Dsp/Sounds.h
  Source/Dsp/Fx.h Source/Dsp/Reverb.h Source/Dsp/PitchDetect.h)

target_compile_definitions(PoomsePhase PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PoomsePhase PRIVATE
  juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
