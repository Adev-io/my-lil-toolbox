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
LIME, MAGENTA, BLUE, WHITE, DIM = '#c8ff3a', '#ff3d9a', '#5ac8e6', '#f2f2f4', '#8d8d96'
FONT = os.environ.get('RACK_FONT', r'C:\Program Files\VCV\Rack2Free\res\fonts\DejaVuSans.ttf')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Radius in mm per kind, from the Rack component SVGs: hero Rogan3PS, knob Rogan1PS, tiny BefacoTinyKnob (8.6 mm), key VCVLightBezelLatch (7.2 mm),
# button VCVButton / VCVLightLatch (6.1 mm), light SmallSimpleLight, out = output jack. Input jacks by role, ring coloured
# as in My lil Acid Bass: seq = clock / reset / triggers (blue), play = pitch (lime), cv = modulation (magenta)
RADIUS = {'hero': 8.8, 'knob': 6.7, 'tiny': 4.3, 'trim': 3.0, 'key': 3.6, 'button': 3.1, 'light': 1.0, 'seq': 4.0, 'play': 4.0, 'cv': 4.0, 'out': 4.0}
LABEL_SIZE = {'hero': 3.0, 'knob': 2.6, 'tiny': 2.4, 'trim': 2.2, 'button': 2.4, 'light': 2.0, 'seq': 2.4, 'play': 2.4, 'cv': 2.4, 'out': 2.6}
RING_COLOR = {'seq': BLUE, 'play': LIME, 'cv': MAGENTA}
PAD_R = 4.8   # dark plate behind an output jack
RING_R = RADIUS['cv'] + 0.7   # ring around an input jack

font = TTFont(FONT)
glyphs, cmap, upm = font.getGlyphSet(), font.getBestCmap(), font['head'].unitsPerEm


def width(s, size):
    """Advance width of the string in mm at this em size."""
    return sum(glyphs[cmap[ord(c)]].width for c in s) * size / upm


def text(s, x, y, size, fill, anchor='middle', stroke=0.0):
    """One <path> for the string: baseline at y, em size in mm, optional stroke for a bolder face."""
    k = size / upm
    w = width(s, size)
    pen_x = x - w / 2 if anchor == 'middle' else x - w if anchor == 'end' else x
    d = []
    for c in s:
        g = glyphs[cmap[ord(c)]]
        pen = SVGPathPen(glyphs)
        g.draw(TransformPen(pen, (k, 0, 0, -k, pen_x, y)))
        d.append(pen.getCommands())
        pen_x += g.width * k
    bold = ' stroke="%s" stroke-width="%g" stroke-linejoin="round"' % (fill, stroke) if stroke else ''
    return '<path fill="%s"%s d="%s"/>' % (fill, bold, ' '.join(d))


# ---- the title band, shared by every module: Rack's screws sit in its corners (5.08 mm squares at x 5.08 and
# W - 10.16), so the title starts right of the left one and the face sits left of the right one
TITLE_X, TITLE_Y, TITLE_SIZE = 11.2, 6.3, 4.3
MASCOT_X = W - 14.46   # 46.5 on 12 HP
MASCOT_R = BAND * 0.36
HP = {'Slider': 18, 'Ripple': 16, 'Mixer': 26}   # panel width per module, 12 HP unless named here


def use(name):
    """Panel globals for this module: width and the face's x (the layout tables below were built at 12 HP and keep it)."""
    global W, MASCOT_X
    W = 5.08 * HP.get(name, 12)
    MASCOT_X = W - 14.46


def mascot(cx, cy, r):
    """The My lil face: magenta disc, dot eyes, a flat mouth (src/TalkingFace.hpp paints the live mouth over it)."""
    e = r * 0.37
    return ''.join([
        '<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (cx, cy, r, MAGENTA),
        '<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (cx - e, cy - r * 0.32, r * 0.145, INK),
        '<circle cx="%g" cy="%g" r="%g" fill="%s"/>' % (cx + e, cy - r * 0.32, r * 0.145, INK),
        '<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="%s" stroke-width="%g" stroke-linecap="round"/>'
        % (cx - r * 0.58, cy + r * 0.45, cx + r * 0.58, cy + r * 0.45, INK, r * 0.16),
    ])


def title_size(title):
    """4.3 mm, or smaller when the title would run into the face (only MY LIL ARPEGGIO on 12 HP)."""
    return min(TITLE_SIZE, TITLE_SIZE * (MASCOT_X - MASCOT_R - 1.5 - TITLE_X) / width(title, TITLE_SIZE))


def band(title):
    """The band's art: lime strip, the title at the left, the face at the right."""
    return ['<rect width="%g" height="%g" fill="%s"/>' % (W, BAND, LIME),
            text(title, TITLE_X, TITLE_Y, title_size(title), INK, 'start', 0.09 * title_size(title)), mascot(MASCOT_X, BAND / 2, MASCOT_R)]


def band_boxes(title):
    """Where the title and the face are, as (x0, y0, x1, y1), for the panel check."""
    return [('title', (TITLE_X, TITLE_Y - 0.73 * title_size(title), TITLE_X + width(title, title_size(title)), TITLE_Y)),
            ('mascot', (MASCOT_X - MASCOT_R, BAND / 2 - MASCOT_R, MASCOT_X + MASCOT_R, BAND / 2 + MASCOT_R))]


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
    ('TYPE_PARAM', 'hero', 15.0, ROW_TYPE, 'TYPE'), ('TYPE_INPUT', 'cv', 31.0, ROW_TYPE, 'CV'),
    ('RAND_PARAM', 'button', 43.0, ROW_TYPE, 'RAND'), ('RAND_INPUT', 'seq', 54.0, ROW_TYPE, 'TRIG'),
    ('MODE_PARAM', 'button', 8.0, ROW_MODE, 'MODE'), ('DIR_PARAM', 'button', 19.0, ROW_MODE, 'DIR'),
] + [('DIR%d_LIGHT' % d, 'light', 27.0 + 5.0 * d, ROW_MODE, l) for d, l in enumerate(('U', 'D', 'UD', 'RND'))] + [
    ('OCT_PARAM', 'knob', 52.5, ROW_MODE, 'OCT'),
    ('AXIS_PARAM', 'knob', 11.0, ROW_AXIS, 'AXIS'), ('AXIS_INPUT', 'cv', 24.0, ROW_AXIS, 'CV'),
    ('INT_PARAM', 'knob', 37.0, ROW_AXIS, 'INTERVAL'), ('INT_INPUT', 'cv', 50.0, ROW_AXIS, 'CV'),
    ('A_CV_INPUT', 'play', JX[0], ROW_IN, 'CV'), ('A_TRIG_INPUT', 'seq', JX[1], ROW_IN, 'TRIG'),
    ('RESET_INPUT', 'seq', JX[2], ROW_IN, 'RESET'),
    ('B_CV_INPUT', 'play', JX[3], ROW_IN, 'CV'), ('B_TRIG_INPUT', 'seq', JX[4], ROW_IN, 'TRIG'),
    ('A_CV_OUTPUT', 'out', JX[0], ROW_OUT, 'CV'), ('A_TRIG_OUTPUT', 'out', JX[1], ROW_OUT, 'TRIG'),
    ('B_CV_OUTPUT', 'out', JX[3], ROW_OUT, 'CV'), ('B_TRIG_OUTPUT', 'out', JX[4], ROW_OUT, 'TRIG'),
]


ZONE_X = 2.5   # the jack zones' side margin; parts below a zone's top stay inside it
ARPEGGIO_ZONE_Y = 94.5


def arpeggio_extras():
    return [
        '<rect x="2.5" y="14.6" width="%g" height="18.4" rx="1.5" fill="none" stroke="%s" stroke-width="0.5"/>' % (W - 5, LIME),
        '<rect x="%g" y="%g" width="%g" height="29.2" rx="2" fill="%s"/>' % (ZONE_X, ARPEGGIO_ZONE_Y, W - 2 * ZONE_X, INK2),   # jack zone
        text('A', (JX[0] + JX[1]) / 2, 98.4, 2.8, LIME, stroke=0.2), text('B', (JX[3] + JX[4]) / 2, 98.4, 2.8, LIME, stroke=0.2),
    ]


# ---- Ripple: tempo row (CV and EXT jacks sit lower, on a label row of their own), transport pairs PLAY+TRIG and
# RESET+TRIG with MODE alone, four generator columns (PERIOD, ON, CV, out) captioned in the left margin, then the mix
# outputs under an OUT header
GX = (10.5, 24.5, 38.5, 52.5)   # generator columns A-D, 14 mm apart (plate D stays inside the jack zone); DENSITY and MODE share column D
ROW_PERIOD, ROW_ON, ROW_CV = 62.0, 75.0, 86.0
OUT_X = (GX[0] + GX[3]) / 2   # the OUT header sits over the middle of the generator grid
RIPPLE_ZONE_Y = 95.0
EX = 69.5   # the ENV column, right of the generator grid
RIPPLE = [
    ('TEMPO_PARAM', 'hero', 12.0, 25.0, 'TEMPO'), ('TEMPO_LIGHT', 'light', 22.5, 17.5, ''),
    ('TEMPO_INPUT', 'cv', 27.0, 29.5, 'CV'), ('EXT_INPUT', 'seq', 38.5, 29.5, 'EXT'),
    ('DENSITY_PARAM', 'tiny', GX[3], 25.0, 'DENSITY'),
    ('PLAY_PARAM', 'button', GX[0], 45.0, 'PLAY'), ('PLAY_INPUT', 'seq', 19.6, 45.0, 'TRIG'),
    ('RESET_PARAM', 'button', 30.5, 45.0, 'RESET'), ('RESET_INPUT', 'seq', 39.6, 45.0, 'TRIG'),
    ('MODE_PARAM', 'button', GX[3], 45.0, 'MODE'),
] + [part for i, (x, name) in enumerate(zip(GX, 'ABCD')) for part in (
    ('PERIOD%d_PARAM' % i, 'tiny', x, ROW_PERIOD, name), ('ON%d_PARAM' % i, 'button', x, ROW_ON, ''),
    ('PERIOD%d_INPUT' % i, 'cv', x, ROW_CV, ''), ('GEN%d_OUTPUT' % i, 'out', x, 101.0, name))] + [
    ('CLOCK_OUTPUT', 'out', GX[0], 115.3, 'CLOCK'), ('R_OUTPUT', 'out', GX[1], 115.3, 'R'),
    ('RP_OUTPUT', 'out', GX[2], 115.3, "R'"), ('ACCENT_OUTPUT', 'out', GX[3], 115.3, 'ACCENT'),
    ('ATTACK_PARAM', 'tiny', EX, 24.0, 'ATTACK'), ('DECAY_PARAM', 'tiny', EX, 39.0, 'DECAY'),
    ('SUSTAIN_PARAM', 'tiny', EX, 54.0, 'SUSTAIN'), ('RELEASE_PARAM', 'tiny', EX, 69.0, 'RELEASE'),
    ('GATELEN_PARAM', 'tiny', EX, 84.0, 'LEN'), ('ENV_OUTPUT', 'out', EX, 115.3, 'ENV'),
]


def caption(s, y):
    """A row caption in the left margin, reading upwards, centred on the row."""
    return '<g transform="rotate(-90 3.4 %g)">%s</g>' % (y, text(s, 3.4, y + 0.8, 2.2, DIM))


def ripple_extras():
    return [
        '<rect x="%g" y="%g" width="%g" height="28.7" rx="2" fill="%s"/>' % (ZONE_X, RIPPLE_ZONE_Y, W - 2 * ZONE_X, INK2),   # jack zone, bottom at 123.7 like Arpeggio's
        '<rect x="%g" y="93.1" width="%g" height="0.5" fill="%s"/>' % (ZONE_X, W - 2 * ZONE_X, MAGENTA),      # OUT header, as Acid Bass's PLAY
        '<rect x="%g" y="91.5" width="11" height="4.2" fill="%s"/>' % (OUT_X - 5.5, INK),
        text('OUT', OUT_X, 94.6, 3.0, MAGENTA, stroke=0.2),
        caption('PERIOD', ROW_PERIOD), caption('ON', ROW_ON), caption('CV', ROW_CV),
        '<rect x="61.2" y="14.6" width="0.4" height="76" fill="%s"/>' % DIM,   # divider: the ENV column
        text('ENV', EX, 17.0, 3.0, MAGENTA, stroke=0.2),
    ]


# ---- Filter: CUTOFF on top, RES and DRIVE under it, the CUT CV amount (trim) over its jack; the jack zone has the three
# CV inputs on top and IN -> OUT along the bottom with a little low-pass curve between them. Audio in is lime, like pitch.
FX = (12.0, W / 2, W - 12.0)
FILTER_ZONE_Y = 94.5
FILTER = [
    ('CUTOFF_PARAM', 'hero', W / 2, 32.0, 'CUTOFF'),
    ('RES_PARAM', 'knob', 14.0, 60.0, 'RES'), ('DRIVE_PARAM', 'knob', W - 14.0, 60.0, 'DRIVE'),
    ('CUT_CV_PARAM', 'trim', FX[1], 84.0, 'AMT'),
    ('VOCT_INPUT', 'play', FX[0], 102.0, 'V/OCT'), ('CUT_CV_INPUT', 'cv', FX[1], 102.0, 'CUT CV'), ('RES_CV_INPUT', 'cv', FX[2], 102.0, 'RES CV'),
    ('IN_INPUT', 'play', FX[0], 115.3, 'IN'), ('OUT_OUTPUT', 'out', FX[2], 115.3, 'OUT'),
]


def filter_extras():
    return [
        '<rect x="%g" y="%g" width="%g" height="29.2" rx="2" fill="%s"/>' % (ZONE_X, FILTER_ZONE_Y, W - 2 * ZONE_X, INK2),   # jack zone, as Arpeggio's
        '<path d="M 20 116.5 H 30.5 C 32.3 116.5 32.8 112.8 34 112.8 C 35.2 112.8 35.4 116.2 36.6 118.2 L 40 122" fill="none" '
        'stroke="%s" stroke-width="0.6" stroke-linecap="round" stroke-linejoin="round"/>' % LIME,   # a resonant low-pass, IN to OUT
    ]


# ---- Slider (18 HP): two sections of 8 sliders, a gate button under each; a control strip (A's LEN RNG REC INV REV,
# CHAIN, B's five) over the jack zone. Built on its own width SW, never the global W.
SW = 5.08 * 18
SLIDER_BOX = (6.72, 25.92)   # VCVSlider.svg is 19.84 x 76.54 px at Rack's 75 dpi; handle and light stay inside it
SX = [SW / 2 + (i - 3.5) * 10.5 for i in range(8)]   # slider columns, 10.5 mm apart
ROW_SLIDE, ROW_GATE = (23.7, 59.7), (41.5, 77.5)   # per section A, B
STRIP_X = [SW / 2 + (i - 5) * 7.8 for i in range(11)]
SIN_X = [SW / 2 + (i - 2.5) * 14.0 for i in range(6)]   # jack columns, 14 mm apart
SLIDER_ZONE_Y = 95.0
SLIDER = [part for sec in (0, 1) for i, x in enumerate(SX) for part in (
    ('SLIDER%d_PARAM' % (sec * 8 + i), 'slider', x, ROW_SLIDE[sec], ''), ('GATE%d_PARAM' % (sec * 8 + i), 'button', x, ROW_GATE[sec], ''))] + [
    ('%s%d_PARAM' % (n, sec), k, STRIP_X[sec * 6 + j], 88.0, l) for sec in (0, 1)
    for j, (n, k, l) in enumerate((('LENGTH', 'trim', 'LEN'), ('RANGE', 'trim', 'RNG'), ('REC', 'button', 'REC'),
                                   ('INVERT', 'button', 'INV'), ('REVERSE', 'button', 'REV')))] + [
    ('CHAIN_PARAM', 'button', STRIP_X[5], 88.0, 'CHAIN'),
    ('CLOCK_INPUT', 'seq', SIN_X[0], 102.0, 'CLOCK'), ('RESET_INPUT', 'seq', SIN_X[1], 102.0, 'RESET'),
    ('A_CV_INPUT', 'cv', SIN_X[2], 102.0, 'CV'), ('A_GATE_INPUT', 'play', SIN_X[3], 102.0, 'GATE'),
    ('B_CV_INPUT', 'cv', SIN_X[4], 102.0, 'CV'), ('B_GATE_INPUT', 'play', SIN_X[5], 102.0, 'GATE'),
    ('A_CV_OUTPUT', 'out', SIN_X[2], 115.3, 'CV'), ('A_GATE_OUTPUT', 'out', SIN_X[3], 115.3, 'GATE'),
    ('B_CV_OUTPUT', 'out', SIN_X[4], 115.3, 'CV'), ('B_GATE_OUTPUT', 'out', SIN_X[5], 115.3, 'GATE'),
]


def slider_box(x, y):
    w, h = SLIDER_BOX
    return x - w / 2, y - h / 2, x + w / 2, y + h / 2


def slider_extras():
    slots = ['<rect x="%g" y="%g" width="4.2" height="%g" rx="1" fill="%s"/>' % (x - 2.1, y - SLIDER_BOX[1] / 2, SLIDER_BOX[1], INK2)
             for _, k, x, y, _ in SLIDER if k == 'slider']   # a lighter slot behind each (black) slider track
    return slots + [
        text('A', 2.9, ROW_SLIDE[0] + 1.0, 2.8, LIME, stroke=0.2), text('B', 2.9, ROW_SLIDE[1] + 1.0, 2.8, LIME, stroke=0.2),
        '<rect x="%g" y="%g" width="%g" height="28.7" rx="2" fill="%s"/>' % (ZONE_X, SLIDER_ZONE_Y, SW - 2 * ZONE_X, INK2),   # jack zone, as Ripple's
        text('A', (SIN_X[2] + SIN_X[3]) / 2, 98.4, 2.8, LIME, stroke=0.2), text('B', (SIN_X[4] + SIN_X[5]) / 2, 98.4, 2.8, LIME, stroke=0.2),
    ]


# ---- Mixer (26 HP): eight strip columns (SEND 1, SEND 2, DUCK, PAN, LEVEL fader, signal light, MUTE, SOLO, IN L, IN R),
# rows captioned in the left margin, strip numbers on top; the master section right of a divider. Built on its own
# width MW, never the global W. Strip parts carry no labels: the captions and numbers name them.
MW = 5.08 * HP['Mixer']
STRIP_DX = 11.0
MIX_X = [10.5 + STRIP_DX * i for i in range(8)]   # strip columns, 11 mm apart (the widget steps by STRIP_DX from strip 0)
ROW_S1, ROW_S2, ROW_DUCK, ROW_PAN, ROW_LEVEL = 19.0, 28.8, 38.6, 48.4, 67.5
ROW_SIG, ROW_MUTE, ROW_SOLO, ROW_INL, ROW_INR = 83.0, 87.8, 95.0, 106.0, 117.6
MC = (101.0, 112.0, 123.0)   # master columns
MIXER_ZONE_Y = 100.0
MIXER = [part for i, x in enumerate(MIX_X) for part in (
    ('SEND1_%d_PARAM' % i, 'tiny', x, ROW_S1, ''), ('SEND2_%d_PARAM' % i, 'tiny', x, ROW_S2, ''),
    ('DUCK%d_PARAM' % i, 'tiny', x, ROW_DUCK, ''), ('PAN%d_PARAM' % i, 'tiny', x, ROW_PAN, ''),
    ('LEVEL%d_PARAM' % i, 'slider', x, ROW_LEVEL, ''), ('SIGNAL%d_LIGHT' % i, 'light', x, ROW_SIG, ''),
    ('MUTE%d_PARAM' % i, 'button', x, ROW_MUTE, ''), ('SOLO%d_PARAM' % i, 'button', x, ROW_SOLO, ''),
    ('L%d_INPUT' % i, 'play', x, ROW_INL, ''), ('R%d_INPUT' % i, 'play', x, ROW_INR, ''))] + [
    ('MASTER_PARAM', 'hero', (MC[0] + MC[1]) / 2, 22.0, 'MASTER'), ('RELEASE_PARAM', 'tiny', MC[2], 22.0, 'REL'),
    ('SC_INPUT', 'seq', MC[0], 44.0, 'SC'),
    ('RETURN1_L_INPUT', 'play', MC[1], 44.0, 'RTN1 L'), ('RETURN1_R_INPUT', 'play', MC[2], 44.0, 'RTN1 R'),
    ('RETURN2_L_INPUT', 'play', MC[1], 59.5, 'RTN2 L'), ('RETURN2_R_INPUT', 'play', MC[2], 59.5, 'RTN2 R'),
    ('SEND1_L_OUTPUT', 'out', MC[1], 75.0, 'SND1 L'), ('SEND1_R_OUTPUT', 'out', MC[2], 75.0, 'SND1 R'),
    ('SEND2_L_OUTPUT', 'out', MC[1], 90.5, 'SND2 L'), ('SEND2_R_OUTPUT', 'out', MC[2], 90.5, 'SND2 R'),
    ('OUT_L_OUTPUT', 'out', MC[1], 110.0, 'OUT L'), ('OUT_R_OUTPUT', 'out', MC[2], 110.0, 'OUT R'),
]


def mixer_extras():
    return [
        '<rect x="%g" y="%g" width="%g" height="23.7" rx="2" fill="%s"/>' % (ZONE_X, MIXER_ZONE_Y, MW - 2 * ZONE_X, INK2),   # jack zone, bottom at 123.7
        '<rect x="%g" y="14.6" width="0.4" height="83" fill="%s"/>' % ((MIX_X[-1] + MC[0]) / 2 - 0.2, DIM),   # divider: the master section, centred between strip 8 and the master columns
    ] + ['<rect x="%g" y="%g" width="4.2" height="%g" rx="1" fill="%s"/>' % (x - 2.1, ROW_LEVEL - SLIDER_BOX[1] / 2, SLIDER_BOX[1], INK2)
         for x in MIX_X] + [text(str(i + 1), x, 13.6, 2.8, LIME, stroke=0.2) for i, x in enumerate(MIX_X)] + [
        (text(s, 3.4, y + 0.8, 2.2, DIM) if len(s) == 1 else caption(s, y))   # one letter reads upright, longer words upwards
        for s, y in (('SEND 1', ROW_S1), ('SEND 2', ROW_S2), ('DUCK', ROW_DUCK), ('PAN', ROW_PAN), ('LEVEL', ROW_LEVEL),
                                   ('M', ROW_MUTE), ('S', ROW_SOLO), ('L', ROW_INL), ('R', ROW_INR))]


MODULES = {'Arpeggio': ('MY LIL ARPEGGIO', ARPEGGIO, arpeggio_extras, ARPEGGIO_ZONE_Y), 'Ripple': ('MY LIL RIPPLE', RIPPLE, ripple_extras, RIPPLE_ZONE_Y),
           'Filter': ('MY LIL FILTER', FILTER, filter_extras, FILTER_ZONE_Y), 'Slider': ('MY LIL SLIDER', SLIDER, slider_extras, SLIDER_ZONE_Y),
           'Mixer': ('MY LIL MIXER', MIXER, mixer_extras, MIXER_ZONE_Y)}
EXTRA_CONSTS = {'Mixer': ['constexpr float STRIP_DX = %.2ff;   // strip column pitch: strip i sits at strip 0 + i * STRIP_DX' % STRIP_DX,
                          'constexpr int STRIPS = %d;   // strip count; Mixer.cpp static_asserts it equals mixer::numStrips' % len(MIX_X)]}


def reach(kind):
    return RING_R if kind in RING_COLOR else PAD_R if kind == 'out' else RADIUS[kind]


def rowmax_of(layout):
    """Each row's biggest part other than outputs, lights, keys and sliders: labels in a row share one baseline set by it."""
    rowmax = {}
    for _, kind, x, y, _ in layout:
        if kind not in ('out', 'light', 'key', 'slider'):
            rowmax[y] = max(rowmax.get(y, 0), reach(kind))
    return rowmax


def label_at(kind, y, rowmax):
    """Baseline, size, colour and stroke of the label under a part of this kind in this row."""
    if kind == 'light':
        return y + RADIUS['light'] + 2.6, LABEL_SIZE[kind], DIM, 0.0
    r = PAD_R if kind == 'out' else rowmax[y]
    colour, stroke = (LIME, 0.3) if kind == 'hero' else (LIME, 0.25) if kind == 'out' else (WHITE, 0.18)
    return y + r + 2.8 + (0.4 if kind == 'hero' else 0), LABEL_SIZE[kind], colour, stroke


def label_box(x, base, size, label):
    """(x0, y0, x1, y1) of a centred label: cap height 0.73 em above the baseline."""
    w = width(label, size)
    return x - w / 2, base - 0.73 * size, x + w / 2, base


SCREW = 5.08   # Rack's ScrewBlack: 5.08 mm squares at (5.08, 0), (W - 10.16, 0) and the same two at the bottom edge


def screws():
    return [(x, y, x + SCREW, y + SCREW) for x in (SCREW, W - 2 * SCREW) for y in (0, H - SCREW)]


def box_hits_circle(b, cx, cy, r):
    nx, ny = min(max(cx, b[0]), b[2]), min(max(cy, b[1]), b[3])   # the box's nearest point to the centre
    return math.hypot(cx - nx, cy - ny) < r


def boxes_hit(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def problems(layout, title, zone_y):
    """Everything on the panel that touches something else or leaves the panel: parts (rings and plates included),
    their labels, the band's title and face, Rack's four screws. Parts that reach below the jack zone's top (zone_y) must stay
    inside the zone's side margin. Sliders are boxes (SLIDER_BOX), the rest circles. Must be empty. Captions are extras and not checked."""
    bad = []
    parts = [(n, x, y, reach(k)) for n, k, x, y, _ in layout if k != 'slider']
    rects = [(n, slider_box(x, y)) for n, k, x, y, _ in layout if k == 'slider']
    for a, box in rects:
        if box[1] < BAND:
            bad.append('%s enters the band' % a)
        if box[3] > zone_y and (box[0] < ZONE_X or box[2] > W - ZONE_X):
            bad.append('%s leaves the jack zone' % a)
    rowmax = rowmax_of(layout)
    labels = []
    for n, k, x, y, label in layout:
        if label:
            base, size, _, _ = label_at(k, y, rowmax)
            labels.append((n + ' label', label_box(x, base, size, label)))
    for j, (a, ax, ay, ar) in enumerate(parts):
        for b, bx, by, br in parts[j + 1:]:
            if math.hypot(ax - bx, ay - by) < ar + br:
                bad.append('%s touches %s' % (a, b))
        if ax - ar < 0 or ax + ar > W or ay - ar < BAND or ay + ar > H:
            bad.append('%s leaves the panel or enters the band' % a)
        if ay + ar > zone_y and (ax - ar < ZONE_X or ax + ar > W - ZONE_X):
            bad.append('%s leaves the jack zone' % a)
        for name, box in labels:
            if name != a + ' label' and box_hits_circle(box, ax, ay, ar):
                bad.append('%s covers %s' % (a, name))
        for name, box in rects:
            if box_hits_circle(box, ax, ay, ar):
                bad.append('%s touches %s' % (a, name))
        for s in screws():
            if box_hits_circle(s, ax, ay, ar):
                bad.append('%s under a screw' % a)
    boxes = labels + band_boxes(title) + rects   # sliders against labels, the band, each other, screws and the panel edges
    for j, (a, box) in enumerate(boxes):
        if box[0] < 0 or box[2] > W or box[3] > H:
            bad.append('%s leaves the panel' % a)
        for b, other in boxes[j + 1:]:
            if boxes_hit(box, other):
                bad.append('%s overlaps %s' % (a, b))
        for s in screws():
            if boxes_hit(box, s):
                bad.append('%s under a screw' % a)
    return bad


def panel(title, layout, extras):
    out = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" width="%gmm" height="%gmm" viewBox="0 0 %g %g">' % (W, H, W, H),
        '<rect width="%g" height="%g" fill="%s"/>' % (W, H, INK),
    ] + band(title) + extras()
    rowmax = rowmax_of(layout)
    for _, kind, x, y, label in layout:
        if kind in RING_COLOR:
            out.append('<circle cx="%g" cy="%g" r="%g" fill="none" stroke="%s" stroke-width="0.5"/>' % (x, y, RING_R, RING_COLOR[kind]))
        elif kind == 'out':
            out.append('<circle cx="%g" cy="%g" r="%g" fill="%s" stroke="%s" stroke-width="0.5"/>' % (x, y, PAD_R, INK, LIME))
        if label:
            base, size, colour, stroke = label_at(kind, y, rowmax)
            out.append(text(label, x, base, size, colour, stroke=stroke))
    out.append(text('A_DEVELOPMENT', W / 2, 126.4, 2.4, DIM))
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def header():
    lines = ['// layout.hpp: generated by tools/panel.py, do not edit. Component centres in mm.', '#pragma once',
             'namespace layout {']
    for name, (_, layout, _, _) in MODULES.items():
        use(name)   # FACE x follows this module's width
        lines += ['namespace %s {' % name.lower()]
        lines += ['constexpr float %s[2] = {%.2ff, %.2ff};' % (i, x, y) for i, _, x, y, _ in layout]
        lines += ['constexpr float FACE[3] = {%.2ff, %.2ff, %.3ff};   // mascot centre x, y, radius (TalkingFace)' % (MASCOT_X, BAND / 2, MASCOT_R)]
        lines += EXTRA_CONSTS.get(name, [])
        lines += ['}']
    use('')
    lines += ['}', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    files = {}
    for name, (title, layout, extras, zone_y) in MODULES.items():
        use(name)
        bad = problems(layout, title, zone_y)
        assert not bad, '%s: %s' % (name, '; '.join(bad))
        files[os.path.join('res', name + '.svg')] = panel(title, layout, extras)
    use('')
    files[os.path.join('src', 'layout.hpp')] = header()
    for rel, body in files.items():
        with open(os.path.join(ROOT, rel), 'w', encoding='utf-8') as f:
            f.write(body)
    print('wrote', ', '.join(files), '(%s components)' % ', '.join('%s %d' % (n, len(s[1])) for n, s in MODULES.items()))
