// Env.h: Ripple's amp envelope. An ADSR (linear attack, exponential decay and release), the tick-counted gate that holds
// it, the knob-to-seconds map and the pick of which rhythm event fires it. No Rack headers.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <cmath>
#include "Rhythm.h"

namespace env
{
enum Source { SrcR, SrcRp, SrcAccent, SrcA, SrcB, SrcC, SrcD, numSources };   // the menu order; saved as an int in the patch

// Did tick k hit this source? In WAVE mode R and R' are the threshold crossings (waveHitOf), so the grid's never hit.
inline bool hitOf (const rhythm::Tick& k, int src, bool wave)
{
    switch (src)
    {
        case SrcR: return ! wave && k.r;
        case SrcRp: return ! wave && k.rp;
        case SrcAccent: return k.accent;
        default: return src >= SrcA && src < numSources && ((k.gens >> (src - SrcA)) & 1u);
    }
}

inline bool waveHitOf (bool wr, bool wrp, int src) { return src == SrcR ? wr : src == SrcRp && wrp; }

// Knob 0..1 -> seconds, log: 1 ms at 0, maxSec at 1 (the panel shows 0.001 * (maxSec * 1000) ^ knob). knobOf inverts it.
inline float timeOf (float knob, float maxSec) { return 0.001f * std::pow (maxSec * 1000.f, knob); }
inline float knobOf (float sec, float maxSec) { return std::log (sec * 1000.f) / std::log (maxSec * 1000.f); }

// The gate: a hit holds it for len ticks (1..16). tick() counts one tick down and says whether the gate just fell.
struct GateHold
{
    int left = 0;

    bool on() const { return left > 0; }
    void hit (int len) { left = len < 1 ? 1 : len > 16 ? 16 : len; }
    bool tick() { return left > 0 && --left == 0; }
    void reset() { left = 0; }
};

// ADSR, level 0..1. Decay is also the sustain stage: it sits at s until release().
struct Env
{
    enum Stage { Idle, Attack, Decay, Release };
    Stage stage = Idle;
    float level = 0.f;

    void trigger() { stage = Attack; }   // attacks from the current level, so a retrigger never jumps
    void release() { if (stage != Idle) stage = Release; }
    void reset() { stage = Idle; level = 0.f; }

    // one-pole toward target that covers 99 % of the distance in sec, snapping on arrival
    static float toward (float level, float target, float dt, float sec)
    {
        level += (target - level) * (1.f - std::exp (-4.605f * dt / sec));
        return std::fabs (target - level) < 1e-4f ? target : level;
    }

    // a, d, r: seconds. s: 0..1. dt: seconds per sample.
    float process (float dt, float a, float d, float s, float r)
    {
        const float minT = 1e-4f;   // a floor, so a time of 0 cannot divide by zero
        switch (stage)
        {
            case Attack:
                level += dt / (a < minT ? minT : a);
                if (level >= 1.f)
                {
                    level = 1.f;
                    stage = Decay;
                }
                break;
            case Decay:
                level = toward (level, s, dt, d < minT ? minT : d);
                break;
            case Release:
                level = toward (level, 0.f, dt, r < minT ? minT : r);
                if (level == 0.f) stage = Idle;
                break;
            default:
                break;
        }
        return level;
    }
};
}
