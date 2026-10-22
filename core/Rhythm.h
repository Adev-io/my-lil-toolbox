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
}
