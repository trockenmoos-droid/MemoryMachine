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
const juce::Colour rust    { 0xffc98a5b };   // accent for the weird section

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
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
    struct Choice
    {
        juce::ComboBox box;
        juce::Label    label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };

    void timerCallback() override;
    Knob& makeKnob (const juce::String& paramId, const juce::String& text, juce::Colour accent = mmui::lichen);
    void makeChoice (Choice&, const juce::String& paramId, const juce::String& text);
    void layoutKnobs (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs);
    void layoutChoice (Choice&, juce::Rectangle<int> area);
    void updateEnablement();

    // preset bar
    void rebuildPresetList();
    void selectPresetById (int id);
    void stepPreset (int delta);
    void askToSave();
    void showPresetMenu();
    std::vector<int> presetIds() const;

    MemoryMachineProcessor& proc;
    mmui::LookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    Knob *fragment, *stutter, *memory, *jumpP, *jumpD, *blur, *bits, *down, *forget,
         *driftD, *driftR, *mix, *out, *fFreq, *fReso, *fDepth, *fRate, *reverse, *warp, *rumination;

    juce::ToggleButton sync { "Sync" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt;
    Choice note, filterType, sweepShape;

    juce::ComboBox presets;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "Save" }, menuButton { "..." };
    juce::StringArray userNames;
    juce::String shownPreset;

    juce::Rectangle<int> recallArea, decayArea, driftArea, outArea, filterArea, weirdArea, ledArea;
    float led = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MemoryMachineEditor)
};
