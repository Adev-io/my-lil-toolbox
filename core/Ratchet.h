// Ratchet.h: ROLL. Every input trig fires at once; with n > 1 it fires n - 1 more times, evenly spaced across the last
// trig-to-trig period, so each step's note repeats n times. The first trig (no period yet) fires once. No Rack headers.
// Irregular trig streams: later repeats may be cut by the next trig. n is read at each trig: a change, letting go
// included, acts from the next step.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <cstdint>

namespace ratchet
{
struct Ratchet
{
    uint32_t since = 0;    // samples since the last input trig (saturates)
    bool timed = false;    // a trig has happened, so `since` measures a period
    int left = 0;          // repeats still to fire for this step
    uint32_t gap = 0, wait = 0;
    uint32_t period = 0;   // spacing of the latest fire, for Gate: the step period, or the roll gap (0 = unknown)

    // trig: an input trig this sample. n: repeats per step, 1 = off. Returns true when the output fires.
    bool process (bool trig, int n)
    {
        bool out = false;
        if (trig)
        {
            out = true;
            left = 0;
            period = timed ? since : 0;
            // remainder (< n samples) stretches the last gap to the next trig; gap >= 1, so wait never wraps
            if (n > 1 && timed && since >= (uint32_t) n) { gap = since / (uint32_t) n; wait = gap; left = n - 1; period = gap; }
            since = 0;
            timed = true;
        }
        else if (left > 0 && --wait == 0)
        {
            out = true;
            period = gap;
            if (--left > 0) wait = gap;
        }
        if (since < 0x7fffffffu) ++since;
        return out;
    }

    void reset() { since = 0; timed = false; left = 0; gap = wait = period = 0; }
};
}
