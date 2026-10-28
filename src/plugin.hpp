// plugin.hpp: the plugin's models and the house light colours.
// Author: Ari Klopfer / A_Development · MIT
#pragma once
#include <rack.hpp>
using namespace rack;
extern Plugin* pluginInstance;
extern Model* modelArpeggio;
extern Model* modelRipple;
extern Model* modelFilter;
extern Model* modelSlider;
extern Model* modelMixer;

// House light colours, the same as the panels (tools/panel.py) and My lil Acid Bass.
struct LimeLight : GrayModuleLightWidget { LimeLight() { addBaseColor (nvgRGB (0xc8, 0xff, 0x3a)); } };
struct MagentaLight : GrayModuleLightWidget { MagentaLight() { addBaseColor (nvgRGB (0xff, 0x3d, 0x9a)); } };
// Two-state lights: channel 0 lime, channel 1 magenta.
struct LimeMagentaLight : GrayModuleLightWidget
{
    LimeMagentaLight()
    {
        addBaseColor (nvgRGB (0xc8, 0xff, 0x3a));
        addBaseColor (nvgRGB (0xff, 0x3d, 0x9a));
    }
};
// Arpeggio keys: channel 0 white (in the scale), 1 lime (A's note), 2 blue (B's note).
struct KeyLight : GrayModuleLightWidget
{
    KeyLight()
    {
        addBaseColor (nvgRGB (0xf2, 0xf2, 0xf4));
        addBaseColor (nvgRGB (0xc8, 0xff, 0x3a));
        addBaseColor (nvgRGB (0x5a, 0xc8, 0xe6));
    }
};
