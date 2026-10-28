// Mixer.h: My lil Mixer's DSP. The LEVEL taper, pan and balance, the mute/solo rule, one strip (duck, slewed gains,
// pre-fader send tap), the shared sidechain envelope (audio follower or trigger shape), the strip peak meter and the master
// soft clip. No Rack headers.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <cmath>
#include "Scale.h"

namespace mixer
{
constexpr int numStrips = 8;
constexpr float unityKnob = 0.70710678f;   // the LEVEL / MASTER knob position for 0 dB
constexpr float hotV = 5.f;                // signal light turns magenta above this; soft clip starts here

// LEVEL / MASTER knob 0..1 -> gain 2 k^2: silence at 0, unity at 1/sqrt 2, +6 dB at 1.
inline float levelGain (float k) { return 2.f * k * k; }
// SEND knob 0..1 -> gain k^2: silence at 0, unity at 1.
inline float sendGain (float k) { return k * k; }

// Pan p -1..1 into per-side gains. Mono source: constant power, -3 dB each side at the centre. Stereo source:
// constant-power balance, unity at the centre, the far side turns down to 0 at a hard pan.
inline void pan (float p, bool stereo, float& gl, float& gr)
{
    const float halfPi = 1.57079633f;
    p = p < -1.f ? -1.f : p > 1.f ? 1.f : p;
    if (! stereo)
    {
        const float a = (p + 1.f) * 0.5f * halfPi;
        gl = std::cos (a);
        gr = std::sin (a);
        return;
    }
    gl = p > 0.f ? std::cos (p * halfPi) : 1.f;
    gr = p < 0.f ? std::cos (-p * halfPi) : 1.f;
}

// Any solo: only soloed strips pass. Mute always wins.
inline bool audible (bool mute, bool solo, bool anySolo) { return ! mute && (! anySolo || solo); }

// Linear move toward target by at most step.
inline float slew (float v, float target, float step)
{
    return v < target ? (target - v > step ? v + step : target) : (v - target > step ? v - step : target);
}

// Per-sample gain step that sweeps the whole gain range (0..2) in 5 ms: no zipper on knobs, no click on mute/solo.
inline float slewStep (float dt) { return 2.f * dt / 0.005f; }

struct Strip
{
    float gl = 0.f, gr = 0.f;   // slewed gains: level x mute/solo x pan; start silent, so a new strip fades in

    // l, r: input volts (r already normalled to l by the caller). stereo: R was patched. level, panPos: knobs.
    // duck: DUCK knob 0..1. env: the sidechain envelope 0..1. step: slewStep (dt).
    // Writes the post-fader pair (out) and the pre-fader send tap (pre: after the duck, before level/mute/pan).
    void process (float l, float r, bool stereo, float level, float panPos, bool on, float duck, float env, float step,
                  float& outL, float& outR, float& preL, float& preR)
    {
        float pl, pr;
        pan (panPos, stereo, pl, pr);
        const float g = on ? levelGain (level) : 0.f;
        gl = slew (gl, g * pl, step);
        gr = slew (gr, g * pr, step);
        const float d = 1.f - duck * env;
        preL = scale::clampV (l) * d;
        preR = scale::clampV (r) * d;
        outL = preL * gl;
        outR = preR * gr;
    }

    void reset() { gl = gr = 0.f; }
};

// The shared sidechain envelope, 0..1. Follower: |x| / 5 V rises to its level linearly within 1 ms, falls by
// exp (-t / release). Trigger: a rising edge (Schmitt 1 V on, 0.1 V off) ramps to 1 over 1 ms, then falls the same way.
struct Sidechain
{
    static constexpr float attack = 0.001f;   // seconds
    float env = 0.f;
    bool high = false;       // trigger mode: the Schmitt state
    bool rising = false;     // trigger mode: in the 1 ms ramp to 1

    float process (float x, bool trigger, float release, float dt)
    {
        x = scale::clampV (x);
        const float up = dt / attack, fall = std::exp (-dt / release);
        if (trigger)
        {
            if (! high && x >= 1.f) high = rising = true;
            else if (high && x <= 0.1f) high = false;
            if (rising)
            {
                env += up;
                if (env >= 1.f) { env = 1.f; rising = false; }
            }
            else env *= fall;
        }
        else
        {
            float target = std::fabs (x) / 5.f;
            target = target > 1.f ? 1.f : target;
            env = target > env ? (target - env > up ? env + up : target) : std::fmax (target, env * fall);
        }
        return env;
    }

    void reset() { env = 0.f; high = rising = false; }
};

// The RELEASE knob 0..1 -> seconds, log: 20 ms at 0, 1 s at 1. knobOfRelease inverts it.
inline float releaseOf (float knob) { return 0.02f * std::pow (50.f, knob); }
inline float knobOfRelease (float sec) { return std::log (sec / 0.02f) / std::log (50.f); }

// Peak meter with ~100 ms fall. coef = peakCoef (dt), computed once per sample rate.
inline float peakCoef (float dt) { return std::exp (-dt / 0.1f); }
inline float peak (float held, float x, float coef) { const float a = std::fabs (x); return a > held * coef ? a : held * coef; }

// Master soft clip: identity up to 5 V, then a tanh knee with a ceiling of 8 V; slope 1 at the knee, so no corner.
inline float softClip (float x)
{
    const float a = std::fabs (x);
    if (! (a > hotV)) return x == x ? x : 0.f;   // NaN reads as 0 V
    const float y = hotV + 3.f * std::tanh ((a - hotV) / 3.f);
    return x < 0.f ? -y : y;
}
}
