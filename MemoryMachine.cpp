// MemoryMachine.cpp
// JUCE audio effect prototype (VST3/AU)
// Concept: "Memory Machine" — a time-warping, human-memory-inspired degrader
//
// NEW (per user request):
//  • Tempo-sync for stutter length (note values incl. triplets & dotted)
//  • "Deja‑Vu Jumps" — random jumps to new recall points inside the memory window
//  • Ready-to-build CMakeLists.txt provided below (search for: CMAKE SECTION)
//
// Drop this file into a fresh JUCE Audio Plug-In project (or use the CMake below),
// build VST3/AU and load in Ableton Live or REAPER.

#include <juce_audio_processors/juce_audio_processors.h>
#include <random>

using namespace juce;

// ============================= Utilities ============================= //
static inline float dBToLin (float dB) { return std::pow (10.0f, dB * 0.05f); }

// One-pole lowpass with coefficient smoothing
struct OnePoleLP
{
    void prepare (double sr) { sampleRate = sr; z = 0.0f; setCutoffHz (20000.0f); }

    void setCutoffHz (float hz)
    {
        hz = jlimit (5.0f, 20000.0f, hz);
        const float x = std::exp (-MathConstants<float>::twoPi * hz / (float) sampleRate);
        a = 1.0f - x; b = x; // y[n] = a*x[n] + b*y[n-1]
    }

    inline float process (float x) { z = a * x + b * z; return z; }

    double sampleRate = 44100.0;
    float a = 1.0f, b = 0.0f, z = 0.0f;
};

// Simple linear ramp smoother (for gates/dropouts to avoid clicks)
struct Slew
{
    void reset (double sr, float timeMs = 5.0f)
    {
        const auto samples = std::max (1, (int) std::round (sr * (timeMs / 1000.0f)));
        step = 1.0f / (float) samples;
        cur = 1.0f;
        target = 1.0f;
    }

    void setTarget (float t) { target = jlimit (0.0f, 1.0f, t); }

    inline float getNext()
    {
        const float diff = target - cur;
        if (std::abs (diff) <= step) { cur = target; return cur; }
        cur += (diff > 0 ? step : -step);
        return cur;
    }

    float step = 0.001f, cur = 1.0f, target = 1.0f;
};

// Circular buffer for short memory window & stutter playback
struct CircularDelay
{
    void prepare (double sr, double maxSeconds)
    {
        sampleRate = sr;
        const size_t maxSamps = (size_t) std::ceil (maxSeconds * sr) + 1;
        buffer.setSize (1, (int) maxSamps);
        buffer.clear();
        writeIdx = 0; loopLen = (int) (0.1 * sr);
    }

    inline void push (float x)
    {
        buffer.setSample (0, writeIdx, x);
        writeIdx = (writeIdx + 1) % buffer.getNumSamples();
    }

    // Read from the past with fractional offset
    inline float readPast (double delaySamples) const
    {
        const int N = buffer.getNumSamples();
        double readPos = (double) writeIdx - delaySamples;
        while (readPos < 0.0) readPos += (double) N;
        const int i0 = (int) readPos % N;
        const int i1 = (i0 + 1) % N;
        const float frac = (float) (readPos - std::floor (readPos));
        const float a = buffer.getSample (0, i0);
        const float b = buffer.getSample (0, i1);
        return a + (b - a) * frac; // linear interp
    }

    AudioBuffer<float> buffer;
    int writeIdx = 0;
    int loopLen = 4410;
    double sampleRate = 44100.0;
};

// ============================= Processor ============================= //
class MemoryMachineAudioProcessor : public AudioProcessor
{
public:
    AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMS", createParameterLayout() };

    MemoryMachineAudioProcessor()
        : AudioProcessor (BusesProperties().withInput  ("Input",  AudioChannelSet::stereo(), true)
                                            .withOutput ("Output", AudioChannelSet::stereo(), true))
    {}

    const String getName() const override { return "MemoryMachine"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (MemoryBlock& destData) override
    {
        if (auto state = apvts.copyState())
            if (auto xml = state.createXml())
                copyXmlToBinary (*xml, destData);
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
            if (xml->hasTagName (apvts.state.getType()))
                apvts.replaceState (ValueTree::fromXml (*xml));
    }

    void prepareToPlay (double sr, int) override
    {
        sampleRate = sr;
        const int channels = getMainBusNumInputChannels();
        lps.resize ((size_t) channels);
        droop.resize ((size_t) channels);
        memories.resize ((size_t) channels);
        crushPhase.assign ((size_t) channels, 0);

        const double maxMemorySeconds = 8.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            lps[(size_t) ch].prepare (sr);
            droop[(size_t) ch].reset   (sr, 8.0f);
            memories[(size_t) ch].prepare (sr, maxMemorySeconds);
        }

        rng.seed (1234567);
        wowPhase = flutterPhase = 0.0;
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto& mainIn  = layouts.getChannelSet (true,  0);
        const auto& mainOut = layouts.getChannelSet (false, 0);
        return (mainIn == mainOut) && (mainOut == AudioChannelSet::mono() || mainOut == AudioChannelSet::stereo());
    }

    // ==================== Core Processing ==================== //
    void processBlock (AudioBuffer<float>& buffer, MidiBuffer&) override
    {
        ScopedNoDenormals noDenormals;
        const int numCh = jmin (getTotalNumInputChannels(), buffer.getNumChannels());
        const int numSamples = buffer.getNumSamples();

        // Fetch params once per block
        const float blurAmt       = *apvts.getRawParameterValue ("blur");
        const float bits          = *apvts.getRawParameterValue ("bits");
        const int   downsampleN   = jmax (1, (int) std::round (*apvts.getRawParameterValue ("downsample")));
        const float fragmentAmt   = *apvts.getRawParameterValue ("fragment");
        const float stutterMs     = *apvts.getRawParameterValue ("stutterMs");
        const float memorySec     = *apvts.getRawParameterValue ("memorySec");
        const float driftCents    = *apvts.getRawParameterValue ("driftDepth");
        const float driftHz       = *apvts.getRawParameterValue ("driftRate");
        const float forgetAmt     = *apvts.getRawParameterValue ("forget");
        const float mix           = *apvts.getRawParameterValue ("mix");
        const float outDB         = *apvts.getRawParameterValue ("outDB");
        const bool  syncOn        = (*apvts.getRawParameterValue ("stutSync")) > 0.5f;
        const int   noteIdx       = (int) *apvts.getRawParameterValue ("stutNote");
        const float jumpProb      = *apvts.getRawParameterValue ("jumpProb");
        const float jumpDepth     = *apvts.getRawParameterValue ("jumpDepth");

        const float outGain = dBToLin (outDB);

        // Host tempo (Ableton/REAPER via JUCE)
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
        {
            AudioPlayHead::CurrentPositionInfo pos;
            if (ph->getCurrentPosition (pos) && pos.bpm > 0.0)
                bpm = pos.bpm;
        }

        // Lowpass cutoff mapping: blur=0 -> 18kHz, blur=1 -> 600 Hz
        const float cutoff = jmap (blurAmt, 0.0f, 1.0f, 18000.0f, 600.0f);
        for (int ch = 0; ch < numCh; ++ch) lps[(size_t) ch].setCutoffHz (cutoff);

        // Dropout target based on forgetAmt
        const float gateOpen = 1.0f - forgetAmt;
        for (int ch = 0; ch < numCh; ++ch)
            droop[(size_t) ch].setTarget (random01() < gateOpen ? 1.0f : 0.0f);

        // Drift LFO
        const double twoPi = MathConstants<double>::twoPi;
        const double wowRate = jlimit (0.005, 1.0, (double) driftHz);
        const double flutterRate = jlimit (0.1, 12.0, (double) (driftHz * 6.0));
        const double driftDepthRatio = std::pow (2.0, (double) driftCents / 1200.0) - 1.0;

        // Stutter length (tempo-sync aware)
        const double stutterLenSamples = syncOn ? noteIndexToSamples (noteIdx, bpm) : (double) stutterMs * 0.001 * sampleRate;
        const double maxDelaySamps = memorySec * sampleRate;
        for (int ch = 0; ch < numCh; ++ch)
            memories[(size_t) ch].loopLen = (int) jlimit (16.0, maxDelaySamps, stutterLenSamples);

        // Decide block-level states
        const bool stutterOn = (fragmentAmt > 0.001f) && (random01() < (0.25f * fragmentAmt));

        // Initial recall point
        double recallDelay = stutterOn ? random01() * (double) maxDelaySamps : 20.0;

        // Maybe jump to a new recall point inside the window (Deja‑Vu)
        if (stutterOn && random01() < jumpProb)
            recallDelay = (double) maxDelaySamps * (randomSigned() * (double) jumpDepth * 0.5 + 0.5); // center-ish with depth

        // Downsample phasors per channel
        for (int ch = 0; ch < numCh; ++ch) crushPhase[(size_t) ch] %= jmax (1, downsampleN);
        float heldSample[2] { 0.0f, 0.0f };

        for (int n = 0; n < numSamples; ++n)
        {
            // update drift LFOs
            wowPhase += wowRate / sampleRate; if (wowPhase >= 1.0) wowPhase -= 1.0;
            flutterPhase += flutterRate / sampleRate; if (flutterPhase >= 1.0) flutterPhase -= 1.0;
            const double lfo = 0.6 * std::sin (twoPi * wowPhase) + 0.4 * std::sin (twoPi * flutterPhase);
            const double readDrift = driftDepthRatio * lfo;

            for (int ch = 0; ch < numCh; ++ch)
            {
                float x = buffer.getSample (ch, n);
                memories[(size_t) ch].push (x);

                double delayRead;
                if (stutterOn)
                {
                    const double local = std::fmod ((double) n, jmax (1.0, stutterLenSamples));
                    delayRead = recallDelay + local;
                }
                else
                {
                    const double base = 20.0 + 10.0 * lfo; // small moving delay for vibrato
                    delayRead = base;
                }

                float y = memories[(size_t) ch].readPast (delayRead * (1.0 + readDrift));

                // Lowpass blur
                y = lps[(size_t) ch].process (y);

                // Downsample + bit crush
                int& ph = crushPhase[(size_t) ch];
                if (ph == 0) heldSample[ch] = y;
                y = heldSample[ch];
                ph = (ph + 1) % downsampleN;

                const int qBits = jlimit (4, 24, (int) std::round (bits));
                const float qLevels = (float) (1 << jmax (1, qBits - 1));
                y = std::round (y * qLevels) / qLevels;

                // Probabilistic dropout gate (slewed)
                const float gate = droop[(size_t) ch].getNext();
                y *= gate;

                const float out = (1.0f - mix) * x + mix * y;
                buffer.setSample (ch, n, out * outGain);
            }
        }
    }

    // ==================== Editor ==================== //
    class Editor : public AudioProcessorEditor
    {
    public:
        Editor (MemoryMachineAudioProcessor& p, AudioProcessorValueTreeState& s)
            : AudioProcessorEditor (&p), state (s)
        {
            setSize (760, 420);

            add (blur,   blurA,   "Blur");
            add (bits,   bitsA,   "Bits");
            add (down,   downA,   "Downsample");
            add (fragment, fragA, "Fragment");
            add (stut,   stutA,   "Stutter (ms)");
            add (sync,   syncA,   "Stut Sync");
            addChoice (note, noteA, "Note", {"1/1","1/2","1/2.","1/2T","1/4","1/4.","1/4T","1/8","1/8.","1/8T","1/16","1/16.","1/16T","1/32"});
            add (mem,    memA,    "Memory (s)");
            add (driftD, driftDA, "Drift Depth (cents)");
            add (driftR, driftRA, "Drift Rate (Hz)");
            add (forget, forgetA, "Forget");
            add (jumpP,  jumpPA,  "Deja‑Vu Prob");
            add (jumpD,  jumpDA,  "Deja‑Vu Depth");
            add (mix,    mixA,    "Mix");
            add (out,    outA,    "Output (dB)");

            title.setText ("Memory Machine", dontSendNotification);
            title.setJustificationType (Justification::centred);
            title.setFont (Font (22.0f, Font::bold));
            addAndMakeVisible (title);

            onResize = [this]
            {
                auto r = getLocalBounds().reduced (14);
                auto top = r.removeFromTop (36); title.setBounds (top);

                auto row = [&](int count, std::initializer_list<Component*> comps)
                {
                    auto line = r.removeFromTop (60);
                    auto w = line.getWidth() / count;
                    for (auto* c : comps) { c->setBounds (line.removeFromLeft (w).reduced (6)); }
                };

                row (5, { &blur, &bits, &down, &fragment, &stut });
                row (5, { &sync, &note, &mem, &driftD, &driftR });
                row (5, { &forget, &jumpP, &jumpD, &mix, &out });
            };
        }

        void resized() override { if (onResize) onResize(); }

    private:
        void add (Slider& s, std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment>& a, const String& name)
        {
            s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (Slider::TextBoxBelow, true, 70, 18);
            s.setName (name);
            addAndMakeVisible (s);
            a.reset (new AudioProcessorValueTreeState::SliderAttachment (state, paramIdFrom (name), s));
        }

        void addChoice (ComboBox& c, std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment>& a, const String& name, const StringArray& items)
        {
            c.setName (name);
            for (int i = 0; i < items.size(); ++i) c.addItem (items[i], i + 1);
            addAndMakeVisible (c);
            a.reset (new AudioProcessorValueTreeState::ComboBoxAttachment (state, paramIdFrom (name), c));
        }

        static String paramIdFrom (String n) { return n.toLowerCase().replaceCharacters (" ()‑.-T", "_______"); }

        Label title; std::function<void()> onResize;
        AudioProcessorValueTreeState& state;

        Slider blur, bits, down, fragment, stut, mem, driftD, driftR, forget, jumpP, jumpD, mix, out;
        ComboBox note; ToggleButton sync;
        std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> blurA, bitsA, downA, fragA, stutA, memA, driftDA, driftRA, forgetA, jumpPA, jumpDA, mixA, outA;
        std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> noteA; std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> syncA;
    };

    AudioProcessorEditor* createEditor() override { return new Editor (*this, apvts); }
    bool hasEditor() const override { return true; }

private:
    // ==================== Params Layout ==================== //
    static AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        std::vector<std::unique_ptr<RangedAudioParameter>> params;
        auto choice = [](const String& id, const String& nm, const StringArray& items, int def)
        {
            return std::make_unique<AudioParameterChoice> (id, nm, items, def);
        };

        params.push_back (std::make_unique<AudioParameterFloat> ("blur",       "Blur",       NormalisableRange<float> (0.0f, 1.0f), 0.35f));
        params.push_back (std::make_unique<AudioParameterFloat> ("bits",       "Bits",       NormalisableRange<float> (4.0f, 24.0f, 1.0f), 12.0f));
        params.push_back (std::make_unique<AudioParameterFloat> ("downsample", "Downsample", NormalisableRange<float> (1.0f, 16.0f, 1.0f), 4.0f));
        params.push_back (std::make_unique<AudioParameterFloat> ("fragment",   "Fragment",   NormalisableRange<float> (0.0f, 1.0f), 0.25f));
        params.push_back (std::make_unique<AudioParameterFloat> ("stutterMs",  "Stutter (ms)", NormalisableRange<float> (10.0f, 400.0f), 120.0f));
        params.push_back (std::make_unique<AudioParameterBool>  ("stutSync",   "Stut Sync",  false));
        params.push_back (choice ("stutNote", "Note", StringArray {"1/1","1/2","1/2.","1/2T","1/4","1/4.","1/4T","1/8","1/8.","1/8T","1/16","1/16.","1/16T","1/32"}, 9 /* default 1/8T */));
        params.push_back (std::make_unique<AudioParameterFloat> ("memorySec",  "Memory (s)", NormalisableRange<float> (0.25f, 8.0f), 3.0f));
        params.push_back (std::make_unique<AudioParameterFloat> ("driftDepth", "Drift Depth (cents)", NormalisableRange<float> (0.0f, 50.0f), 8.0f));
        params.push_back (std::make_unique<AudioParameterFloat> ("driftRate",  "Drift Rate (Hz)",    NormalisableRange<float> (0.01f, 5.0f), 0.4f));
        params.push_back (std::make_unique<AudioParameterFloat> ("forget",     "Forget",     NormalisableRange<float> (0.0f, 1.0f), 0.1f));
        params.push_back (std::make_unique<AudioParameterFloat> ("jumpProb",   "Deja‑Vu Prob", NormalisableRange<float> (0.0f, 1.0f), 0.2f));
        params.push_back (std::make_unique<AudioParameterFloat> ("jumpDepth",  "Deja‑Vu Depth", NormalisableRange<float> (0.0f, 1.0f), 0.75f));
        params.push_back (std::make_unique<AudioParameterFloat> ("mix",        "Mix",        NormalisableRange<float> (0.0f, 1.0f), 0.5f));
        params.push_back (std::make_unique<AudioParameterFloat> ("outDB",      "Output (dB)",NormalisableRange<float> (-24.0f, 6.0f), -3.0f));

        return { params.begin(), params.end() };
    }

    // Note index → seconds per note at current BPM
    double noteIndexToSamples (int idx, double bpm) const
    {
        const double spb = 60.0 / jmax (1.0, bpm); // seconds per beat (quarter)
        // match the Editor list: 1/1,1/2,1/2.,1/2T,1/4,1/4.,1/4T,1/8,1/8.,1/8T,1/16,1/16.,1/16T,1/32
        const double base[] = {4.0, 2.0, 3.0, 4.0/3.0, 1.0, 1.5, 2.0/3.0, 0.5, 0.75, 1.0/3.0, 0.25, 0.375, 1.0/6.0, 0.125};
        const int clamped = jlimit (0, (int) (std::size (base) - 1), idx);
        const double beats = base[clamped];
        return beats * spb * sampleRate;
    }

    // RNG helpers
    inline float random01()             { return dist (rng); }
    inline double randomSigned()        { return (double) dist (rng) * 2.0 - 1.0; }

    // Members
    double sampleRate = 44100.0;
    std::vector<OnePoleLP> lps;               // blur filters per channel
    std::vector<Slew>     droop;              // dropout gate per channel
    std::vector<CircularDelay> memories;      // per-channel memory window
    std::vector<int> crushPhase;              // downsample counters

    std::mt19937 rng { 1234 };
    std::uniform_real_distribution<float> dist { 0.0f, 1.0f };

    double wowPhase = 0.0, flutterPhase = 0.0;
};

// ============================= Factory ============================= //
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MemoryMachineAudioProcessor(); }


// ============================= CMAKE SECTION ============================= //
/*
Save the following as CMakeLists.txt next to MemoryMachine.cpp.
Adjust JUCE path as needed (either add_subdirectory or find_package).

----- CMakeLists.txt -----
cmake_minimum_required(VERSION 3.20)
project(MemoryMachine VERSION 0.1.0)

# Option 1: Use JUCE as a subdirectory (recommended if you have JUCE source)
# set(JUCE_DIR "../JUCE")
# add_subdirectory(${JUCE_DIR} JUCE)

# Option 2: Use installed JUCE (via CMake package)
# find_package(JUCE CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

juce_add_plugin(MemoryMachine
    COMPANY_NAME        "MemoryWorks"
    BUNDLE_ID           com.memoryworks.memorymachine
    IS_SYNTH            FALSE
    NEEDS_MIDI_INPUT    FALSE
    NEEDS_MIDI_OUTPUT   FALSE
    IS_MIDI_EFFECT      FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE  Mwks
    PLUGIN_CODE              MMem
    FORMATS              VST3 AU
    PRODUCT_NAME         "Memory Machine")

# Your single compile unit
juce_generate_juce_header(MemoryMachine)

target_sources(MemoryMachine PRIVATE MemoryMachine.cpp)

# JUCE modules
target_link_libraries(MemoryMachine PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
)

# macOS: set the AU to modern subtype
if(APPLE)
  set_target_properties(MemoryMachine PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
endif()
----- end -----
*/

