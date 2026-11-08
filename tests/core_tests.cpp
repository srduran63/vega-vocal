// Measures the DSP itself: correct pitch, transparency, sibilance reduction, dynamics, stability.
#include "VocalCore.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <random>

using namespace vocal;

static int failures = 0;

static void check (const char* name, double actual, double expected, double tolerance)
{
    const bool ok = std::abs (actual - expected) <= tolerance;
    std::printf ("%s %-58s got %11.4f  want %11.4f (+/-%g)\n", ok ? "PASS" : "FAIL", name, actual, expected, tolerance);
    if (! ok)
        ++failures;
}

// Harmonically rich vowel-like tone with a formant-ish spectral slope.
static std::vector<float> voice (double hz, double sr, int length, float amp = 0.25f)
{
    std::vector<float> v ((size_t) length);
    double phase = 0.0;
    for (int i = 0; i < length; ++i)
    {
        phase += hz / sr;
        double s = 0.0;
        for (int h = 1; h <= 12 && h * hz < sr * 0.45; ++h)
            s += std::sin (2.0 * 3.14159265358979 * phase * h) / (1.0 + 0.5 * h);
        v[(size_t) i] = amp * (float) s;
    }
    return v;
}

static double rms (const std::vector<float>& v, size_t from, size_t to)
{
    double sum = 0;
    for (size_t i = from; i < to; ++i)
        sum += v[i] * v[i];
    return std::sqrt (sum / (double) (to - from));
}

// Estimates pitch from zero crossings of the lowpassed fundamental, independently of the plugin's detector.
static double measureHz (const std::vector<float>& v, size_t from, size_t to, double sr, double guessHz)
{
    Biquad lp;
    lp.setLowPass (guessHz * 1.4, sr);
    Biquad lp2 = lp;
    double first = -1, last = -1;
    int crossings = 0;
    float previous = 0;
    for (size_t i = 0; i < to; ++i)
    {
        const float y = lp2.process (lp.process (v[i]));
        if (i >= from && previous < 0 && y >= 0)
        {
            const double at = (double) i - (double) y / (double) (y - previous);
            if (first < 0) first = at; else ++crossings;
            last = at;
        }
        previous = y;
    }
    return crossings > 0 ? sr * crossings / (last - first) : 0.0;
}

// Short-window pitch for fast-moving notes, where counting zero crossings has too few cycles to work with.
static double autocorrelationHz (const std::vector<float>& v, size_t at, int window, double sr)
{
    const int low = (int) (sr / 300.0), high = (int) (sr / 160.0);
    std::vector<double> r ((size_t) high + 2);
    for (int lag = low - 1; lag <= high + 1; ++lag)
    {
        double sum = 0, energy = 0;
        for (int i = 0; i < window; ++i)
        {
            sum += v[at + (size_t) i] * v[at + (size_t) (i + lag)];
            energy += v[at + (size_t) (i + lag)] * v[at + (size_t) (i + lag)];
        }
        r[(size_t) lag] = sum / std::sqrt (energy + 1.0e-12);
    }
    int best = low;
    for (int lag = low; lag <= high; ++lag)
        if (r[(size_t) lag] > r[(size_t) best])
            best = lag;
    const double a = r[(size_t) best - 1], b = r[(size_t) best], c = r[(size_t) best + 1], d = a - 2.0 * b + c;
    return sr / ((double) best + (d != 0.0 ? 0.5 * (a - c) / d : 0.0));
}

static double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

static Params dryParams()
{
    Params p;
    p.tuneOn = p.deEssOn = p.compOn = p.satOn = p.delayOn = p.reverbOn = false;
    p.tuneAmount = 0; p.compMakeupDb = 0; p.satDrive = 0; p.delayMix = p.reverbMix = 0; p.duck = 0;
    p.lowCutHz = 20;
    return p;
}

static std::vector<float> render (VocalChain& chain, const std::vector<float>& in, int block = 256, std::vector<float>* rightOut = nullptr)
{
    std::vector<float> l (in), r (in);
    for (size_t pos = 0; pos < l.size(); pos += (size_t) block)
        chain.process (l.data() + pos, r.data() + pos, (int) std::min ((size_t) block, l.size() - pos));
    if (rightOut) *rightOut = r;
    return l;
}

int main()
{
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::printf ("\n--- %.0f Hz ---\n", sr);
        const int len = (int) (sr * 2.5);
        const auto from = (size_t) (sr * 1.0), to = (size_t) (sr * 2.4);

        // Everything off: the chain must be a clean delay of exactly latency() samples.
        {
            auto chain = std::make_unique<VocalChain>();
            chain->prepare (sr);
            chain->setParams (dryParams());
            std::mt19937 rng (1);
            std::uniform_real_distribution<float> dist (-0.3f, 0.3f);
            std::vector<float> in ((size_t) len);
            for (auto& s : in) s = dist (rng);
            const auto out = render (*chain, in);
            double errorPeak = 0;
            const int d = chain->latency();
            // HP at 20 Hz removes a sliver of very low noise; compare after matching the same filter.
            Biquad hp;
            hp.setHighPass (20.0, sr);
            std::vector<float> reference (in.size());
            for (size_t i = 0; i < in.size(); ++i) reference[i] = hp.process (in[i]);
            for (size_t i = (size_t) d + 100; i < out.size(); ++i)
                errorPeak = std::max (errorPeak, (double) std::abs (out[i] - reference[i - (size_t) d]));
            check ("all off: null against delayed input (dB, <= -80)", std::max (0.0, gainToDb ((float) errorPeak) + 80.0), 0.0, 0.0);
            check ("latency is under 40 ms", std::min (0.0, 40.0 - 1000.0 * d / sr), 0.0, 0.0);
        }

        // Tuning: sung 30 cents flat of A3 (and others) must land on the note, in several registers.
        for (const double note : { 45.0, 57.0, 64.0, 69.0, 76.0 })
        {
            for (const double offset : { -30.0, 35.0 })
            {
                const double targetHz = 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
                const double inputHz = targetHz * std::pow (2.0, offset / 1200.0);
                auto chain = std::make_unique<VocalChain>();
                chain->prepare (sr);
                auto p = dryParams();
                p.tuneOn = true;
                p.tuneAmount = 1;
                p.tuneSpeedMs = 10;
                chain->setParams (p);
                const auto out = render (*chain, voice (inputHz, sr, len));
                char name[80];
                std::snprintf (name, sizeof name, "tune: note %d, %+.0f c in -> on pitch (cents)", (int) note, offset);
                check (name, cents (measureHz (out, from, to, sr, targetHz), targetHz), 0.0, 6.0);
                std::snprintf (name, sizeof name, "tune: note %d, %+.0f c level held (dB)", (int) note, offset);
                check (name, gainToDb ((float) (rms (out, from, to) / rms (voice (inputHz, sr, len), from, to))), 0.0, 1.5);
            }
        }

        // Scale: C major has no C#, so a sung C# must go to a neighbouring scale note.
        {
            auto chain = std::make_unique<VocalChain>();
            chain->prepare (sr);
            auto p = dryParams();
            p.tuneOn = true; p.tuneAmount = 1; p.tuneSpeedMs = 10; p.key = 0; p.scale = major;
            chain->setParams (p);
            const double csHz = 440.0 * std::pow (2.0, (61.2 - 69.0) / 12.0); // C#4, slightly sharp -> D4
            const double dHz = 440.0 * std::pow (2.0, (62.0 - 69.0) / 12.0);
            const auto out = render (*chain, voice (csHz, sr, len));
            check ("scale: C# in C major moves to D (cents)", cents (measureHz (out, from, to, sr, dHz), dHz), 0.0, 8.0);
        }

        // Amount 50%: half of the correction.
        {
            auto chain = std::make_unique<VocalChain>();
            chain->prepare (sr);
            auto p = dryParams();
            p.tuneOn = true; p.tuneAmount = 0.5f; p.tuneSpeedMs = 10;
            chain->setParams (p);
            const double hz = 220.0 * std::pow (2.0, 40.0 / 1200.0);
            const auto out = render (*chain, voice (hz, sr, len));
            check ("amount 50%: 40 c sharp ends up 20 c sharp", cents (measureHz (out, from, to, sr, 220.0), 220.0), 20.0, 6.0);
        }

        // Note changes and vibrato: what a real phrase does, unlike a steady test tone.
        {
            std::vector<float> in ((size_t) len);
            double phase = 0;
            const auto synth = [&] (double hz, int i)
            {
                phase += hz / sr;
                double y = 0;
                for (int h = 1; h <= 10 && h * hz < sr * 0.45; ++h)
                    y += std::sin (2.0 * 3.14159265358979 * phase * h) / (1.0 + 0.5 * h);
                in[(size_t) i] = 0.25f * (float) y;
            };
            // Sings 25 cents flat, stepping between A3 and C4 every 300 ms.
            const double flat = std::pow (2.0, -25.0 / 1200.0);
            for (int i = 0; i < len; ++i)
                synth ((((int) (i / (sr * 0.3))) % 2 == 0 ? 220.0 : 261.626) * flat, i);

            auto chain = std::make_unique<VocalChain>();
            chain->prepare (sr);
            auto p = dryParams();
            p.tuneOn = true; p.tuneAmount = 1; p.tuneSpeedMs = 10;
            chain->setParams (p);
            const auto out = render (*chain, in);

            // The last 150 ms of each A3 note must be in tune.
            const int d = chain->latency();
            double worstCents = 0;
            for (int n = 4; n <= 6; n += 2)
            {
                const auto a = (size_t) (sr * 0.3 * n + sr * 0.13 + d), b = (size_t) (sr * 0.3 * n + sr * 0.29 + d);
                worstCents = std::max (worstCents, std::abs (cents (measureHz (out, a, b, sr, 220.0), 220.0)));
            }
            check ("phrase: stepped notes land in tune (cents)", worstCents, 0.0, 12.0);

            // A click shows up as a sample jump far bigger than the waveform itself ever makes.
            float inputJump = 0, outputJump = 0;
            for (size_t i = 1; i < in.size(); ++i)
            {
                inputJump = std::max (inputJump, std::abs (in[i] - in[i - 1]));
                if (i > from / 4)
                    outputJump = std::max (outputJump, std::abs (out[i] - out[i - 1]));
            }
            check ("phrase: no clicks (largest jump vs input, at most 2x)", std::max (0.0, (double) (outputJump / inputJump) - 2.0), 0.0, 0.0);
        }
        {
            // Hard tune flattens a 5 Hz, +/-40 cent vibrato; slow tune lets most of it through.
            const auto vibratoDepth = [&] (float speedMs)
            {
                std::vector<float> in ((size_t) len);
                double phase = 0;
                for (int i = 0; i < len; ++i)
                {
                    const double hz = 220.0 * std::pow (2.0, 40.0 * std::sin (2.0 * 3.14159265358979 * 5.0 * i / sr) / 1200.0);
                    phase += hz / sr;
                    double y = 0;
                    for (int h = 1; h <= 10 && h * hz < sr * 0.45; ++h)
                        y += std::sin (2.0 * 3.14159265358979 * phase * h) / (1.0 + 0.5 * h);
                    in[(size_t) i] = 0.25f * (float) y;
                }
                auto chain = std::make_unique<VocalChain>();
                chain->prepare (sr);
                auto p = dryParams();
                p.tuneOn = true; p.tuneAmount = 1; p.tuneSpeedMs = speedMs; p.key = 9; p.scale = minor;
                chain->setParams (p);
                const auto out = render (*chain, in);
                double low = 1e9, high = -1e9;
                for (double t = 1.0; t < 2.3; t += 0.01)
                {
                    const double c = cents (autocorrelationHz (out, (size_t) (t * sr), (int) (sr * 0.012), sr), 220.0);
                    low = std::min (low, c);
                    high = std::max (high, c);
                }
                return high - low;
            };
            const double hard = vibratoDepth (0.0f), slow = vibratoDepth (300.0f);
            check ("vibrato: hard tune flattens it (spread, at most 25 c)", std::max (0.0, hard - 25.0), 0.0, 0.0);
            check ("vibrato: slow tune keeps it (spread, at least 45 c)", std::min (0.0, slow - 45.0), 0.0, 0.0);
        }

        // De-esser: sibilant burst drops, vowel does not.
        {
            std::mt19937 rng (2);
            std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
            Biquad hp; hp.setHighPass (6500.0, sr);
            std::vector<float> ess ((size_t) len);
            for (auto& s : ess) s = hp.process (dist (rng)) * 0.3f;
            const auto vowel = voice (220.0, sr, len);

            auto measure = [&] (const std::vector<float>& input)
            {
                auto chain = std::make_unique<VocalChain>();
                chain->prepare (sr);
                auto p = dryParams();
                p.deEssOn = true; p.deEss = 0.7f;
                chain->setParams (p);
                return rms (render (*chain, input), from, to) / rms (input, from, to);
            };
            check ("de-esser: sibilance reduced at least 6 dB", std::min (0.0, -6.0 - gainToDb ((float) measure (ess))), 0.0, 0.0);
            check ("de-esser: vowel untouched (dB)", gainToDb ((float) measure (vowel)), 0.0, 0.3);
        }

        // Compressor: 4:1 above threshold, using a steady tone.
        {
            auto chain = std::make_unique<VocalChain>();
            chain->prepare (sr);
            auto p = dryParams();
            p.compOn = true; p.compThreshold = -30; p.compRatio = 4; p.compMakeupDb = 0;
            chain->setParams (p);
            std::vector<float> tone ((size_t) len);
            for (int i = 0; i < len; ++i) tone[(size_t) i] = 0.5f * (float) std::sin (2.0 * kPi * 500.0 * i / sr);
            const auto out = render (*chain, tone);
            // Peak of the tone is -6 dBFS, 24 dB over: wants 18 dB of reduction (ripple allows a little).
            check ("compressor: 4:1 at 24 dB over gives ~18 dB", gainToDb ((float) (rms (out, from, to) / rms (tone, from, to))), -18.0, 2.0);
        }

        // Saturation adds harmonics, keeps level reasonable, and stays bounded.
        {
            auto chain = std::make_unique<VocalChain>();
            chain->prepare (sr);
            auto p = dryParams();
            p.satOn = true; p.satDrive = 0.6f;
            chain->setParams (p);
            std::vector<float> tone ((size_t) len);
            for (int i = 0; i < len; ++i) tone[(size_t) i] = 0.3f * (float) std::sin (2.0 * kPi * 440.0 * i / sr);
            const auto out = render (*chain, tone);
            const auto harmonic = [&] (double hz)
            {
                double sine = 0, cosine = 0;
                for (size_t i = from; i < to; ++i)
                {
                    sine += out[i] * std::sin (2.0 * 3.14159265358979 * hz * (double) i / sr);
                    cosine += out[i] * std::cos (2.0 * 3.14159265358979 * hz * (double) i / sr);
                }
                return 2.0 * std::sqrt (sine * sine + cosine * cosine) / (double) (to - from);
            };
            const double second = harmonic (880.0), third = harmonic (1320.0);
            check ("saturation: 3rd harmonic above -40 dB", std::max (0.0, -40.0 - gainToDb ((float) third)), 0.0, 0.0);
            check ("saturation: 2nd harmonic above -55 dB", std::max (0.0, -55.0 - gainToDb ((float) second)), 0.0, 0.0);
            check ("saturation: level within 4 dB of input", gainToDb ((float) (rms (out, from, to) / rms (tone, from, to))), 0.0, 4.0);
        }

        // Delay: an impulse comes back at the requested time.
        {
            auto chain = std::make_unique<VocalChain>();
            chain->prepare (sr);
            auto p = dryParams();
            p.delayOn = true; p.delayMs = 250; p.delayMix = 1; p.delayFeedback = 0; p.delaySpread = 0;
            chain->setParams (p);
            // Run the mix smoother in before the impulse arrives.
            std::vector<float> settle ((size_t) (sr * 0.5), 0.0f);
            render (*chain, settle);
            std::vector<float> in ((size_t) sr, 0.0f);
            in[0] = 1.0f;
            const auto out = render (*chain, in);
            size_t peakAt = 0; float peak = 0;
            for (size_t i = (size_t) chain->latency() + 10; i < out.size(); ++i)
                if (std::abs (out[i]) > peak) { peak = std::abs (out[i]); peakAt = i; }
            check ("delay: echo at 250 ms", 1000.0 * ((double) peakAt - chain->latency()) / sr, 250.0, 1.0);
        }

        // Reverb: decay time roughly follows the setting, tail really dies out.
        {
            auto chain = std::make_unique<VocalChain>();
            chain->prepare (sr);
            auto p = dryParams();
            p.reverbOn = true; p.reverbMix = 1; p.reverbDecay = 1.0f; p.reverbSize = 0.5f; p.reverbDamp = 0.1f;
            chain->setParams (p);
            std::vector<float> settle ((size_t) (sr * 0.2), 0.0f);
            render (*chain, settle);
            std::vector<float> in ((size_t) (sr * 3.0), 0.0f);
            for (int i = 0; i < 100; ++i) in[(size_t) i] = 0.5f;
            const auto out = render (*chain, in);
            const double early = rms (out, (size_t) (sr * 0.2), (size_t) (sr * 0.3));
            const double later = rms (out, (size_t) (sr * 0.7), (size_t) (sr * 0.8));
            const double slope = gainToDb ((float) (later / early)) / 0.5; // dB per second
            check ("reverb: decays about 60 dB/s at 1 s setting", -slope, 60.0, 18.0);
            check ("reverb: tail gone after 2.8 s (below -70 dB)", std::min (0.0, -70.0 - gainToDb ((float) rms (out, (size_t) (sr * 2.8), (size_t) (sr * 2.95)))), 0.0, 0.0);
        }

        // Full chain, loud and silent bursts, wild settings: never NaN, never over full scale.
        {
            auto chain = std::make_unique<VocalChain>();
            chain->prepare (sr);
            Params p;
            p.tuneSpeedMs = 0; p.scale = minor; p.key = 9; p.satDrive = 1; p.delayOn = true; p.delayFeedback = 0.95f;
            p.delayMix = 1; p.reverbMix = 1; p.reverbDecay = 8; p.reverbSize = 1; p.compMakeupDb = 12; p.autoLevel = true; p.inputDb = 12;
            chain->setParams (p);
            std::mt19937 rng (3);
            std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
            std::vector<float> in ((size_t) (sr * 8.0));
            for (size_t i = 0; i < in.size(); ++i)
            {
                const int section = (int) ((double) i / sr) % 4;
                in[i] = section == 0 ? 0.9f * (float) std::sin (2.0 * kPi * 233.0 * (double) i / sr)
                      : section == 1 ? dist (rng)
                      : section == 2 ? 0.0f
                                     : (i % 4000 == 0 ? 1.0f : 0.0f);
            }
            std::vector<float> right;
            const auto out = render (*chain, in, 777, &right);
            int bad = 0;
            float peak = 0;
            for (size_t i = 0; i < out.size(); ++i)
            {
                if (! std::isfinite (out[i]) || ! std::isfinite (right[i])) ++bad;
                peak = std::max ({ peak, std::abs (out[i]), std::abs (right[i]) });
            }
            check ("stress: no NaN or Inf", bad, 0, 0);
            check ("stress: output never above 0 dBFS", std::max (0.0, gainToDb (peak) - 0.0), 0.0, 0.0);
        }

        // CPU: every module on must leave plenty of room for a session with many vocal tracks.
        {
            auto chain = std::make_unique<VocalChain>();
            chain->prepare (sr);
            Params p;
            p.scale = minor; p.key = 9; p.delayOn = true; p.reverbOn = true;
            chain->setParams (p);
            const auto in = voice (233.0, sr, (int) (sr * 4.0));
            const auto start = std::chrono::steady_clock::now();
            render (*chain, in, 256);
            const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            const double speed = 4.0 / seconds;
            std::printf ("     full chain runs at %.0fx real time\n", speed);
            check ("cpu: at least 15x real time", std::min (0.0, speed - 15.0), 0.0, 0.0);
        }

        // Auto-Set hears a quiet singer in A minor and fixes gain and key.
        {
            auto chain = std::make_unique<VocalChain>();
            chain->prepare (sr);
            chain->setParams (dryParams());
            constexpr std::array<int, 16> melody { 69, 72, 76, 69, 74, 72, 71, 69, 69, 76, 72, 69, 71, 74, 72, 69 };
            std::vector<float> in;
            for (int repeat = 0; repeat < 2; ++repeat)
                for (const int note : melody)
                {
                    const auto tone = voice (440.0 * std::pow (2.0, (note - 69.0) / 12.0), sr, (int) (sr * 0.4), 0.03f);
                    in.insert (in.end(), tone.begin(), tone.end());
                    in.insert (in.end(), (size_t) (sr * 0.05), 0.0f);
                }
            chain->requestLearn();
            render (*chain, in);
            LearnResult result;
            const bool done = chain->takeLearnResult (result);
            check ("auto-set: finished", done && result.valid ? 1 : 0, 1, 0);
            check ("auto-set: A minor key root (9)", result.key, 9, 0);
            check ("auto-set: minor scale", result.scale, minor, 0);
            check ("auto-set: input gain raises quiet voice", std::min (0.0, (double) result.inputDb - 6.0), 0.0, 0.0);
        }

        // Auto-Set with silence reports no result instead of inventing one.
        {
            auto chain = std::make_unique<VocalChain>();
            chain->prepare (sr);
            chain->setParams (dryParams());
            std::vector<float> silence ((size_t) (sr * (VoiceLearner::timeoutSeconds + 0.5)), 0.0f);
            chain->requestLearn();
            render (*chain, silence, 4096);
            LearnResult result;
            const bool done = chain->takeLearnResult (result);
            check ("auto-set: silence ends with no valid result", done && ! result.valid ? 1 : 0, 1, 0);
        }
    }

    std::printf (failures == 0 ? "\nALL CORE TESTS PASSED\n" : "\n%d CORE TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
