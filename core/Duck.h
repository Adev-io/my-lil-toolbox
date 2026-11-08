// Duck.h: My lil Duck's DSP. The sidechain envelope is the Mixer's (core/Mixer.h): an audio follower or a trigger shape,
// 0..1. The gain is 1 - DEPTH x envelope. No Rack headers.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include "Mixer.h"

namespace duck
{
// depth: the DEPTH knob 0..1. env: the sidechain envelope 0..1. Both clamp, so 0 <= gain <= 1.
inline float gain (float depth, float env)
{
    depth = depth < 0.f ? 0.f : depth > 1.f ? 1.f : depth;
    env = env < 0.f ? 0.f : env > 1.f ? 1.f : env;
    return 1.f - depth * env;
}

struct Ducker
{
    mixer::Sidechain sc;

    // x: the sidechain volts. trigger: read x as triggers, not audio. release: seconds. Returns the gain for this sample.
    float process (float x, bool trigger, float depth, float release, float dt)
    {
        return gain (depth, sc.process (x, trigger, release, dt));
    }
    void reset() { sc.reset(); }
};
}
