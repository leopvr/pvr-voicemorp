cmake_minimum_required(VERSION 3.22)
project(PVRVoiceMorph VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: si ya lo tenés descargado, pasá -DJUCE_DIR=C:/ruta/a/JUCE.
# Si no, CMake lo baja solo desde GitHub la primera vez.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} juce_build)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(PVRVoiceMorph
    COMPANY_NAME "PVR"
    PRODUCT_NAME "PVR VoiceMorph"
    PLUGIN_MANUFACTURER_CODE Pvrx
    PLUGIN_CODE Pvm1
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Pitch Shift)

target_sources(PVRVoiceMorph PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/VoiceEngine.cpp
    src/dsp/VoiceProfile.cpp)

target_compile_definitions(PVRVoiceMorph PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1)

if(MSVC)
    # los textos de la interfaz están en UTF-8 (acentos)
    target_compile_options(PVRVoiceMorph PRIVATE /utf-8)
endif()

target_link_libraries(PVRVoiceMorph
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Pruebas del motor DSP (no necesitan JUCE): cmake --build . --target engine_tests
add_executable(engine_tests EXCLUDE_FROM_ALL
    tests/test_engine.cpp src/dsp/VoiceEngine.cpp src/dsp/VoiceProfile.cpp)
if(MSVC)
    target_compile_options(engine_tests PRIVATE /utf-8)
    target_compile_definitions(engine_tests PRIVATE _USE_MATH_DEFINES)
endif()

# Test de integración del plugin (opcional): cmake -DPVR_BUILD_TESTS=ON
option(PVR_BUILD_TESTS "Compilar test de integración" OFF)
if(PVR_BUILD_TESTS)
    juce_add_console_app(plugin_tests PRODUCT_NAME "plugin_tests")
    target_sources(plugin_tests PRIVATE tests/test_plugin.cpp src/PluginProcessor.cpp src/PluginEditor.cpp
                   src/dsp/VoiceEngine.cpp src/dsp/VoiceProfile.cpp)
    target_compile_definitions(plugin_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_FLAC=1 JUCE_USE_OGGVORBIS=1)
    target_link_libraries(plugin_tests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
