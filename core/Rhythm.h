// Rhythm.h: Ripple's core. The tick counter, the GRID engine (R, R', ACCENT) and the WAVE engine. No Rack headers.
// One tick = one 16th note. Generator i (0 = A .. 3 = D) is on when bit i of onMask is set.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <cmath>
#include <cstdint>

namespace rhythm
{
constexpr int numGens = 4;
constexpr uint32_t wrap = 720720;   // lcm (1..16): every period divides it, so the counter wraps without a seam

inline int clampPeriod (int p) { return p < 1 ? 1 : p > 16 ? 16 : p; }

inline uint32_t gcd (uint32_t a, uint32_t b)
{
    while (b)
    {
        const uint32_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}

inline uint32_t lcm (uint32_t a, uint32_t b) { return a / gcd (a, b) * b; }

// The pattern length: lcm of the on periods. 0 when nothing is on.
inline uint32_t cycle (const int period[numGens], unsigned onMask)
{
    uint32_t l = 0;
    for (int i = 0; i < numGens; ++i)
        if ((onMask >> i) & 1u)
        {
            const uint32_t p = (uint32_t) clampPeriod (period[i]);
            l = l ? lcm (l, p) : p;
        }
    return l;
}

struct Tick
{
    unsigned gens;   // bit i: generator i fires on this tick
    bool r, rp, accent;
};

// GRID engine for tick t: R when at least `density` generators hit, R' on every other tick.
inline Tick grid (uint32_t t, const int period[numGens], unsigned onMask, int density)
{
    Tick k { 0u, false, false, false };
    int count = 0;
    for (int i = 0; i < numGens; ++i)
        if (((onMask >> i) & 1u) && t % (uint32_t) clampPeriod (period[i]) == 0)
        {
            k.gens |= 1u << i;
            ++count;
        }
    k.r = count >= (density < 1 ? 1 : density > numGens ? numGens : density);
    k.rp = ! k.r;
    const uint32_t l = cycle (period, onMask);
    k.accent = l && t % l == 0;
    return k;
}

// The tick counter. Armed = the next tick played is the downbeat (power-on, RESET).
struct Counter
{
    uint32_t t = 0;
    bool armed = true;

    void reset()
    {
        t = 0;
        armed = true;
    }

    uint32_t next()   // the next tick to play
    {
        if (armed) armed = false;
        else t = (t + 1) % wrap;
        return t;
    }

    uint32_t nextBeat()   // EXT per beat: the next multiple of 4 (a beat)
    {
        if (armed) armed = false;
        else t = (t / 4 + 1) * 4 % wrap;
        return t;
    }
};

constexpr double twoPi = 6.283185307179586;   // M_PI is hidden by -std=c++11 on mingw

// WAVE engine: each on generator is a sine with its period (in ticks), starting at the downbeat; the mean of them.
// T = tick + fraction, as a double: in float the step near 720000 ticks is 1/16 tick and the wave becomes a staircase.
inline float waveSum (double T, const int period[numGens], unsigned onMask)
{
    double s = 0.0;
    int n = 0;
    for (int i = 0; i < numGens; ++i)
        if ((onMask >> i) & 1u)
        {
            s += std::sin (twoPi * T / clampPeriod (period[i]));
            ++n;
        }
    return n ? (float) (s / n) : 0.f;
}

// Threshold crossings of the wave sum: R when it rises through +th, R' when it falls through -th.
// At th = 0 an S sitting exactly at 0 fires R once (from the reset seed) and nothing after; a touch to 0 and back
// fires at most one side. That is intended.
struct Crossing
{
    float prev = -1.f;   // -1 after reset, so the downbeat fires R at threshold 0
    bool primed = true;  // false after an inactive stretch: the next active sample only records s

    void reset()
    {
        prev = -1.f;
        primed = true;
    }

    void process (float s, float th, bool active, bool& r, bool& rp)
    {
        r = rp = false;
        if (! active)   // nothing on: no triggers; the next active sample re-primes
        {
            primed = false;
            return;
        }
        if (primed)
        {
            r = prev < th && s >= th;
            rp = prev > -th && s <= -th;
        }
        primed = true;
        prev = s;
    }
};
}
