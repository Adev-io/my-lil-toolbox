// Gate.h: a gate from a trigger stream, for voices that hold a note while the gate is high. It rises on each trig and
// stays high for `frac` (the GATE knob, 0.05-0.95) of the last trig-to-trig period (10 ms before a period is known),
// clamped to 1 ms..2 s. A trig while high restarts the high phase. Periods are counted in samples, so they follow the
// sample rate exactly. A caller that knows the period of this trig (ROLL) passes it; otherwise the gap since the last trig is used.
// 95 % at most: a gate that never falls gives the voice no new note start.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <cmath>
#include <cstdint>

namespace gate
{
struct Gate
{
    static constexpr float firstLen = 0.01f, minLen = 0.001f, maxLen = 2.f;   // seconds
    uint32_t since = 0;    // samples since the last trig (saturates)
    bool timed = false;    // a trig has happened, so `since` measures a period
    uint32_t left = 0;     // samples of high phase left
    float frac = 0.5f;     // high share of the period (GATE knob); the shell clamps it to 0.05..0.95

    // trig: the section fired this sample. dt: sample time in seconds. period: samples this trig lasts, 0 = unknown.
    // Returns the gate's state this sample.
    bool process (bool trig, float dt, uint32_t period = 0)
    {
        if (trig)
        {
            float len = period ? frac * period * dt : timed ? frac * since * dt : firstLen;
            len = len < minLen ? minLen : len > maxLen ? maxLen : len;
            const long n = std::lround (len / dt);
            left = n < 1 ? 1u : (uint32_t) n;
            since = 0;
            timed = true;
        }
        if (since < 0x7fffffffu) ++since;
        if (! left) return false;
        --left;
        return true;
    }

    void reset() { since = 0; timed = false; left = 0; }
};
}
