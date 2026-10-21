// test.cpp: checks the core (scale, arp, section) with plain asserts. Run: make test
// Author: Ari Klopfer / A_Development · MIT
#include "Scale.h"
#include "Arp.h"
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
    // ---- calls (each task adds its calls above this line)
    std::printf ("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
