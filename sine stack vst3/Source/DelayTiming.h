#pragma once
#include "LfoTiming.h"
#include <algorithm>
namespace DelayTiming {
constexpr double maxSeconds = 8;
inline double milliseconds(double bpm, int division, int rhythm = 0) {
    return std::min(maxSeconds * 1000, 60000 * LfoTiming::beatsFor(division, rhythm) / bpm);
}
}
