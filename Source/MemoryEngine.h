// MemoryEngine.h — the DSP core of Memory Machine.
//
// A degrader modelled on how memory fails: the sound blurs, loses
// resolution, drifts in pitch, drops out, and fragments of the recent past
// come back — sometimes from the wrong place ("déjà-vu"), backwards, at the
// wrong speed, or as memories of memories ("rumination").
//
// Everything is decided per sample, never per audio block, so the effect
// behaves identically at any host buffer size.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace mm
{

enum FilterType  { filterOff = 0, filterLP, filterBP, filterHP };
enum SweepShape  { shapeSine = 0, shapeDrift, shapeSteps, shapeEnvelope };

struct Params
{
    float blur       = 0.35f;  // 0..1  lowpass amount
    float bits       = 12.0f;  // 4..24 (24 = off)
    float downsample = 4.0f;   // 1..16 sample-and-hold factor
    float fragment   = 0.25f;  // 0..1  how often memories repeat
    float stutterMs  = 120.0f; // free-running slice length
    bool  sync       = false;  // slice length from host tempo
    int   note       = 9;      // index into kNoteBeats
    float memorySec  = 3.0f;   // how far back recall can reach
    float driftCents = 8.0f;   // wow/flutter depth
    float driftHz    = 0.4f;   // wow rate (flutter runs 6x faster)
    float forget     = 0.1f;   // 0..1  dropout amount
    float jumpProb   = 0.2f;   // chance a recall comes from further back
    float jumpDepth  = 0.75f;  // how far back such a recall may reach
    float mix        = 0.5f;
    float outDB      = -3.0f;

    // moving filter (wet path)
    int   filterType = filterOff;
    float filterHz   = 1200.0f;
    float filterReso = 0.3f;   // 0..1
    float sweepDepth = 0.5f;   // 0..1 = ±0..4 octaves
    float sweepHz    = 0.25f;
    int   sweepShape = shapeSine;

    // weird
    float reverse    = 0.0f;   // chance a recall plays backwards
    float warp       = 0.0f;   // chance a recall plays at another speed
    float rumination = 0.0f;   // 0..0.95 output written back one slice later
};

struct HostTime
{
    double bpm     = 120.0;
    bool   playing = false;
    bool   hasPpq  = false;
    double ppq     = 0.0;      // at the first sample of the block
};

// 1/1, 1/2, 1/2., 1/2T, 1/4, 1/4., 1/4T, 1/8, 1/8., 1/8T, 1/16, 1/16., 1/16T, 1/32
static constexpr double kNoteBeats[] = { 4.0, 2.0, 3.0, 4.0 / 3.0, 1.0, 1.5, 2.0 / 3.0,
                                         0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0, 0.125 };
static constexpr int kNumNotes = (int) (sizeof (kNoteBeats) / sizeof (kNoteBeats[0]));

class Engine
{
public:
    static constexpr double kMaxMemorySec = 8.0;
    static constexpr double kMaxSliceSec  = 4.0;
    static constexpr double kMaxSpeed     = 2.0;
    static constexpr double kMaxDriftSec  = 0.25;

    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        channels = juce::jmax (1, numChannels);
        // memory window + a slice read at double speed + up to 2 slices of repeats
        bufLen = (int) std::ceil ((kMaxMemorySec + kMaxSpeed * kMaxSliceSec + 2.0 * kMaxSliceSec
                                   + kMaxDriftSec + 0.1) * sr);
        mem.assign ((size_t) channels, std::vector<float> ((size_t) bufLen, 0.0f));
        const auto zero = std::vector<float> ((size_t) channels, 0.0f);
        lp1 = lp2 = held = zero;
        ruLen = (int) std::ceil (kMaxSliceSec * sr) + 2;
        ruMem.assign ((size_t) channels, std::vector<float> ((size_t) ruLen, 0.0f));
        ic1.assign ((size_t) channels, 0.0); ic2.assign ((size_t) channels, 0.0);

        smoothK  = 1.0 - std::exp (-1.0 / (0.05 * sr));        // ~50 ms parameter smoothing
        fadeStep = 1.0f / (float) juce::jmax (1.0, 0.006 * sr); // 6 ms gate / stutter fades
        envAtt   = 1.0 - std::exp (-1.0 / (0.005 * sr));
        envRel   = 1.0 - std::exp (-1.0 / (0.15 * sr));
        reset();
    }

    void reset()
    {
        for (auto& c : mem) std::fill (c.begin(), c.end(), 0.0f);
        for (auto* v : { &lp1, &lp2, &held }) std::fill (v->begin(), v->end(), 0.0f);
        for (auto& c : ruMem) std::fill (c.begin(), c.end(), 0.0f);
        std::fill (ic1.begin(), ic1.end(), 0.0); std::fill (ic2.begin(), ic2.end(), 0.0);
        now = 0;
        wowPhase = flutterPhase = 0.0;
        sWowAmp = sFlutAmp = 0.0;
        sLpCoef = -1.0; sMix = -1.0; sGain = -1.0; sLogHz = -1.0; sReso = -1.0;
        holdCounter = 0;
        stut = {};
        stutGain = 0.0f;
        freeCounter = 0; lastGridIndex = INT64_MIN;
        gate = 1.0f; gateClosedFor = 0; gateTick = 0;
        sweepPhase = 0.0; rndPrev = rndNext = 0.0f; env = 0.0;
        rng.seed (0x7e0c0a5u);
    }

    void process (juce::AudioBuffer<float>& buffer, const Params& p, const HostTime& ht)
    {
        const int numCh = juce::jmin (channels, buffer.getNumChannels());
        const int numSamples = buffer.getNumSamples();
        if (numCh == 0 || numSamples == 0) return;

        const double twoPi = juce::MathConstants<double>::twoPi;

        // ---- per-block targets (smoothed per sample below) ----------------
        const double bpm = juce::jlimit (20.0, 999.0, ht.bpm > 0.0 ? ht.bpm : 120.0);
        const double sliceBeats = kNoteBeats[juce::jlimit (0, kNumNotes - 1, p.note)];
        double slice = p.sync ? sliceBeats * 60.0 / bpm * sr : (double) p.stutterMs * 0.001 * sr;
        slice = juce::jlimit (16.0, kMaxSliceSec * sr, slice);
        const double memSamps = juce::jlimit (slice, kMaxMemorySec * sr, (double) p.memorySec * sr);

        // Pitch drift: a sinusoidal delay modulation of amplitude A at rate f
        // gives a peak pitch ratio of 1 ± 2πfA, so A = r / (2πf).
        const double r        = std::pow (2.0, (double) p.driftCents / 1200.0) - 1.0;
        const double wowHz    = juce::jlimit (0.01, 5.0, (double) p.driftHz);
        const double flutHz   = juce::jlimit (0.1, 30.0, wowHz * 6.0);
        double wowAmp  = 0.7 * r / (twoPi * wowHz)  * sr;
        double flutAmp = 0.3 * r / (twoPi * flutHz) * sr;
        const double cap = kMaxDriftSec * sr;
        if (wowAmp + flutAmp > cap) { const double s = cap / (wowAmp + flutAmp); wowAmp *= s; flutAmp *= s; }

        // Blur: log-mapped 20 kHz → 500 Hz; exactly 0 means the filter is out.
        const double blurAmt = juce::jlimit (0.0, 1.0, (double) p.blur);
        const double cutoff = 20000.0 * std::pow (500.0 / 20000.0, blurAmt);
        const double lpCoef = blurAmt < 1.0e-3 ? 1.0
            : 1.0 - std::exp (-twoPi * juce::jmin (cutoff, 0.45 * sr) / sr);

        const int  holdN     = juce::jlimit (1, 16, (int) std::lround (p.downsample));
        const int  qBits     = juce::jlimit (4, 24, (int) std::lround (p.bits));
        const bool crush     = qBits < 24;
        const float qLevels  = (float) (1 << (qBits - 1));
        const double mixT    = juce::jlimit (0.0f, 1.0f, p.mix);
        const double gainT   = juce::Decibels::decibelsToGain ((double) p.outDB);

        // moving filter
        const int    fType   = juce::jlimit (0, 3, p.filterType);
        const double logHzT  = std::log2 (juce::jlimit (20.0, 20000.0, (double) p.filterHz));
        const double resoT   = juce::jlimit (0.0f, 1.0f, p.filterReso);
        const double octaves = 4.0 * juce::jlimit (0.0f, 1.0f, p.sweepDepth);
        const double sweepHz = juce::jlimit (0.01, 20.0, (double) p.sweepHz);
        const int    shape   = juce::jlimit (0, 3, p.sweepShape);
        const float  fb      = juce::jlimit (0.0f, 0.95f, p.rumination);

        if (sLpCoef < 0.0)
        {
            sLpCoef = lpCoef; sMix = mixT; sGain = gainT; sWowAmp = wowAmp; sFlutAmp = flutAmp;
            sLogHz = logHzT; sReso = resoT;
        }

        const bool   useGrid    = p.sync && ht.hasPpq && ht.playing;
        const double ppqPerSamp = bpm / (60.0 * sr);
        const int    gateTickLen = juce::jmax (1, (int) (0.04 * sr));

        for (int n = 0; n < numSamples; ++n)
        {
            // ---- smoothing ------------------------------------------------
            sWowAmp  += (wowAmp  - sWowAmp)  * smoothK;
            sFlutAmp += (flutAmp - sFlutAmp) * smoothK;
            sLpCoef  += (lpCoef  - sLpCoef)  * smoothK;
            sMix     += (mixT    - sMix)     * smoothK;
            sGain    += (gainT   - sGain)    * smoothK;
            sLogHz   += (logHzT  - sLogHz)   * smoothK;
            sReso    += (resoT   - sReso)    * smoothK;

            // ---- write the present into memory -----------------------------
            // rumination: what came out one slice ago is written back, so the
            // memory keeps circling, a little more worn on every pass.
            const int w = (int) (now % bufLen);
            const int wr = (int) (now % ruLen);
            const int rd = (int) ((now - (int64_t) std::llround (slice) + ruLen) % ruLen);
            for (int ch = 0; ch < numCh; ++ch)
            {
                float in = buffer.getSample (ch, n);
                if (fb > 0.0f) in += std::tanh (fb * ruMem[(size_t) ch][(size_t) rd]);
                mem[(size_t) ch][(size_t) w] = in;
            }

            // ---- drift ----------------------------------------------------
            wowPhase += wowHz / sr;   if (wowPhase >= 1.0) wowPhase -= 1.0;
            flutterPhase += flutHz / sr; if (flutterPhase >= 1.0) flutterPhase -= 1.0;
            // delay ≥ 0 always: offset by the amplitudes, so drift 0 means no delay at all
            const double delay = sWowAmp  * (1.0 + std::sin (twoPi * wowPhase))
                               + sFlutAmp * (1.0 + std::sin (twoPi * flutterPhase));

            // ---- fragment / recall scheduling ------------------------------
            bool boundary;
            if (useGrid)
            {
                const double ppqHere = ht.ppq + n * ppqPerSamp;
                const auto idx = (int64_t) std::floor (ppqHere / sliceBeats);
                boundary = (idx != lastGridIndex);
                lastGridIndex = idx;
            }
            else
            {
                boundary = (freeCounter <= 0);
                if (boundary) freeCounter = (int64_t) std::llround (slice);
                --freeCounter;
                lastGridIndex = INT64_MIN;
            }

            if (boundary && ! stut.active && p.fragment > 0.001f
                && uni() < 0.45f * p.fragment)
                startRecall (slice, memSamps, p);

            // ---- forget: dropouts -----------------------------------------
            if (gateClosedFor > 0) --gateClosedFor;
            if (++gateTick >= gateTickLen)
            {
                gateTick = 0;
                if (gateClosedFor == 0 && p.forget > 0.001f && uni() < 0.3f * p.forget * p.forget + 0.05f * p.forget)
                    gateClosedFor = (int) ((0.03 + uni() * 0.25) * (0.5 + p.forget) * sr);
            }
            const float gateT = gateClosedFor > 0 ? 0.0f : 1.0f;
            gate += juce::jlimit (-fadeStep, fadeStep, gateT - gate);

            // ---- stutter envelope ------------------------------------------
            const float stutT = stut.active ? 1.0f : 0.0f;
            stutGain += juce::jlimit (-fadeStep, fadeStep, stutT - stutGain);

            // ---- filter modulation (shared by both channels) ----------------
            double svfG = 0.0, svfK = 2.0;
            if (fType != filterOff)
            {
                double inLevel = 0.0;
                for (int ch = 0; ch < numCh; ++ch) inLevel = juce::jmax (inLevel, (double) std::abs (buffer.getSample (ch, n)));
                env += (inLevel - env) * (inLevel > env ? envAtt : envRel);

                sweepPhase += sweepHz / sr;
                bool wrapped = false;
                if (sweepPhase >= 1.0) { sweepPhase -= 1.0; wrapped = true; }
                if (wrapped) { rndPrev = rndNext; rndNext = uni() * 2.0f - 1.0f; }

                double mod = 0.0;
                switch (shape)
                {
                    case shapeSine:  mod = std::sin (twoPi * sweepPhase); break;
                    case shapeDrift: mod = rndPrev + (rndNext - rndPrev) * 0.5 * (1.0 - std::cos (juce::MathConstants<double>::pi * sweepPhase)); break;
                    case shapeSteps: mod = rndNext; break;
                    default:         mod = juce::jlimit (0.0, 1.0, env * 3.0); break; // opens with the input
                }
                const double hz = juce::jlimit (20.0, 0.45 * sr, std::pow (2.0, sLogHz + octaves * mod));
                svfG = std::tan (juce::MathConstants<double>::pi * hz / sr);
                svfK = 2.0 - 1.96 * sReso;
            }

            // ---- per channel -----------------------------------------------
            const bool takeHold = (holdCounter == 0);
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x = buffer.getSample (ch, n);
                float y = read (ch, (double) now - delay);

                if (stutGain > 0.0f)
                {
                    const float s = recallSample (ch, delay);
                    y += (s - y) * stutGain;
                }

                // blur: two one-poles (12 dB/oct)
                const float c = (float) sLpCoef;
                lp1[(size_t) ch] += c * (y - lp1[(size_t) ch]);
                lp2[(size_t) ch] += c * (lp1[(size_t) ch] - lp2[(size_t) ch]);
                y = lp2[(size_t) ch];

                // resolution loss
                if (takeHold) held[(size_t) ch] = y;
                y = held[(size_t) ch];
                if (crush) y = std::round (y * qLevels) / qLevels;

                // rumination tap: the worn signal, before the moving filter
                ruMem[(size_t) ch][(size_t) wr] = y * gate;

                if (fType != filterOff)
                    y = softLimit ((float) svf (ch, y, svfG, svfK, fType));

                y *= gate;

                const float m = (float) sMix;
                buffer.setSample (ch, n, ((1.0f - m) * x + m * y) * (float) sGain);
            }

            holdCounter = (holdCounter + 1) % holdN;
            advanceRecall();
            ++now;
        }
    }

    bool isRecalling() const { return stut.active; }

private:
    struct Recall
    {
        bool    active    = false;
        int64_t start     = 0;   // absolute sample index of the source span start
        double  len       = 0;   // playback length of one loop
        double  span      = 0;   // source length = len * speed
        double  speed     = 1.0;
        bool    reverse   = false;
        double  pos       = 0;   // playback position inside the loop
        int     loopsLeft = 0;
        int     xfade     = 0;
    };

    void startRecall (double slice, double memSamps, const Params& p)
    {
        stut.speed   = 1.0;
        stut.reverse = uni() < p.reverse;
        if (uni() < p.warp)
        {
            // misremembered tempo: octave down, octave up, or a fifth up
            const float w = uni();
            stut.speed = w < 0.45f ? 0.5 : (w < 0.8f ? 2.0 : 1.5);
        }
        stut.len  = slice;
        stut.span = slice * stut.speed;

        double back = stut.span; // default: the span that just happened
        if (uni() < p.jumpProb)
        {
            // déjà-vu: somewhere further back in the memory window
            const double room = juce::jmax (0.0, memSamps - stut.span);
            back = stut.span + uni() * p.jumpDepth * room;
        }
        stut.active    = true;
        stut.start     = now - (int64_t) std::llround (back);
        stut.pos       = 0.0;
        // 1..4 repeats, but never so many that the source gets overwritten
        const int maxLoops = juce::jmax (1, (int) (2.0 * kMaxSliceSec * sr / slice));
        stut.loopsLeft = juce::jmin (maxLoops, 1 + (int) (uni() * (1.0f + 3.0f * p.fragment)));
        stut.xfade     = (int) juce::jmin (0.005 * sr, slice * 0.25);
    }

    float recallSample (int ch, double delay) const
    {
        const double base = (double) stut.start - delay;
        const bool plain = ! stut.reverse && stut.speed == 1.0;
        const double off = stut.reverse ? stut.span - stut.pos * stut.speed : stut.pos * stut.speed;
        float s = read (ch, base + off);

        if (plain)
        {
            // Fade the slice start in against the audio that originally
            // followed the slice end, so loop boundaries are seamless.
            if (stut.pos < stut.xfade)
            {
                const float t = (float) (stut.pos / stut.xfade);
                const float tail = read (ch, base + stut.len + stut.pos);
                s = tail + (s - tail) * t;
            }
        }
        else
        {
            // backwards or re-pitched loops have no natural continuation:
            // a short window at both ends keeps them click-free
            const double edge = juce::jmin (stut.pos, stut.len - stut.pos);
            if (edge < stut.xfade) s *= (float) juce::jmax (0.0, edge / stut.xfade);
        }
        return s;
    }

    void advanceRecall()
    {
        if (! stut.active) return;
        stut.pos += 1.0;
        if (stut.pos >= stut.len)
        {
            stut.pos -= stut.len;
            if (--stut.loopsLeft <= 0) stut.active = false; // envelope fades it out
        }
    }

    // Topology-preserving state-variable filter (Zavalishin)
    double svf (int ch, double v0, double g, double k, int type)
    {
        auto& a = ic1[(size_t) ch];
        auto& b = ic2[(size_t) ch];
        const double a1 = 1.0 / (1.0 + g * (g + k));
        const double a2 = g * a1;
        const double a3 = g * a2;
        const double v3 = v0 - b;
        const double v1 = a1 * a + a2 * v3;
        const double v2 = b + a2 * a + a3 * v3;
        a = 2.0 * v1 - a;
        b = 2.0 * v2 - b;
        switch (type)
        {
            case filterLP: return v2;
            // Bandpass: level-compensated so narrow settings don't vanish
            // (energy of v1·√k stays roughly constant over resonance) + 6 dB.
            case filterBP: return 2.0 * v1 * std::sqrt (k);
            default:       return v0 - k * v1 - v2; // HP
        }
    }

    // Transparent below 0.7, smooth (C1) saturation towards ±1 above —
    // keeps self-oscillating resonance from blowing up the output.
    static float softLimit (float x)
    {
        const float ax = std::abs (x);
        if (ax <= 0.7f) return x;
        return std::copysign (0.7f + 0.3f * std::tanh ((ax - 0.7f) / 0.3f), x);
    }

    float read (int ch, double absPos) const
    {
        // never read the future or beyond the buffer
        absPos = juce::jlimit ((double) (now - bufLen + 2), (double) now, absPos);
        const double fl = std::floor (absPos);
        const auto i0 = (int64_t) fl;
        const float frac = (float) (absPos - fl);
        const auto& b = mem[(size_t) ch];
        const float a = b[(size_t) (((i0 % bufLen) + bufLen) % bufLen)];
        if (frac <= 0.0f) return a;
        const float c = b[(size_t) ((((i0 + 1) % bufLen) + bufLen) % bufLen)];
        return a + (c - a) * frac;
    }

    float uni() { return dist (rng); }

    double sr = 44100.0;
    int channels = 2, bufLen = 1;
    std::vector<std::vector<float>> mem;
    std::vector<float> lp1, lp2, held;
    std::vector<std::vector<float>> ruMem;   // worn output, for rumination
    int ruLen = 1;
    std::vector<double> ic1, ic2;
    int64_t now = 0;

    double smoothK = 0.001, envAtt = 0.01, envRel = 0.001;
    float  fadeStep = 0.004f;
    double wowPhase = 0.0, flutterPhase = 0.0;
    double sWowAmp = 0, sFlutAmp = 0, sLpCoef = -1, sMix = -1, sGain = -1, sLogHz = -1, sReso = -1;
    int holdCounter = 0;

    Recall stut;
    float stutGain = 0.0f;
    int64_t freeCounter = 0, lastGridIndex = INT64_MIN;

    float gate = 1.0f;
    int gateClosedFor = 0, gateTick = 0;

    double sweepPhase = 0.0, env = 0.0;
    float rndPrev = 0.0f, rndNext = 0.0f;

    std::mt19937 rng { 0x7e0c0a5u };
    std::uniform_real_distribution<float> dist { 0.0f, 1.0f };
};

} // namespace mm
