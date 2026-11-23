#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace mmui
{
const juce::Colour bg      { 0xff121a13 };
const juce::Colour panel   { 0xff1a261b };
const juce::Colour edge    { 0xff2b3d2c };
const juce::Colour lichen  { 0xffb7c96b };
const juce::Colour moss    { 0xff4f6b3a };
const juce::Colour text    { 0xffd6dec6 };
const juce::Colour dim     { 0xff8a9880 };

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
};
}

class MemoryMachineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MemoryMachineEditor (MemoryMachineProcessor&);
    ~MemoryMachineEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    void timerCallback() override;
    Knob& makeKnob (const juce::String& paramId, const juce::String& text);
    void layoutKnobs (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs);
    void updateSyncState();

    MemoryMachineProcessor& proc;
    mmui::LookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    Knob *fragment, *stutter, *memory, *jumpP, *jumpD, *blur, *bits, *down, *forget,
         *driftD, *driftR, *mix, *out;

    juce::ToggleButton sync { "Sync" };
    juce::ComboBox note, presets;
    juce::Label noteLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> noteAtt;

    juce::Rectangle<int> recallArea, decayArea, driftArea, outArea, ledArea;
    float led = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MemoryMachineEditor)
};
