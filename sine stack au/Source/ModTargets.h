#pragma once
#include "FxDetails.h"
#include "EqSettings.h"
#include "SourceSettings.h"
namespace ModTargets {
// Stable preset/automation indices: append new targets, never reorder entries.
struct Definition { const char *key, *name; int kind, sourceIndex; bool source, continuous; };
inline const auto definitions = [] {
    std::array<Definition, 15 + FxDetails::count + SourceSettings::count + EqSettings::count> result {{
        {"amount", "Amount", 0, -1, false, true},
        {"delayTime", "Delay Time", 3, -1, false, true}, {"delayFeedback", "Delay Feedback", 3, -1, false, true},
        {"reverbSize", "Room Size", 4, -1, false, true}, {"reverbDamping", "Damping", 4, -1, false, true},
        {"chorusRate", "Chorus Rate", 2, -1, false, true}, {"chorusDepth", "Chorus Depth", 2, -1, false, true},
        {"driveStrength", "Drive", 1, -1, false, true}, {"driveTone", "Tone", 1, -1, false, true},
        {"filterCutoff", "Cutoff", 5, -1, false, true}, {"filterResonance", "Resonance", 5, -1, false, true},
        {"tremoloRate", "Tremolo Rate", 6, -1, false, true}, {"tremoloDepth", "Tremolo Depth", 6, -1, false, true},
        {"phaserRate", "Phaser Rate", 7, -1, false, true}, {"phaserDepth", "Phaser Depth", 7, -1, false, true}
    }};
    for (int d = 0; d < FxDetails::count; ++d) {
        const auto& f = FxDetails::definitions[d];
        result[15 + d] = {f.suffix, f.name, f.effect, -1, false, *f.choices == '\0'};
    }
    for (int d = 0; d < SourceSettings::count; ++d) {
        const auto& f = SourceSettings::definitions[d];
        result[15 + FxDetails::count + d] = {f.id, f.name, 100 + f.kind, d, true, *f.choices == '\0'};
    }
    for (int d = 0; d < EqSettings::count; ++d) {
        const auto& f = EqSettings::definitions[d];
        result[15 + FxDetails::count + SourceSettings::count + d] = {f.suffix, f.name, 8, -1, false, true};
    }
    return result;
}();
constexpr int count = 15 + FxDetails::count + SourceSettings::count + EqSettings::count;
constexpr int blocks = 32 + SourceSettings::capacity;
inline int blockIndex(int id) { return id < 32 ? id : 32 + SourceSettings::indexForId(id); }
inline bool validId(int id) { return (id >= 0 && id < 32) || (id >= 100 && id <= 106) || (id >= 200 && id < 232); }
}
