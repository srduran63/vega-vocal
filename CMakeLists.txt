cmake_minimum_required(VERSION 3.22)
project(VegaVocal VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static runtime: the .vst3 works without installing the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Use a local JUCE clone in external/JUCE if there is one, otherwise fetch the same release.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    add_subdirectory(external/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(VegaVocal
    COMPANY_NAME "Vega Audio"
    PRODUCT_NAME "Vega Vocal"
    BUNDLE_ID "com.vegaaudio.vegavocal"
    PLUGIN_MANUFACTURER_CODE Vgad
    PLUGIN_CODE Vgvc
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(VegaVocal PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(VegaVocal PRIVATE Source)

target_compile_definitions(VegaVocal PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(VegaVocal
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

add_executable(vocal_core_tests tests/core_tests.cpp)
target_include_directories(vocal_core_tests PRIVATE Source)

# Drives the complete processor and editor the way a host would.
juce_add_console_app(vocal_host_tests PRODUCT_NAME "vocal_host_tests")
target_sources(vocal_host_tests PRIVATE
    tests/host_tests.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_include_directories(vocal_host_tests PRIVATE Source)
target_compile_definitions(vocal_host_tests PRIVATE
    JucePlugin_Name="Vega Vocal"
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(vocal_host_tests PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# Single-file installer with the Release plugin embedded in it. Build the Release config.
# Windows/MSVC only.
if(MSVC)
    set(VEGA_BUNDLE "${CMAKE_BINARY_DIR}/VegaVocal_artefacts/Release/VST3/Vega Vocal.vst3")
    configure_file(installer/setup.rc.in "${CMAKE_BINARY_DIR}/setup.rc" @ONLY)
    add_executable(VegaVocalSetup WIN32 installer/setup.cpp "${CMAKE_BINARY_DIR}/setup.rc")
    add_dependencies(VegaVocalSetup VegaVocal_VST3)
    set_source_files_properties("${CMAKE_BINARY_DIR}/setup.rc" PROPERTIES
        OBJECT_DEPENDS "${VEGA_BUNDLE}/Contents/x86_64-win/Vega Vocal.vst3")
    target_include_directories(VegaVocalSetup PRIVATE installer)
    target_compile_definitions(VegaVocalSetup PRIVATE UNICODE _UNICODE "VEGA_VERSION_W=L\"${PROJECT_VERSION}\"")
    target_compile_options(VegaVocalSetup PRIVATE /utf-8)
    target_link_libraries(VegaVocalSetup PRIVATE shell32 ole32 user32)
    target_link_options(VegaVocalSetup PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
    set_target_properties(VegaVocalSetup PROPERTIES OUTPUT_NAME "Instalar Vega Vocal")
endif()
