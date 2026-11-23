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
        a.setCurrentProgram (3); b.setCurrentProgram (3); // Childhood: everything moving
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

    // 7. every preset finite, bounded; write demos
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

    // 8. editor builds and paints (snapshot for the README)
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
