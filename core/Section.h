// Section.h: one A/B section of the module. Quantizer mode (continuous, or sample-and-hold on TRIG) or arp mode (one
// pool step per TRIG), then INTERVAL (scale degrees), AXIS (semitones) and the output clamp. Also decides TRIG out.
// The Rack shell only reads jacks, normals B to A, and calls process() once per sample per section.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include "Arp.h"

namespace section
{
struct Settings
{
    uint16_t mask = 0xAB5;
    bool arpMode = false;
    int octaves = 1;          // arp pool span, 1..4
    arp::Dir dir = arp::UP;
    int degrees = 0;          // INTERVAL in scale degrees (the shell passes 0 for section A)
    int axis = 0;             // AXIS in semitones
    bool passThrough = false; // quantizer mode: TRIG out follows TRIG in edges instead of note changes
};

struct Out
{
    float volts;   // CV out
    int pc;        // key to light: pitch class before AXIS, -1 while holding an unquantized voltage
    bool trig;     // fire a 1 ms TRIG out pulse this sample
};

struct Section
{
    arp::Arp arpeggiator;
    bool raw = true;       // holding an unquantized voltage (empty mask, or no arp step yet)
    float rawV = 0.f;
    int semi = 0;          // held pitch before INTERVAL and AXIS
    bool started = false;  // lastNote is valid
    int lastNote = 0;      // last output note, for the note-change trigger

    explicit Section (uint32_t seed = 0x9e3779b9u) : arpeggiator (seed) {}

    // cv: this section's CV in (after normalling). edge: rising edge on its TRIG. patched: a TRIG source is connected.
    Out process (float cv, bool edge, bool patched, const Settings& s)
    {
        cv = scale::clampV (cv);
        bool trig = false;
        if (s.arpMode)
        {
            if (s.mask) arpeggiator.build (scale::nearest (cv * 12.f, s.mask), s.mask, s.octaves);
            if (edge)
            {
                trig = true;
                if (s.mask) { raw = false; semi = arpeggiator.step (s.dir); }
                else { raw = true; rawV = cv; }
            }
        }
        else
        {
            if (! patched || edge)
            {
                if (s.mask) { raw = false; semi = scale::nearest (cv * 12.f, s.mask); }
                else { raw = true; rawV = cv; }
            }
            if (s.passThrough) trig = edge;
        }
        if (raw)
        {
            started = false;
            return { scale::clampV (rawV + s.axis / 12.f), -1, trig };
        }
        const int note = scale::degreeShift (semi, s.mask, s.degrees);
        const int outNote = note + s.axis;
        if (! s.arpMode && ! s.passThrough && started && outNote != lastNote) trig = true;
        started = true;
        lastNote = outNote;
        return { scale::clampV (outNote / 12.f), scale::pc (note), trig };
    }
};
}
