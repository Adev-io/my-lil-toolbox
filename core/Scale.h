// Scale.h: scale masks, presets, the TYPE knob's hysteresis, and the small RNG the core shares. Pure functions.
// Pitches are integer semitones (0 = C4, 1 V/oct); a mask has bit k set when pitch class k (0 = C) is in the scale.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <cmath>
#include <cstdint>

namespace scale
{
constexpr int numPresets = 10;

// xorshift32: tiny and seedable, so tests repeat exactly
struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed = 0x9e3779b9u) : s (seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

inline int pc (int semi) { return ((semi % 12) + 12) % 12; }
inline bool inMask (int semi, uint16_t mask) { return ((mask >> pc (semi)) & 1u) != 0; }

inline uint16_t preset (int i)
{
    static const uint16_t masks[numPresets] = {
        0xAB5,   // major            C D E F G A B
        0x5AD,   // natural minor    C D Eb F G Ab Bb
        0x9AD,   // harmonic minor   C D Eb F G Ab B
        0x6AD,   // dorian           C D Eb F G A Bb
        0x5AB,   // phrygian         C Db Eb F G Ab Bb
        0xAD5,   // lydian           C D E F# G A B
        0x6B5,   // mixolydian       C D E F G A Bb
        0x295,   // major pentatonic C D E G A
        0x4A9,   // minor pentatonic C Eb F G Bb
        0xFFF,   // chromatic
    };
    return masks[i < 0 ? 0 : i >= numPresets ? numPresets - 1 : i];
}

inline const char* presetName (int i)
{
    static const char* const names[numPresets] = { "major", "natural minor", "harmonic minor", "dorian", "phrygian",
        "lydian", "mixolydian", "major pentatonic", "minor pentatonic", "chromatic" };
    return names[i < 0 ? 0 : i >= numPresets ? numPresets - 1 : i];
}

// TYPE knob + CV -> preset index. The current index holds within +-0.6 (wider than the 0.5 rounding band), so a jittery
// CV near a boundary cannot flap the scale. current = -1 means "no index yet": just round.
inline int typeIndex (float v, int current)
{
    if (current >= 0 && std::fabs (v - (float) current) <= 0.6f) return current;
    const int i = (int) std::lround (v);
    return i < 0 ? 0 : i > numPresets - 1 ? numPresets - 1 : i;
}

inline float clampV (float v) { return v < -10.f ? -10.f : v > 10.f ? 10.f : v; }

// Nearest in-mask semitone to a continuous semitone value; ties go to the lower pitch. mask must not be 0.
// No truncation toward zero: works the same below 0 V.
inline int nearest (float semis, uint16_t mask)
{
    const int base = (int) std::floor (semis);
    int best = base;
    float bestD = 1e9f;
    for (int s = base - 12; s <= base + 13; ++s)   // ascending, so the lower pitch wins a tie
    {
        if (! inMask (s, mask)) continue;
        const float d = std::fabs ((float) s - semis);
        if (d < bestD - 1e-4f) { bestD = d; best = s; }   // 1e-4 semitone slack: float ties count as ties
    }
    return best;
}

// Volts in, quantized volts out. Empty mask: the voltage passes through unchanged.
inline float quantize (float volts, uint16_t mask) { return mask ? nearest (volts * 12.f, mask) / 12.f : volts; }
}
