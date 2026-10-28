// MixerWidgets.hpp: My lil Mixer's monitoring on the panel. MeterState is what the audio thread publishes; StripMeter
// draws a strip's LED ladder, peak tick, clip dot and duck bar; MasterDisplay draws the master's L / R bars, peak-hold
// numbers, short-term LUFS and 6 s history. Every level comes from core/Meter.h.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include "plugin.hpp"
#include "Meter.h"
#include "Mixer.h"

// Filled by the audio thread once per meter block, read by the UI thread: a torn float only blurs one frame (as FaceTap).
// The UI writes nothing here but clearReq: a click sets a bit, the audio thread takes the bits and clears those latches.
struct MeterState
{
    static constexpr int N = mixer::numStrips;
    static constexpr uint32_t clearAll = 1u << N;   // clearReq: bit i clears strip i's latch, this bit every latch
    float stripDb[N], stripHoldDb[N];   // strip input (pre-fader, before the duck): bar and peak tick, dBFS
    float duckDb[N];                    // each strip's duck right now, 0 .. -24 dB
    bool stripClip[N];
    float masterDb[2], masterHoldDb[2];   // OUT L / R after the soft clip: bar and peak tick, dBFS
    bool masterClip = false;
    float lufs = meter::floorDb;          // short-term, both channels
    meter::History history;               // the master's last 6 s, filled in place by the audio thread
    std::atomic<uint32_t> clearReq { 0 };

    MeterState()
    {
        for (int i = 0; i < N; ++i)
        {
            stripDb[i] = stripHoldDb[i] = meter::floorDb;
            duckDb[i] = 0.f;
            stripClip[i] = false;
        }
        for (int c = 0; c < 2; ++c) masterDb[c] = masterHoldDb[c] = meter::floorDb;
    }
};

namespace mixerui
{
const NVGcolor INK = nvgRGB (0x16, 0x16, 0x1a), LIME = nvgRGB (0xc8, 0xff, 0x3a), MAGENTA = nvgRGB (0xff, 0x3d, 0x9a),
               BLUE = nvgRGB (0x5a, 0xc8, 0xe6), WHITE = nvgRGB (0xf2, 0xf2, 0xf4), DIM = nvgRGB (0x8d, 0x8d, 0x96);

inline void fillRect (NVGcontext* vg, float x, float y, float w, float h, NVGcolor c)
{
    nvgBeginPath (vg);
    nvgRect (vg, x, y, w, h);
    nvgFillColor (vg, c);
    nvgFill (vg);
}

inline void fillDot (NVGcontext* vg, float cx, float cy, float r, NVGcolor c)
{
    nvgBeginPath (vg);
    nvgCircle (vg, cx, cy, r);
    nvgFillColor (vg, c);
    nvgFill (vg);
}

// One LED ladder in (x, y, w, h), px, bottom up. Dark pass: every segment unlit (ink). Lit pass: the segments under db,
// lime below -6 dBFS and magenta above, and the white peak tick at holdDb. One path (one fill) per colour: the segments
// never overlap, so this draws exactly what a fill per segment did.
inline void ladder (NVGcontext* vg, float x, float y, float w, float h, float db, float holdDb, bool litPass)
{
    const float seg = h / meter::segments, gap = std::min (mm2px (0.25f), seg * 0.3f);
    const int n = litPass ? meter::lit (db) : meter::segments;
    for (int pass = 0; pass < (litPass ? 2 : 1); ++pass)   // dark: ink; lit: lime, then magenta
    {
        bool any = false;
        nvgBeginPath (vg);
        for (int i = 0; i < n; ++i)
            if (! litPass || meter::hot (i) == (pass == 1))
            {
                nvgRect (vg, x, y + h - (i + 1) * seg + gap / 2, w, seg - gap);
                any = true;
            }
        if (! any) continue;
        nvgFillColor (vg, ! litPass ? INK : pass ? MAGENTA : LIME);
        nvgFill (vg);
    }
    if (litPass && holdDb > meter::floorDb)
        fillRect (vg, x, y + h - meter::ladderPos (holdDb) / meter::segments * h, w, std::max (1.f, mm2px (0.3f)), WHITE);
}
}

// A strip's meter, right of its fader: the ladder (input level, clip dot on top) and the blue duck bar beside it.
// Click: clears this strip's clip latch.
struct StripMeter : Widget
{
    MeterState* st = nullptr;   // null in the module browser: a still demo level
    int strip = 0;
    Rect ladderBox, duckBox;    // px, inside box

    void paint (const DrawArgs& a, bool litPass)
    {
        using namespace mixerui;
        const float w = ladderBox.size.x, top = ladderBox.pos.y + w + mm2px (0.5f), h = ladderBox.getBottom() - top;
        const float db = st ? st->stripDb[strip] : -36.f + 4.f * strip, hold = st ? st->stripHoldDb[strip] : db + 4.f;
        ladder (a.vg, ladderBox.pos.x, top, w, h, db, hold, litPass);
        if (! litPass) fillDot (a.vg, ladderBox.pos.x + w / 2, ladderBox.pos.y + w / 2, w * 0.45f, INK);
        else if (st && st->stripClip[strip]) fillDot (a.vg, ladderBox.pos.x + w / 2, ladderBox.pos.y + w / 2, w * 0.45f, MAGENTA);
        if (! litPass) fillRect (a.vg, duckBox.pos.x, top, duckBox.size.x, h, INK);
        else if (st && st->duckDb[strip] < 0.f) fillRect (a.vg, duckBox.pos.x, top, duckBox.size.x, h * st->duckDb[strip] / meter::duckFloorDb, BLUE);
    }

    void draw (const DrawArgs& a) override { paint (a, false); }

    void drawLayer (const DrawArgs& a, int layer) override
    {
        if (layer == 1) paint (a, true);   // lit parts glow like lights, also with the room dimmed
        Widget::drawLayer (a, layer);
    }

    void onButton (const ButtonEvent& e) override
    {
        if (st && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT)
        {
            st->clearReq.fetch_or (1u << strip);
            e.consume (this);
        }
    }
};

// The master display: L / R ladders with the master clip dot, peak-hold numbers (10 Hz), short-term LUFS, and the history
// (120 columns, oldest left). Click: clears the master latch and every strip latch.
struct MasterDisplay : Widget
{
    MeterState* st = nullptr;   // null in the module browser: a still demo
    double shownAt = -1.0;      // the numbers change at most every 0.1 s
    std::string peak[2], lufs;

    void paint (const DrawArgs& a, bool litPass)
    {
        using namespace mixerui;
        const float p = mm2px (1.5f), barW = mm2px (2.6f), gap = mm2px (0.6f), dot = mm2px (2.2f);
        const float top = p + dot + mm2px (0.6f), h = box.size.y - top - p;
        for (int c = 0; c < 2; ++c)
        {
            const float db = st ? st->masterDb[c] : -9.f - 2.f * c, hold = st ? st->masterHoldDb[c] : -4.f - c;
            ladder (a.vg, p + c * (barW + gap), top, barW, h, db, hold, litPass);
        }
        const float dotX = p + barW + gap / 2, dotY = p + dot / 2;
        if (! litPass) fillDot (a.vg, dotX, dotY, dot * 0.45f, INK);
        else if (st && st->masterClip) fillDot (a.vg, dotX, dotY, dot * 0.45f, MAGENTA);

        const float hx = mm2px (10.f), hy = mm2px (13.f), hw = box.size.x - hx - p, hh = box.size.y - hy - p;
        if (! litPass)
        {
            fillRect (a.vg, hx, hy, hw, hh, INK);
            return;
        }
        const int n = meter::History::columns;
        const float cw = hw / n;
        const int h0 = st ? st->history.head : 0;   // read once: the audio thread may advance it while we draw
        for (int hotPass = 0; hotPass < 2; ++hotPass)   // one path per colour
        {
            nvgBeginPath (a.vg);
            for (int k = 0; k < n; ++k)
            {
                const float db = st ? st->history.col[(h0 + k) % n] : -30.f + 20.f * std::sin (k * 0.35f);
                const float t = meter::ladderPos (db) / meter::segments;
                if (t > 0.f && (db > meter::hotDb) == (hotPass == 1)) nvgRect (a.vg, hx + k * cw, hy + hh * (1.f - t), cw, hh * t);
            }
            nvgFillColor (a.vg, hotPass ? MAGENTA : LIME);
            nvgFill (a.vg);
        }

        const double now = system::getTime();
        if (now - shownAt >= 0.1)
        {
            shownAt = now;
            for (int c = 0; c < 2; ++c) peak[c] = string::f ("%.1f", st ? st->masterHoldDb[c] : -4.f - c);
            lufs = string::f ("%.1f", st ? st->lufs : -14.f);
        }
        std::shared_ptr<window::Font> font = APP->window->uiFont;
        if (! font || font->handle < 0) return;
        nvgFontFaceId (a.vg, font->handle);
        nvgTextAlign (a.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
        nvgFontSize (a.vg, mm2px (3.2f));
        const char* side[2] = { "L", "R" };
        for (int c = 0; c < 2; ++c)
        {
            const float x = hx + c * hw / 2;
            nvgFillColor (a.vg, DIM);
            nvgText (a.vg, x, mm2px (5.5f), side[c], nullptr);
            nvgFillColor (a.vg, WHITE);
            nvgText (a.vg, x + mm2px (3.f), mm2px (5.5f), peak[c].c_str(), nullptr);
        }
        nvgFillColor (a.vg, WHITE);
        nvgText (a.vg, hx, mm2px (10.8f), lufs.c_str(), nullptr);
        nvgFillColor (a.vg, DIM);
        nvgText (a.vg, hx + mm2px (11.f), mm2px (10.8f), "LUFS short-term", nullptr);
    }

    void draw (const DrawArgs& a) override { paint (a, false); }

    void drawLayer (const DrawArgs& a, int layer) override
    {
        if (layer == 1) paint (a, true);
        Widget::drawLayer (a, layer);
    }

    void onButton (const ButtonEvent& e) override
    {
        if (st && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT)
        {
            st->clearReq.fetch_or (MeterState::clearAll);
            e.consume (this);
        }
    }
};
