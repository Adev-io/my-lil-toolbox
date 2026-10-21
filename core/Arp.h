// Arp.h: the arpeggiator. A pool of in-scale pitches from a base note up through 1-4 octaves, walked one step per
// trigger in one of four directions. No Rack headers.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include "Scale.h"

namespace arp
{
enum Dir { UP, DOWN, UPDOWN, RANDOM, NUM_DIRS };
constexpr int maxPool = 48;   // 4 octaves of chromatic

struct Arp
{
    int pool[maxPool] = {};
    int size = 0;
    int idx = 0;
    bool goingUp = true;   // up-down: current heading
    bool fresh = true;     // the next step plays the direction's first note (after construction or reset)
    scale::Rng rng;

    explicit Arp (uint32_t seed = 0x9e3779b9u) : rng (seed) {}

    // Pool = base, then every in-mask pitch above it, below base + 12 * octaves. Empty mask: just base.
    // The step index is kept, wrapped to the new size, so a moving CV does not restart the pattern.
    void build (int base, uint16_t mask, int octaves)
    {
        octaves = octaves < 1 ? 1 : octaves > 4 ? 4 : octaves;
        size = 0;
        pool[size++] = base;
        if (mask)
            for (int s = base + 1; s < base + 12 * octaves; ++s)
                if (scale::inMask (s, mask)) pool[size++] = s;
        if (idx >= size) idx %= size;
    }

    void reset() { fresh = true; }

    // Advances one step and returns its pitch. Call build() first.
    int step (Dir dir)
    {
        if (size <= 1) { idx = 0; fresh = false; return pool[0]; }
        if (fresh)
        {
            fresh = false;
            goingUp = true;
            idx = dir == DOWN ? size - 1 : 0;
            return pool[idx];
        }
        switch (dir)
        {
            case UP: idx = (idx + 1) % size; break;
            case DOWN: idx = (idx + size - 1) % size; break;
            case UPDOWN:
                if (goingUp) { if (idx + 1 < size) ++idx; else { goingUp = false; --idx; } }
                else { if (idx > 0) --idx; else { goingUp = true; ++idx; } }
                break;
            default:   // RANDOM: uniform over every index except the current one
            {
                const int r = (int) (rng.next() % (uint32_t) (size - 1));
                idx = r >= idx ? r + 1 : r;
                break;
            }
        }
        return pool[idx];
    }
};
}
