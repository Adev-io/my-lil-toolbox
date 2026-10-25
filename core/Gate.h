// Gate.h: a gate from a trigger stream, for voices that hold a note while the gate is high. It rises on each trig and
// stays high for half the last trig-to-trig period (10 ms before a period is known), clamped to 1 ms..2 s. A trig while
// high restarts the high phase. Periods are counted in samples, so they follow the sample rate exactly.
// ponytail: a fixed 50 %; a GATE LEN knob (fraction of the period) is the upgrade if wanted.
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

    // trig: the section fired this sample. dt: sample time in seconds. Returns the gate's state this sample.
    bool process (bool trig, float dt)
    {
        if (trig)
        {
            float len = timed ? 0.5f * since * dt : firstLen;
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
