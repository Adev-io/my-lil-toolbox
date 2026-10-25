// Ladder.h: the acid low-pass of My lil Acid Bass as an audio filter. DRIVE (a normalised tanh) into two cascaded 12 dB
// topology-preserving-transform state-variable low-passes (Zavalishin), 24 dB/oct, the resonance on the second stage.
// Same stage equations, resonance split, level loss and cutoff limits as Acid Bass (core/Svf.h, core/AcidVoice.h), so
// it sounds like that voice's filter. Volts in, volts out. No Rack dependency.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <cmath>

namespace ladder
{
constexpr float cutLo = 60.f, cutHi = 8000.f;   // the Acid Bass CUTOFF knob: exponential 60 .. 8000 Hz
inline float cutoffHz (float knob) { return cutLo * std::pow (cutHi / cutLo, knob); }
inline float cutoffKnob (float hz) { return std::log (hz / cutLo) / std::log (cutHi / cutLo); }

inline bool finite (float v) { return std::fabs (v) < 1e30f; }   // false for NaN and infinity
inline float clamp01 (float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

struct Ladder
{
    void prepare (double sr) { sampleRate = sr; update(); reset(); }
    void reset() { s1a = s2a = s1b = s2b = 0.f; }

    // cutoff in Hz (held to 5 Hz .. 0.45 * sample rate), res and drive 0..1. A non-finite value keeps the last one.
    void set (float hz, float res, float drive)
    {
        if (finite (hz)) cutoff = hz;
        if (finite (res)) resonance = clamp01 (res);
        if (finite (drive)) driveAmt = clamp01 (drive);
        update();
    }

    float process (float inV)
    {
        float u = inV != inV ? 0.f : inV < -12.f ? -12.f : inV > 12.f ? 12.f : inV;   // NaN reads as 0 V
        u *= 0.2f;                                                                   // 5 V = 1
        if (shapeA > 1e-4f) u = std::tanh (shapeA * u) * shapeNorm;                 // a 5 V peak stays at 5 V
        float y = stage (stage (u, ha, R2a, s1a, s2a), hb, R2b, s1b, s2b) * level * 5.f;
        if (! finite (s1a + s2a + s1b + s2b)) { reset(); return 0.f; }
        flush (s1a); flush (s2a); flush (s1b); flush (s2b);
        const float m = std::fabs (y);   // soft knee: clean up to 5 V, then bends towards 10 V
        if (m > 5.f) y = (y < 0.f ? -1.f : 1.f) * (5.f + 5.f * std::tanh ((m - 5.f) * 0.2f));
        return y;
    }

private:
    // One Svf.h low-pass stage: R2 = 1 / resonance, h = 1 / (1 + R2 g + g^2).
    float stage (float x, float h, float R2, float& s1, float& s2)
    {
        const float yHP = h * (x - s1 * (g + R2) - s2);
        const float yBP = yHP * g + s1;
        s1 = yHP * g + yBP;
        const float yLP = yBP * g + s2;
        s2 = yBP * g + yLP;
        return yLP;
    }
    static void flush (float& s) { if (std::fabs (s) < 1e-20f) s = 0.f; }   // no denormals in a decaying tail

    void update()
    {
        const double hz = std::fmin (std::fmax ((double) cutoff, 5.0), 0.45 * sampleRate);
        g = (float) std::tan (3.141592653589793 * hz / sampleRate);
        R2a = 1.f / 0.5f;                                                   // AcidVoice: f1 resonance 0.5
        R2b = 1.f / (0.7f + std::pow (resonance, 1.6f) * 24.f);             // AcidVoice: f2 resonance 0.7 .. 24.7
        ha = 1.f / (1.f + R2a * g + g * g);
        hb = 1.f / (1.f + R2b * g + g * g);
        level = 1.f / (1.f + resonance * 0.6f);                             // resonance eats level, as in Acid Bass
        shapeA = std::pow (10.f, 1.2f * driveAmt) - 1.f;                    // 0 (clean) .. ~14.8 (~+23 dB on quiet signals)
        shapeNorm = shapeA > 1e-4f ? 1.f / std::tanh (shapeA) : 1.f;
    }

    double sampleRate = 44100.0;
    float cutoff = 460.f, resonance = 0.f, driveAmt = 0.f;
    float g = 0.f, R2a = 2.f, R2b = 1.f, ha = 1.f, hb = 1.f, level = 1.f, shapeA = 0.f, shapeNorm = 1.f;
    float s1a = 0.f, s2a = 0.f, s1b = 0.f, s2b = 0.f;
};
}
