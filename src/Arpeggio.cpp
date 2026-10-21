// Arpeggio.cpp: the Rack module. Reads knobs and jacks, normals B to A, and hands every musical decision to core/.
// The scale lives in the 12 key latches (so the patch saves it and Undo covers clicks); TYPE loads a preset into them.
// Author: Ari Klopfer / A_Development · MIT
#include "plugin.hpp"
#include "layout.hpp"
#include "Section.h"

struct Arpeggio : Module
{
    enum ParamId { ENUMS (KEY_PARAM, 12), TYPE_PARAM, RAND_PARAM, MODE_PARAM, DIR_PARAM, OCT_PARAM, AXIS_PARAM, INT_PARAM, PARAMS_LEN };
    enum InputId { TYPE_INPUT, RAND_INPUT, AXIS_INPUT, INT_INPUT, RESET_INPUT, A_CV_INPUT, A_TRIG_INPUT, B_CV_INPUT, B_TRIG_INPUT, INPUTS_LEN };
    enum OutputId { A_CV_OUTPUT, A_TRIG_OUTPUT, B_CV_OUTPUT, B_TRIG_OUTPUT, OUTPUTS_LEN };
    enum LightId { ENUMS (KEY_LIGHT, 12 * 3), ENUMS (MODE_LIGHT, 2), ENUMS (DIR_LIGHT, arp::NUM_DIRS), LIGHTS_LEN };

    const uint32_t seed = random::u32() | 1u;   // A and B share it: a normalled B walks A's random path, so B stays A + INTERVAL
    section::Section a { seed }, b { seed };
    scale::Rng rng { random::u32() | 1u };
    dsp::SchmittTrigger trigA, trigB, randTrig, resetTrig;
    dsp::BooleanTrigger randBtn, dirBtn;
    dsp::PulseGenerator pulseA, pulseB;
    dsp::ClockDivider lightDiv;
    int lastType = -1;   // TYPE index whose preset the keys last loaded; -1 = load on the next sample
    int typeSettle = 0;   // samples to ignore TYPE after a patch load: cables read 0 V until upstream modules run
    int dir = arp::UP;
    bool passThrough = false;

    Arpeggio()
    {
        config (PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        static const char* const keyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int i = 0; i < 12; ++i) configSwitch (KEY_PARAM + i, 0.f, 1.f, 0.f, std::string (keyNames[i]) + " in scale", { "Off", "On" });
        std::vector<std::string> names;
        for (int i = 0; i < scale::numPresets; ++i) names.push_back (scale::presetName (i));
        configSwitch (TYPE_PARAM, 0.f, scale::numPresets - 1, 0.f, "Scale type (loads a preset into the keys)", names);
        getParamQuantity (TYPE_PARAM)->randomizeEnabled = false;   // Randomize rolls the keys; TYPE stays put
        configButton (RAND_PARAM, "Random scale");
        configSwitch (MODE_PARAM, 0.f, 1.f, 0.f, "Mode", { "Quantizer", "Arpeggiator" });
        configButton (DIR_PARAM, "Arp direction: up, down, up-down, random");
        configParam (OCT_PARAM, 1.f, 4.f, 1.f, "Arp octaves")->snapEnabled = true;
        configParam (AXIS_PARAM, -12.f, 12.f, 0.f, "Axis: transpose both outputs", " semitones")->snapEnabled = true;
        configParam (INT_PARAM, -12.f, 12.f, 0.f, "Interval: B above or below A", " scale degrees")->snapEnabled = true;
        configInput (TYPE_INPUT, "Scale type CV (1 V per preset, added to the knob)");
        configInput (RAND_INPUT, "Random scale trigger");
        configInput (AXIS_INPUT, "Axis CV (1 V/oct, in semitone steps)");
        configInput (INT_INPUT, "Interval CV (1/12 V per scale degree)");
        configInput (RESET_INPUT, "Arp reset");
        configInput (A_CV_INPUT, "A CV (1 V/oct)");
        configInput (A_TRIG_INPUT, "A trigger");
        configInput (B_CV_INPUT, "B CV (A's when unpatched)");
        configInput (B_TRIG_INPUT, "B trigger (A's when unpatched)");
        configOutput (A_CV_OUTPUT, "A CV");
        configOutput (A_TRIG_OUTPUT, "A trigger");
        configOutput (B_CV_OUTPUT, "B CV");
        configOutput (B_TRIG_OUTPUT, "B trigger");
        lightDiv.setDivision (64);
    }

    void setKeys (uint16_t mask)
    {
        for (int i = 0; i < 12; ++i) params[KEY_PARAM + i].setValue ((mask >> i) & 1u ? 1.f : 0.f);
    }

    void process (const ProcessArgs& args) override
    {
        // the scale: TYPE loads a preset when its index changes, RANDOM rolls one, the key latches hold it
        if (typeSettle > 0) --typeSettle;
        else
        {
            const int t = scale::typeIndex (params[TYPE_PARAM].getValue() + inputs[TYPE_INPUT].getVoltage(), lastType);
            if (t != lastType) { lastType = t; setKeys (scale::preset (t)); }
        }
        const bool randPressed = randBtn.process (params[RAND_PARAM].getValue() > 0.f);
        const bool randTrigged = randTrig.process (inputs[RAND_INPUT].getVoltage(), 0.1f, 1.f);
        if (randPressed || randTrigged) setKeys (scale::randomMask (rng));
        uint16_t mask = 0;
        for (int i = 0; i < 12; ++i)
            if (params[KEY_PARAM + i].getValue() > 0.5f) mask |= (uint16_t) (1u << i);

        if (dirBtn.process (params[DIR_PARAM].getValue() > 0.f)) dir = (dir + 1) % arp::NUM_DIRS;
        if (resetTrig.process (inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) { a.arpeggiator.reset(); b.arpeggiator.reset(); }

        section::Settings s;
        s.mask = mask;
        s.arpMode = params[MODE_PARAM].getValue() > 0.5f;
        s.octaves = (int) std::round (params[OCT_PARAM].getValue());
        s.dir = (arp::Dir) dir;
        s.axis = clamp ((int) std::round (params[AXIS_PARAM].getValue() + inputs[AXIS_INPUT].getVoltage() * 12.f), -12, 12);
        s.passThrough = passThrough;

        // B normals to A: an unpatched B jack reads A's
        const float cvA = inputs[A_CV_INPUT].getVoltage();
        const float cvB = inputs[B_CV_INPUT].isConnected() ? inputs[B_CV_INPUT].getVoltage() : cvA;
        const bool ownTrigB = inputs[B_TRIG_INPUT].isConnected();
        const float tA = inputs[A_TRIG_INPUT].getVoltage();
        const float tB = ownTrigB ? inputs[B_TRIG_INPUT].getVoltage() : tA;
        const bool edgeA = trigA.process (tA, 0.1f, 1.f);
        const bool edgeB = trigB.process (tB, 0.1f, 1.f);
        const bool patchedA = inputs[A_TRIG_INPUT].isConnected();

        const section::Out oa = a.process (cvA, edgeA, patchedA, s);
        s.degrees = clamp ((int) std::round (params[INT_PARAM].getValue() + inputs[INT_INPUT].getVoltage() * 12.f), -12, 12);
        const section::Out ob = b.process (cvB, edgeB, ownTrigB || patchedA, s);

        if (oa.trig) pulseA.trigger (1e-3f);
        if (ob.trig) pulseB.trigger (1e-3f);
        outputs[A_CV_OUTPUT].setVoltage (oa.volts);
        outputs[B_CV_OUTPUT].setVoltage (ob.volts);
        outputs[A_TRIG_OUTPUT].setVoltage (pulseA.process (args.sampleTime) ? 10.f : 0.f);
        outputs[B_TRIG_OUTPUT].setVoltage (pulseB.process (args.sampleTime) ? 10.f : 0.f);

        if (lightDiv.process()) setLights (mask, oa.pc, ob.pc, s.arpMode);
    }

    // keys: dim white = in scale, green = A's note, blue = B's note (both = cyan). MODE: green quantizer, amber arp.
    void setLights (uint16_t mask, int pcA, int pcB, bool arpMode)
    {
        for (int i = 0; i < 12; ++i)
        {
            const float in = (mask >> i) & 1u ? 0.25f : 0.f;
            const bool isA = i == pcA, isB = i == pcB;
            lights[KEY_LIGHT + 3 * i + 0].setBrightness (isA || isB ? 0.f : in);
            lights[KEY_LIGHT + 3 * i + 1].setBrightness (isA ? 1.f : isB ? 0.f : in);
            lights[KEY_LIGHT + 3 * i + 2].setBrightness (isB ? 1.f : isA ? 0.f : in);
        }
        lights[MODE_LIGHT + 0].setBrightness (arpMode ? 0.6f : 1.f);   // green
        lights[MODE_LIGHT + 1].setBrightness (arpMode ? 1.f : 0.f);    // red; with green = amber
        for (int d = 0; d < arp::NUM_DIRS; ++d) lights[DIR_LIGHT + d].setBrightness (d == dir ? 1.f : 0.f);
    }

    void onReset (const ResetEvent& e) override   // the base resets params first: all keys off, TYPE 0
    {
        Module::onReset (e);
        lastType = -1;   // the next sample loads major into the keys
        dir = arp::UP;
        passThrough = false;
        a.arpeggiator.reset();
        b.arpeggiator.reset();
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_object_set_new (root, "type", json_integer (lastType));
        json_object_set_new (root, "dir", json_integer (dir));
        json_object_set_new (root, "passThrough", json_boolean (passThrough));
        return root;
    }

    void dataFromJson (json_t* root) override
    {
        if (json_t* j = json_object_get (root, "type")) lastType = clamp ((int) json_integer_value (j), -1, scale::numPresets - 1);
        if (json_t* j = json_object_get (root, "dir")) dir = clamp ((int) json_integer_value (j), 0, arp::NUM_DIRS - 1);
        if (json_t* j = json_object_get (root, "passThrough")) passThrough = json_boolean_value (j);
        typeSettle = 64;
    }
};

struct ArpeggioWidget : ModuleWidget
{
    ArpeggioWidget (Arpeggio* module)
    {
        setModule (module);
        setPanel (createPanel (asset::plugin (pluginInstance, "res/Arpeggio.svg")));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        namespace L = layout::arpeggio;
        auto at = [] (const float* mm) { return mm2px (Vec (mm[0], mm[1])); };

        static const float* const keys[12] = { L::KEY0_PARAM, L::KEY1_PARAM, L::KEY2_PARAM, L::KEY3_PARAM, L::KEY4_PARAM, L::KEY5_PARAM,
            L::KEY6_PARAM, L::KEY7_PARAM, L::KEY8_PARAM, L::KEY9_PARAM, L::KEY10_PARAM, L::KEY11_PARAM };
        for (int i = 0; i < 12; ++i)
            addParam (createLightParamCentered<VCVLightBezelLatch<RedGreenBlueLight>> (at (keys[i]), module, Arpeggio::KEY_PARAM + i, Arpeggio::KEY_LIGHT + 3 * i));

        addParam (createParamCentered<Rogan3PSWhite> (at (L::TYPE_PARAM), module, Arpeggio::TYPE_PARAM));
        addParam (createParamCentered<VCVButton> (at (L::RAND_PARAM), module, Arpeggio::RAND_PARAM));
        addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenRedLight>>> (at (L::MODE_PARAM), module, Arpeggio::MODE_PARAM, Arpeggio::MODE_LIGHT));
        addParam (createParamCentered<VCVButton> (at (L::DIR_PARAM), module, Arpeggio::DIR_PARAM));
        static const float* const dirs[arp::NUM_DIRS] = { L::DIR0_LIGHT, L::DIR1_LIGHT, L::DIR2_LIGHT, L::DIR3_LIGHT };
        for (int d = 0; d < arp::NUM_DIRS; ++d) addChild (createLightCentered<SmallSimpleLight<YellowLight>> (at (dirs[d]), module, Arpeggio::DIR_LIGHT + d));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::OCT_PARAM), module, Arpeggio::OCT_PARAM));
        addParam (createParamCentered<Rogan1PSBlue> (at (L::AXIS_PARAM), module, Arpeggio::AXIS_PARAM));
        addParam (createParamCentered<Rogan1PSBlue> (at (L::INT_PARAM), module, Arpeggio::INT_PARAM));

        static const float* const ins[Arpeggio::INPUTS_LEN] = { L::TYPE_INPUT, L::RAND_INPUT, L::AXIS_INPUT, L::INT_INPUT, L::RESET_INPUT,
            L::A_CV_INPUT, L::A_TRIG_INPUT, L::B_CV_INPUT, L::B_TRIG_INPUT };
        for (int i = 0; i < Arpeggio::INPUTS_LEN; ++i) addInput (createInputCentered<PJ301MPort> (at (ins[i]), module, i));
        static const float* const outs[Arpeggio::OUTPUTS_LEN] = { L::A_CV_OUTPUT, L::A_TRIG_OUTPUT, L::B_CV_OUTPUT, L::B_TRIG_OUTPUT };
        for (int i = 0; i < Arpeggio::OUTPUTS_LEN; ++i) addOutput (createOutputCentered<PJ301MPort> (at (outs[i]), module, i));
    }

    void appendContextMenu (Menu* menu) override
    {
        Arpeggio* m = dynamic_cast<Arpeggio*> (module);
        if (! m) return;
        menu->addChild (new MenuSeparator);
        menu->addChild (createBoolPtrMenuItem ("Triggers pass through (quantizer mode)", "", &m->passThrough));
    }
};

Model* modelArpeggio = createModel<Arpeggio, ArpeggioWidget> ("Arpeggio");
