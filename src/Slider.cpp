// Slider.cpp: the Rack module. Two sections of 8 sliders + gate buttons on a shared clock; every sequencing decision
// (playhead, LENGTH, REVERSE, CHAIN, REC capture, range maths) comes from core/Slider.h. The steps live in the params,
// so the patch saves them and Undo covers hand edits; REC writes what it captures back into them.
// Author: Ari Klopfer / A_Development · MIT
#include "plugin.hpp"
#include "layout.hpp"
#include "Slider.h"
#include "Gate.h"
#include "TalkingFace.hpp"

struct SliderSeq : Module
{
    enum ParamId { ENUMS (SLIDER_PARAM, 16), ENUMS (GATE_PARAM, 16), ENUMS (LENGTH_PARAM, 2), ENUMS (RANGE_PARAM, 2),
                   ENUMS (REC_PARAM, 2), ENUMS (INVERT_PARAM, 2), ENUMS (REVERSE_PARAM, 2), CHAIN_PARAM, PARAMS_LEN };
    enum InputId { CLOCK_INPUT, RESET_INPUT, A_CV_INPUT, A_GATE_INPUT, B_CV_INPUT, B_GATE_INPUT, INPUTS_LEN };
    enum OutputId { A_CV_OUTPUT, A_GATE_OUTPUT, B_CV_OUTPUT, B_GATE_OUTPUT, OUTPUTS_LEN };
    enum LightId { ENUMS (PLAY_LIGHT, 16), ENUMS (GATE_LIGHT, 16), ENUMS (REC_LIGHT, 2), ENUMS (INVERT_LIGHT, 2),
                   ENUMS (REVERSE_LIGHT, 2), CHAIN_LIGHT, LIGHTS_LEN };

    slider::Seq seq;
    gate::Gate clockGate;   // one gate for both sections, fed by every clock edge, so rests do not stretch the period
    dsp::SchmittTrigger clockTrig, resetTrig;
    dsp::ClockDivider lightDiv;
    bool open[2] = {};   // the section's last step had its gate button on: its GATE out follows clockGate
    bool snap = false;   // context menu: CV outs rounded to semitones
    FaceTap face;   // the mascot's mouth

    SliderSeq()
    {
        config (PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        static const char* const sec[2] = { "A", "B" };
        for (int i = 0; i < slider::total; ++i)
        {
            const std::string name = std::string (sec[i / slider::steps]) + std::to_string (i % slider::steps + 1);
            configParam (SLIDER_PARAM + i, 0.f, 1.f, 0.5f, "Step " + name);
            configButton (GATE_PARAM + i, "Gate " + name);
        }
        for (int s = 0; s < 2; ++s)
        {
            const std::string n = sec[s];
            configParam (LENGTH_PARAM + s, 1.f, 8.f, 8.f, n + " length", " steps")->snapEnabled = true;
            configSwitch (RANGE_PARAM + s, 0.f, 4.f, 2.f, n + " range", { "0 to 1 V", "0 to 2 V", "0 to 5 V", "-5 to +5 V", "0 to 10 V" });
            configButton (REC_PARAM + s, n + " record CV and gate into the sliders");
            configButton (INVERT_PARAM + s, n + " invert");
            configButton (REVERSE_PARAM + s, n + " reverse");
        }
        configButton (CHAIN_PARAM, "Chain: A then B as one sequence");
        configInput (CLOCK_INPUT, "Clock");
        configInput (RESET_INPUT, "Reset (the next clock plays step 1)");
        configInput (A_CV_INPUT, "A CV to record");
        configInput (A_GATE_INPUT, "A gate to record");
        configInput (B_CV_INPUT, "B CV to record (A's when unpatched)");
        configInput (B_GATE_INPUT, "B gate to record (A's when unpatched)");
        configOutput (A_CV_OUTPUT, "A CV");
        configOutput (A_GATE_OUTPUT, "A gate");
        configOutput (B_CV_OUTPUT, "B CV");
        configOutput (B_GATE_OUTPUT, "B gate");
        lightDiv.setDivision (64);
    }

    void process (const ProcessArgs& args) override
    {
        // the steps are the params: hand edits and patch loads just work
        for (int i = 0; i < slider::total; ++i)
        {
            seq.val[i] = params[SLIDER_PARAM + i].getValue();
            seq.gate[i] = params[GATE_PARAM + i].getValue() > 0.5f;
        }
        slider::Settings s;
        bool rec[2];
        for (int sec = 0; sec < 2; ++sec)
        {
            s.length[sec] = (int) std::round (params[LENGTH_PARAM + sec].getValue());
            s.range[sec] = (int) std::round (params[RANGE_PARAM + sec].getValue());
            s.invert[sec] = params[INVERT_PARAM + sec].getValue() > 0.5f;
            s.reverse[sec] = params[REVERSE_PARAM + sec].getValue() > 0.5f;
            rec[sec] = params[REC_PARAM + sec].getValue() > 0.5f;
        }
        s.chain = params[CHAIN_PARAM].getValue() > 0.5f;

        // B normals to A: an unpatched B jack reads A's
        const float cvA = inputs[A_CV_INPUT].getVoltage();
        const float cv[2] = { cvA, inputs[B_CV_INPUT].isConnected() ? inputs[B_CV_INPUT].getVoltage() : cvA };
        const bool gA = inputs[A_GATE_INPUT].getVoltage() >= 1.f;
        const bool gateIn[2] = { gA, inputs[B_GATE_INPUT].isConnected() ? inputs[B_GATE_INPUT].getVoltage() >= 1.f : gA };

        // RESET before CLOCK: a reset and a clock on the same sample plays step 1
        if (resetTrig.process (inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) seq.reset();
        const bool edge = clockTrig.process (inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
        if (edge)
        {
            const slider::Out o = seq.clock (s, rec, cv, gateIn);
            for (int sec = 0; sec < 2; ++sec)
            {
                if (o.moved[sec])
                {
                    open[sec] = o.fire[sec];
                    if (rec[sec])   // REC moved the slider and set the button: write them back so the panel shows it
                    {
                        const int i = o.idx[sec];
                        params[SLIDER_PARAM + i].setValue (seq.val[i]);
                        params[GATE_PARAM + i].setValue (seq.gate[i] ? 1.f : 0.f);
                    }
                }
                else if (s.chain) open[sec] = false;   // the idle section holds its CV with the gate low
            }
        }
        const bool high = clockGate.process (edge, args.sampleTime);

        float volts[2];
        for (int sec = 0; sec < 2; ++sec)
        {
            const int i = seq.idx[sec] >= 0 ? seq.idx[sec] : sec * slider::steps;   // before the first step: show step 1's value
            volts[sec] = slider::volts (seq.val[i], s.range[sec], s.invert[sec], snap);
        }
        outputs[A_CV_OUTPUT].setVoltage (volts[0]);
        outputs[B_CV_OUTPUT].setVoltage (volts[1]);
        outputs[A_GATE_OUTPUT].setVoltage (open[0] && high ? 10.f : 0.f);
        outputs[B_GATE_OUTPUT].setVoltage (open[1] && high ? 10.f : 0.f);
        face.push (clamp ((volts[0] + volts[1]) / 4.f, -1.f, 1.f), args.sampleRate);

        if (lightDiv.process()) setLights (s, rec);
    }

    // sliders: lit on the step each section is playing; gate buttons lit while on; REC/INV/REV/CHAIN lit while on
    void setLights (const slider::Settings& s, const bool rec[2])
    {
        for (int i = 0; i < slider::total; ++i)
        {
            lights[PLAY_LIGHT + i].setBrightness (i == seq.idx[0] || i == seq.idx[1] ? 1.f : 0.f);
            lights[GATE_LIGHT + i].setBrightness (seq.gate[i] ? 1.f : 0.f);
        }
        for (int sec = 0; sec < 2; ++sec)
        {
            lights[REC_LIGHT + sec].setBrightness (rec[sec] ? 1.f : 0.f);
            lights[INVERT_LIGHT + sec].setBrightness (s.invert[sec] ? 1.f : 0.f);
            lights[REVERSE_LIGHT + sec].setBrightness (s.reverse[sec] ? 1.f : 0.f);
        }
        lights[CHAIN_LIGHT].setBrightness (s.chain ? 1.f : 0.f);
    }

    void restart()   // playback back to "the next clock plays step 1"
    {
        seq.reset();
        clockGate.reset();
        open[0] = open[1] = false;
    }

    void onReset (const ResetEvent& e) override   // the base resets params first
    {
        Module::onReset (e);
        snap = false;
        restart();
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_object_set_new (root, "snap", json_boolean (snap));
        return root;
    }

    void dataFromJson (json_t* root) override
    {
        snap = false;   // missing key: off
        if (json_t* j = json_object_get (root, "snap")) snap = json_boolean_value (j);
        restart();   // a preset or patch load starts from step 1, like Initialize
    }
};

struct SliderWidget : ModuleWidget
{
    SliderWidget (SliderSeq* module)
    {
        setModule (module);
        setPanel (createPanel (asset::plugin (pluginInstance, "res/Slider.svg")));
        {
            namespace L = layout::slider;
            TalkingFace* f = createWidget<TalkingFace> (mm2px (Vec (L::FACE[0] - L::FACE[2], L::FACE[1] - L::FACE[2])));
            f->box.size = mm2px (Vec (2 * L::FACE[2], 2 * L::FACE[2])); if (module) f->tap = &module->face; addChild (f);
        }
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        namespace L = layout::slider;
        auto at = [] (const float* mm) { return mm2px (Vec (mm[0], mm[1])); };

        static const float* const sliders[slider::total] = { L::SLIDER0_PARAM, L::SLIDER1_PARAM, L::SLIDER2_PARAM, L::SLIDER3_PARAM,
            L::SLIDER4_PARAM, L::SLIDER5_PARAM, L::SLIDER6_PARAM, L::SLIDER7_PARAM, L::SLIDER8_PARAM, L::SLIDER9_PARAM, L::SLIDER10_PARAM,
            L::SLIDER11_PARAM, L::SLIDER12_PARAM, L::SLIDER13_PARAM, L::SLIDER14_PARAM, L::SLIDER15_PARAM };
        static const float* const gates[slider::total] = { L::GATE0_PARAM, L::GATE1_PARAM, L::GATE2_PARAM, L::GATE3_PARAM,
            L::GATE4_PARAM, L::GATE5_PARAM, L::GATE6_PARAM, L::GATE7_PARAM, L::GATE8_PARAM, L::GATE9_PARAM, L::GATE10_PARAM,
            L::GATE11_PARAM, L::GATE12_PARAM, L::GATE13_PARAM, L::GATE14_PARAM, L::GATE15_PARAM };
        for (int i = 0; i < slider::total; ++i)
        {
            addParam (createLightParamCentered<VCVLightSlider<LimeLight>> (at (sliders[i]), module, SliderSeq::SLIDER_PARAM + i, SliderSeq::PLAY_LIGHT + i));
            addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<LimeLight>>> (at (gates[i]), module, SliderSeq::GATE_PARAM + i, SliderSeq::GATE_LIGHT + i));
        }

        static const float* const lengths[2] = { L::LENGTH0_PARAM, L::LENGTH1_PARAM };
        static const float* const ranges[2] = { L::RANGE0_PARAM, L::RANGE1_PARAM };
        static const float* const recs[2] = { L::REC0_PARAM, L::REC1_PARAM };
        static const float* const inverts[2] = { L::INVERT0_PARAM, L::INVERT1_PARAM };
        static const float* const reverses[2] = { L::REVERSE0_PARAM, L::REVERSE1_PARAM };
        for (int s = 0; s < 2; ++s)
        {
            addParam (createParamCentered<Trimpot> (at (lengths[s]), module, SliderSeq::LENGTH_PARAM + s));
            addParam (createParamCentered<Trimpot> (at (ranges[s]), module, SliderSeq::RANGE_PARAM + s));
            addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<RedLight>>> (at (recs[s]), module, SliderSeq::REC_PARAM + s, SliderSeq::REC_LIGHT + s));
            addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<LimeLight>>> (at (inverts[s]), module, SliderSeq::INVERT_PARAM + s, SliderSeq::INVERT_LIGHT + s));
            addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<LimeLight>>> (at (reverses[s]), module, SliderSeq::REVERSE_PARAM + s, SliderSeq::REVERSE_LIGHT + s));
        }
        addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<LimeLight>>> (at (L::CHAIN_PARAM), module, SliderSeq::CHAIN_PARAM, SliderSeq::CHAIN_LIGHT));

        static const float* const ins[SliderSeq::INPUTS_LEN] = { L::CLOCK_INPUT, L::RESET_INPUT, L::A_CV_INPUT, L::A_GATE_INPUT,
            L::B_CV_INPUT, L::B_GATE_INPUT };
        for (int i = 0; i < SliderSeq::INPUTS_LEN; ++i) addInput (createInputCentered<PJ301MPort> (at (ins[i]), module, i));
        static const float* const outs[SliderSeq::OUTPUTS_LEN] = { L::A_CV_OUTPUT, L::A_GATE_OUTPUT, L::B_CV_OUTPUT, L::B_GATE_OUTPUT };
        for (int i = 0; i < SliderSeq::OUTPUTS_LEN; ++i) addOutput (createOutputCentered<PJ301MPort> (at (outs[i]), module, i));
    }

    void appendContextMenu (Menu* menu) override
    {
        SliderSeq* m = dynamic_cast<SliderSeq*> (module);
        if (! m) return;
        menu->addChild (new MenuSeparator);
        menu->addChild (createBoolPtrMenuItem ("Snap CV to semitones", "", &m->snap));
    }
};

Model* modelSlider = createModel<SliderSeq, SliderWidget> ("Slider");
