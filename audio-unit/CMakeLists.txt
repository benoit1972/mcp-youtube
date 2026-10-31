cmake_minimum_required(VERSION 3.22)
project(GlitchGrain VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

option(GG_BUILD_DSP_TESTS "Build the JUCE-free DSP tests" ON)
if(GG_BUILD_DSP_TESTS)
  enable_testing()
  add_executable(GlitchGrainDspTest tests/dsp_test.cpp)
  add_test(NAME dsp COMMAND GlitchGrainDspTest)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
  set(GG_FORMATS AU Standalone)
else()
  set(GG_FORMATS Standalone) # AU only exists on macOS; Standalone lets other OSes compile-check the code
endif()

juce_add_plugin(GlitchGrain
  COMPANY_NAME "Benoit"
  PLUGIN_MANUFACTURER_CODE Benp
  PLUGIN_CODE Ggr1
  FORMATS ${GG_FORMATS}
  PRODUCT_NAME "Glitch Grain"
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD ${APPLE})

target_sources(GlitchGrain PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginProcessor.h
  Source/PluginEditor.cpp
  Source/PluginEditor.h
  Source/Dsp/Rng.h
  Source/Dsp/GranularEngine.h
  Source/Dsp/GlitchFx.h)

target_compile_definitions(GlitchGrain PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(GlitchGrain PRIVATE
  juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
