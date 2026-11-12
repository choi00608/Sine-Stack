#pragma once
#include <array>

namespace FxDetails {
enum Id {
    outputGain, driveBias, driveMode, chorusDelay, chorusFeedback,
    delayLowCut, delayHighCut, delayMode, reverbPreDelay, reverbWidth, reverbLowCut,
    filterDrive, filterSlope, tremoloShape, tremoloPhase, phaserCentre, phaserFeedback, count
};
struct Definition {
    const char* suffix;
    const char* name;
    const char* label;
    const char* unit;
    const char* tooltip;
    int effect; // 0 applies to all audio FX.
    float minimum, maximum, initial, skew;
    const char* choices; // Pipe-separated; empty for continuous parameters.
};
inline constexpr std::array<Definition, count> definitions {{
    {"outputGain", "Output Gain", "OUTPUT", "dB", "Level after this FX, before the next block. Bypass restores the unchanged input.", 0, -24, 12, 0, 1, ""},
    {"driveBias", "Drive Bias", "BIAS", "ratio (-1 to 1)", "Asymmetric distortion adds even harmonics. Zero is symmetric.", 1, -1, 1, 0, 1, ""},
    {"driveMode", "Drive Mode", "DISTORTION", "", "Soft saturation, hard clipping, or a gentler arctangent curve.", 1, 0, 2, 0, 1, "Soft|Hard|Arc"},
    {"chorusDelay", "Chorus Centre Delay", "CENTRE DELAY", "ms", "Base delay around which the chorus moves. Short values give a tighter, metallic sound.", 2, 1, 40, 12, 0.6f, ""},
    {"chorusFeedback", "Chorus Feedback", "FEEDBACK", "ratio (-1 to 1)", "Feeds chorus output back into its delay. Negative feedback reverses polarity.", 2, -0.9f, 0.9f, 0.1f, 1, ""},
    {"delayLowCut", "Delay Low Cut", "LOW CUT", "Hz", "Removes bass from the echoes and feedback. Minimum is Off.", 3, 20, 2000, 20, 0.3f, ""},
    {"delayHighCut", "Delay High Cut", "HIGH CUT", "Hz", "Darkens the echoes and feedback. Maximum is Off.", 3, 200, 20000, 20000, 0.3f, ""},
    {"delayMode", "Delay Mode", "ROUTING", "", "Stereo keeps channels separate. Ping-pong alternates echoes left and right.", 3, 0, 1, 0, 1, "Stereo|Ping-pong"},
    {"reverbPreDelay", "Reverb Pre-delay", "PRE-DELAY", "ms", "Time before the room responds. The dry sound stays immediate.", 4, 0, 200, 0, 0.6f, ""},
    {"reverbWidth", "Reverb Width", "WIDTH", "ratio (0 to 1)", "Stereo width of the reverb tail: mono at 0%, full width at 100%.", 4, 0, 1, 1, 1, ""},
    {"reverbLowCut", "Reverb Low Cut", "LOW CUT", "Hz", "Removes bass before it enters the room. Minimum is Off; dry sound is unchanged.", 4, 20, 2000, 20, 0.3f, ""},
    {"filterDrive", "Filter Drive", "DRIVE", "dB", "Saturates the input to the filter. Zero preserves the clean input.", 5, 0, 24, 0, 1, ""},
    {"filterSlope", "Filter Slope", "SLOPE", "", "12 dB/oct uses one filter stage; 24 dB/oct cascades two stages.", 5, 0, 1, 0, 1, "12 dB/oct|24 dB/oct"},
    {"tremoloShape", "Tremolo Shape", "SHAPE", "", "Sine, triangle, or a softened square pulse for rhythmic gating.", 6, 0, 2, 0, 1, "Sine|Triangle|Square"},
    {"tremoloPhase", "Tremolo Stereo Phase", "STEREO PHASE", "degrees", "Offsets the right channel's modulation. 180 degrees alternates left and right.", 6, 0, 180, 0, 1, ""},
    {"phaserCentre", "Phaser Centre Frequency", "CENTRE", "Hz", "Centre frequency of the phaser sweep.", 7, 20, 20000, 1000, 0.3f, ""},
    {"phaserFeedback", "Phaser Feedback", "FEEDBACK", "ratio (-1 to 1)", "Emphasises the phaser resonances. Negative values reverse feedback polarity.", 7, -0.9f, 0.9f, 0.25f, 1, ""}
}};
}
