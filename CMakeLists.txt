
cmake_minimum_required(VERSION 3.20)
project(MemoryMachine VERSION 0.1.0)

# Prefer a package-managed JUCE (e.g., vcpkg/Homebrew). If not found, you can uncomment
# the add_subdirectory path below and point it at a local JUCE checkout.
find_package(JUCE CONFIG QUIET)

# If not found, try a local JUCE source tree:
if (NOT JUCE_FOUND)
    message(STATUS "JUCE not found via find_package; falling back to add_subdirectory if JUCE_DIR is set")
    # Example:
    # set(JUCE_DIR "${CMAKE_SOURCE_DIR}/JUCE")
    # add_subdirectory(${JUCE_DIR} JUCE)
endif()

if (NOT TARGET juce::juce_audio_processors)
    message(FATAL_ERROR "JUCE not found. Install via vcpkg/Homebrew or point JUCE_DIR to a JUCE source tree.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

juce_add_plugin(MemoryMachine
    COMPANY_NAME              "MemoryWorks"
    BUNDLE_ID                 com.memoryworks.memorymachine
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE
    PLUGIN_MANUFACTURER_CODE  Mwks
    PLUGIN_CODE               MMem
    FORMATS                   VST3 # AU ignored on Windows; add AU if building on macOS locally
    PRODUCT_NAME              "Memory Machine"
)

juce_generate_juce_header(MemoryMachine)

# IMPORTANT: Put MemoryMachine.cpp next to this file (from our chat canvas) before building.
target_sources(MemoryMachine PRIVATE MemoryMachine.cpp)

target_link_libraries(MemoryMachine PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
)
