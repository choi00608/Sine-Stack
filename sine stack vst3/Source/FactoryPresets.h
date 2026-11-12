#pragma once
#include <array>
#include <initializer_list>
namespace FactoryPresets {
struct Value { const char* id; float value; };
struct Preset {
    const char *key, *name, *category, *description, *chain;
    int demoNote; bool chord;
    std::initializer_list<Value> values;
};
inline const std::array<Preset, 10> presets {{
    {"neon-sub", "01 Neon Sub", "Bass", "Round mono sine bass with subtle saturation. Play C1-C3.", "oscillator,fx0,fx1", 36, false,
     {{"attack",.008f},{"decay",.24f},{"sustain",.75f},{"release",.10f},{"level",.65f},{"voiceMode",1},{"monoLegato",1},
      {"fx0type",1},{"fx0amount",.26f},{"fx0driveStrength",.18f},{"fx0driveTone",.28f},
      {"fx1type",5},{"fx1amount",1},{"fx1filterCutoff",850},{"fx1filterResonance",.08f}}},
    {"copper-bass", "02 Copper Bass", "Bass", "Firm saw/sub bass with a darker driven edge. Play C1-C3.", "oscillator,sub,fx0,fx1", 36, false,
     {{"waveform",2},{"subLevel",.28f},{"attack",.006f},{"decay",.2f},{"sustain",.55f},{"release",.08f},{"level",.48f},{"voiceMode",1},
      {"fx0type",1},{"fx0amount",.55f},{"fx0driveStrength",.38f},{"fx0driveTone",.4f},{"fx0driveBias",.08f},
      {"fx1type",5},{"fx1amount",1},{"fx1filterCutoff",1600},{"fx1filterResonance",.23f},{"fx1filterSlope",1}}},
    {"laser-lane", "03 Laser Lane", "Lead", "Legato square lead with a short glide and tempo-synced echoes. Play C3-C5.", "oscillator,glide,unison,fx0,fx1", 60, false,
     {{"waveform",1},{"attack",.012f},{"decay",.3f},{"sustain",.7f},{"release",.22f},{"level",.72f},{"voiceMode",1},{"monoLegato",1},
      {"glide",.09f},{"glideAmount",1},{"detune",7},{"unison",1},{"unisonMix",.55f},{"unisonWidth",.55f},
      {"fx0type",5},{"fx0amount",1},{"fx0filterCutoff",4200},{"fx0filterResonance",.12f},
      {"fx1type",3},{"fx1amount",.27f},{"fx1delaySync",1},{"fx1delayDivision",3},{"fx1delayFeedback",.26f},{"fx1delayHighCut",4500},{"fx1delayMode",1}}},
    {"glass-pluck", "04 Glass Pluck", "Pluck", "Fast sine/fifth pluck with dotted eighth-note echoes. Play C3-C6.", "oscillator,harmonizer,fx0,fx1", 60, true,
     {{"attack",.005f},{"decay",.32f},{"sustain",0},{"release",.16f},{"level",.46f},{"harmony",3},{"harmonyMix",.24f},{"harmonyWidth",.4f},
      {"fx0type",3},{"fx0amount",.4f},{"fx0delaySync",1},{"fx0delayDivision",3},{"fx0delayRhythm",1},{"fx0delayFeedback",.32f},{"fx0delayHighCut",6200},{"fx0delayMode",1},
      {"fx1type",4},{"fx1amount",.22f},{"fx1reverbSize",.55f},{"fx1reverbDamping",.45f},{"fx1reverbLowCut",250}}},
    {"velvet-keys", "05 Velvet Keys", "Keys", "Soft saturated sine keys with gentle chorus and stereo tremolo. Play C3-C5.", "oscillator,fx0,fx1,fx2,fx3", 60, true,
     {{"attack",.012f},{"decay",.85f},{"sustain",.26f},{"release",.45f},{"level",.58f},
      {"fx0type",1},{"fx0amount",.27f},{"fx0driveStrength",.24f},{"fx0driveBias",.22f},{"fx0driveTone",.32f},{"fx0driveMode",2},
      {"fx1type",2},{"fx1amount",.2f},{"fx1chorusRate",.55f},{"fx1chorusDepth",.18f},
      {"fx2type",6},{"fx2amount",.4f},{"fx2tremoloRate",3.2f},{"fx2tremoloDepth",.25f},{"fx2tremoloPhase",130},
      {"fx3type",4},{"fx3amount",.18f},{"fx3reverbSize",.48f},{"fx3reverbDamping",.65f},{"fx3reverbLowCut",200}}},
    {"afterglow-pad", "06 Afterglow Pad", "Pad", "Slow, wide saw pad. Hold chords and let the warm room decay. Play C3-C5.", "oscillator,unison,fx0,fx1,fx2", 60, true,
     {{"waveform",2},{"attack",.7f},{"decay",1.4f},{"sustain",.72f},{"release",2.4f},{"level",.52f},
      {"unison",2},{"detune",11},{"unisonMix",.75f},{"unisonWidth",.9f},
      {"fx0type",5},{"fx0amount",1},{"fx0filterCutoff",2400},{"fx0filterResonance",.12f},{"fx0filterSlope",1},
      {"fx1type",2},{"fx1amount",.25f},{"fx1chorusRate",.18f},{"fx1chorusDepth",.3f},
      {"fx2type",4},{"fx2amount",.48f},{"fx2reverbSize",.85f},{"fx2reverbDamping",.64f},{"fx2reverbPreDelay",28},{"fx2reverbLowCut",280}}},
    {"airlock-pad", "07 Airlock Pad", "Pad", "Airy sine/noise pad with slow filter motion and a wide tail. Play C3-C5.", "oscillator,noise,fx0,fx1,fx2,lfo", 60, true,
     {{"attack",1.2f},{"decay",1.8f},{"sustain",.8f},{"release",3.2f},{"level",.38f},
      {"noiseLevel",.065f},{"noiseColour",-.6f},{"noiseCutoff",1800},{"noiseWidth",.85f},
      {"fx0type",5},{"fx0amount",1},{"fx0filterCutoff",3200},{"fx0filterResonance",.2f},
      {"fx1type",7},{"fx1amount",.32f},{"fx1phaserRate",.12f},{"fx1phaserDepth",.35f},{"fx1phaserCentre",1400},{"fx1phaserFeedback",.16f},
      {"fx2type",4},{"fx2amount",.55f},{"fx2reverbSize",.9f},{"fx2reverbDamping",.6f},{"fx2reverbLowCut",300},
      {"lfoTarget",3},{"lfoRate",.13f},{"lfoDepth",.3f},{"lfoFilterRange",1.3f}}},
    {"circuit-pulse", "08 Circuit Pulse", "Rhythm", "Hold a chord for an eighth-note LFO pulse. Follows the DAW tempo.", "oscillator,fx0,fx1,lfo", 60, true,
     {{"waveform",1},{"attack",.018f},{"decay",.3f},{"sustain",.8f},{"release",.28f},{"level",.5f},
      {"fx0type",5},{"fx0amount",1},{"fx0filterCutoff",2800},{"fx0filterResonance",.15f},
      {"fx1type",3},{"fx1amount",.25f},{"fx1delaySync",1},{"fx1delayDivision",3},{"fx1delayRhythm",1},{"fx1delayFeedback",.22f},{"fx1delayHighCut",4000},{"fx1delayMode",1},
      {"lfoTarget",2},{"lfoShape",4},{"lfoDepth",.85f},{"lfoSync",1},{"lfoDivision",3}}},
    {"prism-bell", "09 Prism Bell", "Bell", "Bright sine harmonics with a long decay and floating repeats. Play C4-C6.", "oscillator,harmonizer,fx0,fx1", 72, true,
     {{"attack",.005f},{"decay",1.8f},{"sustain",0},{"release",1.1f},{"level",.36f},
      {"harmony",5},{"harmonyMix",.45f},{"harmonyShift",12},{"harmonyFine",3},{"harmonyWidth",.8f},
      {"fx0type",3},{"fx0amount",.3f},{"fx0delaySync",1},{"fx0delayDivision",2},{"fx0delayFeedback",.28f},{"fx0delayHighCut",7200},{"fx0delayMode",1},
      {"fx1type",4},{"fx1amount",.42f},{"fx1reverbSize",.78f},{"fx1reverbDamping",.3f},{"fx1reverbPreDelay",18},{"fx1reverbLowCut",420}}},
    {"arcade-orbit", "10 Arcade Orbit", "Motion", "Animated band-pass saw texture with phaser and ping-pong repeats. Play C3-C5.", "oscillator,fx0,fx1,fx2,lfo", 60, false,
     {{"waveform",2},{"attack",.08f},{"decay",.7f},{"sustain",.6f},{"release",.85f},{"level",.68f},
      {"fx0type",5},{"fx0amount",1},{"fx0filterMode",2},{"fx0filterCutoff",1500},{"fx0filterResonance",.34f},
      {"fx1type",7},{"fx1amount",.42f},{"fx1phaserRate",.32f},{"fx1phaserDepth",.55f},{"fx1phaserCentre",1100},{"fx1phaserFeedback",.3f},
      {"fx2type",3},{"fx2amount",.48f},{"fx2delaySync",1},{"fx2delayDivision",3},{"fx2delayRhythm",1},{"fx2delayFeedback",.38f},{"fx2delayHighCut",5000},{"fx2delayMode",1},
      {"lfoTarget",3},{"lfoShape",1},{"lfoDepth",.55f},{"lfoFilterRange",1.8f},{"lfoSync",1},{"lfoDivision",0}}}
}};
constexpr int count = 10;
}
