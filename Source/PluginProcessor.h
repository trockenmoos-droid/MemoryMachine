#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "MemoryEngine.h"

class MemoryMachineProcessor : public juce::AudioProcessor
{
public:
    MemoryMachineProcessor();

    const juce::String getName() const override { return "Memory Machine"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Host programs = the factory presets. User presets live in files and are
    // handled by the editor's preset bar, so host program lists stay stable.
    int getNumPrograms() override;
    int getCurrentProgram() override { return userPreset ? 0 : currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- user presets (message thread only) --------------------------------
    static juce::File defaultPresetFolder();
    void setPresetFolder (const juce::File& f) { presetFolder = f; }   // tests
    juce::File getPresetFolder() const { return presetFolder; }
    juce::StringArray getUserPresetNames() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);

    /** Name shown in the preset bar, and whether it is a user preset. */
    juce::String getPresetName() const { return presetName; }
    bool isUserPreset() const { return userPreset; }

    /** For the editor's "recalling" indicator. */
    std::atomic<bool> recalling { false };

private:
    mm::Params readParams() const;
    void applyValues (const juce::ValueTree& paramsTree);
    void resetToDefaults();

    mm::Engine engine;
    int currentProgram = 0;
    juce::String presetName { "Default" };
    bool userPreset = false;
    juce::File presetFolder { defaultPresetFolder() };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MemoryMachineProcessor)
};
