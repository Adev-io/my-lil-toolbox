// test.cpp: checks the core (scale, arp, section) with plain asserts. Run: make test
// Author: Ari Klopfer / A_Development · MIT
#include "Scale.h"
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
    // ---- calls (each task adds its calls above this line)
    std::printf ("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
