// Vega Vocal signal chain. Plain C++ with no JUCE dependency so it can be tested on its own.
// Nothing here allocates after prepare(), so process() is safe on the audio thread.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vocal
{
constexpr float kPi = 3.14159265358979f;
constexpr float kSilenceDb = -120.0f;

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g) { return g > 1.0e-6f ? 20.0f * std::log10 (g) : kSilenceDb; }
inline float timeCoeff (float seconds, double sr)
{
    return seconds <= 0.0f ? 0.0f : std::exp (-1.0f / (seconds * (float) sr));
}

// ---------------------------------------------------------------------------------------------
// Scales
// ---------------------------------------------------------------------------------------------
enum Scale { chromatic, major, minor, harmonicMinor, pentatonicMajor, pentatonicMinor, numScales };

inline uint16_t scaleMask (int scale, int key)
{
    static constexpr std::array<uint16_t, numScales> masks {
        0b111111111111, // chromatic
        0b101010110101, // major: bit n is n semitones above the root
        0b010110101101, // natural minor
        0b100110101101, // harmonic minor
        0b001010010101, // major pentatonic
        0b010010101001, // minor pentatonic
    };
    const uint32_t m = masks[(size_t) std::clamp (scale, 0, numScales - 1)];
    const int k = ((key % 12) + 12) % 12;
    return (uint16_t) (((m << k) | (m >> (12 - k))) & 0xfff);
}

// Nearest allowed note. Staying on `current` wins until the pitch is clearly closer to another
// note, so held notes that sit between two scale degrees do not flip back and forth.
inline int nearestNote (float midi, uint16_t mask, int current = -1)
{
    int best = -1;
    float bestDistance = 1.0e9f;
    for (int n = (int) midi - 13; n <= (int) midi + 13; ++n)
    {
        if (n < 0 || ! ((mask >> (n % 12)) & 1))
            continue;
        const float distance = std::abs (midi - (float) n) - (n == current ? 0.12f : 0.0f);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = n;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------------------------
// Filters
// ---------------------------------------------------------------------------------------------
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }

    void set (double a0, double nb0, double nb1, double nb2, double na1, double na2)
    {
        b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb2 / a0);
        a1 = (float) (na1 / a0); a2 = (float) (na2 / a0);
    }
    void setHighPass (double f, double sr, double q = 0.7071)
    {
        const double w = 2.0 * 3.14159265358979 * std::min (f, sr * 0.45) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al, (1 + c) / 2, -(1 + c), (1 + c) / 2, -2 * c, 1 - al);
    }
    void setLowPass (double f, double sr, double q = 0.7071)
    {
        const double w = 2.0 * 3.14159265358979 * std::min (f, sr * 0.45) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al, (1 - c) / 2, 1 - c, (1 - c) / 2, -2 * c, 1 - al);
    }
    void setLowShelf (double f, double sr, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * 3.14159265358979 * f / sr, c = std::cos (w);
        const double al = std::sin (w) / 2 * std::sqrt (2.0), r = 2 * std::sqrt (A) * al;
        set ((A + 1) + (A - 1) * c + r, A * ((A + 1) - (A - 1) * c + r), 2 * A * ((A - 1) - (A + 1) * c),
             A * ((A + 1) - (A - 1) * c - r), -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - r);
    }
    void setHighShelf (double f, double sr, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * 3.14159265358979 * std::min (f, sr * 0.45) / sr, c = std::cos (w);
        const double al = std::sin (w) / 2 * std::sqrt (2.0), r = 2 * std::sqrt (A) * al;
        set ((A + 1) - (A - 1) * c + r, A * ((A + 1) + (A - 1) * c + r), -2 * A * ((A - 1) + (A + 1) * c),
             A * ((A + 1) + (A - 1) * c - r), 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - r);
    }
};

// ---------------------------------------------------------------------------------------------
// Pitch detection (YIN) on a decimated mono signal
// ---------------------------------------------------------------------------------------------
class PitchDetector
{
public:
    static constexpr float minHz = 80.0f, maxHz = 1000.0f;

    void prepare (double sampleRate)
    {
        factor = std::max (1, (int) std::lround (sampleRate / 16000.0));
        rate = sampleRate / factor;
        maxLag = (int) std::ceil (rate / minHz) + 2;
        minLag = std::max (2, (int) std::floor (rate / maxHz));
        window = maxLag;
        history.assign ((size_t) (window + maxLag + 2), 0.0f);
        diff.assign ((size_t) (maxLag + 2), 0.0f);
        hop = std::max (1, (int) (rate * 0.006));
        antiAlias.setLowPass (std::min (5000.0, rate * 0.4), sampleRate);
        reset();
    }

    void reset()
    {
        std::fill (history.begin(), history.end(), 0.0f);
        antiAlias.reset();
        phase = sinceAnalysis = 0;
        voicedFrames = 0;
        hz = 0.0f;
        voiced = false;
    }

    // Returns true when a new estimate is available.
    bool push (float x)
    {
        const float filtered = antiAlias.process (x);
        if (++phase < factor)
            return false;
        phase = 0;
        std::move (history.begin() + 1, history.end(), history.begin());
        history.back() = filtered;
        if (++sinceAnalysis < hop)
            return false;
        sinceAnalysis = 0;
        analyse();
        return true;
    }

    bool isVoiced() const { return voiced; }
    float getHz() const { return hz; }

private:
    void analyse()
    {
        const int size = (int) history.size();
        const float* newest = history.data() + size - window; // compare the latest window with earlier ones

        float energy = 0.0f;
        for (int i = 0; i < window; ++i)
            energy += newest[i] * newest[i];

        bool found = false;
        float estimate = 0.0f;
        if (energy / (float) window > 1.0e-6f) // about -60 dBFS
        {
            diff[0] = 1.0f;
            float running = 0.0f;
            int picked = -1;
            for (int lag = 1; lag <= maxLag; ++lag)
            {
                float sum = 0.0f;
                const float* older = newest - lag;
                for (int i = 0; i < window; ++i)
                {
                    const float d = newest[i] - older[i];
                    sum += d * d;
                }
                running += sum;
                diff[(size_t) lag] = running > 0.0f ? sum * (float) lag / running : 1.0f;
            }
            constexpr float threshold = 0.15f;
            for (int lag = minLag; lag < maxLag; ++lag)
            {
                if (diff[(size_t) lag] < threshold)
                {
                    while (lag + 1 < maxLag && diff[(size_t) lag + 1] < diff[(size_t) lag])
                        ++lag;
                    picked = lag;
                    break;
                }
            }
            if (picked > 0)
            {
                const float a = diff[(size_t) picked - 1], b = diff[(size_t) picked], c = diff[(size_t) picked + 1];
                const float denominator = a - 2.0f * b + c;
                const float offset = std::abs (denominator) > 1.0e-9f ? 0.5f * (a - c) / denominator : 0.0f;
                estimate = (float) rate / ((float) picked + std::clamp (offset, -1.0f, 1.0f));
                found = estimate >= minHz * 0.9f && estimate <= maxHz * 1.1f;
            }
        }

        // A couple of frames of hysteresis keep consonants from fluttering the voiced state.
        voicedFrames = found ? std::min (voicedFrames + 1, 3) : std::max (voicedFrames - 1, 0);
        if (found)
            hz = estimate;
        voiced = voicedFrames >= 2 || (voiced && voicedFrames >= 1);
    }

    int factor = 1, maxLag = 200, minLag = 16, window = 200, hop = 96, phase = 0, sinceAnalysis = 0, voicedFrames = 0;
    double rate = 16000.0;
    float hz = 0.0f;
    bool voiced = false;
    std::vector<float> history, diff;
    Biquad antiAlias;
};

// ---------------------------------------------------------------------------------------------
// Pitch-synchronous overlap-add shifter. Moves the pitch without moving the formants, so the
// voice does not turn chipmunk. With ratio == 1 it is a perfect delay of exactly latency() samples.
// ---------------------------------------------------------------------------------------------
class PsolaShifter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        maxPeriod = sr / PitchDetector::minHz;
        minPeriod = sr / (PitchDetector::maxHz * 1.1);
        delay = (int) std::ceil (maxPeriod * 2.75) + 4;
        int size = 1;
        while (size < delay * 2 + (int) (maxPeriod * 4) + 16)
            size <<= 1;
        mask = size - 1;
        for (int c = 0; c < 2; ++c)
        {
            input[(size_t) c].assign ((size_t) size, 0.0f);
            overlap[(size_t) c].assign ((size_t) size, 0.0f);
        }
        norm.assign ((size_t) size, 0.0f);
        idlePeriod = std::round (sr * 0.005);
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (input[(size_t) c].begin(), input[(size_t) c].end(), 0.0f);
            std::fill (overlap[(size_t) c].begin(), overlap[(size_t) c].end(), 0.0f);
        }
        std::fill (norm.begin(), norm.end(), 0.0f);
        now = 0;
        nextMark = 0.0;
        lastAnalysis = -(double) delay;
        wasCoherent = false;
        period = idlePeriod;
        ratio = 1.0;
    }

    int latency() const { return delay; }

    // ratioIn == 1 (or unvoiced) passes the signal through untouched.
    void setPitch (bool voicedIn, double periodSamples, double ratioIn)
    {
        const bool shifting = voicedIn && std::abs (ratioIn - 1.0) > 1.0e-4;
        coherent = shifting;
        ratio = shifting ? std::clamp (ratioIn, 0.7, 1.45) : 1.0;
        period = shifting ? std::clamp (periodSamples, minPeriod, maxPeriod) : idlePeriod;
    }

    void process (float& left, float& right)
    {
        const int64_t writeIndex = now & mask;
        input[0][(size_t) writeIndex] = left;
        input[1][(size_t) writeIndex] = right;

        while (nextMark - period <= (double) now)
            emitGrain();

        const float weight = norm[(size_t) writeIndex];
        const float scale = weight > 0.1f ? 1.0f / weight : 10.0f;
        left = overlap[0][(size_t) writeIndex] * scale;
        right = overlap[1][(size_t) writeIndex] * scale;
        overlap[0][(size_t) writeIndex] = overlap[1][(size_t) writeIndex] = norm[(size_t) writeIndex] = 0.0f;
        ++now;
    }

private:
    float readInput (int channel, double position) const
    {
        const double floored = std::floor (position);
        const auto i = (int64_t) floored;
        const auto t = (float) (position - floored);
        const auto& buffer = input[(size_t) channel];
        if (t == 0.0f)
            return buffer[(size_t) (i & mask)];
        const float y0 = buffer[(size_t) ((i - 1) & mask)], y1 = buffer[(size_t) (i & mask)];
        const float y2 = buffer[(size_t) ((i + 1) & mask)], y3 = buffer[(size_t) ((i + 2) & mask)];
        return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
    }

    float monoAt (int64_t i) const { return input[0][(size_t) (i & mask)] + input[1][(size_t) (i & mask)]; }

    double pickAnalysisCentre (double mark) const
    {
        const double target = mark - (double) delay;
        if (! coherent)
            return target;

        // Jump from the previous grain by a whole number of periods: that is what keeps
        // neighbouring grains in phase while the output runs at a different pitch.
        if (! wasCoherent)
            return target;
        const double steps = std::round ((target - lastAnalysis) / period);
        double centre = lastAnalysis + steps * period;

        if (steps != 0.0)
        {
            // The period estimate is never perfect, so line the waveform up with the last grain.
            const int range = std::max (1, (int) (period * 0.125));
            const int length = (int) period;
            const auto base = (int64_t) centre, previous = (int64_t) lastAnalysis;
            float bestScore = -1.0e30f;
            int bestOffset = 0;
            for (int offset = -range; offset <= range; ++offset)
            {
                float score = 0.0f;
                for (int i = -length / 2; i < length / 2; i += 2)
                    score += monoAt (base + offset + i) * monoAt (previous + i);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestOffset = offset;
                }
            }
            centre += bestOffset;
        }
        return centre;
    }

    void emitGrain()
    {
        const double mark = nextMark;
        const double half = period;
        // Never read input that has not arrived yet.
        const double centre = std::min (pickAnalysisCentre (mark), (double) now - half - 2.0);

        const auto first = std::max ((int64_t) std::ceil (mark - half), now);
        const auto last = (int64_t) std::floor (mark + half);
        for (int64_t i = first; i <= last; ++i)
        {
            const double offset = (double) i - mark;
            const auto w = (float) std::cos (0.5 * 3.14159265358979 * offset / half);
            const float window = w * w;
            const auto slot = (size_t) (i & mask);
            overlap[0][slot] += window * readInput (0, centre + offset);
            overlap[1][slot] += window * readInput (1, centre + offset);
            norm[slot] += window;
        }

        lastAnalysis = centre;
        wasCoherent = coherent;
        nextMark = mark + period / ratio;
    }

    double sr = 44100.0, maxPeriod = 551.0, minPeriod = 40.0, idlePeriod = 220.0;
    double period = 220.0, ratio = 1.0, nextMark = 0.0, lastAnalysis = 0.0;
    bool coherent = false, wasCoherent = false;
    int delay = 1520;
    int64_t now = 0, mask = 0;
    std::array<std::vector<float>, 2> input, overlap;
    std::vector<float> norm;
};

// ---------------------------------------------------------------------------------------------
// De-esser: a dynamic high shelf that dips only when the top band gets close to the level of the
// whole signal. Being relative, it adapts to the singer's level by itself, and at zero reduction
// the shelf is flat, so the voice passes unchanged.
// ---------------------------------------------------------------------------------------------
class DeEsser
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        attack = timeCoeff (0.001f, sr);
        release = timeCoeff (0.06f, sr);
        fullCoeff = timeCoeff (0.03f, sr);
        frequency = 0.0f;
        setFrequency (6500.0f);
        reset();
    }
    void reset()
    {
        for (auto& f : shelf) f.reset();
        for (auto& f : sense) f.reset();
        bandEnv = fullEnv = reduction = 0.0f;
        applied = -1.0f;
        countdown = 0;
    }
    void setFrequency (float hz)
    {
        if (hz == frequency)
            return;
        frequency = hz;
        for (auto& f : sense) f.setHighPass (hz, sr);
        applied = -1.0f;
    }
    void setAmount (float a) { amount = std::clamp (a, 0.0f, 1.0f); }
    float reductionDb() const { return reduction; }

    void process (float& left, float& right)
    {
        const float mono = 0.5f * (left + right);
        const float band = sense[1].process (sense[0].process (mono));
        const float bandAbs = std::abs (band), fullAbs = std::abs (mono);
        bandEnv = (bandAbs > bandEnv ? attack : release) * (bandEnv - bandAbs) + bandAbs;
        fullEnv = fullCoeff * (fullEnv - fullAbs) + fullAbs;

        float wanted = 0.0f;
        if (amount > 0.0f && bandEnv > 1.0e-4f)
        {
            const float excess = gainToDb (bandEnv) - gainToDb (fullEnv) - (-6.0f - 16.0f * amount);
            wanted = std::clamp (excess * 0.8f, 0.0f, 24.0f * amount);
        }
        reduction = (wanted > reduction ? attack : release) * (reduction - wanted) + wanted;

        if (--countdown <= 0)
        {
            countdown = 16;
            if (std::abs (reduction - applied) > 0.02f)
            {
                applied = reduction;
                for (auto& f : shelf)
                    f.setHighShelf ((double) frequency * 0.85, sr, -(double) reduction);
            }
        }
        left = shelf[0].process (left);
        right = shelf[1].process (right);
    }

private:
    double sr = 44100.0;
    float frequency = 0.0f, amount = 0.0f, attack = 0, release = 0, fullCoeff = 0;
    float bandEnv = 0, fullEnv = 0, reduction = 0, applied = -1;
    int countdown = 0;
    std::array<Biquad, 2> shelf, sense;
};

// ---------------------------------------------------------------------------------------------
// Stereo-linked soft-knee compressor
// ---------------------------------------------------------------------------------------------
class Compressor
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() { reduction = 0.0f; }
    void set (float thresholdDb, float ratioIn, float attackMs, float releaseMs, float makeupDb, float enabledIn)
    {
        threshold = thresholdDb;
        slope = 1.0f - 1.0f / std::max (1.0f, ratioIn);
        attack = timeCoeff (attackMs * 0.001f, sr);
        release = timeCoeff (releaseMs * 0.001f, sr);
        makeup = makeupDb;
        enabled = enabledIn;
    }
    float reductionDb() const { return reduction * enabled; }

    void process (float& left, float& right)
    {
        const float over = gainToDb (std::max (std::abs (left), std::abs (right))) - threshold;
        constexpr float knee = 6.0f;
        float wanted = 0.0f;
        if (over >= knee * 0.5f)
            wanted = over * slope;
        else if (over > -knee * 0.5f)
            wanted = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);

        reduction = (wanted > reduction ? attack : release) * (reduction - wanted) + wanted;
        const float gain = dbToGain ((makeup - reduction) * enabled);
        left *= gain;
        right *= gain;
    }

private:
    double sr = 44100.0;
    float threshold = -20, slope = 0.66f, attack = 0, release = 0, makeup = 0, enabled = 1, reduction = 0;
};

// ---------------------------------------------------------------------------------------------
// Saturation: tanh with first-order antiderivative anti-aliasing, a small bias for even harmonics,
// and a warm/bright tilt on the wet path.
// ---------------------------------------------------------------------------------------------
class Saturator
{
public:
    void prepare (double sampleRate) { sr = sampleRate; dcCoeff = timeCoeff (1.0f / (2.0f * kPi * 12.0f), sr); reset(); setTilt (0.0f); }
    void reset()
    {
        for (auto& c : channel) c = {};
    }
    void setDrive (float amount) // 0..1
    {
        drive = dbToGain (amount * 20.0f);
        compensation = 1.0f / std::sqrt (drive); // holds perceived level roughly steady
    }
    void setTilt (float t) // -1 (warm) .. +1 (bright)
    {
        if (t == tilt)
            return;
        tilt = t;
        for (auto& c : channel)
        {
            c.low.setLowShelf (280.0, sr, -t * 5.0);
            c.high.setHighShelf (6000.0, sr, t * 5.0);
        }
    }
    float process (int ch, float x)
    {
        auto& s = channel[(size_t) ch];
        constexpr float bias = 0.12f;
        const float in = x * drive + bias;
        const float delta = in - s.previous;
        const float shaped = std::abs (delta) > 1.0e-4f
                                 ? (logCosh (in) - logCosh (s.previous)) / delta
                                 : std::tanh (0.5f * (in + s.previous));
        s.previous = in;
        // DC blocker removes the bias again.
        s.dc = dcCoeff * (s.dc - shaped) + shaped;
        return s.high.process (s.low.process ((shaped - s.dc) * compensation));
    }

private:
    static float logCosh (float x)
    {
        const float a = std::abs (x);
        return a + std::log1p (std::exp (-2.0f * a)) - 0.693147181f;
    }
    struct Channel { float previous = 0, dc = 0; Biquad low, high; };
    std::array<Channel, 2> channel;
    double sr = 44100.0;
    float drive = 1, compensation = 1, tilt = 1000.0f, dcCoeff = 0;
};

// ---------------------------------------------------------------------------------------------
// Ping-pong delay with filtered repeats
// ---------------------------------------------------------------------------------------------
class PingPongDelay
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        int size = 1;
        while (size < (int) (sr * 2.6))
            size <<= 1;
        mask = size - 1;
        for (auto& b : buffer) b.assign ((size_t) size, 0.0f);
        for (auto& f : high) f.setHighPass (250.0, sr);
        for (auto& f : low) f.setLowPass (6000.0, sr);
        smooth = timeCoeff (0.08f, sr);
        reset();
    }
    void reset()
    {
        for (auto& b : buffer) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& f : high) f.reset();
        for (auto& f : low) f.reset();
        pos = 0;
        current = -1.0f;
    }
    void set (float timeMs, float feedbackIn, float spreadIn)
    {
        target = std::clamp (timeMs * 0.001f * (float) sr, 1.0f, (float) mask - 4.0f);
        if (current < 0.0f)
            current = target;
        feedback = feedbackIn;
        spread = spreadIn;
    }
    void process (float inL, float inR, float& outL, float& outR)
    {
        current = smooth * (current - target) + target;
        const float l = read (0), r = read (1);
        const float fbL = low[0].process (high[0].process (l)) * feedback;
        const float fbR = low[1].process (high[1].process (r)) * feedback;
        const float mono = 0.5f * (inL + inR);
        const float sendL = inL + spread * (mono - inL), sendR = inR + spread * (-inR);
        // Crossed feedback: repeats bounce between the sides by `spread`.
        buffer[0][(size_t) pos] = sendL + fbL + spread * (fbR - fbL);
        buffer[1][(size_t) pos] = sendR + fbR + spread * (fbL - fbR);
        pos = (pos + 1) & mask;
        outL = l;
        outR = r;
    }

private:
    float read (int c) const
    {
        const float position = (float) pos - current;
        const auto base = (int) std::floor (position);
        const float t = position - (float) base;
        const auto& b = buffer[(size_t) c];
        return b[(size_t) (base & mask)] * (1.0f - t) + b[(size_t) ((base + 1) & mask)] * t;
    }
    double sr = 44100.0;
    std::array<std::vector<float>, 2> buffer;
    std::array<Biquad, 2> high, low;
    int pos = 0, mask = 0;
    float current = -1, target = 0, smooth = 0, feedback = 0, spread = 1;
};

// ---------------------------------------------------------------------------------------------
// Reverb: input diffusion into an 8-line feedback delay network with damping
// ---------------------------------------------------------------------------------------------
class Reverb
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double scale = sr / 44100.0;
        constexpr std::array<int, numLines> baseLengths { 1423, 1693, 1973, 2251, 2549, 2843, 3191, 3539 };
        for (int i = 0; i < numLines; ++i)
        {
            lines[(size_t) i].assign ((size_t) (baseLengths[(size_t) i] * scale * 2.2) + 4, 0.0f);
            lengths[(size_t) i] = (double) baseLengths[(size_t) i] * scale;
        }
        constexpr std::array<int, numDiffusers> diffuserLengths { 142, 107, 379, 277 };
        for (int i = 0; i < numDiffusers; ++i)
            diffusers[(size_t) i].assign ((size_t) (diffuserLengths[(size_t) i] * scale) + 1, 0.0f);
        preDelayLine.assign ((size_t) (sr * 0.21) + 2, 0.0f);
        reset();
    }
    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& d : diffusers) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (preDelayLine.begin(), preDelayLine.end(), 0.0f);
        linePos.fill (0);
        diffuserPos.fill (0);
        lowpass.fill (0.0f);
        preDelayPos = 0;
    }
    void set (float size, float decaySeconds, float damping, float preDelayMs)
    {
        const double stretch = 0.45 + 1.75 * (double) size;
        for (int i = 0; i < numLines; ++i)
        {
            activeLength[(size_t) i] = std::min ((int) (lengths[(size_t) i] * stretch), (int) lines[(size_t) i].size() - 1);
            const double seconds = activeLength[(size_t) i] / sr;
            lineGain[(size_t) i] = (float) std::pow (10.0, -3.0 * seconds / std::max (0.1f, decaySeconds));
        }
        damp = std::clamp (damping, 0.0f, 0.95f);
        preDelay = std::clamp ((int) (preDelayMs * 0.001f * (float) sr), 0, (int) preDelayLine.size() - 1);
    }
    void process (float inL, float inR, float& outL, float& outR)
    {
        const float in = 0.5f * (inL + inR);
        preDelayLine[(size_t) preDelayPos] = in;
        int read = preDelayPos - preDelay;
        if (read < 0) read += (int) preDelayLine.size();
        float x = preDelayLine[(size_t) read];
        preDelayPos = (preDelayPos + 1) % (int) preDelayLine.size();

        for (int i = 0; i < numDiffusers; ++i)
        {
            auto& d = diffusers[(size_t) i];
            auto& p = diffuserPos[(size_t) i];
            const float delayed = d[(size_t) p];
            const float v = x + 0.6f * delayed;
            d[(size_t) p] = v;
            x = delayed - 0.6f * v;
            p = (p + 1) % (int) d.size();
        }

        std::array<float, numLines> out;
        float sum = 0.0f;
        for (int i = 0; i < numLines; ++i)
        {
            const auto& line = lines[(size_t) i];
            int r = linePos[(size_t) i] - activeLength[(size_t) i];
            if (r < 0) r += (int) line.size();
            out[(size_t) i] = line[(size_t) r];
            sum += out[(size_t) i];
        }
        const float householder = sum * (2.0f / numLines);
        for (int i = 0; i < numLines; ++i)
        {
            auto& line = lines[(size_t) i];
            auto& p = linePos[(size_t) i];
            float feedback = (out[(size_t) i] - householder) * lineGain[(size_t) i];
            lowpass[(size_t) i] += (1.0f - damp) * (feedback - lowpass[(size_t) i]);
            line[(size_t) p] = lowpass[(size_t) i] + x;
            p = (p + 1) % (int) line.size();
        }
        outL = (out[0] - out[1] + out[2] - out[3] + out[4] + out[5] - out[6] - out[7]) * 0.35f;
        outR = (out[0] + out[1] - out[2] - out[3] - out[4] + out[5] + out[6] - out[7]) * 0.35f;
    }

private:
    static constexpr int numLines = 8, numDiffusers = 4;
    double sr = 44100.0;
    std::array<std::vector<float>, numLines> lines;
    std::array<std::vector<float>, numDiffusers> diffusers;
    std::vector<float> preDelayLine;
    std::array<double, numLines> lengths {};
    std::array<int, numLines> activeLength {}, linePos {};
    std::array<int, numDiffusers> diffuserPos {};
    std::array<float, numLines> lineGain {}, lowpass {};
    int preDelay = 0, preDelayPos = 0;
    float damp = 0.3f;
};

// ---------------------------------------------------------------------------------------------
// Auto-Set: listens to the voice, then works out gain, low cut, de-esser, compressor and key.
// ---------------------------------------------------------------------------------------------
struct LearnResult
{
    bool valid = false;
    float inputDb = 0, lowCutHz = 80, deEss = 0.4f, compThreshold = -20;
    int key = 0, scale = chromatic;
};

// Picks the key whose Krumhansl-Schmuckler profile best matches the sung pitch classes.
inline void estimateKey (const std::array<float, 12>& histogram, int& key, int& scale)
{
    static constexpr std::array<float, 12> majorProfile { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static constexpr std::array<float, 12> minorProfile { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    const auto correlate = [&] (const std::array<float, 12>& profile, int root)
    {
        float meanH = 0, meanP = 0;
        for (int i = 0; i < 12; ++i) { meanH += histogram[(size_t) i]; meanP += profile[(size_t) i]; }
        meanH /= 12; meanP /= 12;
        float num = 0, dh = 0, dp = 0;
        for (int i = 0; i < 12; ++i)
        {
            const float h = histogram[(size_t) ((i + root) % 12)] - meanH, p = profile[(size_t) i] - meanP;
            num += h * p; dh += h * h; dp += p * p;
        }
        return dh > 0 ? num / std::sqrt (dh * dp) : -1.0f;
    };
    float best = -2.0f;
    key = 0;
    scale = chromatic;
    for (int root = 0; root < 12; ++root)
    {
        const float maj = correlate (majorProfile, root), mino = correlate (minorProfile, root);
        if (maj > best) { best = maj; key = root; scale = major; }
        if (mino > best) { best = mino; key = root; scale = minor; }
    }
}

class VoiceLearner
{
public:
    static constexpr float wantedVoiceSeconds = 6.0f, timeoutSeconds = 25.0f, minimumVoiceSeconds = 1.5f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        sense.setHighPass (6000.0, sr);
    }
    void start()
    {
        total = voiced = 0;
        power = highPower = peak = 0.0f;
        pitchClasses.fill (0.0f);
        notes.fill (0.0f);
        sense.reset();
        result = {};
    }
    // Returns true on the sample where listening is finished.
    bool process (float raw, bool isVoiced, float midi)
    {
        ++total;
        const float high = sense.process (raw);
        peak = std::max (peak, std::abs (raw));
        if (isVoiced)
        {
            ++voiced;
            power += raw * raw;
            const int note = std::clamp ((int) std::lround (midi), 0, 127);
            notes[(size_t) note] += 1.0f;
            pitchClasses[(size_t) (note % 12)] += 1.0f;
        }
        highPower += high * high;
        const bool enough = (float) voiced >= wantedVoiceSeconds * (float) sr;
        if (! enough && (float) total < timeoutSeconds * (float) sr)
            return false;
        finish();
        return true;
    }
    const LearnResult& getResult() const { return result; }
    float progress() const { return std::min (1.0f, (float) voiced / (wantedVoiceSeconds * (float) sr)); }

private:
    void finish()
    {
        result = {};
        if ((float) voiced < minimumVoiceSeconds * (float) sr)
            return;

        constexpr float targetRmsDb = -18.0f;
        const float rmsDb = 10.0f * std::log10 (std::max (power / (float) voiced, 1.0e-12f));
        const float peakDb = gainToDb (peak);
        // Bring the voice to the target, but do not push its peaks over -3 dBFS.
        result.inputDb = std::clamp (std::min (targetRmsDb - rmsDb, -3.0f - peakDb), -24.0f, 24.0f);
        // Crest factor: dynamic takes need a threshold further below the peaks to even out.
        const float crest = peakDb - rmsDb;
        result.compThreshold = std::clamp (targetRmsDb - 2.0f - (crest - 12.0f) * 0.25f, -32.0f, -12.0f);

        // Sibilant takes: more energy above 6 kHz relative to the whole.
        const float highShareDb = 10.0f * std::log10 (std::max (highPower / std::max (power, 1.0e-9f), 1.0e-6f));
        result.deEss = std::clamp (0.35f + (highShareDb + 22.0f) * 0.035f, 0.15f, 0.85f);

        // Low cut sits under the lowest sung notes (10th percentile, ignoring stray detections).
        float seen = 0.0f;
        int low = 48;
        for (int n = 0; n < 128; ++n)
        {
            seen += notes[(size_t) n];
            if (seen >= 0.1f * (float) voiced)
            {
                low = n;
                break;
            }
        }
        const float lowHz = 440.0f * std::pow (2.0f, ((float) low - 69.0f) / 12.0f);
        result.lowCutHz = std::clamp (lowHz * 0.6f, 40.0f, 160.0f);

        estimateKey (pitchClasses, result.key, result.scale);
        result.valid = true;
    }

    double sr = 44100.0;
    int64_t total = 0, voiced = 0;
    float power = 0, highPower = 0, peak = 0;
    std::array<float, 12> pitchClasses {};
    std::array<float, 128> notes {};
    Biquad sense;
    LearnResult result;
};

// ---------------------------------------------------------------------------------------------
// The complete chain
// ---------------------------------------------------------------------------------------------
struct Params
{
    float inputDb = 0, lowCutHz = 40, outputDb = 0;
    bool autoLevel = false;

    bool tuneOn = true;
    float tuneAmount = 1, tuneSpeedMs = 40;
    int key = 0, scale = chromatic;

    bool deEssOn = true;
    float deEss = 0.4f, deEssHz = 6500;

    bool compOn = true;
    float compThreshold = -20, compRatio = 3, compAttackMs = 8, compReleaseMs = 120, compMakeupDb = 4;

    bool satOn = true;
    float satDrive = 0.25f, satTilt = 0, satMix = 1;

    bool delayOn = false;
    float delayMs = 375, delayFeedback = 0.3f, delayMix = 0.15f, delaySpread = 1;

    bool reverbOn = true;
    float reverbSize = 0.5f, reverbDecay = 1.6f, reverbDamp = 0.4f, reverbPreDelayMs = 25, reverbMix = 0.15f;
    float duck = 0.4f;
};

struct Meters
{
    std::atomic<float> input { kSilenceDb }, output { kSilenceDb }, deEss { 0 }, comp { 0 }, autoLevel { 0 };
    std::atomic<float> midi { 0 }, target { 0 }, correctionCents { 0 };
    std::atomic<bool> voiced { false };
};

class VocalChain
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        detector.prepare (sr);
        shifter.prepare (sr);
        deEsser.prepare (sr);
        compressor.prepare (sr);
        saturator.prepare (sr);
        delay.prepare (sr);
        reverb.prepare (sr);
        learner.prepare (sr);
        smooth = timeCoeff (0.02f, sr);
        levelCoeff = timeCoeff (0.4f, sr);
        duckAttack = timeCoeff (0.01f, sr);
        duckRelease = timeCoeff (0.35f, sr);
        meterRelease = timeCoeff (0.25f, sr);
        hopSeconds = 0.006f;
        reset();
    }

    void reset()
    {
        detector.reset();
        shifter.reset();
        deEsser.reset();
        compressor.reset();
        saturator.reset();
        delay.reset();
        reverb.reset();
        for (auto& f : lowCut) f.reset();
        correction = 0.0f;
        currentNote = -1;
        levelPower = 0.0f;
        levelGainDb = 0.0f;
        duckEnv = 0.0f;
        inMeter = outMeter = 0.0f;
        lastLowCut = -1.0f;
        inputGain = outputGain = -1.0f;
        satWet = delaySend = delayWet = reverbSend = reverbWet = 0.0f;
    }

    int latency() const { return shifter.latency(); }
    const Meters& getMeters() const { return meters; }

    // ---- Auto-Set, driven from the UI thread through atomics ----
    void requestLearn() { learnRequest.store (true); }
    void cancelLearn() { learnCancel.store (true); }
    bool isLearning() const { return learningShared.load(); }
    float learnProgress() const { return progressShared.load(); }
    // Valid once per finished session; call from the UI thread.
    bool takeLearnResult (LearnResult& out)
    {
        if (! learnDone.exchange (false, std::memory_order_acquire))
            return false;
        out = learner.getResult();
        return true;
    }

    void setParams (const Params& p)
    {
        params = p;
        if (p.lowCutHz != lastLowCut)
        {
            lastLowCut = p.lowCutHz;
            for (auto& f : lowCut) f.setHighPass (p.lowCutHz, sr);
        }
        deEsser.setFrequency (p.deEssHz);
        deEsser.setAmount (p.deEssOn ? p.deEss : 0.0f);
        compressor.set (p.compThreshold, p.compRatio, p.compAttackMs, p.compReleaseMs, p.compMakeupDb, p.compOn ? 1.0f : 0.0f);
        saturator.setDrive (p.satDrive);
        saturator.setTilt (p.satTilt);
        delay.set (p.delayMs, p.delayFeedback, p.delaySpread);
        reverb.set (p.reverbSize, p.reverbDecay, p.reverbDamp, p.reverbPreDelayMs);
        mask = scaleMask (p.scale, p.key);
        retuneCoeff = p.tuneSpeedMs <= 0.5f ? 0.0f : std::exp (-hopSeconds / (p.tuneSpeedMs * 0.001f));
    }

    void process (float* left, float* right, int numSamples)
    {
        if (learnCancel.exchange (false))
        {
            learningLocal = false;
            learningShared.store (false);
        }
        if (learnRequest.exchange (false))
        {
            learner.start();
            learningLocal = true;
            learningShared.store (true);
            progressShared.store (0.0f);
        }

        const float inputTarget = dbToGain (params.inputDb), outputTarget = dbToGain (params.outputDb);
        if (inputGain < 0.0f) inputGain = inputTarget;
        if (outputGain < 0.0f) outputGain = outputTarget;
        const float satTarget = params.satOn ? params.satMix * std::min (params.satDrive * 20.0f, 1.0f) : 0.0f;
        const float delaySendTarget = params.delayOn ? 1.0f : 0.0f, reverbSendTarget = params.reverbOn ? 1.0f : 0.0f;
        float inPeak = 0.0f, outPeak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float rawMono = 0.5f * (left[i] + right[i]);
            inputGain = smooth * (inputGain - inputTarget) + inputTarget;
            outputGain = smooth * (outputGain - outputTarget) + outputTarget;
            satWet = smooth * (satWet - satTarget) + satTarget;
            delaySend = smooth * (delaySend - delaySendTarget) + delaySendTarget;
            delayWet = smooth * (delayWet - params.delayMix) + params.delayMix;
            reverbSend = smooth * (reverbSend - reverbSendTarget) + reverbSendTarget;
            reverbWet = smooth * (reverbWet - params.reverbMix) + params.reverbMix;

            float l = left[i] * inputGain, r = right[i] * inputGain;
            inPeak = std::max (inPeak, std::max (std::abs (l), std::abs (r)));
            l = lowCut[0].process (l);
            r = lowCut[1].process (r);

            // Slow leveller: rides the gain toward -18 dB and holds still between phrases.
            const float mono = 0.5f * (l + r);
            const float level = mono * mono;
            levelPower = levelCoeff * (levelPower - level) + level;
            if (params.autoLevel)
            {
                if (levelPower > 1.0e-5f)
                {
                    const float wanted = std::clamp (-18.0f - 10.0f * std::log10 (levelPower), -12.0f, 12.0f);
                    levelGainDb = levelCoeff * (levelGainDb - wanted) + wanted;
                }
            }
            else
            {
                levelGainDb = levelCoeff * levelGainDb;
            }
            const float levelGain = dbToGain (levelGainDb);
            l *= levelGain;
            r *= levelGain;

            // Pitch
            if (detector.push (0.5f * (l + r)))
                updatePitch();
            if (learningLocal)
            {
                const bool finished = learner.process (rawMono, detector.isVoiced(), currentMidi);
                if (finished)
                {
                    learningLocal = false;
                    learnDone.store (true, std::memory_order_release);
                    learningShared.store (false);
                }
            }
            shifter.process (l, r);

            deEsser.process (l, r);
            compressor.process (l, r);

            // Saturation
            const float wetL = saturator.process (0, l), wetR = saturator.process (1, r);
            l += satWet * (wetL - l);
            r += satWet * (wetR - r);

            // Space: both effects are parallel sends, ducked while the singer is singing.
            const float absDry = std::max (std::abs (l), std::abs (r));
            duckEnv = (absDry > duckEnv ? duckAttack : duckRelease) * (duckEnv - absDry) + absDry;
            const float duckGain = 1.0f - params.duck * 0.75f * std::min (duckEnv * 4.0f, 1.0f);

            float delayL, delayR, reverbL, reverbR;
            delay.process (l * delaySend, r * delaySend, delayL, delayR);
            reverb.process (l * reverbSend, r * reverbSend, reverbL, reverbR);
            l += (delayL * delayWet + reverbL * reverbWet) * duckGain;
            r += (delayR * delayWet + reverbR * reverbWet) * duckGain;

            l = safetyClip (l * outputGain);
            r = safetyClip (r * outputGain);
            outPeak = std::max (outPeak, std::max (std::abs (l), std::abs (r)));
            left[i] = l;
            right[i] = r;
        }

        const float blockRelease = std::pow (meterRelease, (float) numSamples);
        inMeter = std::max (inPeak, inMeter * blockRelease);
        outMeter = std::max (outPeak, outMeter * blockRelease);
        meters.input.store (gainToDb (inMeter));
        meters.output.store (gainToDb (outMeter));
        meters.deEss.store (deEsser.reductionDb());
        meters.comp.store (compressor.reductionDb());
        meters.autoLevel.store (levelGainDb);
        meters.voiced.store (detector.isVoiced());
        meters.midi.store (currentMidi);
        meters.target.store ((float) currentNote);
        meters.correctionCents.store (correction * 100.0f);
        if (learningLocal)
            progressShared.store (learner.progress());
    }

private:
    void updatePitch()
    {
        const bool voiced = detector.isVoiced();
        if (! voiced)
        {
            correction = 0.0f;
            currentNote = -1;
            shifter.setPitch (false, 0.0, 1.0);
            return;
        }
        const double hz = detector.getHz();
        currentMidi = 69.0f + 12.0f * std::log2 ((float) hz / 440.0f);
        const bool active = params.tuneOn && params.tuneAmount > 0.0f;
        currentNote = nearestNote (currentMidi, mask, currentNote);
        const float wanted = active ? ((float) currentNote - currentMidi) * params.tuneAmount : 0.0f;
        correction = retuneCoeff * (correction - wanted) + wanted;
        shifter.setPitch (true, sr / hz, std::pow (2.0, (double) correction / 12.0));
    }

    // Transparent below -1 dBFS; rounds anything hotter off instead of hard clipping.
    static float safetyClip (float x)
    {
        constexpr float knee = 0.891f, headroom = 1.0f - knee;
        const float a = std::abs (x);
        if (a <= knee)
            return x;
        const float over = (a - knee) / headroom;
        return std::copysign (knee + headroom * over / (1.0f + over), x);
    }

    double sr = 44100.0;
    Params params;
    PitchDetector detector;
    PsolaShifter shifter;
    DeEsser deEsser;
    Compressor compressor;
    Saturator saturator;
    PingPongDelay delay;
    Reverb reverb;
    VoiceLearner learner;
    std::array<Biquad, 2> lowCut;
    Meters meters;

    uint16_t mask = 0xfff;
    int currentNote = -1;
    bool learningLocal = false;
    float currentMidi = 0, correction = 0, retuneCoeff = 0, hopSeconds = 0.006f;
    float smooth = 0, levelCoeff = 0, duckAttack = 0, duckRelease = 0, meterRelease = 0;
    float levelPower = 0, levelGainDb = 0, duckEnv = 0, inMeter = 0, outMeter = 0, lastLowCut = -1;
    float inputGain = -1, outputGain = -1, satWet = 0, delaySend = 0, delayWet = 0, reverbSend = 0, reverbWet = 0;
    std::atomic<bool> learnRequest { false }, learnCancel { false }, learnDone { false }, learningShared { false };
    std::atomic<float> progressShared { 0 };
};
} // namespace vocal
