#pragma once
#include <array>

namespace EqSettings {
constexpr int count = 9;
struct Definition { const char *suffix, *name, *label, *unit; float minimum, maximum, initial, skew; };
// Three independent bell bands. Append to the modulation catalog to preserve old route IDs.
inline constexpr std::array<Definition, count> definitions {{
    {"eqLowFrequency", "EQ Low Frequency", "FREQUENCY", "Hz", 20, 20000, 120, 0.25f},
    {"eqLowGain", "EQ Low Gain", "GAIN", "dB", -18, 18, 0, 1},
    {"eqLowQ", "EQ Low Q", "Q / WIDTH", "Q", 0.2f, 8, 0.707f, 0.5f},
    {"eqMidFrequency", "EQ Mid Frequency", "FREQUENCY", "Hz", 20, 20000, 1000, 0.25f},
    {"eqMidGain", "EQ Mid Gain", "GAIN", "dB", -18, 18, 0, 1},
    {"eqMidQ", "EQ Mid Q", "Q / WIDTH", "Q", 0.2f, 8, 0.707f, 0.5f},
    {"eqHighFrequency", "EQ High Frequency", "FREQUENCY", "Hz", 20, 20000, 6000, 0.25f},
    {"eqHighGain", "EQ High Gain", "GAIN", "dB", -18, 18, 0, 1},
    {"eqHighQ", "EQ High Q", "Q / WIDTH", "Q", 0.2f, 8, 0.707f, 0.5f}
}};
}
