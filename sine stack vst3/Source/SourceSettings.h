#pragma once
#include <array>
namespace SourceSettings {
constexpr int poolSize = 32, legacyCount = 7, capacity = poolSize + legacyCount;
enum Id { wave, oscLevel, oscTune, oscFine, oscPan, voices, detune, unisonMix, unisonWidth, unisonPan,
    harmony, harmonyMix, harmonyShift, harmonyFine, harmonyWidth, harmonyPan,
    glide, glideCurve, glideAmount, subLevel, subOctave, subWave, subFine, subPan,
    noiseLevel, noiseColour, noiseCutoff, noiseWidth, noisePan,
    lfoRate, lfoDepth, lfoShape, lfoTarget, lfoPhase, lfoOffset, lfoPitchRange, lfoFilterRange, count };
struct Definition { const char *id, *name, *label, *unit, *choices; int kind; float low, high, initial, skew; bool legacy; };
inline constexpr std::array<Definition, count> definitions {{
    {"waveform","Waveform","WAVEFORM","","Sine|Square|Saw",0,0,2,0,1,true},
    {"oscLevel","Oscillator Level","LEVEL","%","",0,0,1,1,1,false},
    {"oscTune","Oscillator Transpose","TRANSPOSE","st","",0,-24,24,0,1,false},
    {"oscFine","Oscillator Fine","FINE","ct","",0,-100,100,0,1,false},
    {"oscPan","Oscillator Pan","PAN","%","",0,-1,1,0,1,false},
    {"unison","Unison","VOICES","","1 voice|3 voices|5 voices",1,0,2,1,1,true},
    {"detune","Detune","DETUNE","ct","",1,0,50,0,1,true},
    {"unisonMix","Unison Mix","MIX","%","",1,0,1,1,1,false},
    {"unisonWidth","Unison Width","WIDTH","%","",1,0,1,0,1,false},
    {"unisonPan","Unison Pan","PAN","%","",1,-1,1,0,1,false},
    {"harmony","Harmony","INTERVAL","","Off|Minor 3rd|Major 3rd|5th|Minor 3rd + 5th|Major 3rd + 5th",2,0,5,0,1,true},
    {"harmonyMix","Harmony Mix","MIX","%","",2,0,1,0.5f,1,true},
    {"harmonyShift","Harmony Transpose","TRANSPOSE","st","",2,-24,24,0,1,false},
    {"harmonyFine","Harmony Fine","FINE","ct","",2,-50,50,0,1,false},
    {"harmonyWidth","Harmony Width","WIDTH","%","",2,0,1,0,1,false},
    {"harmonyPan","Harmony Pan","PAN","%","",2,-1,1,0,1,false},
    {"glide","Glide","TIME","s","",3,0,2,0,0.4f,true},
    {"glideCurve","Glide Curve","CURVE","x","",3,0.25f,4,1,0.5f,false},
    {"glideAmount","Glide Amount","AMOUNT","%","",3,0,1,1,1,false},
    {"subLevel","Sub Level","LEVEL","%","",4,0,1,0,1,true},
    {"subOctave","Sub Octave","OCTAVE","","-1 octave|-2 octaves|-3 octaves",4,0,2,0,1,false},
    {"subWave","Sub Waveform","WAVEFORM","","Sine|Square|Saw",4,0,2,0,1,false},
    {"subFine","Sub Fine","FINE","ct","",4,-50,50,0,1,false},
    {"subPan","Sub Pan","PAN","%","",4,-1,1,0,1,false},
    {"noiseLevel","Noise Level","LEVEL","%","",5,0,1,0,1,true},
    {"noiseColour","Noise Colour","COLOUR","%","",5,-1,1,0,1,false},
    {"noiseCutoff","Noise Colour Frequency","COLOUR FREQ","Hz","",5,100,16000,1000,0.3f,false},
    {"noiseWidth","Noise Width","WIDTH","%","",5,0,1,0,1,false},
    {"noisePan","Noise Pan","PAN","%","",5,-1,1,0,1,false},
    {"lfoRate","LFO Rate","RATE","Hz","",6,0.05f,20,1,0.4f,true},
    {"lfoDepth","LFO Depth","DEPTH","%","",6,0,1,0.5f,1,true},
    {"lfoShape","LFO Shape","SHAPE","","Sine|Triangle|Saw up|Saw down|Square",6,0,4,0,1,true},
    {"lfoTarget","LFO Target","TARGET","","Off|Pitch|Volume|Filter cutoff|Block parameter",6,0,4,0,1,true},
    {"lfoPhase","LFO Phase","PHASE","deg","",6,0,360,0,1,false},
    {"lfoOffset","LFO Offset","OFFSET","%","",6,-1,1,0,1,false},
    {"lfoPitchRange","LFO Pitch Range","PITCH RANGE","st","",6,0,24,2,1,false},
    {"lfoFilterRange","LFO Filter Range","FILTER RANGE","oct","",6,0,8,4,1,false}
}};
struct Frame { int kind = -1; bool active = false; unsigned generation = 0; std::array<float, count> values {}; };
inline int indexForId(int id) { return id >= 200 ? id - 200 + legacyCount : id - 100; }
inline int idForIndex(int index) { return index < legacyCount ? 100 + index : 200 + index - legacyCount; }
}
