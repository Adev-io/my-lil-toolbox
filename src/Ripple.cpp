// Ripple.cpp: the Rack module. Runs the clock (internal or EXT), PLAY and RESET, and fires pulses. Every rhythm
// decision (which generators hit, R and R', ACCENT, the wave crossings) comes from core/Rhythm.h.
// Author: Ari Klopfer / A_Development · MIT
#include "plugin.hpp"
#include "layout.hpp"
#include "Scale.h"
#include "Rhythm.h"

struct Ripple : Module
{
    enum ParamId { TEMPO_PARAM, PLAY_PARAM, RESET_PARAM, MODE_PARAM, DENSITY_PARAM, ENUMS (PERIOD_PARAM, 4), ENUMS (ON_PARAM, 4), PARAMS_LEN };
    enum InputId { TEMPO_INPUT, PLAY_INPUT, RESET_INPUT, EXT_INPUT, ENUMS (PERIOD_INPUT, 4), INPUTS_LEN };
    enum OutputId { ENUMS (GEN_OUTPUT, 4), CLOCK_OUTPUT, R_OUTPUT, RP_OUTPUT, ACCENT_OUTPUT, OUTPUTS_LEN };
    enum LightId { TEMPO_LIGHT, PLAY_LIGHT, ENUMS (MODE_LIGHT, 2), ENUMS (ON_LIGHT, 4), LIGHTS_LEN };

    rhythm::Clock clock;
    rhythm::Crossing cross;
    dsp::SchmittTrigger playTrig, resetTrig, extTrig;
    dsp::BooleanTrigger playBtn, resetBtn;
    dsp::PulseGenerator genPulse[4], clockPulse, rPulse, rpPulse, accentPulse, tempoPulse;
    dsp::ClockDivider lightDiv;
    bool play = true;
    bool extPerBeat = false;   // context menu: EXT is 1 pulse per beat (else per 16th)
    bool clockBeats = false;   // context menu: CLOCK out fires on beats (else 16ths)
    bool tempoOn = false;

    Ripple()
    {
        config (PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam (TEMPO_PARAM, 30.f, 300.f, 120.f, "Tempo", " BPM");
        configButton (PLAY_PARAM, "Play / stop");
        configButton (RESET_PARAM, "Reset to the downbeat");
        configSwitch (MODE_PARAM, 0.f, 1.f, 0.f, "Mode", { "Grid", "Wave" });
        configParam (DENSITY_PARAM, 1.f, 4.f, 1.f, "Density")->snapEnabled = true;
        static const char* const names[4] = { "A", "B", "C", "D" };
        for (int i = 0; i < 4; ++i)
        {
            configParam (PERIOD_PARAM + i, 1.f, 16.f, (float) (i + 2), std::string (names[i]) + " period", " sixteenths")->snapEnabled = true;
            configSwitch (ON_PARAM + i, 0.f, 1.f, i < 2 ? 1.f : 0.f, std::string (names[i]) + " on", { "Off", "On" });
            configInput (PERIOD_INPUT + i, std::string (names[i]) + " period CV (1 V per sixteenth)");
            configOutput (GEN_OUTPUT + i, std::string (names[i]) + " trigger");
        }
        configInput (TEMPO_INPUT, "Tempo CV (+1 V doubles)");
        configInput (PLAY_INPUT, "Play / stop trigger");
        configInput (RESET_INPUT, "Reset trigger");
        configInput (EXT_INPUT, "External clock");
        configOutput (CLOCK_OUTPUT, "Clock");
        configOutput (R_OUTPUT, "R");
        configOutput (RP_OUTPUT, "R' (complement of R)");
        configOutput (ACCENT_OUTPUT, "Accent (once per cycle)");
        lightDiv.setDivision (64);
    }

    void restart()   // RESET: back to an armed downbeat
    {
        clock.reset();
        cross.reset();
    }

    // One tick of the grid: generator, CLOCK, ACCENT and tempo-light pulses; R and R' too in GRID mode.
    void fire (uint32_t t, const int* period, unsigned on, int density, bool wave)
    {
        const rhythm::Tick k = rhythm::grid (t, period, on, density);
        for (int i = 0; i < 4; ++i)
            if ((k.gens >> i) & 1u) genPulse[i].trigger (1e-3f);
        if (! clockBeats || t % 4 == 0) clockPulse.trigger (1e-3f);
        if (t % 4 == 0) tempoPulse.trigger (0.05f);
        if (k.accent) accentPulse.trigger (1e-3f);
        if (! wave)
        {
            if (k.r) rPulse.trigger (1e-3f);
            if (k.rp) rpPulse.trigger (1e-3f);
        }
    }

    void process (const ProcessArgs& args) override
    {
        const double dt = args.sampleTime;
        const bool playPressed = playBtn.process (params[PLAY_PARAM].getValue() > 0.f);
        const bool playTrigged = playTrig.process (inputs[PLAY_INPUT].getVoltage(), 0.1f, 1.f);
        if (playPressed || playTrigged)
        {
            play = ! play;
            if (play) clock.resume();
        }
        const bool resetPressed = resetBtn.process (params[RESET_PARAM].getValue() > 0.f);
        const bool resetTrigged = resetTrig.process (inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
        if (resetPressed || resetTrigged) restart();

        int period[4];
        unsigned on = 0;
        for (int i = 0; i < 4; ++i)
        {
            const float cv = scale::clampV (inputs[PERIOD_INPUT + i].getVoltage());
            period[i] = rhythm::clampPeriod ((int) std::round (params[PERIOD_PARAM + i].getValue() + cv));
            if (params[ON_PARAM + i].getValue() > 0.5f) on |= 1u << i;
        }
        const int density = (int) std::round (params[DENSITY_PARAM].getValue());
        const bool wave = params[MODE_PARAM].getValue() > 0.5f;

        // the clock: EXT edges when patched, else the internal tempo
        const bool ext = inputs[EXT_INPUT].isConnected();
        const bool edge = extTrig.process (inputs[EXT_INPUT].getVoltage(), 0.1f, 1.f);
        const float bpm = clamp (params[TEMPO_PARAM].getValue() * std::pow (2.f, scale::clampV (inputs[TEMPO_INPUT].getVoltage())), 10.f, 1000.f);
        uint32_t tick = 0;
        if (clock.process (dt, bpm, ext, edge, play, extPerBeat, tick)) fire (tick, period, on, density, wave);

        // WAVE: track the sum every sample (so resuming does not glitch); fire only in WAVE mode while playing.
        // While armed the downbeat has not played yet, so there is nothing to track.
        if (! clock.counter.armed)
        {
            bool wr = false, wrp = false;
            cross.process (rhythm::waveSum (clock.counter.t + (double) clock.frac, period, on), rhythm::threshold (density), on != 0, wr, wrp);
            if (wave && play)
            {
                if (wr) rPulse.trigger (1e-3f);
                if (wrp) rpPulse.trigger (1e-3f);
            }
        }

        const float st = args.sampleTime;
        for (int i = 0; i < 4; ++i) outputs[GEN_OUTPUT + i].setVoltage (genPulse[i].process (st) ? 10.f : 0.f);
        outputs[CLOCK_OUTPUT].setVoltage (clockPulse.process (st) ? 10.f : 0.f);
        outputs[R_OUTPUT].setVoltage (rPulse.process (st) ? 10.f : 0.f);
        outputs[RP_OUTPUT].setVoltage (rpPulse.process (st) ? 10.f : 0.f);
        outputs[ACCENT_OUTPUT].setVoltage (accentPulse.process (st) ? 10.f : 0.f);
        tempoOn = tempoPulse.process (st);

        if (lightDiv.process()) setLights (on, wave);
    }

    // tempo light on each beat; PLAY lit while playing; MODE lime = grid, magenta = wave; ON latches lit while on
    void setLights (unsigned on, bool wave)
    {
        lights[TEMPO_LIGHT].setBrightness (tempoOn ? 1.f : 0.f);
        lights[PLAY_LIGHT].setBrightness (play ? 1.f : 0.f);
        lights[MODE_LIGHT + 0].setBrightness (wave ? 0.f : 1.f);   // lime: grid
        lights[MODE_LIGHT + 1].setBrightness (wave ? 1.f : 0.f);   // magenta: wave
        for (int i = 0; i < 4; ++i) lights[ON_LIGHT + i].setBrightness ((on >> i) & 1u ? 1.f : 0.f);
    }

    void onReset (const ResetEvent& e) override   // the base resets params first
    {
        Module::onReset (e);
        play = true;
        extPerBeat = clockBeats = false;
        restart();
        clock.resume();   // Initialize resumes play: same rule as pressing PLAY
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_object_set_new (root, "play", json_boolean (play));
        json_object_set_new (root, "extPerBeat", json_boolean (extPerBeat));
        json_object_set_new (root, "clockBeats", json_boolean (clockBeats));
        return root;
    }

    void dataFromJson (json_t* root) override
    {
        if (json_t* j = json_object_get (root, "play")) play = json_boolean_value (j);
        if (json_t* j = json_object_get (root, "extPerBeat")) extPerBeat = json_boolean_value (j);
        if (json_t* j = json_object_get (root, "clockBeats")) clockBeats = json_boolean_value (j);
        restart();   // a preset or patch load starts at an armed downbeat, like Initialize
        clock.resume();
    }
};

struct RippleWidget : ModuleWidget
{
    RippleWidget (Ripple* module)
    {
        setModule (module);
        setPanel (createPanel (asset::plugin (pluginInstance, "res/Ripple.svg")));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild (createWidget<ScrewBlack> (Vec (RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild (createWidget<ScrewBlack> (Vec (box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        namespace L = layout::ripple;
        auto at = [] (const float* mm) { return mm2px (Vec (mm[0], mm[1])); };

        addParam (createParamCentered<Rogan3PSWhite> (at (L::TEMPO_PARAM), module, Ripple::TEMPO_PARAM));
        addChild (createLightCentered<SmallSimpleLight<LimeLight>> (at (L::TEMPO_LIGHT), module, Ripple::TEMPO_LIGHT));
        addParam (createParamCentered<Rogan1PSWhite> (at (L::DENSITY_PARAM), module, Ripple::DENSITY_PARAM));
        addParam (createLightParamCentered<VCVLightButton<MediumSimpleLight<LimeLight>>> (at (L::PLAY_PARAM), module, Ripple::PLAY_PARAM, Ripple::PLAY_LIGHT));
        addParam (createParamCentered<VCVButton> (at (L::RESET_PARAM), module, Ripple::RESET_PARAM));
        addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<LimeMagentaLight>>> (at (L::MODE_PARAM), module, Ripple::MODE_PARAM, Ripple::MODE_LIGHT));

        static const float* const periods[4] = { L::PERIOD0_PARAM, L::PERIOD1_PARAM, L::PERIOD2_PARAM, L::PERIOD3_PARAM };
        static const float* const ons[4] = { L::ON0_PARAM, L::ON1_PARAM, L::ON2_PARAM, L::ON3_PARAM };
        for (int i = 0; i < 4; ++i)
        {
            addParam (createParamCentered<Rogan1PSBlue> (at (periods[i]), module, Ripple::PERIOD_PARAM + i));
            addParam (createLightParamCentered<VCVLightLatch<MediumSimpleLight<LimeLight>>> (at (ons[i]), module, Ripple::ON_PARAM + i, Ripple::ON_LIGHT + i));
        }

        static const float* const ins[Ripple::INPUTS_LEN] = { L::TEMPO_INPUT, L::PLAY_INPUT, L::RESET_INPUT, L::EXT_INPUT,
            L::PERIOD0_INPUT, L::PERIOD1_INPUT, L::PERIOD2_INPUT, L::PERIOD3_INPUT };
        for (int i = 0; i < Ripple::INPUTS_LEN; ++i) addInput (createInputCentered<PJ301MPort> (at (ins[i]), module, i));
        static const float* const outs[Ripple::OUTPUTS_LEN] = { L::GEN0_OUTPUT, L::GEN1_OUTPUT, L::GEN2_OUTPUT, L::GEN3_OUTPUT,
            L::CLOCK_OUTPUT, L::R_OUTPUT, L::RP_OUTPUT, L::ACCENT_OUTPUT };
        for (int i = 0; i < Ripple::OUTPUTS_LEN; ++i) addOutput (createOutputCentered<PJ301MPort> (at (outs[i]), module, i));
    }

    void appendContextMenu (Menu* menu) override
    {
        Ripple* m = dynamic_cast<Ripple*> (module);
        if (! m) return;
        menu->addChild (new MenuSeparator);
        menu->addChild (createBoolPtrMenuItem ("EXT clock: 1 pulse per beat (off: per 16th)", "", &m->extPerBeat));
        menu->addChild (createBoolPtrMenuItem ("CLOCK out on beats (off: every 16th)", "", &m->clockBeats));
    }
};

Model* modelRipple = createModel<Ripple, RippleWidget> ("Ripple");
