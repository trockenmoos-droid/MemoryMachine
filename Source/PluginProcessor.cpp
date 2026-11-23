#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
const juce::StringArray kNoteNames { "1/1", "1/2", "1/2.", "1/2T", "1/4", "1/4.", "1/4T",
                                     "1/8", "1/8.", "1/8T", "1/16", "1/16.", "1/16T", "1/32" };

struct Preset
{
    const char* name;
    // blur bits down frag stutMs sync note mem driftC driftHz forget jumpP jumpD mix outDB
    float v[15];
};

// IDs in the same order as Preset::v
const char* kPresetIds[15] = { "blur", "bits", "downsample", "fragment", "stutterMs", "stutSync", "stutNote",
                               "memorySec", "driftDepth", "driftRate", "forget", "jumpProb", "jumpDepth",
                               "mix", "outDB" };

const Preset kPresets[] = {
    { "Default",          { 0.35f, 12, 4, 0.25f, 120, 0, 9, 3.0f,  8, 0.40f, 0.10f, 0.20f, 0.75f, 0.50f, -3 } },
    { "Faint",            { 0.20f, 16, 2, 0.10f, 150, 0, 9, 2.0f,  5, 0.30f, 0.02f, 0.10f, 0.50f, 0.35f, -1 } },
    { "Old Cassette",     { 0.45f, 14, 2, 0.00f, 120, 0, 9, 3.0f, 18, 0.55f, 0.06f, 0.00f, 0.50f, 1.00f, -2 } },
    { "Childhood",        { 0.65f, 10, 5, 0.35f, 250, 0, 9, 6.0f, 25, 0.15f, 0.15f, 0.45f, 0.90f, 0.80f, -3 } },
    { "Deja-vu",          { 0.30f, 12, 3, 0.55f, 120, 1, 7, 6.0f,  6, 0.40f, 0.05f, 0.80f, 1.00f, 0.70f, -3 } },
    { "Stutter Grid",     { 0.15f, 24, 1, 0.70f, 120, 1, 10, 2.0f, 0, 0.40f, 0.00f, 0.15f, 0.40f, 1.00f, -2 } },
    { "Fading Away",      { 0.80f,  8, 8, 0.30f, 300, 0, 9, 8.0f, 30, 0.08f, 0.55f, 0.50f, 1.00f, 1.00f, -4 } },
    { "Broken Recorder",  { 0.40f,  6, 10, 0.60f, 60, 0, 9, 1.5f, 40, 2.50f, 0.35f, 0.30f, 0.60f, 1.00f, -6 } },
};
constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));
}

MemoryMachineProcessor::MemoryMachineProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

APVTS::ParameterLayout MemoryMachineProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> ps;
    // Display: 0..1 controls as percent, everything else with a sensible precision + unit.
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit = "")
    {
        const String u (unit);
        const bool percent = u.isEmpty();
        auto toText = [u, percent] (float v, int)
        {
            if (percent)   return String (roundToInt (v * 100.0f)) + " %";
            if (u == "ms" || u == "bit" || u == "x" || u == "ct") return String (roundToInt (v)) + " " + u;
            if (u == "Hz") return String (v, v < 1.0f ? 2 : 1) + " Hz";
            return String (v, 1) + " " + u;
        };
        auto fromText = [percent] (const String& t)
        {
            const float v = t.retainCharacters ("-0123456789.").getFloatValue();
            return percent ? v / 100.0f : v;
        };
        ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
            AudioParameterFloatAttributes().withLabel (unit)
                                           .withStringFromValueFunction (toText)
                                           .withValueFromStringFunction (fromText)));
    };

    f ("blur",       "Blur",          { 0.0f, 1.0f }, 0.35f);
    f ("bits",       "Bits",          { 4.0f, 24.0f, 1.0f }, 12.0f, "bit");
    f ("downsample", "Downsample",    { 1.0f, 16.0f, 1.0f }, 4.0f, "x");
    f ("fragment",   "Fragment",      { 0.0f, 1.0f }, 0.25f);
    f ("stutterMs",  "Stutter",       { 10.0f, 1000.0f, 0.0f, 0.5f }, 120.0f, "ms");
    ps.push_back (std::make_unique<AudioParameterBool>   (ParameterID { "stutSync", 1 }, "Sync", false));
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "stutNote", 1 }, "Note", kNoteNames, 9));
    f ("memorySec",  "Memory",        { 0.25f, 8.0f, 0.0f, 0.6f }, 3.0f, "s");
    f ("driftDepth", "Drift Depth",   { 0.0f, 50.0f, 0.0f, 0.6f }, 8.0f, "ct");
    f ("driftRate",  "Drift Rate",    { 0.01f, 5.0f, 0.0f, 0.4f }, 0.4f, "Hz");
    f ("forget",     "Forget",        { 0.0f, 1.0f }, 0.1f);
    f ("jumpProb",   "Deja-vu",       { 0.0f, 1.0f }, 0.2f);
    f ("jumpDepth",  "Deja-vu Depth", { 0.0f, 1.0f }, 0.75f);
    f ("mix",        "Mix",           { 0.0f, 1.0f }, 0.5f);
    f ("outDB",      "Output",        { -24.0f, 6.0f }, -3.0f, "dB");

    return { ps.begin(), ps.end() };
}

mm::Params MemoryMachineProcessor::readParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    mm::Params p;
    p.blur = v ("blur"); p.bits = v ("bits"); p.downsample = v ("downsample");
    p.fragment = v ("fragment"); p.stutterMs = v ("stutterMs");
    p.sync = v ("stutSync") > 0.5f; p.note = (int) v ("stutNote");
    p.memorySec = v ("memorySec"); p.driftCents = v ("driftDepth"); p.driftHz = v ("driftRate");
    p.forget = v ("forget"); p.jumpProb = v ("jumpProb"); p.jumpDepth = v ("jumpDepth");
    p.mix = v ("mix"); p.outDB = v ("outDB");
    return p;
}

void MemoryMachineProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, getTotalNumInputChannels());
}

bool MemoryMachineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void MemoryMachineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    mm::HostTime ht;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) ht.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) { ht.ppq = *ppq; ht.hasPpq = true; }
            ht.playing = pos->getIsPlaying();
        }

    engine.process (buffer, readParams(), ht);
    recalling.store (engine.isRecalling(), std::memory_order_relaxed);
}

// ---- programs ---------------------------------------------------------------
int MemoryMachineProcessor::getNumPrograms() { return kNumPresets; }

const juce::String MemoryMachineProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, kNumPresets) ? juce::String (kPresets[index].name) : juce::String();
}

void MemoryMachineProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, kNumPresets)) return;
    currentProgram = index;
    for (int i = 0; i < 15; ++i)
        if (auto* prm = apvts.getParameter (kPresetIds[i]))
            prm->setValueNotifyingHost (prm->convertTo0to1 (kPresets[index].v[i]));
}

// ---- state ------------------------------------------------------------------
void MemoryMachineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MemoryMachineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) tree.getProperty ("program", 0);
            apvts.replaceState (tree);
        }
}

juce::AudioProcessorEditor* MemoryMachineProcessor::createEditor()
{
    return new MemoryMachineEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MemoryMachineProcessor(); }
