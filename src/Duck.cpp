// Duck.cpp: the Rack module. A sidechain ducker for audio that does not go through a Mixer strip: the Mixer's sidechain
// envelope (core/Mixer.h) pulls IN L / IN R down by DEPTH. Each side is polyphonic and keeps its channel count; one
// envelope drives both. SC IN reads audio (follower) or triggers, set in the context menu.
// Author: Ari Klopfer / A_Development · MIT
#include "plugin.hpp"
#include "layout.hpp"
#include "Duck.h"
#include "TalkingFace.hpp"

struct Duck : Module
{
    enum ParamId { DEPTH_PARAM, RELEASE_PARAM, PARAMS_LEN };
    enum InputId { L_INPUT, R_INPUT, SC_INPUT, INPUTS_LEN };
    enum OutputId { L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
    enum LightId { DUCK_LIGHT, LIGHTS_LEN };

    duck::Ducker ducker;
    FaceTap face;   // the mascot's mouth
    bool triggerMode = false;

    Duck()
    {
        config (PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam (DEPTH_PARAM, 0.f, 1.f, 0.8f, "Depth (how far the sidechain pulls the audio down)", "%", 0.f, 100.f);
        configParam (RELEASE_PARAM, 0.f, 1.f, mixer::knobOfRelease (0.15f), "Release", " ms", 50.f, 20.f);
        configInput (L_INPUT, "Audio L (polyphonic)");
        configInput (R_INPUT, "Audio R (polyphonic)");
        configInput (SC_INPUT, "Sidechain (audio or triggers, see the menu)");
        configOutput (L_OUTPUT, "Audio L");
        configOutput (R_OUTPUT, "Audio R");
        configLight (DUCK_LIGHT, "Gain reduction");
        configBypass (L_INPUT, L_OUTPUT);
        configBypass (R_INPUT, R_OUTPUT);
    }

    void process (const ProcessArgs& args) override
    {
        // SC unpatched reads 0 V, so the envelope releases (no jump when the cable is pulled)
        const float g = ducker.process (inputs[SC_INPUT].getVoltage(), triggerMode, params[DEPTH_PARAM].getValue(),
                                        mixer::releaseOf (params[RELEASE_PARAM].getValue()), args.sampleTime);
        for (int side = 0; side < 2; ++side)
        {
            Input& in = inputs[L_INPUT + side];
            Output& out = outputs[L_OUTPUT + side];
            const int n = in.getChannels();
            for (int c = 0; c < n; ++c) out.setVoltage (mixer::finite (in.getVoltage (c)) * g, c);
            out.setChannels (n);
        }
        lights[DUCK_LIGHT].setSmoothBrightness (1.f - g, args.sampleTime);
        face.push (clamp (outputs[L_OUTPUT].getVoltage (0) / 5.f, -1.f, 1.f), args.sampleRate);
    }

    void onReset (const ResetEvent& e) override
    {
        Module::onReset (e);
        ducker.reset();
        triggerMode = false;   // Initialize restores the follower, like the Mixer's SC IN
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_object_set_new (root, "triggerMode", json_boolean (triggerMode));
        return root;
    }

    void dataFromJson (json_t* root) override
    {
        if (json_t* j = json_object_get (root, "triggerMode")) triggerMode = json_boolean_value (j);
    }
};

struct DuckWidget : ModuleWidget
{
    DuckWidget (Duck* module)
    {
        setModule (module);
        setPanel (createPanel (asset::plugin (pluginInstance, "res/Duck.svg")));
        {
            namespace L = layout::duck;
            TalkingFace* f = createWidget<TalkingFace> (mm2px (Vec (L::FACE[0] - L::FACE[2], L::FACE[1] - L::FACE[2])));
            f->box.size = mm2px (Vec (2 * L::FACE[2], 2 * L::FACE[2])); if (module) f->tap = &module->face; addChild (f);
        }
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        namespace L = layout::duck;
        auto at = [] (const float* mm) { return mm2px (Vec (mm[0], mm[1])); };

        addParam (createParamCentered<Rogan3PSWhite> (at (L::DEPTH_PARAM), module, Duck::DEPTH_PARAM));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::RELEASE_PARAM), module, Duck::RELEASE_PARAM));
        addInput (createInputCentered<PJ301MPort> (at (L::L_INPUT), module, Duck::L_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::R_INPUT), module, Duck::R_INPUT));
        addInput (createInputCentered<PJ301MPort> (at (L::SC_INPUT), module, Duck::SC_INPUT));
        addOutput (createOutputCentered<PJ301MPort> (at (L::L_OUTPUT), module, Duck::L_OUTPUT));
        addOutput (createOutputCentered<PJ301MPort> (at (L::R_OUTPUT), module, Duck::R_OUTPUT));
        addChild (createLightCentered<SmallSimpleLight<LimeLight>> (at (L::DUCK_LIGHT), module, Duck::DUCK_LIGHT));
    }

    void appendContextMenu (Menu* menu) override
    {
        Duck* m = dynamic_cast<Duck*> (module);
        if (! m) return;
        menu->addChild (new MenuSeparator);
        menu->addChild (createIndexSubmenuItem ("SC IN reads", { "Audio (follower)", "Triggers" },
            [=] () { return (size_t) (m->triggerMode ? 1 : 0); }, [=] (size_t i) { m->triggerMode = i == 1; }));
    }
};

Model* modelDuck = createModel<Duck, DuckWidget> ("Duck");
