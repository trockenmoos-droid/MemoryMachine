// Offline render test for Memory Machine.
// Build with -DMM_BUILD_TESTS=ON, run: MemoryMachineRender <outdir>
//
// Checks: block-size independence, transparency when everything is off,
// mix=0 is dry, drift really moves pitch, fragment really recalls the past,
// every preset stays finite and bounded. Also writes demo WAVs.

#include "../Source/PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>

using namespace juce;

static constexpr double kSR = 48000.0;
static int failures = 0;

static void check (bool ok, const String& what)
{
    std::printf ("[%s] %s\n", ok ? " ok " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

// A 120 BPM loop: kick, hats, a pad chord and plucks — something with
// rhythm and harmony for the memory to chew on.
static AudioBuffer<float> makeLoop (double seconds)
{
    const int len = (int) (kSR * seconds);
    AudioBuffer<float> b (2, len);
    b.clear();
    Random rng (7);
    const double beat = 0.5; // seconds
    const double twoPi = MathConstants<double>::twoPi;
    const double chords[4][3] = { { 220.0, 261.63, 329.63 }, { 174.61, 220.0, 261.63 },
                                  { 196.0, 246.94, 293.66 }, { 164.81, 196.0, 246.94 } };
    const double pluck[8] = { 659.25, 587.33, 523.25, 493.88, 523.25, 659.25, 783.99, 587.33 };

    for (int i = 0; i < len; ++i)
    {
        const double t = i / kSR;
        const double tb = std::fmod (t, beat);
        const int bar = (int) (t / (4 * beat)) % 4;

        double kick = std::sin (twoPi * (45.0 * tb + 60.0 * (1.0 - std::exp (-tb * 30.0)) / 30.0))
                    * std::exp (-tb * 9.0) * 0.8;
        const double th = std::fmod (t + beat * 0.5, beat);
        double hat = (rng.nextDouble() * 2.0 - 1.0) * std::exp (-th * 60.0) * 0.18;

        double pad = 0.0;
        for (double f : chords[bar])
            for (int h = 1; h <= 4; ++h)
                pad += std::sin (twoPi * f * h * t + h) / (h * h);
        pad *= 0.06 * (0.8 + 0.2 * std::sin (t * 0.7));

        const double e8 = std::fmod (t, beat * 0.5);
        const int step = (int) (t / (beat * 0.5)) % 8;
        double pl = std::sin (twoPi * pluck[step] * t) * std::exp (-e8 * 12.0) * 0.12;

        const double l = kick + hat + pad + pl * 0.7;
        const double r = kick + hat * 0.7 + pad + pl;
        b.setSample (0, i, (float) l);
        b.setSample (1, i, (float) r);
    }
    return b;
}

static void setP (MemoryMachineProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

static void cleanSettings (MemoryMachineProcessor& p)
{
    setP (p, "blur", 0); setP (p, "bits", 24); setP (p, "downsample", 1); setP (p, "fragment", 0);
    setP (p, "driftDepth", 0); setP (p, "forget", 0); setP (p, "jumpProb", 0);
    setP (p, "mix", 1); setP (p, "outDB", 0); setP (p, "stutSync", 0);
    setP (p, "filterType", 0); setP (p, "reverse", 0); setP (p, "warp", 0); setP (p, "rumination", 0);
}

static AudioBuffer<float> render (MemoryMachineProcessor& p, const AudioBuffer<float>& in, int block)
{
    AudioBuffer<float> out (in);
    p.prepareToPlay (kSR, block);
    MidiBuffer midi;
    for (int pos = 0; pos < out.getNumSamples(); pos += block)
    {
        const int n = jmin (block, out.getNumSamples() - pos);
        AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
        p.processBlock (view, midi);
    }
    return out;
}

static float maxDiff (const AudioBuffer<float>& a, const AudioBuffer<float>& b, int from = 0)
{
    float m = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = from; i < a.getNumSamples(); ++i)
            m = jmax (m, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return m;
}

static void writeWav (const File& f, const AudioBuffer<float>& b)
{
    f.deleteFile();
    WavAudioFormat fmt;
    std::unique_ptr<FileOutputStream> os (f.createOutputStream());
    std::unique_ptr<AudioFormatWriter> w (fmt.createWriterFor (os.get(), kSR, 2, 24, {}, 0));
    if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const File outDir (argc > 1 ? File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                : File::getCurrentWorkingDirectory());
    outDir.createDirectory();

    const auto loop = makeLoop (16.0);

    // 1. block-size independence (the old prototype decided everything per block)
    {
        MemoryMachineProcessor a, b;
        // Backwards Dream + rumination + an envelope... everything moving at once
        for (auto* q : { &a, &b })
        {
            q->setCurrentProgram (10);
            setP (*q, "rumination", 0.7f); setP (*q, "forget", 0.2f);
        }
        const auto ra = render (a, loop, 32);
        const auto rb = render (b, loop, 512);
        check (maxDiff (ra, rb) < 1e-4f, "same output at 32 and 512 sample buffers (diff "
                                         + String (maxDiff (ra, rb), 7) + ")");
    }

    // 2. transparent when every stage is off
    {
        MemoryMachineProcessor p; cleanSettings (p);
        const auto r = render (p, loop, 256);
        check (maxDiff (r, loop, 4800) < 1e-5f, "all stages off -> output equals input");
    }

    // 3. mix = 0 -> dry * output gain
    {
        MemoryMachineProcessor p; p.setCurrentProgram (7);
        setP (p, "mix", 0); setP (p, "outDB", -6);
        const auto r = render (p, loop, 256);
        AudioBuffer<float> ref (loop); ref.applyGain ((float) Decibels::decibelsToGain (-6.0));
        check (maxDiff (r, ref, 9600) < 1e-4f, "mix 0 -> dry signal at output gain");
    }

    // 4. drift moves pitch by roughly the set amount
    {
        MemoryMachineProcessor p; cleanSettings (p);
        setP (p, "driftDepth", 30); setP (p, "driftRate", 0.5f);
        AudioBuffer<float> sine (2, (int) (kSR * 8));
        for (int i = 0; i < sine.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                sine.setSample (ch, i, (float) (0.5 * std::sin (MathConstants<double>::twoPi * 440.0 * i / kSR)));
        const auto r = render (p, sine, 256);

        // frequency per 50 ms window from zero crossings
        double lo = 1e9, hi = 0;
        const int win = (int) (kSR * 0.05);
        for (int s = (int) kSR * 2; s + win < r.getNumSamples(); s += win)
        {
            int zc = 0; double first = -1, last = -1;
            for (int i = s + 1; i < s + win; ++i)
                if (r.getSample (0, i - 1) < 0.0f && r.getSample (0, i) >= 0.0f)
                {
                    const double a = r.getSample (0, i - 1), b = r.getSample (0, i);
                    const double t = (i - 1) + (-a) / (b - a);
                    if (first < 0) first = t; last = t; ++zc;
                }
            if (zc > 2) { const double f = (zc - 1) * kSR / (last - first); lo = jmin (lo, f); hi = jmax (hi, f); }
        }
        const double spanCents = 1200.0 * std::log2 (hi / lo);
        check (spanCents > 25.0 && spanCents < 90.0,
               "drift 30 ct -> pitch swings " + String (spanCents, 1) + " ct peak-to-peak");
    }

    // 5. fragment recalls the past (output departs from the present)
    {
        MemoryMachineProcessor p; cleanSettings (p);
        setP (p, "fragment", 1.0f); setP (p, "stutterMs", 125);
        const auto r = render (p, loop, 256);
        int differ = 0, total = 0;
        for (int i = 4800; i < r.getNumSamples(); ++i, ++total)
            if (std::abs (r.getSample (0, i) - loop.getSample (0, i)) > 1e-3f) ++differ;
        const double frac = (double) differ / total;
        check (frac > 0.2 && frac < 0.95, "fragment 1 -> recalled audio " + String (frac * 100.0, 0) + "% of the time");
    }

    // 6. tempo-synced slices land on the grid (host playhead)
    {
        struct Head : AudioPlayHead
        {
            int64_t pos = 0;
            Optional<PositionInfo> getPosition() const override
            {
                PositionInfo i; i.setBpm (120.0); i.setIsPlaying (true);
                i.setPpqPosition ((double) pos / kSR * 2.0); i.setTimeInSamples (pos);
                return i;
            }
        } head;
        MemoryMachineProcessor p; cleanSettings (p);
        setP (p, "fragment", 1.0f); setP (p, "stutSync", 1); setP (p, "stutNote", 10); // 1/16
        p.setPlayHead (&head);
        p.prepareToPlay (kSR, 300);
        AudioBuffer<float> out (loop);
        MidiBuffer midi;
        for (int pos = 0; pos < out.getNumSamples(); pos += 300)
        {
            head.pos = pos;
            const int n = jmin (300, out.getNumSamples() - pos);
            AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, n);
            p.processBlock (view, midi);
        }

        // every recall onset (output leaves the dry signal after >= 10 ms of
        // being identical) must sit on a 1/16 line
        const int sixteenth = (int) (kSR * 0.125);
        int onsets = 0, offGrid = 0, same = 0;
        for (int i = (int) kSR; i < out.getNumSamples(); ++i)
        {
            const bool differs = std::abs (out.getSample (0, i) - loop.getSample (0, i)) > 1e-4f;
            if (differs && same >= 480)
            {
                ++onsets;
                const int d = i % sixteenth;
                if (d > 48 && sixteenth - d > 4) ++offGrid; // fade-in makes detection lag a few samples
            }
            same = differs ? 0 : same + 1;
        }
        check (onsets > 10 && offGrid == 0,
               "synced recalls start on the 1/16 grid (" + String (onsets) + " onsets, " + String (offGrid) + " off-grid)");
    }

    // 7. moving filter really moves (windowed level of filtered noise swings)
    {
        AudioBuffer<float> noise (2, (int) (kSR * 8));
        Random rn (3);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < noise.getNumSamples(); ++i)
                noise.setSample (ch, i, rn.nextFloat() * 0.6f - 0.3f);

        auto swing = [&] (float depth)
        {
            MemoryMachineProcessor p; cleanSettings (p);
            setP (p, "filterType", 1); setP (p, "filterHz", 600); setP (p, "filterReso", 0.3f);
            setP (p, "sweepDepth", depth); setP (p, "sweepRate", 0.5f); setP (p, "sweepShape", 0);
            const auto r = render (p, noise, 256);
            float lo = 1e9f, hi = 0.0f;
            const int win = (int) (kSR * 0.05);
            for (int s0 = (int) kSR; s0 + win < r.getNumSamples(); s0 += win)
            {
                const float rms = r.getRMSLevel (0, s0, win);
                lo = jmin (lo, rms); hi = jmax (hi, rms);
            }
            return hi / jmax (lo, 1e-6f);
        };
        const float still = swing (0.0f), moving = swing (1.0f);
        check (still < 1.6f && moving > 3.0f,
               "filter sweep: level swing " + String (still, 2) + "x static vs " + String (moving, 2) + "x moving");
    }

    // 8. rumination: memories of memories outlive the input; stable at maximum
    {
        AudioBuffer<float> burst (loop);
        const int stop = (int) (kSR * 6);
        for (int ch = 0; ch < 2; ++ch) burst.clear (ch, stop, burst.getNumSamples() - stop);

        auto tail = [&] (float fbAmt, float& peak)
        {
            MemoryMachineProcessor p; cleanSettings (p);
            setP (p, "fragment", 1.0f); setP (p, "stutterMs", 150); setP (p, "rumination", fbAmt);
            setP (p, "blur", 0.3f);
            const auto r = render (p, burst, 256);
            peak = r.getMagnitude (0, r.getNumSamples());
            return r.getRMSLevel (0, stop + (int) (kSR * 1.0), (int) (kSR * 2.0));
        };
        float pk0 = 0, pk1 = 0;
        const float t0 = tail (0.0f, pk0), t1 = tail (0.95f, pk1);
        check (t1 > 5.0f * t0 + 1e-4f, "rumination keeps recalling after the input stopped (tail "
                                         + String (t0, 4) + " -> " + String (t1, 4) + ")");
        check (std::isfinite (pk1) && pk1 < 1.5f, "rumination at maximum stays bounded (peak " + String (pk1, 2) + ")");
    }

    // 9. backwards / warp change what comes back, stay finite
    {
        MemoryMachineProcessor a, b; cleanSettings (a); cleanSettings (b);
        for (auto* q : { &a, &b }) { setP (*q, "fragment", 1.0f); setP (*q, "stutterMs", 200); }
        setP (b, "reverse", 1.0f); setP (b, "warp", 1.0f);
        const auto ra = render (a, loop, 256), rb = render (b, loop, 256);
        const float pk = rb.getMagnitude (0, rb.getNumSamples());
        check (maxDiff (ra, rb) > 0.05f && std::isfinite (pk) && pk < 1.5f,
               "backwards + warp recalls differ from plain ones, peak " + String (pk, 2));
    }

    // 10. user presets: save, list, load, delete; old sessions get defaults
    {
        auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("mm-preset-test");
        dir.deleteRecursively();
        MemoryMachineProcessor p; p.setPresetFolder (dir);
        p.setCurrentProgram (10);
        setP (p, "filterHz", 2345.0f); setP (p, "rumination", 0.42f);
        const bool saved = p.saveUserPreset ("My: Memory?");    // illegal chars get cleaned
        const auto names = p.getUserPresetNames();

        std::vector<float> before;
        for (auto* prm : p.getParameters()) before.push_back (prm->getValue());
        p.setCurrentProgram (0);
        const bool loaded = names.size() == 1 && p.loadUserPreset (names[0]);
        bool same = true; size_t i = 0;
        for (auto* prm : p.getParameters()) same = same && std::abs (prm->getValue() - before[i++]) < 1e-6f;
        check (saved && loaded && same && p.isUserPreset() && p.getPresetName() == names[0],
               "user preset round trip (\"" + names.joinIntoString (",") + "\")");

        check (p.deleteUserPreset (names[0]) && p.getUserPresetNames().isEmpty(), "user preset delete");

        // a v0.2 session has none of the new parameters -> they must come back as defaults
        MemoryBlock mb; p.getStateInformation (mb);
        auto xml = AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        for (auto* e = xml->getFirstChildElement(); e != nullptr;)
        {
            auto* next = e->getNextElement();
            const auto id = e->getStringAttribute ("id");
            if (id == "filterType" || id == "filterHz" || id == "rumination") xml->removeChildElement (e, true);
            e = next;
        }
        MemoryBlock old; AudioProcessor::copyXmlToBinary (*xml, old);
        MemoryMachineProcessor q;
        setP (q, "rumination", 0.9f);
        q.setStateInformation (old.getData(), (int) old.getSize());
        auto* rum = q.apvts.getParameter ("rumination");
        check (std::abs (rum->getValue() - rum->getDefaultValue()) < 1e-6f
               && std::abs (q.apvts.getRawParameterValue ("blur")->load() - p.apvts.getRawParameterValue ("blur")->load()) < 1e-5f,
               "old session: kept values load, missing parameters fall back to defaults");
        dir.deleteRecursively();
    }

    // 11. every preset finite, bounded; write demos
    MemoryMachineProcessor probe;
    writeWav (outDir.getChildFile ("00_dry.wav"), loop);
    for (int i = 0; i < probe.getNumPrograms(); ++i)
    {
        MemoryMachineProcessor p; p.setCurrentProgram (i);
        const auto r = render (p, loop, 441);
        float peak = 0; bool finite = true;
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < r.getNumSamples(); ++s)
            {
                const float v = r.getSample (ch, s);
                if (! std::isfinite (v)) finite = false;
                peak = jmax (peak, std::abs (v));
            }
        const auto name = p.getProgramName (i);
        check (finite && peak < 1.5f, "preset '" + name + "' finite, peak " + String (peak, 2));
        writeWav (outDir.getChildFile (String (i + 1).paddedLeft ('0', 2) + "_" + name.replaceCharacter (' ', '_') + ".wav"), r);
    }

    // 12. editor builds and paints (snapshot for the README)
    {
        MemoryMachineProcessor p;
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        check (ed != nullptr && ed->getWidth() > 0, "editor opens");
        if (ed != nullptr)
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            auto f = outDir.getChildFile ("screenshot.png"); f.deleteFile();
            FileOutputStream os (f); PNGImageFormat png; png.writeImageToStream (img, os);
        }
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
