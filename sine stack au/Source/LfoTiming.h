#pragma once
#include <array>
#include <algorithm>
namespace LfoTiming {
// Quarter-note beats per complete LFO cycle. Keep choice order stable for presets.
inline constexpr std::array<double, 7> beats {4, 2, 1, .5, .25, .125, .0625};
inline constexpr const char* divisions = "1/1|1/2|1/4|1/8|1/16|1/32|1/64";
constexpr int defaultDivision = 2;
// A separate modifier keeps the original 0..6 Beat automation range unchanged.
inline constexpr std::array<double, 3> rhythmFactors {1, 1.5, 2.0 / 3.0};
inline constexpr const char* rhythms = "Straight|Dotted|Triplet";
inline double beatsFor(int division, int rhythm) {
    return beats[std::clamp(division, 0, static_cast<int>(beats.size()) - 1)]
        * rhythmFactors[std::clamp(rhythm, 0, static_cast<int>(rhythmFactors.size()) - 1)];
}
}
