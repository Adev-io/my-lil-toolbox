// Meter.h: My lil Mixer's monitoring. dBFS against 10 V, peak ballistics with a held tick, the clip latch, the 16-segment
// LED ladder, short-term loudness (BS.1770-4 K-weighting, 3 s) and the 6 s level history. No Rack headers.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <algorithm>
#include <cmath>

namespace meter
{
constexpr float fullScaleV = 10.f;   // Rack Core Audio sends 10 V to the driver as 0 dBFS
constexpr float floorDb = -60.f;     // silence, NaN and inf read this
constexpr float fallDbPerSec = 20.f; // bar and tick fall rate
constexpr float holdSec = 1.5f;      // the peak tick holds this long, then falls
constexpr float hotDb = -6.f;        // ladder colour edge: lime below, magenta from here to 0
constexpr float duckFloorDb = -24.f; // the duck bar's bottom

// |v| in dBFS, floored. Non-finite reads the floor.
inline float dbfs (float v)
{
    const float a = std::fabs (v);
    return std::isfinite (a) ? std::fmax (floorDb, 20.f * std::log10 (a / fullScaleV)) : floorDb;
}

// Bar and peak tick in dBFS, fed once per block with the block's peak (dbfs) and the block length in seconds.
// The bar rises at once and falls at 20 dB/s; the tick holds each new peak for 1.5 s, then falls the same way.
struct Ballistics
{
    float level = floorDb, hold = floorDb, holdLeft = 0.f;

    void process (float db, float dt)
    {
        level = std::fmax (db, level - fallDbPerSec * dt);
        if (db >= hold) { hold = db; holdLeft = holdSec; }
        else if (holdLeft > 0.f) holdLeft -= dt;
        else hold = std::fmax (level, hold - fallDbPerSec * dt);
    }

    void reset() { level = hold = floorDb; holdLeft = 0.f; }
};

// Latches once a peak reaches 10 V; only clear() lets go.
struct ClipLatch
{
    bool on = false;
    void process (float peakV) { if (peakV >= fullScaleV) on = true; }
    void clear() { on = false; }
};

// The LED ladder: 16 segments from -60 to 0 dBFS, finer near the top. Segment i spans edge (i) .. edge (i + 1).
constexpr int segments = 16;
inline float edge (int i)
{
    static const float e[segments + 1] = { -60.f, -54.f, -48.f, -42.f, -36.f, -30.f, -27.f, -24.f, -21.f, -18.f, -15.f, -12.f,
                                           -9.f, -6.f, -4.f, -2.f, 0.f };
    return e[i];
}
// Segments lit by a level: each one whose bottom edge the level passes.
inline int lit (float db)
{
    int n = 0;
    while (n < segments && db > edge (n)) ++n;
    return n;
}
// Segment colour: magenta from -6 dBFS up, lime below.
inline bool hot (int seg) { return edge (seg) >= hotDb; }
// A level's height on the ladder, 0 (floor) .. 16 (0 dBFS and over), linear inside each segment: the peak tick.
inline float ladderPos (float db)
{
    if (! (db > edge (0))) return 0.f;
    for (int i = 0; i < segments; ++i)
        if (db <= edge (i + 1)) return i + (db - edge (i)) / (edge (i + 1) - edge (i));
    return (float) segments;
}

// A strip's duck right now in dB: 20 log10 (1 - DUCK x env), 0 .. -24.
inline float duckDb (float duck, float env)
{
    const float g = 1.f - duck * env;
    return g > 0.f ? std::fmax (duckFloorDb, 20.f * std::log10 (g)) : duckFloorDb;
}

// One biquad, transposed direct form II, in double: the 38 Hz high-pass keeps its poles near 1. A decaying high-pass can sit in denormals when FTZ is off; Rack's engine threads set FTZ/DAZ and the plugin relies on that.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// BS.1770-4 K-weighting for any sample rate: the pre-filter high shelf, then the RLB high-pass. The analog prototypes
// (gain, Q, corner) are the ones libebur128 and pyloudnorm fit to the standard's 48 kHz coefficients.
inline void kWeight (double sr, Biquad& shelf, Biquad& hp)
{
    const double pi = 3.14159265358979323846;
    {
        const double G = 3.999843853973347, Q = 0.7071752369554196, fc = 1681.974450955533;
        const double K = std::tan (pi * fc / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double Q = 0.5003270373238773, fc = 38.13547087602444;
        const double K = std::tan (pi * fc / sr), a0 = 1.0 + K / Q + K * K;
        hp.b0 = 1.0;
        hp.b1 = -2.0;
        hp.b2 = 1.0;
        hp.a1 = 2.0 * (K * K - 1.0) / a0;
        hp.a2 = (1.0 - K / Q + K * K) / a0;
    }
}

// Short-term loudness in LUFS: K-weighted mean square of L + R over the last 3 s (30 blocks of 100 ms), -0.691 offset,
// floored at -60. Volts in (10 V = full scale); refreshed once per 100 ms block.
struct Loudness
{
    static constexpr int blocks = 30;
    Biquad shelf[2], hp[2];
    double ms[blocks] = {};   // each block's mean square, L + R
    double sum = 0.0;
    int n = 0, per = 4800, head = 0;
    float value = floorDb;

    void setRate (float sr)
    {
        for (int c = 0; c < 2; ++c) kWeight (sr, shelf[c], hp[c]);
        per = std::max (1, (int) std::lround (sr * 0.1f));
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c) shelf[c].z1 = shelf[c].z2 = hp[c].z1 = hp[c].z2 = 0.0;
        std::fill (ms, ms + blocks, 0.0);
        sum = 0.0;
        n = head = 0;
        value = floorDb;
    }

    void process (float l, float r)
    {
        const float in[2] = { l, r };
        for (int c = 0; c < 2; ++c)
        {
            const double x = std::isfinite (in[c]) ? in[c] / fullScaleV : 0.0;   // NaN / inf would poison the filters for good
            const double y = hp[c].process (shelf[c].process (x));
            sum += y * y;
        }
        if (++n < per) return;
        ms[head] = sum / per;
        head = (head + 1) % blocks;
        sum = 0.0;
        n = 0;
        double total = 0.0;
        for (double m : ms) total += m;
        const double mean = total / blocks;
        value = mean > 0.0 ? (float) std::fmax ((double) floorDb, -0.691 + 10.0 * std::log10 (mean)) : floorDb;
    }
};

// The last 6 s of the master: 120 columns of 50 ms, each the loudest sample of its column in dBFS. col[head] is the oldest.
struct History
{
    static constexpr int columns = 120;
    float col[columns];
    int head = 0, n = 0, per = 2400;
    float peak = 0.f;

    History() { reset(); }

    void setRate (float sr) { per = std::max (1, (int) std::lround (sr * 0.05f)); }

    void process (float v)
    {
        peak = std::fmax (peak, std::fabs (v));
        if (++n < per) return;
        col[head] = dbfs (peak);
        head = (head + 1) % columns;
        n = 0;
        peak = 0.f;
    }

    void reset()
    {
        std::fill (col, col + columns, floorDb);
        head = n = 0;
        peak = 0.f;
    }
};
}
