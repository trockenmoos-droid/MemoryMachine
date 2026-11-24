cmake_minimum_required(VERSION 3.22)

# Universal binary on macOS. Must be set before project().
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "macOS deployment target")
endif()

project(MemoryMachine VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    # Static runtime so the VST3 runs without a redistributable installed.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

set(MM_FORMATS VST3)
if(APPLE)
    list(APPEND MM_FORMATS AU)
elseif(UNIX AND NOT APPLE)
    list(APPEND MM_FORMATS LV2)
endif()

option(COPY_PLUGIN_AFTER_BUILD "Copy the plugin into the system plugin folder" ON)

juce_add_plugin(MemoryMachine
    COMPANY_NAME             "trockenmoos"
    COMPANY_WEBSITE          "https://www.trockenmoos.com"
    BUNDLE_ID                "com.trockenmoos.memorymachine"
    PLUGIN_MANUFACTURER_CODE Trkn
    PLUGIN_CODE              MMem
    LV2URI                   "https://www.trockenmoos.com/plugins/memorymachine"
    FORMATS                  ${MM_FORMATS}
    PRODUCT_NAME             "Memory Machine"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  ${COPY_PLUGIN_AFTER_BUILD}
    VST3_CATEGORIES          "Fx" "Distortion" "Delay"
    AU_MAIN_TYPE             "kAudioUnitType_Effect"
)

set(MM_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)
target_sources(MemoryMachine PRIVATE ${MM_SOURCES})

target_compile_definitions(MemoryMachine PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(MemoryMachine PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Offline render test: checks block-size independence, transparency, drift,
# stutter and stability of every preset; writes demo WAVs.
# Build with -DMM_BUILD_TESTS=ON, run: MemoryMachineRender <outdir>
option(MM_BUILD_TESTS "Build the offline render test" OFF)

if(MM_BUILD_TESTS)
    juce_add_console_app(MemoryMachineRender PRODUCT_NAME "MemoryMachineRender")
    target_sources(MemoryMachineRender PRIVATE tests/RenderTest.cpp ${MM_SOURCES})
    target_compile_definitions(MemoryMachineRender PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="Memory Machine"
        JucePlugin_IsSynth=0
        JucePlugin_VersionString="${PROJECT_VERSION}"
    )
    target_link_libraries(MemoryMachineRender PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
    )
endif()
