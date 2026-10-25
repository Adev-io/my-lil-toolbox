// Filter.cpp: the Rack module. The Acid Bass 24 dB low-pass (core/Ladder.h) as an audio filter, one filter per poly
// channel. CUT CV moves the CUTOFF knob (10 V = the whole range, scaled by AMT), V/OCT tracks pitch, RES CV adds to RES.
// Author: Ari Klopfer / A_Development · MIT
#include "plugin.hpp"
#include "layout.hpp"
#include "Ladder.h"
#include "Scale.h"
#include "TalkingFace.hpp"

struct Filter : Module
{
    enum ParamId { CUTOFF_PARAM, RES_PARAM, DRIVE_PARAM, CUT_CV_PARAM, PARAMS_LEN };
    enum InputId { IN_INPUT, VOCT_INPUT, CUT_CV_INPUT, RES_CV_INPUT, INPUTS_LEN };
    enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };

    ladder::Ladder filters[PORT_MAX_CHANNELS];
    FaceTap face;   // the mascot's mouth
    float sampleRate = 0.f;
    int channels = 0;
    unsigned tick = 0;

    Filter()
    {
        config (PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam (CUTOFF_PARAM, 0.f, 1.f, ladder::cutoffKnob (460.f), "Cutoff", " Hz", ladder::cutHi / ladder::cutLo, ladder::cutLo);
        configParam (RES_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
        configParam (DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
        configParam (CUT_CV_PARAM, -1.f, 1.f, 1.f, "Cutoff CV amount (100% = 10 V sweeps the whole range)", "%", 0.f, 100.f);
        configInput (IN_INPUT, "Audio");
        configInput (VOCT_INPUT, "Cutoff key tracking (1 V/oct)");
        configInput (CUT_CV_INPUT, "Cutoff CV (scaled by AMT)");
        configInput (RES_CV_INPUT, "Resonance CV (10 V = full range)");
        configOutput (OUT_OUTPUT, "Audio");
        configBypass (IN_INPUT, OUT_OUTPUT);
    }

    void process (const ProcessArgs& args) override
    {
        if (args.sampleRate != sampleRate)
        {
            sampleRate = args.sampleRate;
            for (ladder::Ladder& f : filters) f.prepare (sampleRate);
            tick = 0;
        }
        const int n = std::max (1, inputs[IN_INPUT].getChannels());
        if (n != channels)
        {
            for (int c = channels; c < n; ++c) filters[c].reset();   // a channel that comes back starts silent
            channels = n;
            tick = 0;
        }
        if ((tick++ & 3) == 0)   // coefficients every 4 samples: smooth enough for sweeps, cheap with 16 channels
        {
            const float knob = params[CUTOFF_PARAM].getValue(), amt = params[CUT_CV_PARAM].getValue();
            const float res = params[RES_PARAM].getValue(), drive = params[DRIVE_PARAM].getValue();
            for (int c = 0; c < n; ++c)
            {
                const float pos = knob + scale::clampV (inputs[CUT_CV_INPUT].getPolyVoltage (c)) * 0.1f * amt;
                const float hz = ladder::cutoffHz (pos) * std::pow (2.f, scale::clampV (inputs[VOCT_INPUT].getPolyVoltage (c)));
                filters[c].set (hz, res + scale::clampV (inputs[RES_CV_INPUT].getPolyVoltage (c)) * 0.1f, drive);
            }
        }
        for (int c = 0; c < n; ++c) outputs[OUT_OUTPUT].setVoltage (filters[c].process (inputs[IN_INPUT].getVoltage (c)), c);
        outputs[OUT_OUTPUT].setChannels (n);
        face.push (clamp (outputs[OUT_OUTPUT].getVoltage (0) / 5.f, -1.f, 1.f), args.sampleRate);
    }

    void onReset (const ResetEvent& e) override
    {
        Module::onReset (e);
        for (ladder::Ladder& f : filters) f.reset();
    }
};

struct FilterWidget : ModuleWidget
{
    FilterWidget (Filter* module)
    {
        setModule (module);
        setPanel (createPanel (asset::plugin (pluginInstance, "res/Filter.svg")));
        {
            namespace L = layout::filter;
            TalkingFace* f = createWidget<TalkingFace> (mm2px (Vec (L::FACE[0] - L::FACE[2], L::FACE[1] - L::FACE[2])));
            f->box.size = mm2px (Vec (2 * L::FACE[2], 2 * L::FACE[2])); if (module) f->tap = &module->face; addChild (f);
        }
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        namespace L = layout::filter;
        auto at = [] (const float* mm) { return mm2px (Vec (mm[0], mm[1])); };

        addParam (createParamCentered<Rogan3PSWhite> (at (L::CUTOFF_PARAM), module, Filter::CUTOFF_PARAM));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::RES_PARAM), module, Filter::RES_PARAM));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::DRIVE_PARAM), module, Filter::DRIVE_PARAM));
        addParam (createParamCentered<Trimpot> (at (L::CUT_CV_PARAM), module, Filter::CUT_CV_PARAM));

        static const float* const ins[Filter::INPUTS_LEN] = { L::IN_INPUT, L::VOCT_INPUT, L::CUT_CV_INPUT, L::RES_CV_INPUT };
        for (int i = 0; i < Filter::INPUTS_LEN; ++i) addInput (createInputCentered<PJ301MPort> (at (ins[i]), module, i));
        addOutput (createOutputCentered<PJ301MPort> (at (L::OUT_OUTPUT), module, Filter::OUT_OUTPUT));
    }
};

Model* modelFilter = createModel<Filter, FilterWidget> ("Filter");
