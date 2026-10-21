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
}
// ---- tests (each task adds its functions above this line)
}

int main()
{
    testPresets();
    testTypeIndex();
    testRngAndClamp();
    // ---- calls (each task adds its calls above this line)
    std::printf ("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
