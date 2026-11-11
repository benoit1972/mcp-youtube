cmake_minimum_required(VERSION 3.22)
project(PoomseMurmur VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

option(PM_BUILD_DSP_TESTS "Build the JUCE-free DSP tests" ON)
if(PM_BUILD_DSP_TESTS)
  enable_testing()
  add_executable(PoomseMurmurDspTest tests/dsp_test.cpp)
  add_test(NAME dsp COMMAND PoomseMurmurDspTest)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
  set(PM_FORMATS AU Standalone)
else()
  set(PM_FORMATS Standalone)
endif()

# An INSTRUMENT (aumu): hold one note and a flock of voices of ONE sample gathers around it and sings.
juce_add_plugin(PoomseMurmur
  COMPANY_NAME "Benoit"
  PLUGIN_MANUFACTURER_CODE Benp
  PLUGIN_CODE Mmr1
  FORMATS ${PM_FORMATS}
  PRODUCT_NAME "Poomse Murmur"
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD ${APPLE})

target_sources(PoomseMurmur PRIVATE
  Source/PluginProcessor.cpp Source/PluginProcessor.h
  Source/PluginEditor.cpp Source/PluginEditor.h
  Source/Look.h
  Source/Dsp/Rng.h Source/Dsp/Sounds.h Source/Dsp/Flock.h Source/Dsp/Murmur.h Source/Dsp/Reverb.h
  Source/Dsp/PitchDetect.h Source/Dsp/Aiff.h)

target_compile_definitions(PoomseMurmur PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(PoomseMurmur PRIVATE
  juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
