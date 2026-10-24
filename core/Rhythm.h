// Rhythm.h: Ripple's core. The tick counter and clock, the GRID engine (R, R', ACCENT) and the WAVE engine. No Rack headers.
// One tick = one 16th note. Generator i (0 = A .. 3 = D) is on when bit i of onMask is set.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <algorithm>
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

// The clock: ticks from the internal tempo or from EXT pulses, the PLAY hold, the measured EXT interval and the
// per-beat in-between ticks. One call per sample; at most one tick per sample. frac = position inside the current tick.
struct Clock
{
    Counter counter;
    float frac = 0.f;          // 0..1 (capped at 0.999 on EXT, so it holds just short of the next tick)
    double phase = 0.0;        // internal clock: position inside the current tick, 0..1
    double sinceTick = 0.0;    // EXT: seconds since the last tick played
    double sincePulse = 0.0;   // EXT: seconds since the last pulse
    double tickLen = 0.125;    // EXT: measured seconds per tick
    int pulses = 0;            // EXT pulses seen, counting up to 2 (two measure tickLen)
    int pending = 0;           // EXT per beat: in-between ticks still to play before the next pulse
    bool skipMeasure = false;  // EXT: the first pulse after a resume keeps the old tickLen

    void reset()   // RESET: back to an armed downbeat
    {
        counter.reset();
        phase = sinceTick = 0.0;
        pending = 0;
    }

    void resume()   // play turned on (PLAY, Initialize, preset load): a stop of unknown length sits before the next pulse
    {
        if (pulses < 2) pulses = 0;
        else skipMeasure = true;
    }

    // dt: seconds per sample. bpm: internal tempo, already clamped. ext: EXT patched. edge: EXT rising edge this sample.
    // Returns true when a tick plays; tick is then its number.
    bool process (double dt, float bpm, bool ext, bool edge, bool play, bool perBeat, uint32_t& tick)
    {
        bool fired = false;
        if (ext)
        {
            if (play)
            {
                sinceTick += dt;
                sincePulse += dt;
            }
            if (edge && play)
            {
                if (pulses < 2) ++pulses;
                if (pulses == 2 && ! skipMeasure) tickLen = perBeat ? sincePulse / 4.0 : sincePulse;
                skipMeasure = false;
                sincePulse = sinceTick = 0.0;
                tick = perBeat ? counter.nextBeat() : counter.next();
                pending = perBeat ? 3 : 0;
                fired = true;
            }
            else if (perBeat && play && pending > 0 && pulses == 2 && sinceTick >= tickLen)
            {
                sinceTick -= tickLen;
                --pending;
                tick = counter.next();
                fired = true;
            }
            frac = pulses == 2 && ! counter.armed ? (float) std::min (sinceTick / tickLen, 0.999) : 0.f;
            phase = frac;   // the internal clock continues from where EXT left off, so T does not jump
        }
        else
        {
            pulses = 0;   // EXT unpatched: the next patch re-measures its interval from two fresh pulses
            if (play)
            {
                if (counter.armed)   // power-on or RESET: the downbeat plays at once
                {
                    phase = 0.0;
                    tick = counter.next();
                    fired = true;
                }
                else
                {
                    phase += bpm * 4.0 / 60.0 * dt;
                    if (phase >= 1.0)
                    {
                        phase -= 1.0;
                        tick = counter.next();
                        fired = true;
                    }
                }
            }
            frac = (float) phase;
        }
        return fired;
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

// WAVE threshold for DENSITY 1..4 (clamped): 0, 0.25, 0.5, 0.75
inline float threshold (int density) { return ((density < 1 ? 1 : density > numGens ? numGens : density) - 1) / 4.f; }

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
