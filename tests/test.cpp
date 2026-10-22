// test.cpp: checks the core (scale, arp, section) with plain asserts. Run: make test
// Author: Ari Klopfer / A_Development · MIT
#include "Scale.h"
#include "Arp.h"
#include "Section.h"
#include "Rhythm.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
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
    const float th = (density - 1) / 4.f;
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
}
// ---- tests (each task adds its functions above this line)
}

int main()
{
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
    // ---- calls (each task adds its calls above this line)
    std::printf ("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
