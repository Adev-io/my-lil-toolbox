// test.cpp: checks the core (scale, arp, section, rhythm, gate, ladder) with plain asserts. Run: make test
// Author: Ari Klopfer / A_Development · MIT
#include "Scale.h"
#include "Arp.h"
#include "Section.h"
#include "Rhythm.h"
#include "FaceTap.h"
#include "Gate.h"
#include "Ladder.h"
#include "Env.h"
#include "Slider.h"
#include "Mixer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
int failures = 0;

void check (bool ok, const char* what, double got = 0.0)
{
    std::printf ("%s  %s  (%g)\n", ok ? "ok  " : "FAIL", what, got);
    if (! ok) ++failures;
}
bool near (double a, double b, double tol = 1e-5) { return std::abs (a - b) <= tol; }
uint16_t maskOf (std::initializer_list<int> pcs) { uint16_t m = 0; for (int p : pcs) m |= (uint16_t) (1u << p); return m; }
typedef std::vector<std::pair<int, uint32_t>> Ticks;   // (sample index, tick number)
const double clockDt = 1.0 / 1024;   // a power of two: 120 BPM is exactly 128 samples per tick and EXT intervals are exact (120 BPM only: under -funsafe-math-optimizations other tempos may round)

// Advances the clock n samples. EXT edges fire on the sample indexes in `edges` (relative to this run). Appends every
// tick played as (sample, tick) to `out` and returns how many this run played.
int runClock (rhythm::Clock& c, int n, bool ext, std::initializer_list<int> edges, bool play, bool perBeat, Ticks& out, float bpm = 120.f)
{
    int count = 0;
    for (int i = 0; i < n; ++i)
    {
        bool edge = false;
        for (int e : edges) edge = edge || e == i;
        uint32_t tick = 0;
        if (c.process (clockDt, bpm, ext, edge, play, perBeat, tick))
        {
            out.push_back (std::make_pair (i, tick));
            ++count;
        }
    }
    return count;
}

bool sameTicks (const Ticks& got, std::initializer_list<std::pair<int, uint32_t>> want)
{
    if (got.size() != want.size()) return false;
    size_t i = 0;
    for (const std::pair<int, uint32_t>& w : want)
        if (got[i++] != w) return false;
    return true;
}

void testPresets()
{
    const uint16_t want[scale::numPresets] = {
        maskOf ({ 0, 2, 4, 5, 7, 9, 11 }), maskOf ({ 0, 2, 3, 5, 7, 8, 10 }), maskOf ({ 0, 2, 3, 5, 7, 8, 11 }),
        maskOf ({ 0, 2, 3, 5, 7, 9, 10 }), maskOf ({ 0, 1, 3, 5, 7, 8, 10 }), maskOf ({ 0, 2, 4, 6, 7, 9, 11 }),
        maskOf ({ 0, 2, 4, 5, 7, 9, 10 }), maskOf ({ 0, 2, 4, 7, 9 }), maskOf ({ 0, 3, 5, 7, 10 }), 0xFFF };
    for (int i = 0; i < scale::numPresets; ++i) check (scale::preset (i) == want[i], scale::presetName (i), scale::preset (i));
    check (std::string (scale::presetName (0)) == "major" && std::string (scale::presetName (9)) == "chromatic", "preset names");
    check (scale::pc (-1) == 11 && scale::pc (12) == 0 && scale::pc (-13) == 11, "pitch class wraps negatives");
    check (scale::inMask (-1, maskOf ({ 11 })) && ! scale::inMask (-1, maskOf ({ 0 })), "inMask below 0 V");
}

void testTypeIndex()
{
    check (scale::typeIndex (0.f, -1) == 0, "type: first read rounds");
    check (scale::typeIndex (3.4f, -1) == 3, "type: 3.4 rounds to 3");
    check (scale::typeIndex (3.55f, 3) == 3, "type: holds inside the band (+0.55)");
    check (scale::typeIndex (3.65f, 3) == 4, "type: moves outside the band (+0.65)");
    check (scale::typeIndex (2.45f, 3) == 3, "type: holds inside the band (-0.55)");
    check (scale::typeIndex (2.35f, 3) == 2, "type: moves outside the band (-0.65)");
    check (scale::typeIndex (12.f, 9) == 9 && scale::typeIndex (-3.f, -1) == 0, "type: clamps to 0..9");
}

void testRngAndClamp()
{
    scale::Rng r1 (42), r2 (42), z (0);
    bool same = true;
    for (int i = 0; i < 100; ++i) same = same && r1.next() == r2.next();
    check (same, "rng: same seed, same stream");
    check (z.next() != 0, "rng: seed 0 still runs");
    check (scale::clampV (15.f) == 10.f && scale::clampV (-15.f) == -10.f && scale::clampV (1.5f) == 1.5f, "clampV");
    check (scale::clampV (std::nanf ("")) == 0.f, "clampV: NaN reads as 0 V");
}
// Reference: nearest in-mask semitone to an integer semitone, the lower one on a tie.
int refNearest (int s, uint16_t mask)
{
    for (int d = 0; d <= 12; ++d)
    {
        if (scale::inMask (s - d, mask)) return s - d;
        if (scale::inMask (s + d, mask)) return s + d;
    }
    return s;
}

void testQuantize()
{
    bool all = true;
    for (int i = 0; i < scale::numPresets; ++i)
        for (int s = -12; s <= 23; ++s)   // three octaves, -1 V .. +2 V
            all = all && near (scale::quantize (s / 12.f, scale::preset (i)), refNearest (s, scale::preset (i)) / 12.0);
    check (all, "quantize table: 10 presets x 3 octaves");
    const uint16_t cb = maskOf ({ 0, 11 });
    check (near (scale::quantize (-0.01f, cb), 0.0), "quantize: -0.01 V snaps up to C4", scale::quantize (-0.01f, cb));
    check (near (scale::quantize (-0.05f, cb), -1.0 / 12), "quantize: -0.05 V snaps down to B3", scale::quantize (-0.05f, cb));
    const uint16_t cd = maskOf ({ 0, 2 });
    check (near (scale::quantize (1.f / 12, cd), 0.0), "quantize: a tie goes to the lower note");
    check (near (scale::quantize (1.2f / 12, cd), 2.0 / 12), "quantize: past the middle goes up");
    check (scale::quantize (0.123f, 0) == 0.123f, "quantize: empty mask passes through");
    check (near (scale::quantize (5.f, scale::preset (0)), 5.0) && near (scale::quantize (-3.f, scale::preset (8)), -3.0),
           "quantize: C in any octave stays put");
    check (scale::nearest (1.1f, scale::preset (0)) == 2, "nearest: C# + 0.1 in major is D");
}
void testDegrees()
{
    const uint16_t maj = scale::preset (0), mp = scale::preset (8);
    check (scale::degreeShift (0, maj, 1) == 2 && scale::degreeShift (0, maj, -1) == -1, "degrees: major +-1");
    check (scale::degreeShift (0, maj, 7) == 12 && scale::degreeShift (0, maj, -7) == -12, "degrees: major +-7 is an octave");
    check (scale::degreeShift (0, maj, 12) == 21 && scale::degreeShift (0, maj, -12) == -20, "degrees: major +-12");
    check (scale::degreeShift (0, mp, 1) == 3 && scale::degreeShift (0, mp, -1) == -2, "degrees: minor pentatonic +-1");
    check (scale::degreeShift (0, mp, 7) == 17 && scale::degreeShift (0, mp, -7) == -17, "degrees: minor pentatonic +-7");
    check (scale::degreeShift (0, mp, 12) == 29 && scale::degreeShift (0, mp, -12) == -29, "degrees: minor pentatonic +-12");
    check (scale::degreeShift (1, maj, 1) == 2 && scale::degreeShift (1, maj, -1) == 0, "degrees: from outside the scale");
    check (scale::degreeShift (5, 0, 3) == 5, "degrees: empty mask leaves the pitch alone");
    check (scale::degreeShift (7, maj, 0) == 7, "degrees: 0 is identity");
}

void testRandomMask()
{
    scale::Rng rng (777);
    const uint16_t first = scale::randomMask (rng);
    bool cAlways = true, noHigh = true, varies = false;
    int on[12] = {};
    for (int i = 0; i < 100; ++i)
    {
        const uint16_t m = scale::randomMask (rng);
        cAlways = cAlways && (m & 1u);
        noHigh = noHigh && ! (m & ~0xFFFu);
        varies = varies || m != first;
        for (int k = 0; k < 12; ++k) on[k] += (m >> k) & 1;
    }
    bool everyBitBoth = true;
    for (int k = 1; k < 12; ++k) everyBitBoth = everyBitBoth && on[k] > 0 && on[k] < 100;
    check (cAlways && noHigh, "random mask: C always in, nothing past B");
    check (varies && everyBitBoth, "random mask: every other note both on and off over 100 rolls");
}
// Steps n times and returns the pitches.
std::vector<int> run (arp::Arp& a, arp::Dir d, int n)
{
    std::vector<int> v;
    for (int i = 0; i < n; ++i) v.push_back (a.step (d));
    return v;
}

void testArpPool()
{
    arp::Arp a;
    a.build (0, scale::preset (0), 1);
    check (a.size == 7 && a.pool[0] == 0 && a.pool[6] == 11, "pool: major, 1 octave", a.size);
    a.build (0, scale::preset (0), 2);
    check (a.size == 14 && a.pool[13] == 23, "pool: major, 2 octaves", a.size);
    a.build (2, scale::preset (0), 1);
    check (a.size == 7 && a.pool[0] == 2 && a.pool[6] == 12, "pool: starts at the base note (D)");
    a.build (0, 0xFFF, 4);
    check (a.size == arp::maxPool && a.pool[47] == 47, "pool: chromatic, 4 octaves fills it", a.size);
    a.build (0, 0xFFF, 9);
    check (a.size == arp::maxPool, "pool: octaves clamp to 4", a.size);
    a.build (5, 0, 3);
    check (a.size == 1 && a.pool[0] == 5, "pool: empty mask holds the base note");
}

void testArpDirections()
{
    arp::Arp a;
    a.build (0, scale::preset (0), 1);
    check (run (a, arp::UP, 8) == std::vector<int> { 0, 2, 4, 5, 7, 9, 11, 0 }, "arp: up starts low and wraps");
    a.reset();
    check (run (a, arp::DOWN, 8) == std::vector<int> { 11, 9, 7, 5, 4, 2, 0, 11 }, "arp: down starts on top after reset, wraps");
    arp::Arp b;
    b.build (0, maskOf ({ 0, 4, 7 }), 1);
    check (run (b, arp::UPDOWN, 7) == std::vector<int> { 0, 4, 7, 4, 0, 4, 7 }, "arp: up-down does not repeat the ends");
    b.step (arp::UPDOWN);   // now heading down
    b.reset();
    check (run (b, arp::UPDOWN, 3) == std::vector<int> { 0, 4, 7 }, "arp: reset, up-down restarts going up");
    arp::Arp c;
    c.build (0, scale::preset (0), 1);
    run (c, arp::UP, 3);
    c.reset();
    check (c.step (arp::UP) == 0, "arp: reset, up plays the lowest note next");
}

void testArpEdges()
{
    arp::Arp a;
    a.build (0, scale::preset (0), 1);
    run (a, arp::UP, 7);                       // on index 6 (B)
    a.build (0, maskOf ({ 0, 4, 7 }), 1);      // pool shrinks to 3: index 6 wraps to 0
    check (a.idx == 0 && a.step (arp::UP) == 4, "arp: a shrinking pool wraps the index");
    arp::Arp one;
    one.build (0, maskOf ({ 0 }), 2);
    check (run (one, arp::UP, 4) == std::vector<int> { 0, 12, 0, 12 }, "arp: single-note scale repeats it each octave");
    arp::Arp solo;
    solo.build (3, maskOf ({ 3 }), 1);
    check (run (solo, arp::RANDOM, 3) == std::vector<int> { 3, 3, 3 }, "arp: a pool of 1 repeats, even in random");
}
void testArpRandom()
{
    arp::Arp a (12345);
    a.build (0, scale::preset (0), 1);
    a.step (arp::RANDOM);   // the first step after construction plays the lowest note
    int prev = a.idx, hits[7] = {};
    bool repeat = false;
    for (int i = 0; i < 1000; ++i)
    {
        a.step (arp::RANDOM);
        repeat = repeat || a.idx == prev;
        prev = a.idx;
        ++hits[a.idx];
    }
    bool all = true;
    for (int h : hits) all = all && h > 0;
    check (! repeat, "arp random: never the same note twice in a row (1000 steps)");
    check (all, "arp random: every pool note gets played");
    arp::Arp r1 (99), r2 (99);
    r1.build (0, scale::preset (0), 2);
    r2.build (0, scale::preset (0), 2);
    check (run (r1, arp::RANDOM, 50) == run (r2, arp::RANDOM, 50), "arp random: same seed, same notes");
    arp::Arp f (7);
    f.build (0, scale::preset (0), 1);
    run (f, arp::RANDOM, 5);
    f.reset();
    check (f.step (arp::RANDOM) == 0, "arp random: reset plays the lowest note next");
}
section::Settings majorSettings()
{
    section::Settings s;
    s.mask = scale::preset (0);
    return s;
}

void testSectionQuantizer()
{
    section::Settings s = majorSettings();
    section::Section q;
    section::Out o = q.process (1.1f / 12, false, false, s);
    check (near (o.volts, 2.0 / 12) && o.pc == 2 && ! o.trig, "section: continuous quantize, no trigger on the first note");
    o = q.process (4.f / 12, false, false, s);
    check (near (o.volts, 4.0 / 12) && o.trig, "section: a new note fires TRIG out");
    o = q.process (4.2f / 12, false, false, s);
    check (! o.trig, "section: same note, no trigger");

    section::Section h;   // TRIG patched: sample and hold
    h.process (0.f, true, true, s);
    o = h.process (7.f / 12, false, true, s);
    check (near (o.volts, 0.0), "section: patched TRIG holds between edges");
    o = h.process (7.f / 12, true, true, s);
    check (near (o.volts, 7.0 / 12) && o.trig, "section: an edge samples the new note");

    s.passThrough = true;
    section::Section p;
    p.process (0.f, false, false, s);
    o = p.process (9.f / 12, false, false, s);
    check (! o.trig, "section: pass-through ignores note changes");
    o = p.process (9.f / 12, true, true, s);
    check (o.trig, "section: pass-through fires on each TRIG edge");
}

void testSectionPitch()
{
    section::Settings s = majorSettings();
    section::Section x;
    s.mask = 0;
    section::Out o = x.process (0.123f, false, false, s);
    check (near (o.volts, 0.123) && o.pc == -1, "section: empty mask passes CV through");
    s.mask = scale::preset (0);
    s.axis = 12;
    o = x.process (4.f / 12, false, false, s);
    check (near (o.volts, 16.0 / 12) && o.pc == 4, "section: AXIS shifts the output, the key light keeps the scale note");
    s.axis = 0;
    s.degrees = 2;
    o = x.process (0.f, false, false, s);
    check (near (o.volts, 4.0 / 12) && o.pc == 4, "section: INTERVAL +2 degrees from C is E");
    s.degrees = 0;
    s.axis = 12;
    o = x.process (15.f, false, false, s);
    check (near (o.volts, 10.0), "section: input and output clamp to 10 V");
    o = x.process (-15.f, false, false, s);   // s.axis is still 12: -10 V clamped in, +1 octave = -9 V
    check (near (o.volts, -9.0), "section: input clamps before the math", o.volts);
}

void testSectionArp()
{
    section::Settings s = majorSettings();
    s.arpMode = true;
    section::Section a;
    std::vector<int> got;
    for (int i = 0; i < 8; ++i) got.push_back (a.process (0.f, true, true, s).pc);
    check (got == std::vector<int> { 0, 2, 4, 5, 7, 9, 11, 0 }, "section arp: steps up the scale on each edge");
    section::Out o = a.process (0.f, false, true, s);
    check (o.pc == 0 && ! o.trig, "section arp: holds between edges, no trigger");
    o = a.process (0.f, true, true, s);
    check (o.trig && o.pc == 2, "section arp: TRIG out passes each edge");

    s.degrees = 2;
    section::Section b;
    o = b.process (0.f, true, true, s);
    check (near (o.volts, 4.0 / 12) && o.pc == 4, "section arp: INTERVAL shifts the arp note");

    s.degrees = 0;
    s.octaves = 2;
    section::Section r;
    r.process (0.f, true, true, s);
    r.process (0.f, true, true, s);
    r.arpeggiator.reset();
    o = r.process (0.f, true, true, s);
    check (o.pc == 0 && near (o.volts, 0.0), "section arp: after reset the next edge plays the first note");

    section::Section m;
    o = m.process (2.f / 12, true, true, s);
    check (o.pc == 2 && near (o.volts, 2.0 / 12), "section arp: CV in sets the base note");

    s.mask = 0;
    section::Section e;
    o = e.process (0.3f, true, true, s);
    check (near (o.volts, 0.3) && o.trig, "section arp: empty mask holds the CV on each edge");
}
// R (or R') of the grid engine over ticks 0..n-1: 'x' = fires, '.' = silent
std::string gridPattern (const int period[4], unsigned on, int density, bool wantR, int n)
{
    std::string s;
    for (int t = 0; t < n; ++t)
    {
        const rhythm::Tick k = rhythm::grid ((uint32_t) t, period, on, density);
        s += (wantR ? k.r : k.rp) ? 'x' : '.';
    }
    return s;
}

void testRhythmCycle()
{
    bool divides = true;
    for (uint32_t p = 1; p <= 16; ++p) divides = divides && rhythm::wrap % p == 0;
    check (divides, "rhythm: every period 1..16 divides the wrap");
    check (rhythm::clampPeriod (0) == 1 && rhythm::clampPeriod (17) == 16 && rhythm::clampPeriod (7) == 7, "rhythm: clampPeriod");
    check (rhythm::lcm (4, 6) == 12, "rhythm: lcm (4, 6)", rhythm::lcm (4, 6));
    const int p345[4] = { 3, 4, 5, 2 };
    check (rhythm::cycle (p345, 0x7u) == 60, "cycle {3,4,5} = 60", rhythm::cycle (p345, 0x7u));
    const int p24[4] = { 2, 4, 1, 1 };
    check (rhythm::cycle (p24, 0x3u) == 4, "cycle {2,4} = 4", rhythm::cycle (p24, 0x3u));
    const int big[4] = { 16, 15, 13, 11 };
    check (rhythm::cycle (big, 0xFu) == 34320, "cycle {16,15,13,11} = 34320", rhythm::cycle (big, 0xFu));
    check (rhythm::cycle (big, 0u) == 0, "cycle: nothing on = 0");
}

void testRhythmGrid()
{
    const int p[4] = { 2, 3, 4, 5 };   // A=2, B=3, C=4 on (0x7); D=5 off
    check (gridPattern (p, 0x7u, 1, true, 12) == "x.xxx.x.xxx.", "grid density 1: R is any hit");
    check (gridPattern (p, 0x7u, 1, false, 12) == ".x...x.x...x", "grid density 1: R' fills the gaps");
    check (gridPattern (p, 0x7u, 2, true, 12) == "x...x.x.x...", "grid density 2: R");
    check (gridPattern (p, 0x7u, 2, false, 12) == ".xxx.x.x.xxx", "grid density 2: R' is every other tick");
    check (gridPattern (p, 0x7u, 3, true, 12) == "x...........", "grid density 3: R");
    check (gridPattern (p, 0x7u, 3, false, 12) == ".xxxxxxxxxxx", "grid density 3: R'");
    check (gridPattern (p, 0x7u, 4, true, 12) == "............", "grid density 4 above the active count: R never");
    check (gridPattern (p, 0x7u, 0, true, 2) == "x.", "grid: density below 1 acts as 1");
    check (rhythm::grid (6, p, 0x7u, 1).gens == 0x3u, "grid: tick 6 fires A and B", rhythm::grid (6, p, 0x7u, 1).gens);
    check (rhythm::grid (0, p, 0xFu, 1).gens == 0xFu, "grid: the downbeat fires every on generator");
    check (gridPattern (p, 0u, 1, true, 8) == "........" && gridPattern (p, 0u, 1, false, 8) == "xxxxxxxx",
           "grid: all off, R silent and R' every tick");
    const int one[4] = { 1, 9, 9, 9 };
    check (gridPattern (one, 0x1u, 1, true, 5) == "xxxxx", "grid: period 1 fires every tick");
    bool accentOk = true;
    for (uint32_t t = 0; t <= 36; ++t) accentOk = accentOk && rhythm::grid (t, p, 0x7u, 1).accent == (t % 12 == 0);
    check (accentOk, "grid: ACCENT every 12 ticks for {2,3,4}");
    bool silent = true;
    for (uint32_t t = 0; t <= 12; ++t) silent = silent && ! rhythm::grid (t, p, 0u, 1).accent;
    check (silent, "grid: ACCENT silent with nothing on");
}

void testRhythmCounter()
{
    rhythm::Counter c;
    check (c.armed && c.next() == 0 && c.next() == 1 && c.next() == 2, "counter: starts armed at the downbeat");
    c.reset();
    check (c.next() == 0, "counter: reset arms tick 0");
    c.t = rhythm::wrap - 1;
    check (c.next() == 0, "counter: wraps 720719 -> 0");
    c.t = 1;
    check (c.nextBeat() == 4 && c.nextBeat() == 8, "counter: nextBeat jumps to the next multiple of 4");
    c.reset();
    check (c.nextBeat() == 0, "counter: nextBeat after reset plays tick 0");
    c.t = rhythm::wrap - 3;
    check (c.nextBeat() == 0, "counter: nextBeat wraps");
}
// Runs the wave detector over ticks 0..ticks at 64 samples per tick; records where R and R' fire (in ticks).
void waveCrossings (const int period[4], unsigned on, int density, double ticks, std::vector<double>& rAt, std::vector<double>& rpAt)
{
    rhythm::Crossing c;
    const float th = rhythm::threshold (density);
    for (int i = 0; i < (int) (ticks * 64); ++i)
    {
        const double T = i / 64.0;
        bool r = false, rp = false;
        c.process (rhythm::waveSum (T, period, on), th, on != 0, r, rp);
        if (r) rAt.push_back (T);
        if (rp) rpAt.push_back (T);
    }
}

void testRhythmWave()
{
    const int p[4] = { 4, 3, 2, 5 };
    check (near (rhythm::waveSum (1.0, p, 0x1u), 1.0) && near (rhythm::waveSum (3.0, p, 0x1u), -1.0),
           "wave: period 4 peaks at T=1 and dips at T=3");
    check (near (rhythm::waveSum (0.5, p, 0x3u), (std::sin (rhythm::twoPi * 0.5 / 4) + std::sin (rhythm::twoPi * 0.5 / 3)) / 2),
           "wave: the sum is the mean of the on sines", rhythm::waveSum (0.5, p, 0x3u));
    check (rhythm::waveSum (1.3, p, 0u) == 0.f, "wave: nothing on, sum 0");
    check (rhythm::threshold (1) == 0.f && rhythm::threshold (4) == 0.75f && rhythm::threshold (0) == 0.f && rhythm::threshold (9) == 0.75f,
           "wave: threshold per density, clamped");

    std::vector<double> r, rp;
    waveCrossings (p, 0x1u, 1, 8.0, r, rp);
    check (r.size() == 2 && r[0] < 0.02 && std::abs (r[1] - 4.0) < 0.02, "wave: one generator, R on its beats", (double) r.size());
    check (rp.size() == 2 && std::abs (rp[0] - 2.0) < 0.02 && std::abs (rp[1] - 6.0) < 0.02, "wave: R' halfway between", (double) rp.size());

    r.clear();
    rp.clear();
    waveCrossings (p, 0x1u, 3, 4.0, r, rp);   // threshold 0.5: sin rises past +0.5 at T = 1/3, falls past -0.5 at T = 7/3
    check (r.size() == 1 && std::abs (r[0] - 1.0 / 3.0) < 0.02, "wave: density 3 fires R at the +0.5 crossing", r.empty() ? -1.0 : r[0]);
    check (rp.size() == 1 && std::abs (rp[0] - 7.0 / 3.0) < 0.02, "wave: density 3 fires R' at the -0.5 crossing", rp.empty() ? -1.0 : rp[0]);

    std::vector<double> r1, rp1, r4, rp4;
    waveCrossings (p, 0x6u, 1, 12.0, r1, rp1);
    waveCrossings (p, 0x6u, 4, 12.0, r4, rp4);
    check (! r1.empty() && r4.size() < r1.size(), "wave: density 4 fires fewer R than density 1", (double) r4.size());

    r.clear();
    rp.clear();
    waveCrossings (p, 0u, 1, 8.0, r, rp);
    check (r.empty() && rp.empty(), "wave: nothing on, silent");

    rhythm::Crossing c;
    bool a = false, b = false;
    c.process (0.f, 0.f, true, a, b);
    check (a && ! b, "wave: after reset the downbeat fires R");
    c.process (0.5f, 0.f, false, a, b);   // nothing on
    check (! a && ! b, "wave: inactive, no triggers");
    c.process (0.3f, 0.f, true, a, b);    // a generator switched on mid-wave, above 0
    check (! a && ! b, "wave: switching a generator on mid-wave does not fire");
    c.process (0.f, 0.25f, false, a, b);  // nothing on again
    c.process (0.6f, 0.25f, true, a, b);  // switched on with S already past +th
    check (! a && ! b, "wave: switching on above the threshold does not fire R");
    c.process (0.f, 0.5f, false, a, b);
    c.process (-0.7f, 0.5f, true, a, b);  // switched on with S already past -th
    check (! a && ! b, "wave: switching on below the threshold does not fire R'");
    c.process (0.f, 0.f, false, a, b);   // an inactive stretch before RESET: reset must re-prime
    c.reset();
    c.process (0.f, 0.f, true, a, b);
    check (a && ! b, "wave: reset re-arms the downbeat");
}
// The clock at 1024 Hz. Each check replays one rule of the spec or one of the fixed review bugs (F13, F15, F16, F17, F20).
void testRhythmClock()
{
    {   // 1. internal 120 BPM: the downbeat at once, then one tick every 128 samples
        rhythm::Clock c;
        Ticks t;
        runClock (c, 400, false, {}, true, false, t);
        check (sameTicks (t, { { 0, 0 }, { 128, 1 }, { 256, 2 }, { 384, 3 } }), "clock: internal 120 BPM ticks every 128 samples from sample 0", t.size());
    }
    {   // 2. internal stop holds the phase; resume continues from it
        rhythm::Clock c;
        Ticks t;
        runClock (c, 200, false, {}, true, false, t);   // ticks 0 and 1; phase 71/128 afterwards
        Ticks stopped;
        const int n = runClock (c, 1000, false, {}, false, false, stopped);
        c.resume();
        Ticks resumed;
        runClock (c, 100, false, {}, true, false, resumed);
        check (n == 0 && sameTicks (resumed, { { 56, 2 } }), "clock: stopped internal clock holds; resume plays tick 2 after the remaining 57 samples", resumed.empty() ? -1 : resumed[0].first);
    }
    {   // 3. EXT per 16th: one tick per edge; frac 0 until the second edge, then rising, capped at 0.999 when edges stop
        rhythm::Clock c;
        Ticks t;
        runClock (c, 100, true, { 0 }, true, false, t);
        const bool fracZero = c.frac == 0.f;
        runClock (c, 50, true, { 0 }, true, false, t);   // second edge, 100 samples after the first
        const bool fracHalf = near (c.frac, 0.49);
        const int late = runClock (c, 300, true, {}, true, false, t);
        check (sameTicks (t, { { 0, 0 }, { 0, 1 } }) && fracZero && fracHalf && late == 0 && near (c.frac, 0.999),
               "clock: EXT per 16th plays a tick per edge; frac 0, then 0.49, then holds at 0.999", c.frac);
    }
    {   // 4. EXT per beat: in-between ticks at a quarter of the measured interval; an early edge drops the rest
        rhythm::Clock c;
        Ticks t;
        runClock (c, 100, true, { 0, 40, 80 }, true, true, t);
        const bool spaced = sameTicks (t, { { 0, 0 }, { 40, 4 }, { 50, 5 }, { 60, 6 }, { 70, 7 }, { 80, 8 }, { 90, 9 } });
        Ticks early;
        runClock (c, 60, true, { 5 }, true, true, early);   // tick 10 is due at sample 0; the edge at 5 is early
        bool jumped = early.size() == 5;
        if (jumped)
        {
            const uint32_t want[5] = { 10, 12, 13, 14, 15 };
            for (int i = 0; i < 5; ++i) jumped = jumped && early[i].second == want[i];
            jumped = jumped && early[1].first == 5;
        }
        check (spaced && jumped, "clock: EXT per beat fills three ticks per beat; an early edge skips to the next beat", early.size());
    }
    {   // 5. F13: while stopped, EXT edges are ignored and the timers do not advance
        rhythm::Clock c;
        Ticks t;
        runClock (c, 150, true, { 0, 100 }, true, false, t);   // frac 0.49 afterwards
        Ticks stopped;
        const int n = runClock (c, 500, true, { 50, 150 }, false, false, stopped);
        check (n == 0 && near (c.frac, 0.49) && c.counter.t == 1, "clock: F13 stopped EXT ignores edges and freezes frac", c.frac);
    }
    {   // 6. F15: the first edge after a resume keeps the old tick length
        rhythm::Clock c;
        Ticks t;
        runClock (c, 100, true, { 0, 40 }, true, true, t);   // tickLen = 10 samples
        Ticks stopped;
        runClock (c, 2000, true, {}, false, true, stopped);
        c.resume();
        Ticks after;
        runClock (c, 100, true, { 0, 40 }, true, true, after);   // 60 samples since the last edge, yet spacing stays 10
        check (sameTicks (after, { { 0, 8 }, { 10, 9 }, { 20, 10 }, { 30, 11 }, { 40, 12 }, { 50, 13 }, { 60, 14 }, { 70, 15 } }), "clock: F15 first edge after resume keeps the measured tick length", after.size());
    }
    {   // 7. F16: unpatching EXT continues the internal clock from frac, no early tick
        rhythm::Clock c;
        Ticks t;
        runClock (c, 151, true, { 0, 100 }, true, false, t);   // frac 0.5 afterwards
        Ticks internal;
        runClock (c, 150, false, {}, true, false, internal);   // 150 < 63 + 128: only one tick fits
        check (sameTicks (internal, { { 63, 2 } }), "clock: F16 unpatched EXT hands the internal clock phase 0.5 (tick after 64 samples)", internal.empty() ? -1 : internal[0].first);
    }
    {   // 8. F17: switching per beat -> per 16th mid-beat plays no leftover in-between tick
        rhythm::Clock c;
        Ticks t;
        runClock (c, 45, true, { 0, 40 }, true, true, t);   // pending 3 after the edge at 40
        Ticks leftover;
        const int n = runClock (c, 100, true, {}, true, false, leftover);
        Ticks next;
        runClock (c, 1, true, { 0 }, true, false, next);
        check (n == 0 && sameTicks (next, { { 0, 5 } }), "clock: F17 per-16th mode plays no pending per-beat tick; the next edge plays tick 5", n);
    }
    {   // 9. F20: reset and resume in the same sample keep the resume flag and arm tick 0
        rhythm::Clock c;
        Ticks t;
        runClock (c, 100, true, { 0, 40 }, true, true, t);
        Ticks stopped;
        runClock (c, 500, true, {}, false, true, stopped);
        c.resume();   // RUN...
        c.reset();    // ...and RESET in the same sample
        Ticks after;
        runClock (c, 100, true, { 0, 40 }, true, true, after);
        check (sameTicks (after, { { 0, 0 }, { 10, 1 }, { 20, 2 }, { 30, 3 }, { 40, 4 }, { 50, 5 }, { 60, 6 }, { 70, 7 } }), "clock: F20 RESET with RUN keeps the old tick length and plays tick 0", after.size());
    }
    {   // 10. RESET on EXT: the next edge plays tick 0 instead of advancing
        rhythm::Clock c;
        Ticks t;
        runClock (c, 250, true, { 0, 100, 200 }, true, false, t);
        c.reset();
        Ticks after;
        runClock (c, 50, true, { 0 }, true, false, after);
        check (sameTicks (after, { { 0, 0 } }) && c.counter.t == 0, "clock: RESET on EXT makes the next edge play tick 0", c.counter.t);
    }
    {   // 11. resume after a single edge: the stop forgets that edge, so the next edge counts as the first again
        rhythm::Clock c;
        Ticks t;
        runClock (c, 50, true, { 0 }, true, false, t);   // one edge: pulses 1
        Ticks stopped;
        runClock (c, 100, true, {}, false, false, stopped);
        c.resume();   // pulses < 2: back to 0
        Ticks b;
        runClock (c, 60, true, { 0 }, true, false, b);   // first edge again: no interval measured, frac stays 0
        check (sameTicks (b, { { 0, 1 } }) && c.frac == 0.f, "clock: resume after one edge restarts the interval measurement (frac stays 0)", c.frac);
    }
    {   // 12. unpatching EXT forgets the measured interval; re-patching measures it again from two fresh pulses
        rhythm::Clock c;
        Ticks t;
        runClock (c, 150, true, { 0, 100 }, true, false, t);   // tickLen 100 samples
        Ticks internal;
        runClock (c, 10, false, {}, true, false, internal);   // unpatched for 10 samples: no tick, pulses back to 0
        Ticks c1;
        runClock (c, 50, true, { 0 }, true, false, c1);   // first fresh pulse: tick 2, frac 0 while waiting for the second
        const bool waiting = sameTicks (c1, { { 0, 2 } }) && c.frac == 0.f;
        Ticks c2;
        runClock (c, 30, true, { 0 }, true, false, c2);   // second: tickLen is now 50 samples, so 29 samples later frac = 0.58
        check (internal.empty() && waiting && sameTicks (c2, { { 0, 3 } }) && near (c.frac, 0.58), "clock: re-patched EXT measures a fresh interval (frac 0, then 29/50)", c.frac);
    }
}
// Runs the gate n samples with trigs on the sample indexes in `trigs` (relative to this run); returns each sample's state.
std::vector<bool> runGate (gate::Gate& g, int n, std::initializer_list<int> trigs, float dt = 1e-3f)
{
    std::vector<bool> high;
    for (int i = 0; i < n; ++i)
    {
        bool trig = false;
        for (int t : trigs) trig = trig || t == i;
        high.push_back (g.process (trig, dt));
    }
    return high;
}

// True when the gate is high exactly on samples [from, to) of `h` and low on every other sample from `from` on.
bool highOnly (const std::vector<bool>& h, int from, int to)
{
    for (int i = from; i < (int) h.size(); ++i)
        if (h[i] != (i < to)) return false;
    return true;
}

void testGate()
{
    {
        gate::Gate g;
        check (highOnly (runGate (g, 100, { 0 }), 0, 10), "gate: first trig gives 10 ms");
    }
    {
        gate::Gate g;
        const std::vector<bool> h = runGate (g, 400, { 0, 100, 200, 300 });
        check (highOnly (h, 300, 350) && h[100] && h[149] && ! h[150], "gate: steady 100 ms period gives 50 ms");
    }
    {
        gate::Gate g;
        check (highOnly (runGate (g, 400, { 0, 100, 200, 260 }), 260, 290), "gate: a period change sets the next gate (60 ms -> 30 ms)");
    }
    {
        gate::Gate g;
        const std::vector<bool> h = runGate (g, 300, { 0, 100, 140 });   // 140 lands inside 100's 50 ms gate
        check (highOnly (h, 100, 160), "gate: a retrigger while high restarts it (40 ms -> 20 ms from the retrigger)");
    }
    {
        gate::Gate g;
        bool any = false;
        for (bool b : runGate (g, 5000, {})) any = any || b;
        check (! any, "gate: no trig, no gate");
        runGate (g, 200, { 0, 100 });
        any = false;
        for (bool b : runGate (g, 5000, {})) any = any || b;
        check (! any, "gate: low while idle after a gate");
    }
    {
        gate::Gate g;   // 10 kHz, trigs 0.4 ms apart: half is 0.2 ms, clamped up to 1 ms = 10 samples
        check (highOnly (runGate (g, 100, { 0, 4, 8 }, 1e-4f), 8, 18), "gate: clamps up to 1 ms");
    }
    {
        gate::Gate g;   // 6 s between trigs (clock stopped): half is 3 s, clamped down to 2 s
        check (highOnly (runGate (g, 10000, { 0, 6000 }), 6000, 8000), "gate: clamps down to 2 s");
    }
    {
        gate::Gate g;
        runGate (g, 300, { 0, 100, 200 });
        g.reset();
        check (highOnly (runGate (g, 100, { 0 }), 0, 10), "gate: after reset the first trig gives 10 ms again");
    }
}
// Peak output of a sine through a fresh filter: 0.5 s to settle, then the peak of the next 0.25 s. 44.1 kHz.
const double ladderSr = 44100.0;
double ladderPeak (float hz, float volts, float cutoffHz, float res, float drive)
{
    ladder::Ladder f;
    f.prepare (ladderSr);
    f.set (cutoffHz, res, drive);
    double peak = 0.0;
    const int settle = (int) (0.5 * ladderSr), n = settle + (int) (0.25 * ladderSr);
    for (int i = 0; i < n; ++i)
    {
        const float y = f.process (volts * (float) std::sin (2.0 * 3.141592653589793 * hz * i / ladderSr));
        if (i >= settle) peak = std::max (peak, (double) std::fabs (y));
    }
    return peak;
}
double ladderDb (float hz, float cutoffHz, float res, float drive = 0.f)
{
    return 20.0 * std::log10 (ladderPeak (hz, 0.1f, cutoffHz, res, drive) / 0.1);   // 100 mV: well under the output knee
}

void testLadderResponse()
{
    {
        ladder::Ladder f;
        f.prepare (ladderSr);
        f.set (100.f, 0.f, 0.f);
        float y = 0.f;
        for (int i = 0; i < (int) ladderSr; ++i) y = f.process (1.f);
        check (near (y, 1.0, 1e-3), "ladder: 1 V DC passes at a 100 Hz cutoff", y);
    }
    const double lo = ladderDb (50.f, 8000.f, 0.f);
    check (std::abs (lo) < 0.1, "ladder: drive 0, res 0: a 50 Hz tone passes at unity (dB)", lo);
    const double at2 = ladderDb (2000.f, 500.f, 0.f), at4 = ladderDb (4000.f, 500.f, 0.f), at15 = ladderDb (1414.f, 500.f, 0.f);
    std::printf ("      ladder measured: 500 Hz cutoff: 1414 Hz %.1f dB, 2 kHz %.1f dB, 4 kHz %.1f dB, slope 2-4 kHz %.1f dB/oct\n", at15, at2, at4, at4 - at2);
    check (at15 < -30.0, "ladder: 1.5 octaves above cutoff is more than 30 dB down", at15);
    check (at4 - at2 < -21.0 && at4 - at2 > -27.0, "ladder: about 24 dB per octave above cutoff (dB/oct)", at4 - at2);
    const double flat = ladderDb (1000.f, 1000.f, 0.f), peaked = ladderDb (1000.f, 1000.f, 1.f), half = ladderDb (1000.f, 1000.f, 0.5f);
    std::printf ("      ladder measured: gain at cutoff (1 kHz): res 0 %.1f dB, res 0.5 %.1f dB, res 1 %.1f dB\n", flat, half, peaked);
    check (half > flat + 15.0 && peaked > half + 5.0, "ladder: resonance raises the gain at cutoff (res 1 vs 0, dB)", peaked - flat);   // measured +19.5, then +7.4
    const double d1 = 20.0 * std::log10 (ladderPeak (100.f, 5.f, 8000.f, 0.f, 1.f) / 5.0);
    check (std::abs (d1) < 1.0, "ladder: full drive keeps a 5 V peak near 5 V (dB)", d1);
    const double small0 = ladderDb (100.f, 8000.f, 0.f, 0.f), small1 = 20.0 * std::log10 (ladderPeak (100.f, 0.05f, 8000.f, 0.f, 1.f) / 0.05);
    check (small1 > small0 + 12.0, "ladder: drive lifts quiet signals (dB at 50 mV, drive 1)", small1);
}

void testLadderSafety()
{
    ladder::Ladder f;
    f.prepare (ladderSr);
    f.set (1000.f, 1.f, 1.f);
    bool bounded = true;
    for (int i = 0; i < 20000; ++i)
    {
        const float y = f.process ((i / 20) % 2 ? 1e6f : -1e6f);
        bounded = bounded && y == y && std::fabs (y) <= 10.f;
    }
    check (bounded, "ladder: +-1e6 V square at full res and drive stays finite and within 10 V");
    const float n = f.process (std::nanf (""));
    float after = 0.f;
    for (int i = 0; i < 100; ++i) after = f.process (0.5f);
    check (n == n && after == after && std::fabs (after) <= 10.f, "ladder: a NaN input does not poison the state", after);
    f.set (std::nanf (""), std::nanf (""), std::nanf (""));
    const float bad = f.process (1.f);
    check (bad == bad && std::fabs (bad) <= 10.f, "ladder: NaN settings are ignored", bad);

    f.reset();
    bool zero = true;
    for (int i = 0; i < 64; ++i) zero = zero && f.process (0.f) == 0.f;
    check (zero, "ladder: reset clears the state (silence in, silence out)");

    ladder::Ladder s;
    s.prepare (ladderSr);
    scale::Rng rng (5);
    bool stable = true;
    float peak = 0.f;
    for (int i = 0; i < (int) (2 * ladderSr); ++i)
    {
        const float hz = 60.f * std::pow (2.f, 10.f * (float) i / (float) (2 * ladderSr));   // 60 Hz .. 61 kHz, clamped at 0.45 * sr
        if (i % 4 == 0) s.set (hz, 1.f, 0.5f);
        const float y = s.process (10.f * ((float) (rng.next() % 2001) / 1000.f - 1.f));
        stable = stable && y == y && std::fabs (y) <= 10.f;
        peak = std::max (peak, std::fabs (y));
    }
    check (stable && peak > 0.1f, "ladder: noise through a sweep to Nyquist at full res stays finite and within 10 V", peak);
    s.set (1e9f, 1.f, 0.f);
    float top = 0.f;
    for (int i = 0; i < 1000; ++i) top = s.process (i % 2 ? 5.f : -5.f);
    check (top == top && std::fabs (top) <= 10.f, "ladder: a cutoff far above Nyquist clamps", top);
}
// ---- tests (each task adds its functions above this line)
}

void faceTapChecks()
{
    // 48 kHz: stride = 48000*0.04/48 = 40 samples per point
    FaceTap t;
    for (int i = 0; i < 40; ++i) t.push (i == 7 ? 1.f : 0.f, 48000.f);       // one 1-sample pulse inside one stride
    check (t.buf[0] == 1.f, "FaceTap keeps a pulse shorter than the stride", t.buf[0]);
    check (t.head == 1, "FaceTap advances one point per stride", t.head);

    FaceTap n;
    for (int i = 0; i < 40; ++i) n.push (i == 3 ? -0.9f : 0.5f, 48000.f);
    check (n.buf[0] == -0.9f, "FaceTap keeps the largest magnitude with its sign", n.buf[0]);

    FaceTap w;
    for (int i = 0; i < 40 * FaceTap::N * 2; ++i) w.push (0.25f, 48000.f);
    check (w.head == 0, "FaceTap head wraps after N points", w.head);

    FaceTap s;
    for (int i = 0; i < 39; ++i) s.push (1.f, 48000.f);
    check (s.head == 0, "FaceTap writes nothing before a full stride", s.head);

    FaceTap lo;
    lo.push (1.f, 100.f);                                                       // stride clamps to 1 at tiny rates
    check (lo.head == 1, "FaceTap stride never drops below 1", lo.head);
}

void sliderChecks()
{
    using namespace slider;
    // range maths
    check (near (volts (0.5f, R0_5, false, false), 2.5), "slider: 0-5 V centre is 2.5 V");
    check (near (volts (0.f, RPM5, false, false), -5.0) && near (volts (1.f, RPM5, false, false), 5.0), "slider: +-5 V ends");
    check (near (volts (0.25f, R0_10, true, false), 7.5), "slider: invert flips about the centre (0.25 -> 7.5 V)");
    check (near (volts (0.1f, R0_1, false, true), 1.0 / 12.0), "slider: snap rounds to a semitone (0.1 V -> 1/12)");
    check (near (volts (2.f, R0_1, false, false), 1.0) && near (volts (-1.f, R0_1, false, false), 0.0), "slider: out-of-range value clamps");
    check (near (fromVolts (2.5f, R0_5), 0.5) && near (fromVolts (-5.f, RPM5), 0.0), "slider: fromVolts inverts volts");
    check (near (fromVolts (NAN, RPM5), 0.5), "slider: NaN CV reads as 0 V");

    Settings s;
    const bool noRec[2] = { false, false };
    const float cv0[2] = { 0.f, 0.f };
    const bool g0[2] = { false, false };

    // forward order, length 4
    { Seq q; s = Settings(); s.length[0] = 4; std::vector<int> got;
      for (int i = 0; i < 6; ++i) got.push_back (q.clock (s, noRec, cv0, g0).idx[0]);
      check (got == std::vector<int> ({ 0, 1, 2, 3, 0, 1 }), "slider: forward order wraps at LENGTH"); }
    // reverse, length 4
    { Seq q; s = Settings(); s.length[0] = 4; s.reverse[0] = true; std::vector<int> got;
      for (int i = 0; i < 5; ++i) got.push_back (q.clock (s, noRec, cv0, g0).idx[0]);
      check (got == std::vector<int> ({ 3, 2, 1, 0, 3 }), "slider: reverse plays LENGTH-1 down to 0"); }
    // polyrhythm: A length 3, B length 2 (B steps live at 8..15)
    { Seq q; s = Settings(); s.length[0] = 3; s.length[1] = 2; std::vector<int> a, b;
      for (int i = 0; i < 6; ++i) { Out o = q.clock (s, noRec, cv0, g0); a.push_back (o.idx[0]); b.push_back (o.idx[1]); }
      check (a == std::vector<int> ({ 0, 1, 2, 0, 1, 2 }) && b == std::vector<int> ({ 8, 9, 8, 9, 8, 9 }), "slider: A and B run independent lengths"); }
    // chain: A length 3, B length 2 -> A0 A1 A2 B0 B1 A0
    { Seq q; s = Settings(); s.length[0] = 3; s.length[1] = 2; s.chain = true; q.gate[2] = true; q.gate[9] = true;
      std::vector<int> seq; std::vector<bool> fireA, fireB, movedA;
      for (int i = 0; i < 6; ++i) { Out o = q.clock (s, noRec, cv0, g0); seq.push_back (o.moved[0] ? o.idx[0] : o.idx[1]);
          fireA.push_back (o.fire[0]); fireB.push_back (o.fire[1]); movedA.push_back (o.moved[0]); }
      check (seq == std::vector<int> ({ 0, 1, 2, 8, 9, 0 }), "slider: chain runs A then B then wraps");
      check (fireA == std::vector<bool> ({ false, false, true, false, false, false }) && fireB == std::vector<bool> ({ false, false, false, false, true, false }),
            "slider: chain fires only the active section's gate buttons");
      check (movedA == std::vector<bool> ({ true, true, true, false, false, true }), "slider: chain moves one section at a time");
      check (q.idx[0] == 0 && q.idx[1] == 9, "slider: the idle section holds its last step"); }
    // gate buttons
    { Seq q; s = Settings(); q.gate[1] = true; bool f0 = q.clock (s, noRec, cv0, g0).fire[0]; bool f1 = q.clock (s, noRec, cv0, g0).fire[0];
      check (! f0 && f1, "slider: fire follows the step's gate button"); }
    // reset: next clock plays step 0 again
    { Seq q; s = Settings(); for (int i = 0; i < 3; ++i) q.clock (s, noRec, cv0, g0); q.reset();
      check (q.clock (s, noRec, cv0, g0).idx[0] == 0, "slider: reset then clock plays the first step"); }
    // LENGTH shrink wraps by modulo
    { Seq q; s = Settings(); s.length[0] = 4; q.pos[0] = 6; q.started = true;
      check (q.clock (s, noRec, cv0, g0).idx[0] == 3, "slider: a shorter LENGTH wraps the playhead (6 -> 7 mod 4 = 3)"); }
    // REC captures CV and gate into the step being played
    { Seq q; s = Settings(); s.range[0] = R0_5; const bool rec[2] = { true, false }; const float cv[2] = { 2.5f, 0.f }; const bool g[2] = { true, false };
      Out o = q.clock (s, rec, cv, g);
      check (near (q.val[o.idx[0]], 0.5) && q.gate[o.idx[0]] && o.fire[0], "slider: REC stores CV and gate, and the step fires");
      q.reset(); const float cv2[2] = { 5.f, 0.f };
      q.clock (s, noRec, cv2, g);
      check (near (q.val[0], 0.5), "slider: disarmed section keeps its slider");
      q.reset(); const bool gOff[2] = { false, false };
      q.clock (s, rec, cv, gOff);
      check (! q.gate[0], "slider: REC overwrites a gate with 'off'");
      check (near (q.val[8], 0.0), "slider: REC on A does not touch B"); }
    // REC stores raw (invert is output-side only)
    { Seq q; s = Settings(); s.invert[0] = true; s.range[0] = R0_10; const bool rec[2] = { true, false }; const float cv[2] = { 2.5f, 0.f };
      q.clock (s, rec, cv, g0);
      check (near (q.val[0], 0.25), "slider: REC stores the raw value even with INVERT on"); }
}

void testEnv()
{
    const float dt = 1.f / 48000.f;
    check (near (env::timeOf (0.f, 5.f), 0.001, 1e-6) && near (env::timeOf (1.f, 5.f), 5.0, 1e-3) && near (env::timeOf (1.f, 10.f), 10.0, 1e-3),
           "env: knob maps 0..1 to 1 ms .. max seconds, log");
    check (near (env::timeOf (env::knobOf (0.2f, 5.f), 5.f), 0.2, 1e-4), "env: knobOf inverts timeOf");
    {
        env::Env e;
        float lo = 0.f, hi = 0.f;
        auto run = [&] (int cnt) { for (int i = 0; i < cnt; ++i) { const float v = e.process (dt, 0.01f, 0.1f, 0.3f, 0.05f); lo = std::min (lo, v); hi = std::max (hi, v); } };
        e.trigger();
        int n = 0;
        while (e.stage == env::Env::Attack && n < 2000) { run (1); ++n; }
        check (near (n, 480, 1.5) && e.level == 1.f && e.stage == env::Env::Decay, "env: attack reaches 1 in A seconds", n);
        run (4800);
        check (near (e.level, 0.307, 2e-4), "env: decay covers 99 % of the way to S in D seconds", e.level);
        run (96000);
        check (near (e.level, 0.3, 1e-6) && e.stage == env::Env::Decay, "env: settles at S and holds while the gate is on", e.level);
        e.release();
        run (2400);
        check (near (e.level, 0.003, 2e-4), "env: release covers 99 % of the way to 0 in R seconds", e.level);
        run (4800);
        check (e.level == 0.f && e.stage == env::Env::Idle, "env: release ends at exactly 0 and idles", e.level);
        check (lo >= 0.f && hi <= 1.f, "env: stays inside 0..1", hi);
        e.trigger();
        run (100);
        const bool live = e.level > 0.f && e.stage != env::Env::Idle;
        e.reset();
        check (live && e.level == 0.f && e.stage == env::Env::Idle, "env: reset clears level and stage", e.level);
    }
    {
        env::Env e;
        e.trigger();
        for (int i = 0; i < 480 + 2400; ++i) e.process (dt, 0.01f, 0.1f, 0.2f, 0.05f);   // attack, then half a decay: level near 0.28
        const float before = e.level;
        e.trigger();
        const float after = e.process (dt, 0.01f, 0.1f, 0.2f, 0.05f);
        check (before > 0.2f && after > before && after - before <= dt / 0.01f * 1.01f, "env: a retrigger attacks from the current level, no jump", after - before);
    }
    {
        env::Env e;
        e.trigger();
        for (int i = 0; i < 480; ++i) e.process (dt, 0.01f, 0.1f, 0.2f, 0.05f);   // attack to the top
        e.release();
        for (int i = 0; i < 1200; ++i) e.process (dt, 0.01f, 0.1f, 0.2f, 0.05f);   // half a release: level near 0.1
        const float before = e.level;
        const bool releasing = e.stage == env::Env::Release;
        e.trigger();
        const float after = e.process (dt, 0.01f, 0.1f, 0.2f, 0.05f);
        check (releasing && before > 0.05f && e.stage == env::Env::Attack && after > before && after - before <= dt / 0.01f * 1.01f,
               "env: a hit during release attacks from the current level, no jump", after - before);
    }
    {
        env::Env e;
        e.release();
        check (e.process (dt, 0.01f, 0.1f, 0.5f, 0.05f) == 0.f && e.stage == env::Env::Idle, "env: release while idle stays idle");
        e.trigger();
        bool ok = true;
        for (int i = 0; i < 10; ++i)
        {
            const float v = e.process (dt, 0.f, 0.f, 0.5f, 0.f);   // every time at 0: no divide by zero
            ok = ok && std::isfinite (v) && v >= 0.f && v <= 1.f;
        }
        check (ok, "env: times of 0 stay finite and inside 0..1");
        env::Env z;
        z.trigger();
        const float first = z.process (dt, 0.f, 0.1f, 0.5f, 0.1f);   // the floor makes the first sample dt / 1e-4 = 0.208, not a jump to 1
        check (first > 0.f && first < 0.5f, "env: an attack time of 0 is floored, not a jump to the top", first);
    }
    {
        env::GateHold g;
        g.hit (1);
        const bool onAfterHit = g.on();
        const bool fell = g.tick();
        check (onAfterHit && fell && ! g.on(), "gate hold: LEN 1 lasts exactly one tick");
    }
    {
        env::GateHold g;
        g.hit (16);
        int falls = 0, at = 0;
        for (int i = 1; i <= 20; ++i)
            if (g.tick()) { ++falls; at = i; }
        check (falls == 1 && at == 16, "gate hold: LEN 16 falls once, on the 16th tick", at);
    }
    {
        env::GateHold g;
        g.hit (4); g.tick(); g.tick();
        g.hit (4);   // re-arm while on
        g.tick(); g.tick(); g.tick();
        const bool stillOn = g.on();
        const bool fell = g.tick();
        check (stillOn && fell && ! g.on(), "gate hold: a hit while on re-arms the full length");
        g.hit (3);
        g.reset();
        check (! g.on() && ! g.tick(), "gate hold: reset clears it, and an idle tick never reports a fall");
        g.hit (0);
        const bool low = g.tick();   // 0 clamps to 1
        g.hit (99);
        int n = 0;
        while (! g.tick() && n < 100) ++n;   // 99 clamps to 16: 15 ticks, then the 16th falls
        check (low && n == 15, "gate hold: LEN clamps to 1..16", n);
    }
    {
        const rhythm::Tick k { 5u, true, false, true };   // A and C hit; R and ACCENT, not R'
        const bool want[env::numSources] = { true, false, true, true, false, true, false };   // R R' ACCENT A B C D
        const rhythm::Tick k2 { 10u, false, true, false };   // B and D hit; R' only
        const bool want2[env::numSources] = { false, true, false, false, true, false, true };   // R R' ACCENT A B C D
        bool grid = true, wave = true;
        for (int s = 0; s < env::numSources; ++s)
        {
            const bool crossing = s == env::SrcR || s == env::SrcRp;
            grid = grid && env::hitOf (k, s, false) == want[s] && env::hitOf (k2, s, false) == want2[s];
            wave = wave && env::hitOf (k, s, true) == (crossing ? false : want[s]) && env::hitOf (k2, s, true) == (crossing ? false : want2[s]);
        }
        check (grid, "env: hitOf picks exactly the chosen source (GRID)");
        check (wave, "env: hitOf leaves R and R' to the crossings in WAVE");
        check (env::waveHitOf (true, false, env::SrcR) && ! env::waveHitOf (false, true, env::SrcR)
               && env::waveHitOf (false, true, env::SrcRp) && ! env::waveHitOf (true, false, env::SrcRp)
               && ! env::waveHitOf (true, true, env::SrcA), "env: waveHitOf maps the R and R' crossings and nothing else");
        check (! env::hitOf (k, 99, false) && ! env::hitOf (k, -1, false), "env: an out-of-range source never hits");
    }
}

double dB (double g) { return 20.0 * std::log10 (g); }

void testMixer()
{
    const float dt = 1.f / 48000.f, step = mixer::slewStep (dt);
    check (near (dB (mixer::levelGain (mixer::unityKnob)), 0.0, 0.05), "mixer: LEVEL at 0.707 is unity", dB (mixer::levelGain (mixer::unityKnob)));
    check (mixer::levelGain (0.f) == 0.f && near (dB (mixer::levelGain (1.f)), 6.02, 0.01), "mixer: LEVEL 0 is silence, 1 is +6 dB", dB (mixer::levelGain (1.f)));
    check (mixer::sendGain (0.f) == 0.f && mixer::sendGain (1.f) == 1.f, "mixer: SEND 0..1 is silence..unity");
    {
        float l, r;
        mixer::pan (0.f, false, l, r);
        check (near (dB (l), -3.01, 0.01) && near (l, r), "mixer: mono centre is -3 dB each side", dB (l));
        mixer::pan (-1.f, false, l, r);
        check (near (l, 1.f) && near (r, 0.f), "mixer: mono hard left", r);
        mixer::pan (0.f, true, l, r);
        check (l == 1.f && r == 1.f, "mixer: stereo centre is unity");
        mixer::pan (1.f, true, l, r);
        check (near (l, 0.f) && r == 1.f, "mixer: stereo hard right turns L down to 0", l);
        mixer::pan (-1.f, true, l, r);
        check (l == 1.f && near (r, 0.f), "mixer: stereo hard left turns R down to 0", r);
        mixer::pan (0.5f, true, l, r);
        check (near (l, 0.70710678) && r == 1.f, "mixer: stereo balance follows a cosine law", l);
        mixer::pan (7.f, false, l, r);
        check (near (l, 0.f) && near (r, 1.f), "mixer: pan clamps to -1..1", l);
    }
    check (mixer::audible (false, false, false) && ! mixer::audible (true, false, false), "mixer: mute silences");
    check (! mixer::audible (false, false, true) && mixer::audible (false, true, true), "mixer: solo isolates");
    check (! mixer::audible (true, true, true), "mixer: mute beats solo");
    {
        mixer::Strip s;
        float ol = 0, orr = 0, pl = 0, pr = 0;
        int n = 0;
        while (s.gl < mixer::levelGain (mixer::unityKnob) * 0.7071f - 1e-6f && n < 10000)
        {
            s.process (1.f, 1.f, false, mixer::unityKnob, 0.f, true, 0.f, 0.f, step, ol, orr, pl, pr);
            ++n;
        }
        check (n <= 240, "mixer: a new strip fades in within 5 ms", n);
        check (near (ol, 0.7071, 1e-3) && near (orr, 0.7071, 1e-3), "mixer: unity strip, mono centre, 1 V in -> 0.707 V each side", ol);
        float worst = 0.f, prev = ol;
        for (int i = 0; i < 480; ++i)
        {
            s.process (1.f, 1.f, false, mixer::unityKnob, 0.f, false, 0.f, 0.f, step, ol, orr, pl, pr);
            worst = std::max (worst, std::fabs (ol - prev));
            prev = ol;
        }
        check (ol == 0.f && worst <= step + 1e-6f, "mixer: mute ramps to 0 with no step bigger than the slew", worst);
        check (near (pl, 1.f) && near (pr, 1.f), "mixer: pre-fader tap ignores level, mute and pan", pl);
        s.process (1.f, 1.f, false, 1.f, 0.f, false, 0.f, 0.f, step, ol, orr, pl, pr);
        check (near (pl, 1.f) && near (pr, 1.f), "mixer: pre-fader tap ignores a +6 dB level too", pl);
        s.process (2.f, 2.f, false, mixer::unityKnob, 0.f, true, 0.5f, 1.f, step, ol, orr, pl, pr);
        check (near (pl, 1.f), "mixer: DUCK 0.5 at full envelope halves the strip, sends too", pl);
        s.process (2.f, 2.f, false, mixer::unityKnob, 0.f, true, 0.f, 1.f, step, ol, orr, pl, pr);
        check (near (pl, 2.f), "mixer: DUCK 0 is immune to the envelope", pl);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        s.process (nan, nan, true, mixer::unityKnob, 0.f, true, 0.f, 0.f, step, ol, orr, pl, pr);
        check (ol == 0.f && orr == 0.f && pl == 0.f && pr == 0.f, "mixer: NaN in reads as 0 V");
    }
    {
        mixer::Sidechain sc;
        const float rel = 0.15f;
        int n = 0;
        while (sc.process (5.f, false, rel, dt) < 0.95f && n < 1000) ++n;
        check (n <= 48, "mixer: follower, a 5 V step reaches 0.95 within 1 ms", n);
        for (int i = 0; i < 480; ++i) sc.process (5.f, false, rel, dt);
        check (near (sc.env, 1.f), "mixer: follower holds 1 at 5 V", sc.env);
        for (int i = 0; i < (int) (rel * 48000); ++i) sc.process (0.f, false, rel, dt);
        check (near (sc.env, std::exp (-1.0), 2e-3), "mixer: follower falls to 1/e in RELEASE", sc.env);
        sc.reset();
        check (sc.process (0.f, false, rel, dt) == 0.f, "mixer: unpatched (0 V) sidechain is no envelope");
        for (int i = 0; i < 480; ++i) sc.process (0.5f, true, rel, dt);
        check (sc.env == 0.f, "mixer: trigger, 0.5 V does not fire", sc.env);
        n = 0;
        while (sc.process (10.f, true, rel, dt) < 1.f && n < 1000) ++n;
        check (n <= 49, "mixer: trigger, a rising edge ramps to 1 within 1 ms", n);
        for (int i = 0; i < (int) (rel * 48000); ++i) sc.process (10.f, true, rel, dt);
        check (near (sc.env, std::exp (-1.0), 2e-3), "mixer: trigger, a held gate does not refire, falls to 1/e in RELEASE", sc.env);
        sc.process (0.f, true, rel, dt);
        sc.process (10.f, true, rel, dt);
        check (sc.rising, "mixer: trigger, the next rising edge fires again");
    }
    {
        mixer::Sidechain t;
        const float rel = 0.15f;
        t.process (0.99f, true, rel, dt);
        check (! t.rising, "mixer: trigger, 0.99 V does not fire");
        t.process (1.f, true, rel, dt);
        check (t.rising, "mixer: trigger, 1 V fires");
        for (int i = 0; i < 100; ++i) t.process (10.f, true, rel, dt);
        t.process (0.5f, true, rel, dt);
        t.process (10.f, true, rel, dt);
        check (! t.rising, "mixer: trigger, a dip to 0.5 V is not a new edge (0.1 V hysteresis)");
    }
    {
        mixer::Sidechain m;
        const float rel = 0.15f;
        for (int i = 0; i < 10; ++i) m.process (10.f, true, rel, dt);
        for (int i = 0; i < 48000; ++i) m.process (0.f, false, rel, dt);
        for (int i = 0; i < 100; ++i) m.process (0.f, true, rel, dt);
        check (m.env < 0.01f, "mixer: a mode switch mid-ramp does not fire a duck later", m.env);
    }
    {
        mixer::Sidechain f;
        for (int i = 0; i < 480; ++i) f.process (-5.f, false, 0.15f, dt);
        check (near (f.env, 1.f), "mixer: follower reads the negative half too", f.env);
    }
    {
        mixer::Sidechain z;
        for (int i = 0; i < 480; ++i) z.process (5.f, false, 0.02f, dt);
        for (int i = 0; i < 48000 * 3; ++i) z.process (0.f, false, 0.02f, dt);
        check (z.env == 0.f, "mixer: the envelope settles to exactly 0", z.env);
    }
    check (near (mixer::releaseOf (0.f), 0.02) && near (mixer::releaseOf (1.f), 1.0, 1e-4) && near (mixer::releaseOf (mixer::knobOfRelease (0.15f)), 0.15, 1e-4),
           "mixer: RELEASE knob is 20 ms .. 1 s, log, and knobOfRelease inverts it");
    {
        const float c = mixer::peakCoef (dt);
        float p = mixer::peak (0.f, -6.f, c);
        check (p == 6.f, "mixer: peak catches the magnitude", p);
        for (int i = 0; i < 4800; ++i) p = mixer::peak (p, 0.f, c);
        check (near (p, 6.0 * std::exp (-1.0), 1e-3), "mixer: peak falls to 1/e in 100 ms", p);
    }
    check (mixer::softClip (4.9f) == 4.9f && mixer::softClip (-5.f) == -5.f, "mixer: soft clip is the identity up to 5 V");
    check (near ((mixer::softClip (5.001f) - 5.f) / 0.001f, 1.0, 1e-2), "mixer: soft clip has slope 1 at the knee", mixer::softClip (5.001f));
    check (mixer::softClip (100.f) <= 8.f && mixer::softClip (-100.f) >= -8.f && mixer::softClip (100.f) > 7.9f && mixer::softClip (6.f) < 6.f, "mixer: soft clip bends above 5 V and never passes 8 V", mixer::softClip (100.f));
    check (mixer::softClip (std::numeric_limits<float>::quiet_NaN()) == 0.f, "mixer: soft clip NaN reads as 0 V");
}

int main()
{
    sliderChecks();
    faceTapChecks();
    testPresets();
    testTypeIndex();
    testRngAndClamp();
    testQuantize();
    testDegrees();
    testRandomMask();
    testArpPool();
    testArpDirections();
    testArpEdges();
    testArpRandom();
    testSectionQuantizer();
    testSectionPitch();
    testSectionArp();
    testRhythmCycle();
    testRhythmGrid();
    testRhythmCounter();
    testRhythmWave();
    testRhythmClock();
    testGate();
    testLadderResponse();
    testLadderSafety();
    testEnv();
    testMixer();
    // ---- calls (each task adds its calls above this line)
    std::printf ("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
