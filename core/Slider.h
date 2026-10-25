// Slider.h: the slider sequencer's logic, no Rack: range maths and the playhead for two sections of 8 steps.
// Slider value s is 0..1; volts = lo + s * (hi - lo) for the section's RANGE; INVERT flips s; SNAP rounds to a semitone.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <cmath>

namespace slider
{
constexpr int steps = 8, total = 16;
enum Range { R0_1, R0_2, R0_5, RPM5, R0_10, numRanges };

inline int clampRange (int r) { return r < 0 ? 0 : r >= numRanges ? numRanges - 1 : r; }
inline float lo (int r) { static const float v[numRanges] = { 0.f, 0.f, 0.f, -5.f, 0.f }; return v[clampRange (r)]; }
inline float hi (int r) { static const float v[numRanges] = { 1.f, 2.f, 5.f, 5.f, 10.f }; return v[clampRange (r)]; }
inline float clamp01 (float s) { return s != s ? 0.f : s < 0.f ? 0.f : s > 1.f ? 1.f : s; }

inline float volts (float s, int range, bool invert, bool snap)
{
    s = clamp01 (s);
    if (invert) s = 1.f - s;
    float v = lo (range) + s * (hi (range) - lo (range));
    if (snap) v = std::round (v * 12.f) / 12.f;
    return v;
}

inline float fromVolts (float v, int range)
{
    if (v != v) v = 0.f;   // NaN reads as 0 V
    return clamp01 ((v - lo (range)) / (hi (range) - lo (range)));
}

struct Settings
{
    int length[2] = { steps, steps };
    int range[2] = { R0_5, R0_5 };
    bool invert[2] = { false, false }, reverse[2] = { false, false };
    bool chain = false;
};

struct Out
{
    int idx[2] = { -1, -1 };      // the step each section is on (index into val/gate: A 0..7, B 8..15); held when idle, -1 before the first step
    bool fire[2] = { false, false };    // this section stepped now and the step's gate button is on
    bool moved[2] = { false, false };   // this section stepped now
};

struct Seq
{
    float val[total] = {};
    bool gate[total] = {};
    int pos[2] = { 0, 0 };   // play position per section (0..length-1)
    int cpos = 0;            // play position along the chain (0..lenA+lenB-1)
    bool started = false;    // false until the first clock after load / reset: that clock plays the first step
    int idx[2] = { -1, -1 };

    static int len (const Settings& s, int sec) { return s.length[sec] < 1 ? 1 : s.length[sec] > steps ? steps : s.length[sec]; }
    void reset() { started = false; pos[0] = pos[1] = 0; cpos = 0; }

    // One clock edge. rec[sec]: capture cv[sec] and gateIn[sec] into the step played now.
    Out clock (const Settings& s, const bool rec[2], const float cv[2], const bool gateIn[2])
    {
        Out o;
        int play[2] = { -1, -1 };   // position within the section, or -1 if it does not step this tick
        if (s.chain)
        {
            const int la = len (s, 0), n = la + len (s, 1);
            cpos = started ? (cpos + 1) % n : 0;
            const int sec = cpos < la ? 0 : 1;
            play[sec] = sec ? cpos - la : cpos;
        }
        else
            for (int sec = 0; sec < 2; ++sec)
            {
                pos[sec] = started ? (pos[sec] + 1) % len (s, sec) : 0;
                play[sec] = pos[sec];
            }
        started = true;
        for (int sec = 0; sec < 2; ++sec)
        {
            if (play[sec] < 0) continue;
            const int n = len (s, sec);
            const int i = sec * steps + (s.reverse[sec] ? n - 1 - play[sec] : play[sec]);
            if (rec[sec]) { val[i] = fromVolts (cv[sec], s.range[sec]); gate[i] = gateIn[sec]; }
            idx[sec] = i;
            o.moved[sec] = true;
            o.fire[sec] = gate[i];
        }
        o.idx[0] = idx[0]; o.idx[1] = idx[1];
        return o;
    }
};
}
