#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{
const juce::StringArray kNoteNames { "1/1", "1/2", "1/2.", "1/2T", "1/4", "1/4.", "1/4T",
                                     "1/8", "1/8.", "1/8T", "1/16", "1/16.", "1/16T", "1/32" };
const juce::StringArray kFilterTypes { "Off", "Lowpass", "Bandpass", "Highpass" };
const juce::StringArray kShapes      { "Sine", "Drift", "Steps", "Envelope" };

struct Value { const char* id; float v; };
struct Preset { const char* name; std::vector<Value> values; }; // unlisted = default

// Choice parameters take their index (filterType 1 = Lowpass, sweepShape 2 = Steps …).
const std::vector<Preset> kPresets = {
    { "Default", {} },
    { "Faint", { { "blur", 0.20f }, { "bits", 16 }, { "downsample", 2 }, { "fragment", 0.10f },
                 { "stutterMs", 150 }, { "memorySec", 2 }, { "driftDepth", 5 }, { "driftRate", 0.3f },
                 { "forget", 0.02f }, { "jumpProb", 0.1f }, { "jumpDepth", 0.5f }, { "mix", 0.35f }, { "outDB", -1 } } },
    { "Old Cassette", { { "blur", 0.45f }, { "bits", 14 }, { "downsample", 2 }, { "fragment", 0 },
                        { "driftDepth", 18 }, { "driftRate", 0.55f }, { "forget", 0.06f }, { "jumpProb", 0 },
                        { "mix", 1 }, { "outDB", -2 } } },
    { "Childhood", { { "blur", 0.65f }, { "bits", 10 }, { "downsample", 5 }, { "fragment", 0.35f },
                     { "stutterMs", 250 }, { "memorySec", 6 }, { "driftDepth", 25 }, { "driftRate", 0.15f },
                     { "forget", 0.15f }, { "jumpProb", 0.45f }, { "jumpDepth", 0.9f }, { "mix", 0.8f } } },
    { "Deja-vu", { { "blur", 0.30f }, { "downsample", 3 }, { "fragment", 0.55f }, { "stutSync", 1 },
                   { "stutNote", 7 }, { "memorySec", 6 }, { "driftDepth", 6 }, { "forget", 0.05f },
                   { "jumpProb", 0.8f }, { "jumpDepth", 1 }, { "mix", 0.7f } } },
    { "Stutter Grid", { { "blur", 0.15f }, { "bits", 24 }, { "downsample", 1 }, { "fragment", 0.7f },
                        { "stutSync", 1 }, { "stutNote", 10 }, { "memorySec", 2 }, { "driftDepth", 0 },
                        { "forget", 0 }, { "jumpProb", 0.15f }, { "jumpDepth", 0.4f }, { "mix", 1 }, { "outDB", -2 } } },
    { "Fading Away", { { "blur", 0.80f }, { "bits", 8 }, { "downsample", 8 }, { "fragment", 0.30f },
                       { "stutterMs", 300 }, { "memorySec", 8 }, { "driftDepth", 30 }, { "driftRate", 0.08f },
                       { "forget", 0.55f }, { "jumpProb", 0.5f }, { "jumpDepth", 1 }, { "mix", 1 }, { "outDB", -4 } } },
    { "Broken Recorder", { { "blur", 0.40f }, { "bits", 6 }, { "downsample", 10 }, { "fragment", 0.6f },
                           { "stutterMs", 60 }, { "memorySec", 1.5f }, { "driftDepth", 40 }, { "driftRate", 2.5f },
                           { "forget", 0.35f }, { "jumpProb", 0.3f }, { "jumpDepth", 0.6f }, { "mix", 1 }, { "outDB", -6 } } },
    // --- moving filter & weird ---
    { "Moving Haze", { { "blur", 0.10f }, { "bits", 16 }, { "downsample", 1 }, { "fragment", 0.2f },
                       { "driftDepth", 6 }, { "filterType", 1 }, { "filterHz", 700 }, { "filterReso", 0.55f },
                       { "sweepDepth", 0.7f }, { "sweepRate", 0.3f }, { "sweepShape", 0 }, { "mix", 1 } } },
    { "Ghost Steps", { { "blur", 0.20f }, { "bits", 14 }, { "downsample", 2 }, { "fragment", 0.35f },
                       { "stutSync", 1 }, { "stutNote", 10 }, { "warp", 0.5f }, { "filterType", 2 },
                       { "filterHz", 1400 }, { "filterReso", 0.7f }, { "sweepDepth", 0.55f }, { "sweepRate", 4 },
                       { "sweepShape", 2 }, { "mix", 1 }, { "outDB", 3 } } },
    { "Backwards Dream", { { "blur", 0.45f }, { "bits", 14 }, { "downsample", 2 }, { "fragment", 0.5f },
                           { "stutterMs", 400 }, { "memorySec", 6 }, { "driftDepth", 15 }, { "driftRate", 0.2f },
                           { "jumpProb", 0.4f }, { "jumpDepth", 1 }, { "reverse", 0.7f }, { "warp", 0.3f },
                           { "filterType", 2 }, { "filterHz", 900 }, { "filterReso", 0.4f }, { "sweepDepth", 0.4f },
                           { "sweepRate", 0.1f }, { "sweepShape", 1 }, { "mix", 0.75f }, { "outDB", 5 } } },
    { "Rumination", { { "blur", 0.50f }, { "bits", 10 }, { "downsample", 3 }, { "fragment", 0.55f },
                      { "stutterMs", 180 }, { "memorySec", 4 }, { "driftDepth", 10 }, { "forget", 0.08f },
                      { "jumpProb", 0.3f }, { "jumpDepth", 0.8f }, { "rumination", 0.8f }, { "mix", 0.8f },
                      { "outDB", -4 } } },
    { "Envelope Wah", { { "blur", 0 }, { "bits", 24 }, { "downsample", 1 }, { "fragment", 0.15f },
                        { "driftDepth", 3 }, { "forget", 0 }, { "jumpProb", 0 }, { "filterType", 2 },
                        { "filterHz", 350 }, { "filterReso", 0.65f }, { "sweepDepth", 0.8f }, { "sweepShape", 3 },
                        { "mix", 1 }, { "outDB", 6 } } },
};

const juce::Identifier kPresetTag  { "MemoryMachinePreset" };
const juce::String     kPresetExt  { ".mmpreset" };
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
            if (u == "Hz") return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz"
                                               : String (v, v < 1.0f ? 2 : (v < 100.0f ? 1 : 0)) + " Hz";
            return String (v, 1) + " " + u;
        };
        auto fromText = [percent, u] (const String& t)
        {
            float v = t.retainCharacters ("-0123456789.").getFloatValue();
            if (u == "Hz" && t.containsIgnoreCase ("k")) v *= 1000.0f;
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

    // v0.3: moving filter
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "filterType", 2 }, "Filter", kFilterTypes, 0));
    f ("filterHz",   "Filter Freq",   { 40.0f, 18000.0f, 0.0f, 0.25f }, 1200.0f, "Hz");
    f ("filterReso", "Resonance",     { 0.0f, 1.0f }, 0.3f);
    f ("sweepDepth", "Sweep Depth",   { 0.0f, 1.0f }, 0.5f);
    f ("sweepRate",  "Sweep Rate",    { 0.02f, 12.0f, 0.0f, 0.35f }, 0.25f, "Hz");
    ps.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "sweepShape", 2 }, "Sweep Shape", kShapes, 0));

    // v0.3: weird
    f ("reverse",    "Backwards",     { 0.0f, 1.0f }, 0.0f);
    f ("warp",       "Warp",          { 0.0f, 1.0f }, 0.0f);
    f ("rumination", "Rumination",    { 0.0f, 0.95f }, 0.0f);

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
    p.filterType = (int) v ("filterType"); p.filterHz = v ("filterHz"); p.filterReso = v ("filterReso");
    p.sweepDepth = v ("sweepDepth"); p.sweepHz = v ("sweepRate"); p.sweepShape = (int) v ("sweepShape");
    p.reverse = v ("reverse"); p.warp = v ("warp"); p.rumination = v ("rumination");
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

// ---- factory presets ---------------------------------------------------------
int MemoryMachineProcessor::getNumPrograms() { return (int) kPresets.size(); }

const juce::String MemoryMachineProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) kPresets.size()) ? juce::String (kPresets[(size_t) index].name)
                                                                    : juce::String();
}

void MemoryMachineProcessor::resetToDefaults()
{
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            r->setValueNotifyingHost (r->getDefaultValue());
}

void MemoryMachineProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) kPresets.size())) return;
    resetToDefaults();
    for (const auto& v : kPresets[(size_t) index].values)
        if (auto* prm = apvts.getParameter (v.id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v.v));
    currentProgram = index;
    presetName = kPresets[(size_t) index].name;
    userPreset = false;
}

// ---- user presets --------------------------------------------------------------
juce::File MemoryMachineProcessor::defaultPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("trockenmoos").getChildFile ("Memory Machine").getChildFile ("Presets");
}

juce::StringArray MemoryMachineProcessor::getUserPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : presetFolder.findChildFiles (juce::File::findFiles, false, "*" + kPresetExt))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool MemoryMachineProcessor::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return false;
    if (! presetFolder.createDirectory()) return false;

    juce::ValueTree preset (kPresetTag);
    preset.setProperty ("name", name, nullptr);
    preset.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
    preset.appendChild (apvts.copyState(), nullptr);

    auto xml = preset.createXml();
    if (xml == nullptr || ! xml->writeTo (presetFolder.getChildFile (name + kPresetExt)))
        return false;

    presetName = name;
    userPreset = true;
    return true;
}

void MemoryMachineProcessor::applyValues (const juce::ValueTree& params)
{
    // Parameters missing from an older preset fall back to their defaults.
    resetToDefaults();
    for (const auto& child : params)
        if (auto* prm = apvts.getParameter (child.getProperty ("id").toString()))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) child.getProperty ("value")));
}

bool MemoryMachineProcessor::loadUserPreset (const juce::String& name)
{
    auto xml = juce::XmlDocument::parse (presetFolder.getChildFile (name + kPresetExt));
    if (xml == nullptr || ! xml->hasTagName (kPresetTag.toString())) return false;

    const auto tree = juce::ValueTree::fromXml (*xml);
    const auto params = tree.getChildWithName (apvts.state.getType());
    if (! params.isValid()) return false;

    applyValues (params);
    presetName = name;
    userPreset = true;
    return true;
}

bool MemoryMachineProcessor::deleteUserPreset (const juce::String& name)
{
    auto file = presetFolder.getChildFile (name + kPresetExt);
    const auto ok = file.moveToTrash() || file.deleteFile();
    if (ok && userPreset && presetName == name) presetName = name + " (deleted)";
    return ok;
}

// ---- state ------------------------------------------------------------------
void MemoryMachineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("userPreset", userPreset, nullptr);
    state.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);
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
            presetName = tree.getProperty ("presetName", getProgramName (currentProgram)).toString();
            userPreset = (bool) tree.getProperty ("userPreset", false);
            applyValues (tree); // sessions from v0.2 get defaults for the new parameters
        }
}

juce::AudioProcessorEditor* MemoryMachineProcessor::createEditor()
{
    return new MemoryMachineEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MemoryMachineProcessor(); }
