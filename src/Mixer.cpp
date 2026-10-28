// Mixer.cpp: the Rack module. Eight stereo strips into a master bus, two send/return pairs and one sidechain that ducks
// every strip by its DUCK knob. Every gain, pan, duck and clip decision comes from core/Mixer.h, every meter reading
// from core/Meter.h (drawn by MixerWidgets.hpp).
// Author: Ari Klopfer / A_Development · MIT
#include "plugin.hpp"
#include "layout.hpp"
#include "Mixer.h"
#include "Meter.h"
#include "MixerWidgets.hpp"
#include "TalkingFace.hpp"
static_assert (layout::mixer::STRIPS == mixer::numStrips, "tools/panel.py strip count must match core/Mixer.h");

// LEVEL / MASTER shown in dB of the knob's 2 k^2 gain
struct MixerGainQuantity : ParamQuantity
{
    float getDisplayValue() override
    {
        const float g = mixer::levelGain (getValue());
        const float db = 20.f * std::log10 (g);
        return g > 0.f ? (std::fabs (db) < 1e-3f ? 0.f : db) : -INFINITY;   // 0 dB snapped: 2 k^2 is 0.99999994 at the default
    }
    void setDisplayValue (float db) override
    {
        if (std::isnan (db)) return;   // as ParamQuantity's own guard: NaN would clamp to +6 dB
        setValue (std::sqrt (std::pow (10.f, db / 20.f) / 2.f));
    }
};

struct Mixer : Module
{
    static constexpr int N = mixer::numStrips;
    enum ParamId { ENUMS (LEVEL_PARAM, N), ENUMS (PAN_PARAM, N), ENUMS (SEND1_PARAM, N), ENUMS (SEND2_PARAM, N), ENUMS (DUCK_PARAM, N),
        ENUMS (MUTE_PARAM, N), ENUMS (SOLO_PARAM, N), MASTER_PARAM, RELEASE_PARAM, PARAMS_LEN };
    enum InputId { ENUMS (L_INPUT, N), ENUMS (R_INPUT, N), SC_INPUT, RETURN1_L_INPUT, RETURN1_R_INPUT, RETURN2_L_INPUT, RETURN2_R_INPUT, INPUTS_LEN };
    enum OutputId { OUT_L_OUTPUT, OUT_R_OUTPUT, SEND1_L_OUTPUT, SEND1_R_OUTPUT, SEND2_L_OUTPUT, SEND2_R_OUTPUT, OUTPUTS_LEN };
    enum LightId { ENUMS (MUTE_LIGHT, N), ENUMS (SOLO_LIGHT, N), ENUMS (SIGNAL_LIGHT, 2 * N), LIGHTS_LEN };   // SIGNAL: retired (v1's level light), kept so ids never move

    mixer::Strip strips[N];
    mixer::Sidechain sc;
    float masterGain = 0.f;   // slewed MASTER gain
    float sampleRate = 0.f, step = 0.f;
    FaceTap face;            // the band mascot's mouth: the master out
    FaceTap stripFace[N];    // each strip's mouth: its post-fader out
    MeterState meters;       // what the meters show, refreshed every light block
    meter::Ballistics inBal[N], outBal[2];
    meter::ClipLatch inClip[N], outClip;
    meter::Loudness loudness;
    float inPeak[N] = {}, outPeak[2] = {}, envPeak = 0.f;   // this light block's peaks
    dsp::ClockDivider lightDiv;
    bool triggerMode = false;   // context menu: SC IN reads triggers (else audio, followed)
    bool preFader = false;      // context menu: sends tap pre-fader (else post-fader)
    bool softClip = true;       // context menu: master soft clip

    Mixer()
    {
        config (PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        for (int i = 0; i < N; ++i)
        {
            const std::string s = "Strip " + std::to_string (i + 1);
            configParam<MixerGainQuantity> (LEVEL_PARAM + i, 0.f, 1.f, mixer::unityKnob, s + " level", " dB");
            configParam (PAN_PARAM + i, -1.f, 1.f, 0.f, s + " pan", "%", 0.f, 100.f);
            configParam (SEND1_PARAM + i, 0.f, 1.f, 0.f, s + " send 1", "%", 0.f, 100.f);
            configParam (SEND2_PARAM + i, 0.f, 1.f, 0.f, s + " send 2", "%", 0.f, 100.f);
            configParam (DUCK_PARAM + i, 0.f, 1.f, 0.f, s + " duck (how far the sidechain pulls it down)", "%", 0.f, 100.f);
            configSwitch (MUTE_PARAM + i, 0.f, 1.f, 0.f, s + " mute", { "Off", "On" });
            configSwitch (SOLO_PARAM + i, 0.f, 1.f, 0.f, s + " solo", { "Off", "On" });
            configInput (L_INPUT + i, s + " left / mono (poly cables are summed)");
            configInput (R_INPUT + i, s + " right (unpatched: copies left)");
        }
        configParam<MixerGainQuantity> (MASTER_PARAM, 0.f, 1.f, mixer::unityKnob, "Master level", " dB");
        configParam (RELEASE_PARAM, 0.f, 1.f, mixer::knobOfRelease (0.15f), "Duck release", " ms", 50.f, 20.f);
        configInput (SC_INPUT, "Sidechain (audio or triggers, see the menu)");
        configInput (RETURN1_L_INPUT, "Return 1 left / mono");
        configInput (RETURN1_R_INPUT, "Return 1 right (unpatched: copies left)");
        configInput (RETURN2_L_INPUT, "Return 2 left / mono");
        configInput (RETURN2_R_INPUT, "Return 2 right (unpatched: copies left)");
        configOutput (OUT_L_OUTPUT, "Master left");
        configOutput (OUT_R_OUTPUT, "Master right");
        configOutput (SEND1_L_OUTPUT, "Send 1 left");
        configOutput (SEND1_R_OUTPUT, "Send 1 right");
        configOutput (SEND2_L_OUTPUT, "Send 2 left");
        configOutput (SEND2_R_OUTPUT, "Send 2 right");
        // Randomize leaves the mix alone: levels, mutes, solos, master
        for (int i = 0; i < N; ++i)
        {
            paramQuantities[LEVEL_PARAM + i]->randomizeEnabled = false;
            paramQuantities[MUTE_PARAM + i]->randomizeEnabled = false;
            paramQuantities[SOLO_PARAM + i]->randomizeEnabled = false;
        }
        paramQuantities[MASTER_PARAM]->randomizeEnabled = false;
        lightDiv.setDivision (64);
    }

    void returnOf (int lId, int rId, float& l, float& r)   // a stereo return, R normalled to L
    {
        l = mixer::finite (inputs[lId].getVoltage());
        r = inputs[rId].isConnected() ? mixer::finite (inputs[rId].getVoltage()) : l;
    }

    void process (const ProcessArgs& args) override
    {
        if (args.sampleRate != sampleRate)
        {
            sampleRate = args.sampleRate;
            step = mixer::slewStep (args.sampleTime);
            loudness.setRate (sampleRate);
            meters.history.setRate (sampleRate);
        }
        bool anySolo = false;
        for (int i = 0; i < N; ++i) anySolo = anySolo || params[SOLO_PARAM + i].getValue() > 0.5f;

        // SC IN unpatched reads 0 V, so the envelope releases (no jump when the cable is pulled)
        const float env = sc.process (inputs[SC_INPUT].getVoltage(), triggerMode, mixer::releaseOf (params[RELEASE_PARAM].getValue()), args.sampleTime);

        float busL = 0.f, busR = 0.f, s1L = 0.f, s1R = 0.f, s2L = 0.f, s2R = 0.f;
        for (int i = 0; i < N; ++i)
        {
            const float l = inputs[L_INPUT + i].getVoltageSum();
            const bool stereo = inputs[R_INPUT + i].isConnected();
            const float r = stereo ? inputs[R_INPUT + i].getVoltageSum() : l;
            const bool on = mixer::audible (params[MUTE_PARAM + i].getValue() > 0.5f, params[SOLO_PARAM + i].getValue() > 0.5f, anySolo);
            float oL, oR, pL, pR;
            strips[i].process (l, r, stereo, params[LEVEL_PARAM + i].getValue(), params[PAN_PARAM + i].getValue(), on,
                               params[DUCK_PARAM + i].getValue(), env, step, oL, oR, pL, pR);
            busL += oL;
            busR += oR;
            const float tapL = preFader ? pL : oL, tapR = preFader ? pR : oR;
            const float g1 = mixer::sendGain (params[SEND1_PARAM + i].getValue()), g2 = mixer::sendGain (params[SEND2_PARAM + i].getValue());
            s1L += tapL * g1;
            s1R += tapR * g1;
            s2L += tapL * g2;
            s2R += tapR * g2;
            inPeak[i] = std::fmax (inPeak[i], std::fmax (std::fabs (mixer::finite (l)), std::fabs (mixer::finite (r))));   // the input, pre-fader, before the duck
            stripFace[i].push (clamp ((oL + oR) / 10.f, -1.f, 1.f), args.sampleRate);
        }
        float rl, rr;
        returnOf (RETURN1_L_INPUT, RETURN1_R_INPUT, rl, rr);
        busL += rl;
        busR += rr;
        returnOf (RETURN2_L_INPUT, RETURN2_R_INPUT, rl, rr);
        busL += rl;
        busR += rr;

        masterGain = mixer::slew (masterGain, mixer::levelGain (params[MASTER_PARAM].getValue()), step);
        float outL = busL * masterGain, outR = busR * masterGain;
        if (softClip)
        {
            outL = mixer::softClip (outL);
            outR = mixer::softClip (outR);
        }
        outputs[OUT_L_OUTPUT].setVoltage (outL);
        outputs[OUT_R_OUTPUT].setVoltage (outR);
        outputs[SEND1_L_OUTPUT].setVoltage (s1L);
        outputs[SEND1_R_OUTPUT].setVoltage (s1R);
        outputs[SEND2_L_OUTPUT].setVoltage (s2L);
        outputs[SEND2_R_OUTPUT].setVoltage (s2R);
        face.push (clamp ((outL + outR) / 10.f, -1.f, 1.f), args.sampleRate);
        outPeak[0] = std::fmax (outPeak[0], std::fabs (outL));
        outPeak[1] = std::fmax (outPeak[1], std::fabs (outR));
        envPeak = std::fmax (envPeak, env);
        loudness.process (outL, outR);
        meters.history.process (std::fmax (std::fabs (outL), std::fabs (outR)));

        if (lightDiv.process())
        {
            setLights();
            updateMeters (args.sampleTime * lightDiv.getDivision());
        }
    }

    // MUTE magenta, SOLO lime while latched
    void setLights()
    {
        for (int i = 0; i < N; ++i)
        {
            lights[MUTE_LIGHT + i].setBrightness (params[MUTE_PARAM + i].getValue() > 0.5f ? 1.f : 0.f);
            lights[SOLO_LIGHT + i].setBrightness (params[SOLO_PARAM + i].getValue() > 0.5f ? 1.f : 0.f);
        }
    }

    // Once per light block (dt seconds): the block's peaks through the ballistics and clip latches into `meters`, the
    // duck readout from the block's deepest envelope, the loudness. Clears the latches a click or Initialize asked for.
    void updateMeters (float dt)
    {
        const uint32_t clear = meters.clearReq.exchange (0);
        for (int i = 0; i < N; ++i)
        {
            if (clear & (MeterState::clearAll | 1u << i)) inClip[i].clear();
            inBal[i].process (meter::dbfs (inPeak[i]), dt);
            inClip[i].process (inPeak[i]);
            meters.stripDb[i] = inBal[i].level;
            meters.stripHoldDb[i] = inBal[i].hold;
            meters.stripClip[i] = inClip[i].on;
            meters.duckDb[i] = meter::duckDb (params[DUCK_PARAM + i].getValue(), envPeak);
            inPeak[i] = 0.f;
        }
        if (clear & MeterState::clearAll) outClip.clear();
        for (int c = 0; c < 2; ++c)
        {
            outBal[c].process (meter::dbfs (outPeak[c]), dt);
            outClip.process (outPeak[c]);
            meters.masterDb[c] = outBal[c].level;
            meters.masterHoldDb[c] = outBal[c].hold;
            outPeak[c] = 0.f;
        }
        meters.masterClip = outClip.on;
        meters.lufs = loudness.value;
        envPeak = 0.f;
    }

    void onReset (const ResetEvent& e) override   // the base resets params first
    {
        Module::onReset (e);
        triggerMode = preFader = false;
        softClip = true;
        // slewed gains are left alone: they glide to the reset params, so Initialize does not click
        sc.reset();
        meters.clearReq.fetch_or (MeterState::clearAll);   // Initialize clears every clip latch
        for (meter::Ballistics& b : inBal) b.reset();      // and drops every bar and peak tick
        for (meter::Ballistics& b : outBal) b.reset();
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_object_set_new (root, "triggerMode", json_boolean (triggerMode));
        json_object_set_new (root, "preFader", json_boolean (preFader));
        json_object_set_new (root, "softClip", json_boolean (softClip));
        return root;
    }

    void dataFromJson (json_t* root) override
    {
        if (json_t* j = json_object_get (root, "triggerMode")) triggerMode = json_boolean_value (j);
        if (json_t* j = json_object_get (root, "preFader")) preFader = json_boolean_value (j);
        if (json_t* j = json_object_get (root, "softClip")) softClip = json_boolean_value (j);
    }
};

struct MixerWidget : ModuleWidget
{
    MixerWidget (Mixer* module)
    {
        setModule (module);
        setPanel (createPanel (asset::plugin (pluginInstance, "res/Mixer.svg")));
        {
            namespace L = layout::mixer;
            TalkingFace* f = createWidget<TalkingFace> (mm2px (Vec (L::FACE[0] - L::FACE[2], L::FACE[1] - L::FACE[2])));
            f->box.size = mm2px (Vec (2 * L::FACE[2], 2 * L::FACE[2])); if (module) f->tap = &module->face; addChild (f);
        }
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        namespace L = layout::mixer;
        auto at = [] (const float* mm) { return mm2px (Vec (mm[0], mm[1])); };
        auto strip = [] (const float* first, int i) { return mm2px (Vec (first[0] + i * L::STRIP_DX, first[1])); };   // strip i's part, from strip 0's
        auto boxAt = [] (const float* centre, const float* size, float dx) {   // a layout box, px; dx moves it right, mm
            return Rect (mm2px (Vec (centre[0] + dx - size[0] / 2, centre[1] - size[1] / 2)), mm2px (Vec (size[0], size[1]))); };

        for (int i = 0; i < Mixer::N; ++i)
        {
            TalkingFace* f = createWidget<TalkingFace> (strip (L::STRIP_FACE0, i).minus (mm2px (Vec (L::STRIP_FACE_R, L::STRIP_FACE_R))));
            f->box.size = mm2px (Vec (2 * L::STRIP_FACE_R, 2 * L::STRIP_FACE_R));
            if (module) f->tap = &module->stripFace[i];
            addChild (f);
            const Rect lad = boxAt (L::METER0, L::METER_SIZE, i * L::STRIP_DX), duck = boxAt (L::DUCKBAR0, L::DUCKBAR_SIZE, i * L::STRIP_DX);
            StripMeter* m = new StripMeter;
            m->box = lad.expand (duck);
            m->ladderBox = Rect (lad.pos.minus (m->box.pos), lad.size);
            m->duckBox = Rect (duck.pos.minus (m->box.pos), duck.size);
            m->strip = i;
            if (module) m->st = &module->meters;
            addChild (m);

            addParam (createParamCentered<BefacoTinyKnob> (strip (L::SEND1_0_PARAM, i), module, Mixer::SEND1_PARAM + i));
            addParam (createParamCentered<BefacoTinyKnob> (strip (L::SEND2_0_PARAM, i), module, Mixer::SEND2_PARAM + i));
            addParam (createParamCentered<BefacoTinyKnob> (strip (L::DUCK0_PARAM, i), module, Mixer::DUCK_PARAM + i));
            addParam (createParamCentered<BefacoTinyKnob> (strip (L::PAN0_PARAM, i), module, Mixer::PAN_PARAM + i));
            addParam (createParamCentered<VCVSlider> (strip (L::LEVEL0_PARAM, i), module, Mixer::LEVEL_PARAM + i));
            addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<MagentaLight>>> (strip (L::MUTE0_PARAM, i), module, Mixer::MUTE_PARAM + i, Mixer::MUTE_LIGHT + i));
            addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<LimeLight>>> (strip (L::SOLO0_PARAM, i), module, Mixer::SOLO_PARAM + i, Mixer::SOLO_LIGHT + i));
            addInput (createInputCentered<PJ301MPort> (strip (L::L0_INPUT, i), module, Mixer::L_INPUT + i));
            addInput (createInputCentered<PJ301MPort> (strip (L::R0_INPUT, i), module, Mixer::R_INPUT + i));
        }

        MasterDisplay* d = new MasterDisplay;
        d->box = boxAt (L::DISPLAY, L::DISPLAY_SIZE, 0.f);
        if (module) d->st = &module->meters;
        addChild (d);
        addParam (createParamCentered<Rogan3PSWhite> (at (L::MASTER_PARAM), module, Mixer::MASTER_PARAM));
        addParam (createParamCentered<BefacoTinyKnob> (at (L::RELEASE_PARAM), module, Mixer::RELEASE_PARAM));
        static const float* const ins[5] = { L::SC_INPUT, L::RETURN1_L_INPUT, L::RETURN1_R_INPUT, L::RETURN2_L_INPUT, L::RETURN2_R_INPUT };
        for (int i = 0; i < 5; ++i) addInput (createInputCentered<PJ301MPort> (at (ins[i]), module, Mixer::SC_INPUT + i));
        static const float* const outs[Mixer::OUTPUTS_LEN] = { L::OUT_L_OUTPUT, L::OUT_R_OUTPUT, L::SEND1_L_OUTPUT, L::SEND1_R_OUTPUT,
            L::SEND2_L_OUTPUT, L::SEND2_R_OUTPUT };
        for (int i = 0; i < Mixer::OUTPUTS_LEN; ++i) addOutput (createOutputCentered<PJ301MPort> (at (outs[i]), module, i));
    }

    void appendContextMenu (Menu* menu) override
    {
        Mixer* m = dynamic_cast<Mixer*> (module);
        if (! m) return;
        menu->addChild (new MenuSeparator);
        menu->addChild (createIndexSubmenuItem ("SC IN reads", { "Audio (follower)", "Triggers" },
            [=] () { return (size_t) (m->triggerMode ? 1 : 0); }, [=] (size_t i) { m->triggerMode = i == 1; }));
        menu->addChild (createBoolPtrMenuItem ("Sends pre-fader (off: post-fader)", "", &m->preFader));
        menu->addChild (createBoolPtrMenuItem ("Master soft clip", "", &m->softClip));
    }
};

Model* modelMixer = createModel<Mixer, MixerWidget> ("Mixer");
