#!/usr/bin/env python3
"""panel.py: writes res/<Module>.svg for each module and src/layout.hpp, from one layout table per module.

Labels become paths (DejaVu Sans from the Rack install, or the font named by RACK_FONT), so the SVG has no <text>,
as the VCV panel guide asks. The widgets read the same tables through layout.hpp, so art and components cannot drift.
Run from anywhere: py -3.11 tools/panel.py   (needs fontTools).
Author: Ari Klopfer / A_Development. MIT
"""
import math
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

H = 128.5
W = 5.08 * 12
BAND = 8.6
INK, INK2 = '#16161a', '#202027'
LIME, MAGENTA, WHITE, DIM = '#c8ff3a', '#ff3d9a', '#f2f2f4', '#8d8d96'
FONT = os.environ.get('RACK_FONT', r'C:\Program Files\VCV\Rack2Free\res\fonts\DejaVuSans.ttf')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Radius in mm per kind, from the Rack component SVGs: hero Rogan3PS, knob Rogan1PS, key VCVLightBezelLatch (7.2 mm),
# button VCVButton / VCVLightLatch (6.1 mm), light SmallSimpleLight, in = input jack (magenta ring), out = output jack
RADIUS = {'hero': 8.8, 'knob': 6.7, 'key': 3.6, 'button': 3.1, 'light': 1.0, 'in': 4.0, 'out': 4.0}
LABEL_SIZE = {'hero': 3.0, 'knob': 2.6, 'button': 2.4, 'light': 2.0, 'in': 2.4, 'out': 2.6}
PAD_R = 4.8   # dark plate behind an output jack
RING_R = RADIUS['in'] + 0.7   # magenta ring around an input jack

font = TTFont(FONT)
glyphs, cmap, upm = font.getGlyphSet(), font.getBestCmap(), font['head'].unitsPerEm


def text(s, x, y, size, fill, anchor='middle', stroke=0.0):
    """One <path> for the string: baseline at y, em size in mm, optional stroke for a bolder face."""
    k = size / upm
    width = sum(glyphs[cmap[ord(c)]].width for c in s) * k
    pen_x = x - width / 2 if anchor == 'middle' else x - width if anchor == 'end' else x
    d = []
    for c in s:
        g = glyphs[cmap[ord(c)]]
        pen = SVGPathPen(glyphs)
        g.draw(TransformPen(pen, (k, 0, 0, -k, pen_x, y)))
        d.append(pen.getCommands())
        pen_x += g.width * k
    bold = ' stroke="%s" stroke-width="%g" stroke-linejoin="round"' % (fill, stroke) if stroke else ''
    return '<path fill="%s"%s d="%s"/>' % (fill, bold, ' '.join(d))


# ---- Arpeggio
# Keyboard: white keys on the lower row, black keys above, between their neighbours. Key id = pitch class.
KX0, KDX = 7.08, 7.8
WHITE_PC, BLACK_PC = (0, 2, 4, 5, 7, 9, 11), (1, 3, 6, 8, 10)
BLACK_AFTER = (0, 1, 3, 4, 5)   # white-key column each black key sits to the right of
KEYS = {}
for col, pc in enumerate(WHITE_PC):
    KEYS[pc] = (KX0 + col * KDX, 28.0)
for col, pc in zip(BLACK_AFTER, BLACK_PC):
    KEYS[pc] = (KX0 + (col + 0.5) * KDX, 19.5)

ROW_TYPE, ROW_MODE, ROW_AXIS, ROW_IN, ROW_OUT = 45.0, 65.0, 83.0, 102.0, 115.3
JX = (8.48, 19.48, W / 2, 41.48, 52.48)   # jack-zone columns, 11 mm apart
ARPEGGIO = [('KEY%d_PARAM' % pc, 'key', x, y, '') for pc, (x, y) in sorted(KEYS.items())] + [
    ('TYPE_PARAM', 'hero', 15.0, ROW_TYPE, 'TYPE'), ('TYPE_INPUT', 'in', 31.0, ROW_TYPE, 'CV'),
    ('RAND_PARAM', 'button', 43.0, ROW_TYPE, 'RAND'), ('RAND_INPUT', 'in', 54.0, ROW_TYPE, 'TRIG'),
    ('MODE_PARAM', 'button', 8.0, ROW_MODE, 'MODE'), ('DIR_PARAM', 'button', 19.0, ROW_MODE, 'DIR'),
] + [('DIR%d_LIGHT' % d, 'light', 27.0 + 5.0 * d, ROW_MODE, l) for d, l in enumerate(('U', 'D', 'UD', 'RND'))] + [
    ('OCT_PARAM', 'knob', 52.5, ROW_MODE, 'OCT'),
    ('AXIS_PARAM', 'knob', 11.0, ROW_AXIS, 'AXIS'), ('AXIS_INPUT', 'in', 24.0, ROW_AXIS, 'CV'),
    ('INT_PARAM', 'knob', 37.0, ROW_AXIS, 'INTERVAL'), ('INT_INPUT', 'in', 50.0, ROW_AXIS, 'CV'),
    ('A_CV_INPUT', 'in', JX[0], ROW_IN, 'CV'), ('A_TRIG_INPUT', 'in', JX[1], ROW_IN, 'TRIG'),
    ('RESET_INPUT', 'in', JX[2], ROW_IN, 'RESET'),
    ('B_CV_INPUT', 'in', JX[3], ROW_IN, 'CV'), ('B_TRIG_INPUT', 'in', JX[4], ROW_IN, 'TRIG'),
    ('A_CV_OUTPUT', 'out', JX[0], ROW_OUT, 'CV'), ('A_TRIG_OUTPUT', 'out', JX[1], ROW_OUT, 'TRIG'),
    ('B_CV_OUTPUT', 'out', JX[3], ROW_OUT, 'CV'), ('B_TRIG_OUTPUT', 'out', JX[4], ROW_OUT, 'TRIG'),
]


def arpeggio_extras():
    return [
        '<rect x="2.5" y="14.6" width="%g" height="18.4" rx="1.5" fill="none" stroke="%s" stroke-width="0.5"/>' % (W - 5, LIME),
        '<rect x="2.5" y="94.5" width="%g" height="29.2" rx="2" fill="%s"/>' % (W - 5, INK2),   # jack zone
        text('A', (JX[0] + JX[1]) / 2, 98.4, 2.8, LIME, stroke=0.2), text('B', (JX[3] + JX[4]) / 2, 98.4, 2.8, LIME, stroke=0.2),
    ]


# ---- Ripple: tempo row (CV and EXT jacks sit lower, on a label row of their own), transport pairs PLAY+TRIG and
# RESET+TRIG with MODE alone, four generator columns (PERIOD, ON, CV, out), then the mix outputs
GX = (8.73, 23.23, 37.73, 52.23)   # generator columns A-D, 14.5 mm apart; DENSITY and MODE share column D
RIPPLE = [
    ('TEMPO_PARAM', 'hero', 12.0, 25.0, 'TEMPO'), ('TEMPO_LIGHT', 'light', 22.5, 17.5, ''),
    ('TEMPO_INPUT', 'in', 27.0, 29.5, 'CV'), ('EXT_INPUT', 'in', 38.5, 29.5, 'EXT'),
    ('DENSITY_PARAM', 'knob', GX[3], 25.0, 'DENSITY'),
    ('PLAY_PARAM', 'button', GX[0], 45.0, 'PLAY'), ('PLAY_INPUT', 'in', 17.8, 45.0, 'TRIG'),
    ('RESET_PARAM', 'button', 29.5, 45.0, 'RESET'), ('RESET_INPUT', 'in', 38.5, 45.0, 'TRIG'),
    ('MODE_PARAM', 'button', GX[3], 45.0, 'MODE'),
] + [part for i, (x, name) in enumerate(zip(GX, 'ABCD')) for part in (
    ('PERIOD%d_PARAM' % i, 'knob', x, 62.5, name), ('ON%d_PARAM' % i, 'button', x, 77.0, ''),
    ('PERIOD%d_INPUT' % i, 'in', x, 87.5, ''), ('GEN%d_OUTPUT' % i, 'out', x, 101.0, name))] + [
    ('CLOCK_OUTPUT', 'out', GX[0], 115.3, 'CLOCK'), ('R_OUTPUT', 'out', GX[1], 115.3, 'R'),
    ('RP_OUTPUT', 'out', GX[2], 115.3, "R'"), ('ACCENT_OUTPUT', 'out', GX[3], 115.3, 'ACCENT'),
]


def ripple_extras():
    return ['<rect x="2.5" y="94.5" width="%g" height="29.2" rx="2" fill="%s"/>' % (W - 5, INK2)]   # jack zone


MODULES = {'Arpeggio': ('ARPEGGIO', ARPEGGIO, arpeggio_extras), 'Ripple': ('RIPPLE', RIPPLE, ripple_extras)}


def reach(kind):
    return RING_R if kind == 'in' else PAD_R if kind == 'out' else RADIUS[kind]


def overlaps(layout):
    """Pairs of parts whose outlines (input rings and output plates included) touch. Must be empty."""
    return [(a[0], b[0]) for j, a in enumerate(layout) for b in layout[j + 1:]
            if math.hypot(a[2] - b[2], a[3] - b[3]) < reach(a[1]) + reach(b[1])]


def panel(title, layout, extras):
    out = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" width="%gmm" height="%gmm" viewBox="0 0 %g %g">' % (W, H, W, H),
        '<rect width="%g" height="%g" fill="%s"/>' % (W, H, INK),
        '<rect width="%g" height="%g" fill="%s"/>' % (W, BAND, LIME),
        text(title, W / 2, 6.3, 4.3, INK, stroke=0.39),
        text('my lil toolbox', W / 2, 12.6, 2.4, WHITE, stroke=0.12),
    ] + extras()
    rowmax = {}   # labels in a row share one baseline, set by the row's biggest part
    for _, kind, x, y, _ in layout:
        if kind not in ('out', 'light', 'key'):
            rowmax[y] = max(rowmax.get(y, 0), RING_R if kind == 'in' else RADIUS[kind])
    for _, kind, x, y, label in layout:
        if kind == 'in':
            out.append('<circle cx="%g" cy="%g" r="%g" fill="none" stroke="%s" stroke-width="0.5"/>' % (x, y, RING_R, MAGENTA))
        elif kind == 'out':
            out.append('<circle cx="%g" cy="%g" r="%g" fill="%s" stroke="%s" stroke-width="0.5"/>' % (x, y, PAD_R, INK, LIME))
        if not label:
            continue
        if kind == 'light':
            out.append(text(label, x, y + RADIUS['light'] + 2.6, LABEL_SIZE[kind], DIM))
            continue
        r = PAD_R if kind == 'out' else rowmax[y]
        colour, stroke = (LIME, 0.3) if kind == 'hero' else (LIME, 0.25) if kind == 'out' else (WHITE, 0.18)
        out.append(text(label, x, y + r + 2.8 + (0.4 if kind == 'hero' else 0), LABEL_SIZE[kind], colour, stroke=stroke))
    out.append(text('A_DEVELOPMENT', W / 2, 126.4, 2.4, DIM))
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def header():
    lines = ['// layout.hpp: generated by tools/panel.py, do not edit. Component centres in mm.', '#pragma once',
             'namespace layout {']
    for name, (_, layout, _) in MODULES.items():
        lines += ['namespace %s {' % name.lower()]
        lines += ['constexpr float %s[2] = {%.2ff, %.2ff};' % (i, x, y) for i, _, x, y, _ in layout]
        lines += ['}']
    lines += ['}', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    for name, (_, layout, _) in MODULES.items():
        bad = overlaps(layout)
        assert not bad, '%s: overlapping parts %s' % (name, bad)
    files = {os.path.join('res', name + '.svg'): panel(*spec) for name, spec in MODULES.items()}
    files[os.path.join('src', 'layout.hpp')] = header()
    for rel, body in files.items():
        with open(os.path.join(ROOT, rel), 'w', encoding='utf-8') as f:
            f.write(body)
    print('wrote', ', '.join(files), '(%s components)' % ', '.join('%s %d' % (n, len(s[1])) for n, s in MODULES.items()))
