#include "Plugin.h"
#include "BuildInfo.h"
#include "EqGraph.h"
#include "DesignTheme.h"
#include "FactoryPresets.h"
#include "PresetCategories.h"
#include <cstdlib>

namespace {
const juce::StringArray extraFxIds {"filterCutoff", "filterResonance", "tremoloRate", "tremoloDepth", "phaserRate", "phaserDepth", "filterMode"};
const juce::StringArray sourceIds {"oscillator", "unison", "harmonizer", "glide", "sub", "noise", "lfo"};
const juce::StringArray sourceNames {"Oscillator", "Unison / Detune", "Harmonizer", "Glide", "Sub Oscillator", "Noise", "LFO"};
const juce::StringArray effectNames {"Empty", "Drive", "Chorus", "Delay", "Reverb", "Filter", "Tremolo", "Phaser", "EQ"};
juce::String chainText(const SineStack::Chain& chain) {
    juce::StringArray ids;
    for (int n = 0; n < chain.size; ++n) ids.add(chain.ids[n] >= 200 ? "synth" + juce::String(chain.ids[n] - 200) : chain.ids[n] >= 100 ? sourceIds[chain.ids[n] - 100] : "fx" + juce::String(chain.ids[n]));
    return ids.joinIntoString(",");
}
std::array<int, 4> decodeFxOrder(int index) {
    index = juce::jlimit(0, 23, index);
    std::array<int, 4> available {0, 1, 2, 3}, result {};
    constexpr int divisors[] {6, 2, 1, 1};
    for (int position = 0; position < 4; ++position) {
        const int selected = index / divisors[position];
        index %= divisors[position];
        result[position] = available[selected];
        for (int i = selected; i < 3 - position; ++i) available[i] = available[i + 1];
    }
    return result;
}
struct Sound final : juce::SynthesiserSound {
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};
#include "SourceVoice.h"
juce::AudioProcessorValueTreeState::ParameterLayout parameters() {
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    auto add = [&](const juce::String& id, const juce::String& name, float lo, float hi, float initial, float skew = 1.0f, int version = 1) {
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, version}, name, juce::NormalisableRange<float>{lo, hi, 0.0f, skew}, initial));
    };
    add("attack", "Attack", 0.005f, 2.0f, 0.015f, 0.35f);
    add("release", "Release", 0.02f, 4.0f, 0.45f, 0.4f);
    add("level", "Output", 0.0f, 1.0f, 0.7f);
    for (int i = 0; i < 4; ++i) {
        auto prefix = "fx" + juce::String(i);
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix + "type", 1}, "FX " + juce::String(i + 1), juce::StringArray{"Empty", "Drive", "Chorus", "Delay", "Reverb", "Filter", "Tremolo", "Phaser", "EQ"}, 0));
        add(prefix + "amount", "FX " + juce::String(i + 1) + " Amount", 0, 1, 0.35f);
    }
    // Append new parameters and use a newer AU version hint to retain existing automation IDs.
    add("decay", "Decay", 0.005f, 4.0f, 0.1f, 0.4f, 2);
    add("sustain", "Sustain", 0.0f, 1.0f, 1.0f, 1.0f, 2);
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"waveform", 3}, "Waveform",
                                                    juce::StringArray{"Sine", "Square", "Saw"}, 0));
    juce::StringArray orders;
    for (int i = 0; i < 24; ++i) {
        const auto order = decodeFxOrder(i);
        orders.add(juce::String(order[0] + 1) + " > " + juce::String(order[1] + 1) + " > "
                   + juce::String(order[2] + 1) + " > " + juce::String(order[3] + 1));
    }
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"fxOrder", 4}, "FX Order", orders, 0));
    for (int i = 0; i < 4; ++i)
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"fx" + juce::String(i) + "bypass", 5},
                                                      "FX " + juce::String(i + 1) + " Bypass", false));
    for (int i = 0; i < 4; ++i) {
        const auto prefix = "fx" + juce::String(i);
        add(prefix + "delayTime", "FX " + juce::String(i + 1) + " Delay Time", 10, 2000, 320, 0.45f, 6);
        add(prefix + "delayFeedback", "FX " + juce::String(i + 1) + " Delay Feedback", 0, 0.95f, 0.3425f, 1, 6);
    }
    for (int i = 0; i < 4; ++i) {
        const auto prefix = "fx" + juce::String(i);
        add(prefix + "reverbSize", "FX " + juce::String(i + 1) + " Reverb Room Size", 0, 1, 0.72f, 1, 7);
        add(prefix + "reverbDamping", "FX " + juce::String(i + 1) + " Reverb Damping", 0, 1, 0.5f, 1, 7);
    }
    for (int i = 0; i < 4; ++i) {
        const auto prefix = "fx" + juce::String(i);
        add(prefix + "chorusRate", "FX " + juce::String(i + 1) + " Chorus Rate", 0.05f, 10, 0.4f, 0.4f, 8);
        add(prefix + "chorusDepth", "FX " + juce::String(i + 1) + " Chorus Depth", 0, 1, 0.245f, 1, 8);
    }
    for (int i = 0; i < 4; ++i) {
        const auto prefix = "fx" + juce::String(i);
        add(prefix + "driveStrength", "FX " + juce::String(i + 1) + " Drive Strength", 0, 1, 0.35f, 1, 9);
        add(prefix + "driveTone", "FX " + juce::String(i + 1) + " Drive Tone", 0, 1, 0.5f, 1, 9);
    }
    add("detune", "Detune", 0, 50, 0, 1, 10);
    add("glide", "Glide", 0, 2, 0, 0.4f, 10);
    add("subLevel", "Sub Level", 0, 1, 0, 1, 10);
    add("noiseLevel", "Noise Level", 0, 1, 0, 1, 10);
    auto choice = [&](const char* id, const char* name, const juce::StringArray& items, int initial = 0) {
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, 11}, name, items, initial));
    };
    choice("unison", "Unison", {"1 voice", "3 voices", "5 voices"}, 1);
    choice("harmony", "Harmony", {"Off", "Minor 3rd", "Major 3rd", "5th", "Minor 3rd + 5th", "Major 3rd + 5th"});
    add("harmonyMix", "Harmony Mix", 0, 1, 0.5f, 1, 11);
    choice("lfoTarget", "LFO Target", {"Off", "Pitch", "Volume", "Filter cutoff", "Block parameter"});
    choice("lfoShape", "LFO Shape", {"Sine", "Triangle", "Saw up", "Saw down", "Square"});
    add("lfoRate", "LFO Rate", 0.05f, 20, 1, 0.4f, 11);
    add("lfoDepth", "LFO Depth", 0, 1, 0.5f, 1, 11);
    for (int i = 0; i < 4; ++i) {
        const auto prefix = "fx" + juce::String(i);
        const auto name = "FX " + juce::String(i + 1) + " ";
        add(prefix + "filterCutoff", name + "Filter Cutoff", 20, 20000, 2000, 0.25f, 11);
        add(prefix + "filterResonance", name + "Filter Resonance", 0, 1, 0.1f, 1, 11);
        add(prefix + "tremoloRate", name + "Tremolo Rate", 0.05f, 20, 4, 0.4f, 11);
        add(prefix + "tremoloDepth", name + "Tremolo Depth", 0, 1, 0.5f, 1, 11);
        add(prefix + "phaserRate", name + "Phaser Rate", 0.05f, 10, 0.4f, 0.4f, 11);
        add(prefix + "phaserDepth", name + "Phaser Depth", 0, 1, 0.5f, 1, 11);
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix + "filterMode", 11}, name + "Filter Mode",
            juce::StringArray{"Low-pass", "High-pass", "Band-pass"}, 0));
    }
    // Keep the original parameter IDs/version hints stable; append the larger pool.
    for (int i = 4; i < SineStack::maxBlocks; ++i) {
        const auto prefix = "fx" + juce::String(i);
        const auto name = "FX " + juce::String(i + 1) + " ";
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix + "type", 12}, name + "Type", effectNames, 0));
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{prefix + "bypass", 12}, name + "Bypass", false));
        add(prefix + "amount", name + "Amount", 0, 1, 0.35f, 1, 12);
        add(prefix + "delayTime", name + "Delay Time", 10, 2000, 320, 0.45f, 12);
        add(prefix + "delayFeedback", name + "Delay Feedback", 0, 0.95f, 0.3425f, 1, 12);
        add(prefix + "reverbSize", name + "Reverb Room Size", 0, 1, 0.72f, 1, 12);
        add(prefix + "reverbDamping", name + "Reverb Damping", 0, 1, 0.5f, 1, 12);
        add(prefix + "chorusRate", name + "Chorus Rate", 0.05f, 10, 0.4f, 0.4f, 12);
        add(prefix + "chorusDepth", name + "Chorus Depth", 0, 1, 0.245f, 1, 12);
        add(prefix + "driveStrength", name + "Drive Strength", 0, 1, 0.35f, 1, 12);
        add(prefix + "driveTone", name + "Drive Tone", 0, 1, 0.5f, 1, 12);
        add(prefix + "filterCutoff", name + "Filter Cutoff", 20, 20000, 2000, 0.25f, 12);
        add(prefix + "filterResonance", name + "Filter Resonance", 0, 1, 0.1f, 1, 12);
        add(prefix + "tremoloRate", name + "Tremolo Rate", 0.05f, 20, 4, 0.4f, 12);
        add(prefix + "tremoloDepth", name + "Tremolo Depth", 0, 1, 0.5f, 1, 12);
        add(prefix + "phaserRate", name + "Phaser Rate", 0.05f, 10, 0.4f, 0.4f, 12);
        add(prefix + "phaserDepth", name + "Phaser Depth", 0, 1, 0.5f, 1, 12);
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix + "filterMode", 12}, name + "Filter Mode", juce::StringArray{"Low-pass", "High-pass", "Band-pass"}, 0));
    }
    for (int i = 0; i < sourceIds.size(); ++i)
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"source" + juce::String(i) + "bypass", 12}, sourceNames[i] + " Bypass", false));
    for (int i = 0; i < SineStack::maxBlocks; ++i) {
        const auto prefix = "fx" + juce::String(i);
        const auto name = "FX " + juce::String(i + 1) + " ";
        for (const auto& detail : FxDetails::definitions) {
            if (*detail.choices != '\0')
                p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix + detail.suffix, 13}, name + detail.name,
                    juce::StringArray::fromTokens(detail.choices, "|", ""), static_cast<int>(detail.initial)));
            else add(prefix + detail.suffix, name + detail.name, detail.minimum, detail.maximum, detail.initial, detail.skew, 13);
        }
    }
    for (int node = -1; node < SourceSettings::poolSize; ++node) {
        const auto prefix = node < 0 ? juce::String() : "synth" + juce::String(node) + "_";
        const auto name = node < 0 ? juce::String() : "Synth " + juce::String(node + 1) + " ";
        if (node >= 0) {
            p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix + "kind", 14}, name + "Kind", sourceNames, 0));
            p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{prefix + "bypass", 14}, name + "Bypass", false));
        }
        for (const auto& d : SourceSettings::definitions) {
            if (node < 0 && d.legacy) continue;
            if (*d.choices != '\0')
                p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix + d.id, 14}, name + d.name,
                    juce::StringArray::fromTokens(d.choices, "|", ""), static_cast<int>(d.initial)));
            else {
                const float interval = juce::String(d.unit) == "st" && d.kind != 6 ? 1.0f : 0.0f;
                p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{prefix + d.id, 14}, name + d.name,
                    juce::NormalisableRange<float>{d.low, d.high, interval, d.skew}, d.initial));
            }
        }
    }
    juce::StringArray targetNames {"None"};
    for (const auto& d : ModTargets::definitions) targetNames.add((d.source ? "Source: " : "FX: ") + juce::String(d.name));
    for (int node = -1; node < SourceSettings::poolSize; ++node) {
        const auto prefix = node < 0 ? juce::String() : "synth" + juce::String(node) + "_";
        const auto name = node < 0 ? juce::String() : "Synth " + juce::String(node + 1) + " ";
        p.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{prefix + "lfoBlock", 15}, name + "LFO Target Block", -1, 231, -1));
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix + "lfoParameter", 15}, name + "LFO Target Parameter", targetNames, 0));
        add(prefix + "lfoRange", name + "LFO Parameter Range", -1, 1, 0.25f, 1, 15);
    }
    for (int i = 0; i < SineStack::maxBlocks; ++i)
        for (const auto& d : EqSettings::definitions)
            add("fx" + juce::String(i) + d.suffix, "FX " + juce::String(i + 1) + " " + d.name,
                d.minimum, d.maximum, d.initial, d.skew, 16);
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"voiceMode", 17}, "Voice Mode",
        juce::StringArray{"Poly (8 voices)", "Mono (Retrigger)"}, 0));
    // Preserve the existing Voice Mode automation range; expose legato separately.
    p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"monoLegato", 18}, "Mono Legato", false));
    p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"pitchLatency", 19}, "Pitch Latency",
        juce::StringArray{"Live (12 ms)", "Detail (48 ms)"}, 0));
    // Append route slots without changing the original route's AU automation IDs.
    for (int node = -1; node < SourceSettings::poolSize; ++node) {
        const auto prefix = node < 0 ? juce::String() : "synth" + juce::String(node) + "_";
        for (int route = 1; route < SineStack::maxLfoRoutes; ++route) {
            const auto key = prefix + SineStack::routeSetting(route, "");
            const auto name = (node < 0 ? juce::String() : "Synth " + juce::String(node + 1) + " ") + "LFO Route " + juce::String(route + 1) + " ";
            p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{key + "enabled", 20}, name + "Enabled", false));
            p.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{key + "lfoBlock", 20}, name + "Block", -1, 231, -1));
            p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{key + "lfoParameter", 20}, name + "Parameter", targetNames, 0));
            add(key + "lfoRange", name + "Depth", -1, 1, 0.25f, 1, 20);
        }
    }
    // Keep timing outside SourceSettings to preserve existing modulation-target indices.
    for (int node = -1; node < SourceSettings::poolSize; ++node) {
        const auto prefix = node < 0 ? juce::String() : "synth" + juce::String(node) + "_";
        const auto name = (node < 0 ? juce::String() : "Synth " + juce::String(node + 1) + " ") + "LFO ";
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix + "lfoSync", 21}, name + "Clock",
            juce::StringArray{"Free (Hz)", "Tempo Sync"}, 0));
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix + "lfoDivision", 21}, name + "Beat",
            juce::StringArray::fromTokens(LfoTiming::divisions, "|", ""), LfoTiming::defaultDivision));
    }
    for (int i = 0; i < SineStack::maxBlocks; ++i) {
        const auto prefix = "fx" + juce::String(i);
        const auto name = "FX " + juce::String(i + 1) + " Delay ";
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix + "delaySync", 22}, name + "Clock",
            juce::StringArray{"Free (ms)", "Tempo Sync"}, 0));
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix + "delayDivision", 22}, name + "Beat",
            juce::StringArray::fromTokens(LfoTiming::divisions, "|", ""), LfoTiming::defaultDivision));
    }
    // New rhythm modifiers preserve all existing Beat indices, ranges and version hints.
    for (int node = -1; node < SourceSettings::poolSize; ++node) {
        const auto prefix = node < 0 ? juce::String() : "synth" + juce::String(node) + "_";
        const auto name = (node < 0 ? juce::String() : "Synth " + juce::String(node + 1) + " ") + "LFO Rhythm";
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{prefix + "lfoRhythm", 23}, name,
            juce::StringArray::fromTokens(LfoTiming::rhythms, "|", ""), 0));
    }
    for (int i = 0; i < SineStack::maxBlocks; ++i)
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"fx" + juce::String(i) + "delayRhythm", 23},
            "FX " + juce::String(i + 1) + " Delay Rhythm", juce::StringArray::fromTokens(LfoTiming::rhythms, "|", ""), 0));
    return p;
}
}

#include "SourceAudioEffect.h"

float SineStack::LfoLane::depth(int route, int sample) const {
    const float distance = slew * static_cast<float>(sample);
    return start[route] + juce::jlimit(-distance, distance, target[route] - start[route]);
}
float SineStack::LfoLane::wave(int sample) const {
    const double position = phase + step * sample + phaseOffset / 360;
    const double cycle = position - std::floor(position);
    const float value = static_cast<float>(shape == 0 ? std::sin(cycle * juce::MathConstants<double>::twoPi)
        : shape == 1 ? 1 - 4 * std::abs(cycle - 0.5) : shape == 2 ? 2 * cycle - 1
        : shape == 3 ? 1 - 2 * cycle : std::tanh(12 * std::sin(cycle * juce::MathConstants<double>::twoPi)));
    return juce::jlimit(-1.0f, 1.0f, value + offset);
}

float SineStack::Modulation::pitchAt(int sample) const {
    float result = wave(sample) * depth(0, sample) * pitchRange;
    for (const auto& lane : extra) if (lane.start[0] != 0 || lane.target[0] != 0)
        result += lane.wave(sample) * lane.depth(0, sample) * lane.pitchRange;
    return juce::jlimit(-24.0f, 24.0f, result);
}
float SineStack::Modulation::volumeAt(int sample) const {
    float result = 1 - depth(1, sample) * (1 + wave(sample)) * .5f;
    for (const auto& lane : extra) if (lane.start[1] != 0 || lane.target[1] != 0)
        result *= 1 - lane.depth(1, sample) * (1 + lane.wave(sample)) * .5f;
    return result;
}
float SineStack::Modulation::filterAt(int sample) const {
    float result = wave(sample) * depth(2, sample) * filterRange;
    for (const auto& lane : extra) if (lane.start[2] != 0 || lane.target[2] != 0) result += lane.wave(sample) * lane.depth(2, sample) * lane.filterRange;
    return juce::jlimit(-8.0f, 8.0f, result);
}

int SineStack::Synth::latestHeld() const {
    int result = -1;
    for (int i = 0; i < static_cast<int>(held.size()); ++i)
        if (held[i].active() && (result < 0 || held[i].order > held[result].order)) result = i;
    return result;
}
void SineStack::Synth::setMono(bool next, bool useLegato) {
    const juce::ScopedLock scopedLock(lock);
    legato = next && useLegato;
    if (mono == next) return;
    mono = next; monoCurrent = -1;
    if (!mono) return;
    const int target = latestHeld();
    juce::SynthesiserVoice* keep = nullptr;
    for (auto* voice : voices) if (voice->isVoiceActive() && !static_cast<Voice*>(voice)->retiring) {
        const bool matches = target < 0 || (voice->getCurrentlyPlayingNote() == target % 128 && voice->isPlayingChannel(target / 128 + 1));
        if (matches && (keep == nullptr || keep->wasStartedBefore(*voice))) keep = voice;
    }
    if (keep == nullptr && target >= 0) { playMono(target); return; }
    for (auto* voice : voices) if (voice != keep) static_cast<Voice*>(voice)->retireForMono();
    monoCurrent = target;
}
void SineStack::Synth::playMono(int index) {
    const int channel = index / 128 + 1, note = index % 128;
    recordPitch(channel, note);
    if (legato && monoCurrent >= 0) {
        for (auto* base : voices) {
            auto* voice = static_cast<Voice*>(base);
            if (!voice->isVoiceActive() || voice->retiring || voice->getCurrentlyPlayingNote() != monoCurrent % 128
                || !voice->isPlayingChannel(monoCurrent / 128 + 1)) continue;
            auto sound = voice->getCurrentlyPlayingSound();
            voice->legatoTransition = true;
            startVoice(voice, sound.get(), channel, note, held[index].velocity);
            monoCurrent = index;

            syncMonoVoiceFlags();
            return;
        }
    }
    for (auto* voice : voices) static_cast<Voice*>(voice)->retireForMono();

    juce::Synthesiser::noteOn(channel, note, held[index].velocity);

    monoCurrent = index;
    syncMonoVoiceFlags();
}
void SineStack::Synth::syncMonoVoiceFlags() {
    if (monoCurrent < 0) return;
    const int index = monoCurrent, channel = index / 128 + 1, note = index % 128;
    for (auto* voice : voices) if (voice->isVoiceActive() && !static_cast<Voice*>(voice)->retiring
        && voice->getCurrentlyPlayingNote() == note && voice->isPlayingChannel(channel)) {
        voice->setKeyDown(held[index].key);
        voice->setSustainPedalDown(sustainDown[channel - 1]);
        voice->setSostenutoPedalDown(held[index].captured);
    }
}
void SineStack::Synth::updateMonoPriority() {
    if (!mono) return;
    const int next = latestHeld();
    if (next == monoCurrent) { syncMonoVoiceFlags(); return; }
    if (next >= 0) playMono(next);
    else {
        monoCurrent = -1;
        for (auto* voice : voices) if (voice->isVoiceActive() && !static_cast<Voice*>(voice)->retiring) {
            voice->setKeyDown(false); voice->setSustainPedalDown(false); voice->setSostenutoPedalDown(false);
            stopVoice(voice, 0, true);
        }
    }
}
void SineStack::Synth::noteOn(int channel, int note, float velocity) {
    if (channel < 1 || channel > 16 || note < 0 || note > 127) return;
    const juce::ScopedLock scopedLock(lock);
    if (velocity <= 0) { noteOff(channel, note, 0, true); return; }
    const int index = (channel - 1) * 128 + note;
    held[index] = {++noteOrder, velocity, true, false, false};
    if (mono) { playMono(index); return; }
    recordPitch(channel, note);

    juce::Synthesiser::noteOn(channel, note, velocity);

}
void SineStack::Synth::noteOff(int channel, int note, float velocity, bool tail) {
    if (channel < 1 || channel > 16 || note < 0 || note > 127) return;
    const juce::ScopedLock scopedLock(lock);
    auto& entry = held[(channel - 1) * 128 + note];
    // An unmatched Note Off must not create a sustain-held note.
    if (entry.active()) { entry.key = false; entry.sustained = tail && sustainDown[channel - 1]; }
    if (!tail) entry = {};
    // Releasing the newest key may return to another held key. Resolve that
    // before sending noteOff, which would otherwise start the ADSR release.
    if (!legato) juce::Synthesiser::noteOff(channel, note, velocity, tail);
    updateMonoPriority();
}
void SineStack::Synth::handleSustainPedal(int channel, bool down) {
    if (channel < 1 || channel > 16) return;
    const juce::ScopedLock scopedLock(lock);
    sustainDown[channel - 1] = down;
    if (!down) for (int note = 0; note < 128; ++note) held[(channel - 1) * 128 + note].sustained = false;
    if (legato) updateMonoPriority();
    juce::Synthesiser::handleSustainPedal(channel, down);
    updateMonoPriority();
}
void SineStack::Synth::handleSostenutoPedal(int channel, bool down) {
    if (channel < 1 || channel > 16) return;
    const juce::ScopedLock scopedLock(lock);
    if (down != sostenutoDown[channel - 1])
        for (int note = 0; note < 128; ++note) {
            auto& entry = held[(channel - 1) * 128 + note]; entry.captured = down && entry.key;
        }
    sostenutoDown[channel - 1] = down;
    if (!mono) juce::Synthesiser::handleSostenutoPedal(channel, down);
    else for (auto* voice : voices) if (voice->isVoiceActive() && voice->isPlayingChannel(channel))
        voice->setSostenutoPedalDown(held[(channel - 1) * 128 + voice->getCurrentlyPlayingNote()].captured);
    updateMonoPriority();
}
void SineStack::Synth::handleMidiEvent(const juce::MidiMessage& message) {
    if (mono && message.isAllSoundOff()) allNotesOff(message.getChannel(), false);
    else juce::Synthesiser::handleMidiEvent(message);
}
void SineStack::Synth::allNotesOff(int channel, bool tail) {
    const juce::ScopedLock scopedLock(lock);
    for (int c = 0; c < 16; ++c) if (channel <= 0 || channel == c + 1) {
        for (int note = 0; note < 128; ++note) held[c * 128 + note] = {};
        sustainDown[c] = sostenutoDown[c] = false;

    }
    if (legato && channel > 0) updateMonoPriority();
    juce::Synthesiser::allNotesOff(channel, tail);
    if (channel <= 0) { monoCurrent = -1; noteOrder = 0; }
    else updateMonoPriority();
}
SineStack::SineStack() : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)), state(*this, nullptr, "SineStack", parameters()) {
    for (int i = 0; i < 8; ++i) synth.addVoice(new Voice(i));
    synth.addSound(new Sound());
    for (int node = 0; node < SourceSettings::capacity; ++node) {
        const auto prefix = node < 7 ? juce::String() : "synth" + juce::String(node - 7) + "_";
        for (int d = 0; d < SourceSettings::count; ++d) sourceValues[node][d] = state.getRawParameterValue(prefix + SourceSettings::definitions[d].id);
        if (node >= 7) { sourceKinds[node - 7] = state.getRawParameterValue(prefix + "kind"); sourceMutes[node - 7] = state.getRawParameterValue(prefix + "bypass"); }
    }
    for (int node = 0; node < SourceSettings::capacity; ++node) {
        const int id = SourceSettings::idForIndex(node);
        lfoSync[node] = state.getRawParameterValue(sourceParameter(id, "lfoSync"));
        lfoDivision[node] = state.getRawParameterValue(sourceParameter(id, "lfoDivision"));
        lfoRhythm[node] = state.getRawParameterValue(sourceParameter(id, "lfoRhythm"));
        for (int r = 0; r < maxLfoRoutes; ++r) {
            auto& route = parameterRoutes[node][r];
            route.block = state.getRawParameterValue(sourceParameter(id, routeSetting(r, "lfoBlock")));
            route.parameter = state.getRawParameterValue(sourceParameter(id, routeSetting(r, "lfoParameter")));
            route.range = state.getRawParameterValue(sourceParameter(id, routeSetting(r, "lfoRange")));
            if (r > 0) route.enabled = state.getRawParameterValue(sourceParameter(id, routeSetting(r, "enabled")));
        }
    }
    for (int index = 0; index < ModTargets::blocks; ++index) {
        const int id = index < maxBlocks ? index : SourceSettings::idForIndex(index - maxBlocks);
        for (int setting = 0; setting < ModTargets::count; ++setting) {
            const auto& definition = ModTargets::definitions[setting];
            if (definition.source != (id >= 100) || !definition.continuous) continue;
            const auto parameterId = modulationParameterId(id, setting + 1);
            targetBindings[index][setting] = {state.getRawParameterValue(parameterId), state.getParameter(parameterId)};
        }
    }
    attack = state.getRawParameterValue("attack"); release = state.getRawParameterValue("release"); level = state.getRawParameterValue("level");
    decay = state.getRawParameterValue("decay"); sustain = state.getRawParameterValue("sustain");
    voiceMode = state.getRawParameterValue("voiceMode");
    monoLegato = state.getRawParameterValue("monoLegato");
    pitchLatency = state.getRawParameterValue("pitchLatency");
    waveform = state.getRawParameterValue("waveform");
    detune = state.getRawParameterValue("detune"); glide = state.getRawParameterValue("glide");
    subLevel = state.getRawParameterValue("subLevel"); noiseLevel = state.getRawParameterValue("noiseLevel");
    unison = state.getRawParameterValue("unison"); harmony = state.getRawParameterValue("harmony"); harmonyMix = state.getRawParameterValue("harmonyMix");
    lfoRate = state.getRawParameterValue("lfoRate"); lfoDepth = state.getRawParameterValue("lfoDepth");
    lfoShape = state.getRawParameterValue("lfoShape"); lfoTarget = state.getRawParameterValue("lfoTarget");
    fxOrder = state.getRawParameterValue("fxOrder");
    for (int i = 0; i < 7; ++i) sourceBypasses[i] = state.getRawParameterValue("source" + juce::String(i) + "bypass");
    for (int i = 0; i < maxBlocks; ++i) {
        types[i] = state.getRawParameterValue("fx" + juce::String(i) + "type");
        amounts[i] = state.getRawParameterValue("fx" + juce::String(i) + "amount");
        bypasses[i] = state.getRawParameterValue("fx" + juce::String(i) + "bypass");
        delayTimes[i] = state.getRawParameterValue("fx" + juce::String(i) + "delayTime");
        delaySync[i] = state.getRawParameterValue("fx" + juce::String(i) + "delaySync");
        delayDivision[i] = state.getRawParameterValue("fx" + juce::String(i) + "delayDivision");
        delayRhythm[i] = state.getRawParameterValue("fx" + juce::String(i) + "delayRhythm");
        for (int d = 0; d < FxDetails::count; ++d)
            detailFx[i][d] = state.getRawParameterValue("fx" + juce::String(i) + FxDetails::definitions[d].suffix);
        for (int d = 0; d < EqSettings::count; ++d)
            eqFx[i][d] = state.getRawParameterValue("fx" + juce::String(i) + EqSettings::definitions[d].suffix);
        delayFeedbacks[i] = state.getRawParameterValue("fx" + juce::String(i) + "delayFeedback");
        reverbSizes[i] = state.getRawParameterValue("fx" + juce::String(i) + "reverbSize");
        reverbDampings[i] = state.getRawParameterValue("fx" + juce::String(i) + "reverbDamping");
        chorusRates[i] = state.getRawParameterValue("fx" + juce::String(i) + "chorusRate");
        chorusDepths[i] = state.getRawParameterValue("fx" + juce::String(i) + "chorusDepth");
        driveStrengths[i] = state.getRawParameterValue("fx" + juce::String(i) + "driveStrength");
        driveTones[i] = state.getRawParameterValue("fx" + juce::String(i) + "driveTone");
        for (int setting = 0; setting < 7; ++setting)
            extraFx[i][setting] = state.getRawParameterValue("fx" + juce::String(i) + extraFxIds[setting]);
    }
}
std::array<int, 4> SineStack::getFxOrder() const { return decodeFxOrder(static_cast<int>(fxOrder->load())); }
SineStack::Chain SineStack::getChain() const {
    Chain result;
    { const juce::SpinLock::ScopedLockType lock(chainLock); result = chain; }
    if (result.explicitOrder) return result;
    result.ids[result.size++] = oscillatorBlock;
    if (detune->load() > 0 || unison->load() != 1) result.ids[result.size++] = unisonBlock;
    if (harmony->load() > 0) result.ids[result.size++] = harmonyBlock;
    if (glide->load() > 0) result.ids[result.size++] = glideBlock;
    if (subLevel->load() > 0) result.ids[result.size++] = subBlock;
    if (noiseLevel->load() > 0) result.ids[result.size++] = noiseBlock;
    if (lfoTarget->load() > 0) result.ids[result.size++] = lfoBlock;
    for (int id : getFxOrder()) if (types[id]->load() > 0) result.ids[result.size++] = id;
    return result;
}
SineStack::Chain SineStack::getAudioChain() const {
    const auto all = getChain(); Chain result; result.explicitOrder = all.explicitOrder;
    for (int n = 0; n < all.size; ++n) if (blockKind(all.ids[n]) != lfoBlock) result.ids[result.size++] = all.ids[n];
    return result;
}
SineStack::Chain SineStack::getModulators() const {
    const auto all = getChain(); Chain result; result.explicitOrder = all.explicitOrder;
    for (int n = 0; n < all.size; ++n) if (blockKind(all.ids[n]) == lfoBlock) result.ids[result.size++] = all.ids[n];
    return result;
}
void SineStack::moveAudioBlock(int from, int to) {
    auto all = getChain();
    std::array<int, maxBlocks> positions {}; int count = 0;
    for (int n = 0; n < all.size; ++n) if (blockKind(all.ids[n]) != lfoBlock) positions[count++] = n;
    if (from < 0 || to < 0 || from >= count || to >= count || from == to) return;
    const int moved = all.ids[positions[from]], direction = from < to ? 1 : -1;
    for (int n = from; n != to; n += direction) all.ids[positions[n]] = all.ids[positions[n + direction]];
    all.ids[positions[to]] = moved; storeChain(all);
}
bool SineStack::decodeChain(const juce::String& text, Chain& result) {
    result = {}; result.explicitOrder = true;
    if (text.isEmpty()) return true;
    const auto tokens = juce::StringArray::fromTokens(text, ",", "");
    if (tokens.isEmpty() || tokens.size() > maxBlocks) return false;
    for (const auto& token : tokens) {
        int id = sourceIds.indexOf(token);
        if (id >= 0) id += 100;
        else {
            const bool source = token.startsWith("synth");
            const auto prefix = source ? juce::String("synth") : juce::String("fx");
            const auto number = token.substring(prefix.length());
            if (!token.startsWith(prefix) || number.isEmpty() || !number.containsOnly("0123456789")) return false;
            id = number.getIntValue();
            if (id < 0 || id >= maxBlocks || token != prefix + juce::String(id)) return false;
            if (source) id += 200;
        }
        for (int n = 0; n < result.size; ++n) if (result.ids[n] == id) return false;
        result.ids[result.size++] = id;
    }
    return true;
}
void SineStack::storeChain(const Chain& next) {
    auto updated = next; updated.explicitOrder = true;
    std::fill(updated.ids.begin() + updated.size, updated.ids.end(), 0);
    state.state.setProperty("chain", chainText(updated), nullptr);
    { const juce::SpinLock::ScopedLockType lock(chainLock); chain = updated; }
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true)); sendChangeMessage();
}
void SineStack::materialiseChain() { const auto current = getChain(); if (!current.explicitOrder) storeChain(current); }
int SineStack::addBlock(int kind) {
    auto next = getChain();
    if (next.size >= maxBlocks || !((kind >= 1 && kind <= 8) || (kind >= oscillatorBlock && kind <= lfoBlock))) return -1;
    auto set = [this](const juce::String& id, float value) {
        auto* parameter = state.getParameter(id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    int id = kind;
    if (kind < 100) {
        for (id = 0; id < maxBlocks; ++id) {
            bool used = false;
            for (int n = 0; n < next.size; ++n) used = used || next.ids[n] == id;
            if (!used) break;
        }
        if (id >= maxBlocks) return -1;
        const auto prefix = "fx" + juce::String(id);
        for (auto* parameter : getParameters()) {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            if (ranged->paramID.startsWith(prefix) && !juce::CharacterFunctions::isDigit(ranged->paramID[prefix.length()]))
                parameter->setValueNotifyingHost(parameter->getDefaultValue());
        }
        set(prefix + "type", static_cast<float>(kind));
        if (kind == 8) set(prefix + "amount", 1);
        resetSlots[id].store(true);
    } else {
        bool exists = false;
        for (int n = 0; n < next.size; ++n) exists = exists || next.ids[n] == kind;
        if (exists) {
            for (id = 200; id < 200 + SourceSettings::poolSize; ++id) {
                bool used = false; for (int n = 0; n < next.size; ++n) used = used || next.ids[n] == id;
                if (!used) break;
            }
        }
        if (id >= 200) {
            const auto prefix = "synth" + juce::String(id - 200) + "_";
            for (auto* parameter : getParameters()) if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                if (ranged->paramID.startsWith(prefix)) ranged->setValueNotifyingHost(ranged->getDefaultValue());
            set(prefix + "kind", static_cast<float>(kind - 100));
        } else {
            for (const auto& definition : SourceSettings::definitions) if (definition.kind == kind - 100)
                set(definition.id, definition.initial);
        }
        if (id < 200 && kind == lfoBlock) {
            set("lfoSync", 0); set("lfoDivision", LfoTiming::defaultDivision); set("lfoRhythm", 0);
            for (int r = 0; r < maxLfoRoutes; ++r) {
                set(routeSetting(r, "lfoBlock"), -1); set(routeSetting(r, "lfoParameter"), 0); set(routeSetting(r, "lfoRange"), 0.25f);
                if (r > 0) set(routeSetting(r, "enabled"), 0);
            }
        }
        set(blockBypassParameter(id), 0);
        if (id >= 200) {
            if (kind == unisonBlock) set(sourceParameter(id, "detune"), 10);
            if (kind == harmonyBlock) set(sourceParameter(id, "harmony"), 3);
            if (kind == glideBlock) set(sourceParameter(id, "glide"), 0.12f);
            if (kind == subBlock) set(sourceParameter(id, "subLevel"), 0.3f);
            if (kind == noiseBlock) set(sourceParameter(id, "noiseLevel"), 0.1f);
            if (kind == lfoBlock) set(sourceParameter(id, "lfoTarget"), 4);
        } else {
        if (kind == unisonBlock && detune->load() == 0) { set("unison", 1); set("detune", 10); }
        if (kind == harmonyBlock && harmony->load() == 0) set("harmony", 3);
        if (kind == glideBlock && glide->load() == 0) set("glide", 0.12f);
        if (kind == subBlock && subLevel->load() == 0) set("subLevel", 0.3f);
        if (kind == noiseBlock && noiseLevel->load() == 0) set("noiseLevel", 0.1f);
        if (kind == lfoBlock && lfoTarget->load() == 0) set("lfoTarget", 4);
        }
        sourceGenerations[SourceSettings::indexForId(id)].fetch_add(1);
    }
    next.ids[next.size++] = id; storeChain(next); return id;
}
int SineStack::duplicateBlock(int id) {
    auto next = getChain();
    if (id >= 100) {
        int position = -1;
        for (int n = 0; n < next.size; ++n) if (next.ids[n] == id) position = n;
        if (position < 0 || next.size >= maxBlocks) return -1;
        // Populate a distinct pool slot before publishing the duplicate.
        int copy = 200;
        for (; copy < 200 + SourceSettings::poolSize; ++copy) {
            bool used = false; for (int n = 0; n < next.size; ++n) used = used || next.ids[n] == copy;
            if (!used) break;
        }
        if (copy == 200 + SourceSettings::poolSize) return -1;
        auto* kind = state.getParameter("synth" + juce::String(copy - 200) + "_kind");
        kind->setValueNotifyingHost(kind->convertTo0to1(static_cast<float>(blockKind(id) - 100)));
        for (const auto& definition : SourceSettings::definitions) {
            auto* from = state.getParameter(sourceParameter(id, definition.id));
            auto* to = state.getParameter(sourceParameter(copy, definition.id));
            to->setValueNotifyingHost(from->getValue());
        }
        for (int r = 0; r < maxLfoRoutes; ++r)
            for (const auto* setting : {"lfoBlock", "lfoParameter", "lfoRange", "enabled"}) {
                if (r == 0 && juce::String(setting) == "enabled") continue;
                const auto key = routeSetting(r, setting);
                state.getParameter(sourceParameter(copy, key))->setValueNotifyingHost(state.getParameter(sourceParameter(id, key))->getValue());
            }
        for (const auto* key : {"lfoSync", "lfoDivision", "lfoRhythm"})
            state.getParameter(sourceParameter(copy, key))->setValueNotifyingHost(state.getParameter(sourceParameter(id, key))->getValue());
        state.getParameter(blockBypassParameter(copy))->setValueNotifyingHost(state.getParameter(blockBypassParameter(id))->getValue());
        sourceGenerations[SourceSettings::indexForId(copy)].fetch_add(1);
        for (int n = next.size; n > position + 1; --n) next.ids[n] = next.ids[n - 1];
        next.ids[position + 1] = copy; ++next.size; storeChain(next); return copy;
    }
    if (id < 0 || id >= maxBlocks || next.size >= maxBlocks) return -1;
    int position = -1;
    std::array<bool, maxBlocks> used {};
    for (int n = 0; n < next.size; ++n) {
        if (next.ids[n] == id) position = n;
        if (next.ids[n] < maxBlocks) used[next.ids[n]] = true;
    }
    if (position < 0) return -1;
    int copy = 0;
    while (copy < maxBlocks && used[copy]) ++copy;
    if (copy == maxBlocks) return -1;
    const auto sourcePrefix = "fx" + juce::String(id);
    const auto copyPrefix = "fx" + juce::String(copy);
    // Populate an unused slot before publishing it to the audio chain. Copy all
    // settings, including bypass and details for other effect types, but no DSP tails.
    for (auto* parameter : getParameters()) {
        const auto* source = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        if (!source->paramID.startsWith(sourcePrefix)
            || juce::CharacterFunctions::isDigit(source->paramID[sourcePrefix.length()])) continue;
        if (auto* destination = state.getParameter(copyPrefix + source->paramID.substring(sourcePrefix.length())))
            destination->setValueNotifyingHost(source->getValue());
    }
    resetSlots[copy].store(true);
    for (int n = next.size; n > position + 1; --n) next.ids[n] = next.ids[n - 1];
    next.ids[position + 1] = copy; ++next.size;
    storeChain(next);
    return copy;
}
void SineStack::removeBlock(int id) {
    auto next = getChain();
    for (int n = 0; n < next.size; ++n) if (next.ids[n] == id) {
        for (int j = n; j + 1 < next.size; ++j) next.ids[j] = next.ids[j + 1];
        --next.size;
        // A recycled slot must never inherit an incoming LFO connection.
        for (int node = 6; node < SourceSettings::capacity; ++node) {
            for (int r = 0; r < maxLfoRoutes; ++r) {
                auto& route = parameterRoutes[node][r];
                if (static_cast<int>(route.block->load()) != id) continue;
                auto* target = state.getParameter(sourceParameter(SourceSettings::idForIndex(node), routeSetting(r, "lfoBlock")));
                target->setValueNotifyingHost(target->convertTo0to1(-1));
            }
        }
        storeChain(next); return;
    }
}
void SineStack::moveBlock(int from, int to) {
    auto next = getChain();
    if (from < 0 || to < 0 || from >= next.size || to >= next.size || from == to) return;
    const int moved = next.ids[from], direction = from < to ? 1 : -1;
    for (int n = from; n != to; n += direction) next.ids[n] = next.ids[n + direction];
    next.ids[to] = moved; storeChain(next);
}
juce::String SineStack::blockName(int id) const {
    if (id >= 100) return sourceNames[blockKind(id) - 100];
    return id >= 0 && id < maxBlocks ? effectNames[juce::jlimit(0, 8, static_cast<int>(types[id]->load()))] : "Select a block";
}
juce::String SineStack::blockBypassParameter(int id) const {
    if (id >= 200) return "synth" + juce::String(id - 200) + "_bypass";
    return id >= 100 ? "source" + juce::String(id - 100) + "bypass" : "fx" + juce::String(id) + "bypass";
}
int SineStack::blockKind(int id) const {
    if (id >= 200 && id < 200 + SourceSettings::poolSize) return 100 + juce::jlimit(0, 6, static_cast<int>(sourceKinds[id - 200]->load()));
    return id;
}
juce::String SineStack::sourceParameter(int id, const juce::String& setting) const {
    return id >= 200 ? "synth" + juce::String(id - 200) + "_" + setting : setting;
}
bool SineStack::canModulate(int blockId, int parameterIndex) const {
    if (!ModTargets::validId(blockId) || parameterIndex <= 0 || parameterIndex > ModTargets::count) return false;
    const auto& definition = ModTargets::definitions[parameterIndex - 1];
    if (!definition.continuous || definition.source != (blockId >= 100)) return false;
    const int kind = blockId < 100 ? static_cast<int>(types[blockId]->load()) : blockKind(blockId);
    return kind != 0 && (definition.kind == 0 || definition.kind == kind);
}
int SineStack::modulationParameterIndex(const juce::String& setting, bool source) const {
    for (int i = 0; i < ModTargets::count; ++i)
        if (ModTargets::definitions[i].source == source && setting == ModTargets::definitions[i].key) return i + 1;
    return 0;
}
juce::String SineStack::modulationParameterId(int blockId, int parameterIndex) const {
    if (!ModTargets::validId(blockId) || parameterIndex <= 0 || parameterIndex > ModTargets::count) return {};
    const auto& definition = ModTargets::definitions[parameterIndex - 1];
    return blockId >= 100 ? sourceParameter(blockId, definition.key) : "fx" + juce::String(blockId) + definition.key;
}
double SineStack::getTailLengthSeconds() const {
    double tail = release->load();
    const auto current = getChain();
    auto maximumIfRouted = [&](int block, const char* key, double base, double maximum) {
        const int setting = modulationParameterIndex(key, false);
        for (int n = 0; n < current.size; ++n) {
            const int id = current.ids[n];
            if (id < 100 || blockKind(id) != lfoBlock) continue;
            const int node = SourceSettings::indexForId(id);
            if ((node < 7 ? sourceBypasses[node]->load() : sourceMutes[node - 7]->load()) >= 0.5f) continue;
            for (const auto& route : parameterRoutes[node]) {
                const bool enabled = route.enabled ? route.enabled->load() >= 0.5f : static_cast<int>(sourceValues[node][SourceSettings::lfoTarget]->load()) == 4;
                if (enabled && static_cast<int>(route.block->load()) == block && static_cast<int>(route.parameter->load()) == setting
                    && route.range->load() != 0) return maximum;
            }
        }
        return base;
    };
    for (int n = 0; n < current.size; ++n) {
        const int i = current.ids[n];
        if (i >= 100) {
            const int kind = blockKind(i);
            if (kind == unisonBlock || kind == harmonyBlock || kind == glideBlock)
                tail += .15; // Spectral analysis/synthesis tail, including bypass fades.
            continue;
        }
        if (bypasses[i]->load() >= 0.5f) continue;
        const int type = static_cast<int>(types[i]->load());
        if (type == 8) tail += 2.0; // High-Q bass bands can ring after the envelope ends.
        // Allow the longest room setting to decay when the host renders tails.
        if (type == 4) tail += maximumIfRouted(i, "reverbPreDelay", detailFx[i][FxDetails::reverbPreDelay]->load(), 200) * 0.001 + juce::jmax(12.0, 2.0 + 0.06 * std::ceil(std::log(0.001) / std::log(0.7 + 0.28 * maximumIfRouted(i, "reverbSize", reverbSizes[i]->load(), 1))));
        if (type == 2) {
            const double feedback = juce::jlimit(0.0, 0.9, maximumIfRouted(i, "chorusFeedback", std::abs(static_cast<double>(detailFx[i][FxDetails::chorusFeedback]->load())), 0.9));
            const double repeats = feedback > 0 ? std::ceil(std::log(0.001) / std::log(feedback)) : 0;
            tail += juce::jmax(0.1, (maximumIfRouted(i, "chorusDelay", detailFx[i][FxDetails::chorusDelay]->load(), 40) + 10 * maximumIfRouted(i, "chorusDepth", chorusDepths[i]->load(), 1)) * 0.001 * (1 + repeats));
        }
        if (type == 5) tail += 2.0;
        if (type == 7) {
            const double feedback = juce::jlimit(0.0, 0.9, maximumIfRouted(i, "phaserFeedback", std::abs(static_cast<double>(detailFx[i][FxDetails::phaserFeedback]->load())), 0.9));
            const double repeats = feedback > 0 ? std::ceil(std::log(0.001) / std::log(feedback)) : 0;
            tail += juce::jmax(1.0, 6 / (juce::MathConstants<double>::twoPi * 20) * (1 + repeats));
        }
        if (type == 3) {
            const double feedback = juce::jlimit(0.0, 0.95, maximumIfRouted(i, "delayFeedback", delayFeedbacks[i]->load(), 0.95));
            const double time = delaySync[i]->load() >= .5f
                ? DelayTiming::milliseconds(getTempoBpm(), static_cast<int>(delayDivision[i]->load()), static_cast<int>(delayRhythm[i]->load()))
                : maximumIfRouted(i, "delayTime", delayTimes[i]->load(), 2000);
            tail += time * 0.001 * (1 + (feedback > 0 ? std::ceil(std::log(0.001) / std::log(feedback)) : 0));
        }
    }
    return tail;
}
void SineStack::moveFx(int from, int to) {
    if (from < 0 || from >= 4 || to < 0 || to >= 4 || from == to) return;
    auto order = getFxOrder();
    const int moved = order[from];
    const int direction = from < to ? 1 : -1;
    for (int i = from; i != to; i += direction) order[i] = order[i + direction];
    order[to] = moved;
    for (int index = 0; index < 24; ++index) if (decodeFxOrder(index) == order) {
        auto* parameter = state.getParameter("fxOrder");
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(index)));
        parameter->endChangeGesture();
        break;
    }
}
void SineStack::Slot::prepare(double sr, int block, bool bypassed) {
    sampleRate = sr;
    chorus.setDepth(0.25f); chorus.setMix(0.5f);
    chorus.prepare({sr, static_cast<juce::uint32>(block), 2});
    filter.prepare({sr, static_cast<juce::uint32>(juce::jmax(1, block)), 2}); filter.reset();
    secondFilter.prepare({sr, static_cast<juce::uint32>(juce::jmax(1, block)), 2}); secondFilter.reset();
    for (auto& smoother : eqSmoothers) smoother.reset(sr, 0.03);
    for (auto& smoother : detailSmoothers) smoother.reset(sr, 0.03);
    for (auto& smoother : driveModes) smoother.reset(sr, 0.01);
    for (auto& smoother : tremoloShapes) smoother.reset(sr, 0.01);
    preDelay.setSize(2, static_cast<int>(std::ceil(sr * 0.2)) + 2); preDelay.clear(); preWrite = 0;
    roomDry.reset(sr, 0.01);
    roomInput.setSize(2, juce::jmax(1, block));
    phaser.prepare({sr, static_cast<juce::uint32>(juce::jmax(1, block)), 2});
    phaser.setCentreFrequency(1000); phaser.setFeedback(0.25f); phaser.reset();
    for (auto* smoother : {&cutoff, &resonance, &tremoloRate, &tremoloDepth}) smoother->reset(sr, 0.03);
    tremoloPhase = 0;
    chorus.setRate(0.4f); chorus.setCentreDelay(12); chorus.setFeedback(0.1f); chorus.reset();
    reverb.setParameters(juce::Reverb::Parameters{});
    reverb.setSampleRate(sr); reverb.reset();
    roomDry.setCurrentAndTargetValue(reverb.getParameters().dryLevel * 2);
    delay.setSize(2, static_cast<int>(std::ceil(sr * DelayTiming::maxSeconds)) + 2); delay.clear();
    delaySamples.reset(sr, 0.05); feedback.reset(sr, 0.03);
    driveStrength.reset(sr, 0.03); driveTone.reset(sr, 0.03); toneLow.fill(0);
    toneCoefficient = static_cast<float>(1 - std::exp(-juce::MathConstants<double>::twoPi * juce::jmin(1000.0, sr * 0.45) / sr));
    write = 0; previousType = -1; amount.reset(sr, 0.03);
    dry.setSize(2, juce::jmax(1, block));
    wetGain.reset(sr, 0.01);
    wetGain.setCurrentAndTargetValue(bypassed ? 0.0f : 1.0f);
}
void SineStack::Slot::process(juce::AudioBuffer<float>& b, int type, float value, bool bypassed, float timeMs, float feedbackValue, float roomSize, float damping, float rate, float depth, float strength, float tone) {
    wetGain.setTargetValue(bypassed ? 0.0f : 1.0f);
    // Keep each effect's history running, but crossfade its output to the exact dry
    // signal. Scratch memory is allocated in prepare(), never on the audio thread.
    for (int offset = 0; offset < b.getNumSamples();) {
        const int count = juce::jmin(dry.getNumSamples(), b.getNumSamples() - offset);
        juce::AudioBuffer<float> chunk(b.getArrayOfWritePointers(), 2, offset, count);
        for (int c = 0; c < 2; ++c) dry.copyFrom(c, 0, chunk, c, 0, count);
        processEffect(chunk, type, value, timeMs, feedbackValue, roomSize, damping, rate, depth, strength, tone, offset);
        for (int n = 0; n < count; ++n) {
            const float mix = wetGain.getNextValue();
            const float output = juce::Decibels::decibelsToGain(detailSmoothers[FxDetails::outputGain].getNextValue());
            for (int c = 0; c < 2; ++c) {
                const float unprocessed = dry.getSample(c, n);
                if (type != 0) chunk.setSample(c, n, chunk.getSample(c, n) * output);
                if (mix == 0) chunk.setSample(c, n, unprocessed);
                else if (mix != 1) chunk.setSample(c, n, unprocessed + mix * (chunk.getSample(c, n) - unprocessed));
            }
        }
        offset += count;
    }
}
void SineStack::Slot::processEffect(juce::AudioBuffer<float>& b, int type, float value, float timeMs, float feedbackValue, float roomSize, float damping, float rate, float depth, float strength, float tone, int offset) {
    using namespace FxDetails;
    for (int d = 0; d < EqSettings::count; ++d) {
        if (previousType != type) eqSmoothers[d].setCurrentAndTargetValue(eqValues[d]);
        else eqSmoothers[d].setTargetValue(eqValues[d]);
    }
    for (int d = 0; d < FxDetails::count; ++d) {
        if (previousType != type) detailSmoothers[d].setCurrentAndTargetValue(details[d]);
        else detailSmoothers[d].setTargetValue(details[d]);
    }
    for (int mode = 0; mode < 3; ++mode) {
        const float drive = static_cast<int>(details[driveMode]) == mode ? 1.0f : 0.0f;
        const float tremolo = static_cast<int>(details[tremoloShape]) == mode ? 1.0f : 0.0f;
        if (previousType != type) {
            driveModes[mode].setCurrentAndTargetValue(drive); tremoloShapes[mode].setCurrentAndTargetValue(tremolo);
        } else { driveModes[mode].setTargetValue(drive); tremoloShapes[mode].setTargetValue(tremolo); }
    }
    const float targetSamples = static_cast<float>(sampleRate * 0.001 * juce::jlimit(static_cast<float>(1000 / sampleRate), static_cast<float>(DelayTiming::maxSeconds * 1000), timeMs));
    feedbackValue = juce::jlimit(0.0f, 0.95f, feedbackValue);
    strength = juce::jlimit(0.0f, 1.0f, strength);
    tone = juce::jlimit(0.0f, 1.0f, tone);
    if (previousType != type) {
        chorus.reset(); reverb.reset(); delay.clear(); write = 0; previousType = type;
        phaser.setRate(extra.phaserRate); phaser.setDepth(extra.phaserDepth); phaser.setMix(value * 0.5f);
        filter.reset(); secondFilter.reset(); phaser.reset(); tremoloPhase = 0;
        preDelay.clear(); preWrite = 0; eqHistory = {};
        echoLow.fill(0); echoHigh.fill(0); roomLow.fill(0); driveDCInput.fill(0); driveDCOutput.fill(0);
        cutoff.setCurrentAndTargetValue(extra.cutoff); resonance.setCurrentAndTargetValue(extra.resonance);
        tremoloRate.setCurrentAndTargetValue(extra.tremoloRate); tremoloDepth.setCurrentAndTargetValue(extra.tremoloDepth);
        amount.setCurrentAndTargetValue(value);
        delaySamples.setCurrentAndTargetValue(targetSamples);
        feedback.setCurrentAndTargetValue(feedbackValue);
        driveStrength.setCurrentAndTargetValue(strength); driveTone.setCurrentAndTargetValue(tone);
        toneLow.fill(0);
    }
    amount.setTargetValue(value);
    delaySamples.setTargetValue(targetSamples); feedback.setTargetValue(feedbackValue);
    driveStrength.setTargetValue(strength); driveTone.setTargetValue(tone);
    if (type == 0) return;
    if (type == 8) {
        // Stack-allocated coefficients and double-precision histories, with 30 ms
        // parameter ramps. No coefficient objects or memory allocation in rendering.
        for (int start = 0; start < b.getNumSamples(); start += 16) {
            const int count = juce::jmin(16, b.getNumSamples() - start);
            std::array<std::array<double, 6>, 3> coefficients;
            for (int band = 0; band < 3; ++band) {
                const double frequency = juce::jlimit(20.0, sampleRate * 0.45, eqSmoothers[band * 3].skip(count));
                const double db = eqSmoothers[band * 3 + 1].skip(count);
                const double q = eqSmoothers[band * 3 + 2].skip(count);
                coefficients[band] = EqResponse::coefficients(sampleRate, frequency, db, q);
            }
            for (int n = start; n < start + count; ++n) {
                const double mix = amount.getNextValue();
                for (int c = 0; c < 2; ++c) {
                    const double input = b.getSample(c, n); double output = input;
                    for (int band = 0; band < 3; ++band) {
                        const auto& a = coefficients[band]; auto& z = eqHistory[band][c];
                        const double filtered = a[0] * output + z[0];
                        z[0] = a[1] * output - a[4] * filtered + z[1];
                        z[1] = a[2] * output - a[5] * filtered;
                        output = filtered;
                    }
                    b.setSample(c, n, static_cast<float>(input + mix * (output - input)));
                }
            }
        }
        return;
    }
    if (type == 7) {
        phaser.setRate(extra.phaserRate); phaser.setDepth(extra.phaserDepth); phaser.setMix(value * 0.5f);
        phaser.setFeedback(details[phaserFeedback]); // JUCE smooths feedback internally.
        juce::dsp::AudioBlock<float> block(b);
        for (size_t start = 0; start < block.getNumSamples();) {
            const size_t count = detailSmoothers[phaserCentre].isSmoothing() ? juce::jmin(size_t(16), block.getNumSamples() - start) : block.getNumSamples() - start;
            phaser.setCentreFrequency(juce::jlimit(20.0f, static_cast<float>(sampleRate * 0.45), detailSmoothers[phaserCentre].skip(static_cast<int>(count))));
            auto part = block.getSubBlock(start, count);
            juce::dsp::ProcessContextReplacing<float> context(part); phaser.process(context);
            start += count;
        }
        return;
    }
    if (type == 5 || type == 6) {
        cutoff.setTargetValue(extra.cutoff); resonance.setTargetValue(extra.resonance);
        tremoloRate.setTargetValue(extra.tremoloRate); tremoloDepth.setTargetValue(extra.tremoloDepth);
        using FilterType = juce::dsp::StateVariableTPTFilterType;
        const auto mode = extra.filterMode == 1 ? FilterType::highpass : extra.filterMode == 2 ? FilterType::bandpass : FilterType::lowpass;
        filter.setType(mode); secondFilter.setType(mode);
        for (int n = 0; n < b.getNumSamples(); ++n) {
            const float mix = amount.getNextValue();
            float drive = 1, slope = 0, depthValue = 0, phaseOffset = 0;
            std::array<float, 3> shapeMix {};
            if (type == 5) {
                const float mod = modulation.filterAt(offset + n);
                const float frequency = juce::jlimit(20.0f, static_cast<float>(sampleRate * 0.45), cutoff.getNextValue() * std::exp2(mod));
                const float q = 0.70710678f + 4.29289322f * resonance.getNextValue();
                filter.setCutoffFrequency(frequency); secondFilter.setCutoffFrequency(frequency);
                filter.setResonance(q); secondFilter.setResonance(q);
                drive = juce::Decibels::decibelsToGain(detailSmoothers[filterDrive].getNextValue());
                slope = detailSmoothers[filterSlope].getNextValue();
            } else {
                depthValue = tremoloDepth.getNextValue();
                phaseOffset = detailSmoothers[FxDetails::tremoloPhase].getNextValue() / 360;
                for (int k = 0; k < 3; ++k) shapeMix[k] = tremoloShapes[k].getNextValue();
            }
            for (int c = 0; c < b.getNumChannels(); ++c) {
                const float drySample = b.getSample(c, n);
                float wet;
                if (type == 5) {
                    // Continuous from clean at 0 dB into soft input saturation.
                    const float colour = juce::jmin(1.0f, drive - 1);
                    const float input = drySample + colour * (std::tanh(drySample * drive) - drySample);
                    const float first = filter.processSample(c, input);
                    const float second = secondFilter.processSample(c, first);
                    wet = first + slope * (second - first);
                } else {
                    const double phase = tremoloPhase + (c == 1 ? phaseOffset : 0);
                    const float sine = static_cast<float>(std::sin(phase * juce::MathConstants<double>::twoPi));
                    // A short smooth edge prevents hard square-wave discontinuities.
                    const float triangle = static_cast<float>(2 / juce::MathConstants<double>::pi * std::asin(sine));
                    const float square = std::tanh(12 * sine) / std::tanh(12.0f);
                    const float wave = shapeMix[0] * sine + shapeMix[1] * triangle + shapeMix[2] * square;
                    wet = drySample * (1 - depthValue * 0.5f * (1 + wave));
                }
                b.setSample(c, n, drySample + mix * (wet - drySample));
            }
            if (type == 6) { tremoloPhase += tremoloRate.getNextValue() / sampleRate; tremoloPhase -= std::floor(tremoloPhase); }
        }
        filter.snapToZero(); secondFilter.snapToZero();
        return;
    }
    if (type == 2) {
        chorus.setRate(juce::jlimit(0.05f, 10.0f, rate));
        chorus.setDepth(juce::jlimit(0.0f, 1.0f, depth)); chorus.setMix(value * 0.65f);
        chorus.setFeedback(details[chorusFeedback]);
        juce::dsp::AudioBlock<float> block(b);
        for (size_t start = 0; start < block.getNumSamples();) {
            const size_t count = detailSmoothers[chorusDelay].isSmoothing() ? juce::jmin(size_t(16), block.getNumSamples() - start) : block.getNumSamples() - start;
            chorus.setCentreDelay(detailSmoothers[chorusDelay].skip(static_cast<int>(count)));
            auto part = block.getSubBlock(start, count);
            juce::dsp::ProcessContextReplacing<float> context(part); chorus.process(context);
            start += count;
        }
        return;
    }
    if (type == 4) {
        juce::Reverb::Parameters p;
        p.roomSize = juce::jlimit(0.0f, 1.0f, roomSize); p.damping = juce::jlimit(0.0f, 1.0f, damping);
        p.wetLevel = value * 0.45f; p.dryLevel = 1 - value * 0.25f; p.width = details[reverbWidth];
        roomDry.setTargetValue(p.dryLevel * 2);
        for (int n = 0; n < b.getNumSamples(); ++n) {
            const float lowCut = detailSmoothers[reverbLowCut].getNextValue();
            const float coefficient = static_cast<float>(1 - std::exp(-juce::MathConstants<double>::twoPi * juce::jmin(lowCut, static_cast<float>(sampleRate * 0.45)) / sampleRate));
            const float delayTime = detailSmoothers[reverbPreDelay].getNextValue() * static_cast<float>(sampleRate * 0.001);
            double read = preWrite - static_cast<double>(delayTime);
            if (read < 0) read += preDelay.getNumSamples();
            const int index = static_cast<int>(read), next = (index + 1) % preDelay.getNumSamples();
            const float fraction = static_cast<float>(read - index);
            for (int c = 0; c < 2; ++c) {
                const float input = b.getSample(c, n);
                roomLow[c] += coefficient * (input - roomLow[c]);
                const float filtered = input - juce::jlimit(0.0f, 1.0f, (lowCut - 20) / 20) * roomLow[c];
                preDelay.setSample(c, preWrite, filtered);
                const float first = preDelay.getSample(c, index);
                const float delayed = first + fraction * (preDelay.getSample(c, next) - first);
                b.setSample(c, n, delayed); roomInput.setSample(c, n, delayed);
            }
            if (++preWrite == preDelay.getNumSamples()) preWrite = 0;
        }
        reverb.setParameters(p); reverb.processStereo(b.getWritePointer(0), b.getWritePointer(1), b.getNumSamples());
        for (int n = 0; n < b.getNumSamples(); ++n) {
            const float dryLevel = roomDry.getNextValue();
            for (int c = 0; c < 2; ++c)
                b.addSample(c, n, (dry.getSample(c, n) - roomInput.getSample(c, n)) * dryLevel);
        }
        return;
    }
    for (int n = 0; n < b.getNumSamples(); ++n) {
        const float a = amount.getNextValue();
        if (type == 1) {
            const float distortionGain = 1 + 24 * driveStrength.getNextValue();
            const float toneBalance = 2 * driveTone.getNextValue() - 1;
            const float bias = detailSmoothers[driveBias].getNextValue();
            std::array<float, 3> modes;
            for (int k = 0; k < 3; ++k) modes[k] = driveModes[k].getNextValue();
            const float dcCoefficient = static_cast<float>(std::exp(-juce::MathConstants<double>::twoPi * 10 / sampleRate));
            auto distort = [&](float input) {
                return modes[0] * std::tanh(input) + modes[1] * juce::jlimit(-1.0f, 1.0f, input)
                    + modes[2] * (2 / juce::MathConstants<float>::pi) * std::atan(input);
            };
            for (int c = 0; c < 2; ++c) {
                const float x = b.getSample(c, n);
                const float driven = (distort(x * distortionGain + bias) - distort(bias)) * 0.5f;
                const float dcBlocked = driven - driveDCInput[c] + dcCoefficient * driveDCOutput[c];
                driveDCInput[c] = driven; driveDCOutput[c] = dcBlocked;
                // Bias needs DC removal; zero bias retains the original transfer exactly.
                const float balanced = driven + juce::jmin(1.0f, std::abs(bias) * 20) * (dcBlocked - driven);
                toneLow[c] += toneCoefficient * (balanced - toneLow[c]);
                const float coloured = balanced + toneBalance * (balanced - toneLow[c]);
                b.setSample(c, n, x + a * (coloured - x));
            }
        } else if (type == 3) {
            const float delayLength = delaySamples.getNextValue(), regeneration = feedback.getNextValue();
            const float lowCut = detailSmoothers[delayLowCut].getNextValue(), highCut = detailSmoothers[delayHighCut].getNextValue();
            const float lowCoefficient = static_cast<float>(1 - std::exp(-juce::MathConstants<double>::twoPi * juce::jmin(lowCut, static_cast<float>(sampleRate * 0.45)) / sampleRate));
            const float highCoefficient = static_cast<float>(1 - std::exp(-juce::MathConstants<double>::twoPi * juce::jmin(highCut, static_cast<float>(sampleRate * 0.45)) / sampleRate));
            const float pingPong = detailSmoothers[delayMode].getNextValue();
            double read = write - static_cast<double>(delayLength);
            if (read < 0) read += delay.getNumSamples();
            const int index = static_cast<int>(read), next = (index + 1) % delay.getNumSamples();
            const float fraction = static_cast<float>(read - index);
            std::array<float, 2> echo, input {b.getSample(0, n), b.getSample(1, n)};
            for (int c = 0; c < 2; ++c) {
                const float first = delay.getSample(c, index);
                const float delayed = first + fraction * (delay.getSample(c, next) - first);
                echoLow[c] += lowCoefficient * (delayed - echoLow[c]);
                const float highPassed = delayed - juce::jlimit(0.0f, 1.0f, (lowCut - 20) / 20) * echoLow[c];
                echoHigh[c] += highCoefficient * (highPassed - echoHigh[c]);
                echo[c] = highPassed + juce::jlimit(0.0f, 1.0f, (20000 - highCut) / 1000) * (echoHigh[c] - highPassed);
            }
            for (int c = 0; c < 2; ++c) {
                const float injection = c == 0 ? (input[0] + input[1]) * 0.5f : 0;
                const float source = input[c] + pingPong * (injection - input[c]);
                const float loop = echo[c] + pingPong * (echo[1 - c] - echo[c]);
                delay.setSample(c, write, source + loop * regeneration);
                b.setSample(c, n, input[c] + echo[c] * a * 0.65f);
            }
            if (++write >= delay.getNumSamples()) write = 0;
        }
    }
}
void SineStack::updateSources(const Chain& current, bool initialise) {
    using namespace SourceSettings;
    std::array<bool, capacity> present {};
    if (!current.explicitOrder) for (int n = 0; n < legacyCount; ++n) present[n] = true;
    for (int n = 0; n < current.size; ++n) if (current.ids[n] >= 100) present[indexForId(current.ids[n])] = true;
    for (int node = 0; node < capacity; ++node) {
        auto& frame = sourceFrames[node];
        const int kind = node < legacyCount ? node : juce::jlimit(0, 6, static_cast<int>(sourceKinds[node - legacyCount]->load()));
        const auto generation = sourceGenerations[node].load();
        const bool reset = initialise || frame.generation != generation || frame.kind != kind;
        frame.kind = kind; frame.generation = generation;
        frame.active = present[node] && (node < legacyCount ? sourceBypasses[node]->load() : sourceMutes[node - legacyCount]->load()) < 0.5f;
        for (int d = 0; d < SourceSettings::count; ++d) {
            const auto& definition = definitions[d];
            const float value = sourceValues[node][d]->load();
            const float tolerance = 4 * std::numeric_limits<float>::epsilon() * (definition.high - definition.low);
            frame.values[d] = !definition.legacy && std::abs(value - definition.initial) <= tolerance ? definition.initial : value;
        }
        if (node == 6 || node >= legacyCount) configureLfo(node, initialise, reset);
    }
}
void SineStack::configureLfo(int node, bool initialise, bool reset) {
    using namespace SourceSettings;
    const auto& frame = sourceFrames[node];
    auto& lane = node == 6 ? static_cast<LfoLane&>(modulation) : modulation.extra[node - legacyCount];
    if (reset) lane = {};
    lane.slew = static_cast<float>(1 / (getSampleRate() * 0.02));
    lane.step = frame.values[SourceSettings::lfoRate] / getSampleRate();
    syncLfoClock(node);
    lane.shape = static_cast<int>(frame.values[SourceSettings::lfoShape]);
    lane.phaseOffset = frame.values[SourceSettings::lfoPhase]; lane.offset = frame.values[SourceSettings::lfoOffset];
    lane.pitchRange = frame.values[lfoPitchRange]; lane.filterRange = frame.values[lfoFilterRange];
    for (int route = 0; route < 3; ++route) {
        lane.target[route] = frame.active && frame.kind == 6 && static_cast<int>(frame.values[SourceSettings::lfoTarget]) == route + 1 ? frame.values[SourceSettings::lfoDepth] : 0;
        if (initialise) lane.start[route] = lane.target[route];
    }
}

void SineStack::syncLfoClock(int node) {
    if (lfoSync[node]->load() < .5f) return;
    auto& lane = node == 6 ? static_cast<LfoLane&>(modulation) : modulation.extra[node - SourceSettings::legacyCount];
    const int division = juce::jlimit(0, static_cast<int>(LfoTiming::beats.size()) - 1, static_cast<int>(lfoDivision[node]->load()));
    const double beats = LfoTiming::beatsFor(division, static_cast<int>(lfoRhythm[node]->load()));
    lane.step = tempoBpm / (60 * beats * getSampleRate());
    if (hostPhaseAvailable) {
        const double phase = controlPpq / beats;
        lane.phase = phase - std::floor(phase);
    }
}

void SineStack::advanceModulation(int samples) {
    auto advance = [samples](LfoLane& lane) {
        lane.phase += lane.step * samples; lane.phase -= std::floor(lane.phase);
        for (int route = 0; route < 3; ++route) lane.start[route] = lane.depth(route, samples);
    };
    advance(modulation); for (auto& lane : modulation.extra) advance(lane);
}
float SineStack::effectiveValue(std::atomic<float>* parameter) const {
    for (int i = 0; i < overrideCount; ++i) if (overrides[i].parameter == parameter) return overrides[i].value;
    return parameter->load();
}
void SineStack::applyParameterRoutes(const Chain& current, int samples) {
    // Read all modulators before applying any destination. This makes LFO-to-LFO
    // connections causal (one control interval). Position has no routing meaning.
    std::array<float, SourceSettings::capacity> waves {}, depths {};
    for (int node = 6; node < SourceSettings::capacity; ++node) {
        // Align the snapshot as well as the rendered lane, including seek/loop boundaries.
        syncLfoClock(node);
        const auto& lane = node == 6 ? static_cast<const LfoLane&>(modulation) : modulation.extra[node - 7];
        waves[node] = lane.wave(samples / 2);
        depths[node] = sourceFrames[node].values[SourceSettings::lfoDepth];
    }
    updateSources(current, false);
    std::array<int, ModTargets::blocks> positions; positions.fill(-1);
    for (int n = 0; n < current.size; ++n) positions[ModTargets::blockIndex(current.ids[n])] = n;
    auto validTarget = [&](int node, int id, int setting) {
        const int origin = positions[ModTargets::blockIndex(SourceSettings::idForIndex(node))];
        return origin >= 0 && id != SourceSettings::idForIndex(node)
            && canModulate(id, setting) && positions[ModTargets::blockIndex(id)] >= 0;
    };
    overrideCount = 0;
    for (int node = 6; node < SourceSettings::capacity; ++node) {
        const auto& frame = sourceFrames[node];
        for (auto& route : parameterRoutes[node]) {
            if (route.generation != frame.generation) {
                route.generation = frame.generation; route.boundBlock = -1; route.boundParameter = 0;
                route.strength.setCurrentAndTargetValue(0);
            }
            const int block = static_cast<int>(route.block->load()), setting = static_cast<int>(route.parameter->load());
            const bool enabled = route.enabled ? route.enabled->load() >= 0.5f : static_cast<int>(frame.values[SourceSettings::lfoTarget]) == 4;
            const bool assigned = frame.kind == 6 && enabled && validTarget(node, block, setting);
            if (assigned && (route.boundBlock != block || route.boundParameter != setting)) {
                route.boundBlock = block; route.boundParameter = setting; route.strength.setCurrentAndTargetValue(0);
            } else if (!assigned && (route.boundBlock != block || route.boundParameter != setting)) {
                route.boundBlock = -1; route.strength.setCurrentAndTargetValue(0); continue;
            }
            if (!validTarget(node, route.boundBlock, route.boundParameter)) {
                route.strength.setCurrentAndTargetValue(0); route.boundBlock = -1; continue;
            }
            route.strength.setTargetValue(assigned && frame.active ? depths[node] * route.range->load() : 0);
            const float strength = 0.5f * (route.strength.getCurrentValue() + route.strength.skip(samples));
            if (strength == 0) continue;
            const auto& binding = targetBindings[ModTargets::blockIndex(route.boundBlock)][route.boundParameter - 1];
            int index = 0;
            while (index < overrideCount && overrides[index].parameter != binding.value) ++index;
            if (index == overrideCount) {
                overrides[index] = {binding.value, 0, 0, route.boundBlock, route.boundParameter}; ++overrideCount;
            }
            overrides[index].delta += waves[node] * strength;
        }
    }
    for (int n = 0; n < overrideCount; ++n) {
        auto& value = overrides[n];
        const auto& binding = targetBindings[ModTargets::blockIndex(value.block)][value.setting - 1];
        const float centre = binding.parameter->convertTo0to1(binding.value->load());
        value.value = binding.parameter->convertFrom0to1(juce::jlimit(0.0f, 1.0f, centre + value.delta));
        const auto& definition = ModTargets::definitions[value.setting - 1];
        if (definition.source) sourceFrames[SourceSettings::indexForId(value.block)].values[definition.sourceIndex] = value.value;
    }
    for (int node = 6; node < SourceSettings::capacity; ++node) configureLfo(node, false, false);
}
void SineStack::prepareToPlay(double sr, int block) {
    synth.setCurrentPlaybackSampleRate(sr);
    synth.allNotesOff(0, false); synth.setMono(voiceMode->load() >= 0.5f, monoLegato->load() >= 0.5f); keyboard.reset();
    modulation = {}; overrideCount = 0;
    tempoBpm = 120; reportedTempo.store(120, std::memory_order_relaxed); controlPpq = 0; hostPhaseAvailable = false;
    for (auto& routes : parameterRoutes) for (auto& route : routes) {
        route.strength.reset(sr, 0.02); route.strength.setCurrentAndTargetValue(0);
        route.boundBlock = -1; route.boundParameter = 0;
    }
    updateSources(getChain(), true);
    const int latencyMode = static_cast<int>(pitchLatency->load());
    for (int node = 0; node < SourceSettings::capacity; ++node) {
        sourceAudio[node].setSize(2, 32); sourceAudio[node].clear();
        sourceEffects[node].prepare(sr, latencyMode); sourceEffects[node].configure(sourceFrames[node], true, latencyMode);
    }
    for (int i = 0; i < synth.getNumVoices(); ++i) {
        auto& voice = *static_cast<Voice*>(synth.getVoice(i));
        voice.sourceAudio = &sourceAudio; voice.modulation = &modulation;
        voice.prepare(sr, static_cast<int>(waveform->load()));
        voice.configure(sourceFrames, true);
    }
    for (int i = 0; i < maxBlocks; ++i) { slots[i].prepare(sr, block, bypasses[i]->load() >= 0.5f); resetSlots[i].store(false); }
    gain.reset(sr, 0.03); gain.setCurrentAndTargetValue(level->load());
}
void SineStack::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    b.clear();
    synth.setMono(voiceMode->load() >= 0.5f, monoLegato->load() >= 0.5f);
    keyboard.processNextMidiBuffer(midi, 0, b.getNumSamples(), true);
    const juce::ADSR::Parameters next {attack->load(), decay->load(), sustain->load(), release->load()};
    const auto currentChain = getChain();
    const int latencyMode = static_cast<int>(pitchLatency->load());
    std::array<bool, maxBlocks> usedSlots {};
    for (int n = 0; n < currentChain.size; ++n) if (currentChain.ids[n] < 100) usedSlots[currentChain.ids[n]] = true;
    for (int n = 0; n < maxBlocks; ++n) if (!usedSlots[n]) slots[n].previousType = -1;
    // Hosts may omit tempo or PPQ. Retain the last valid tempo (120 BPM initially)
    // and free-run when stopped or without a musical position.
    hostPhaseAvailable = false;
    double blockPpq = 0;
    if (auto* playHead = getPlayHead()) if (const auto position = playHead->getPosition()) {
        if (const auto bpm = position->getBpm(); bpm && std::isfinite(*bpm) && *bpm > 0 && *bpm <= 1000) tempoBpm = *bpm;
        if (const auto ppq = position->getPpqPosition(); ppq && std::isfinite(*ppq) && position->getIsPlaying()) {
            blockPpq = *ppq; hostPhaseAvailable = true;
        }
    }
    reportedTempo.store(tempoBpm, std::memory_order_relaxed);
    auto event = midi.begin();
    for (int offset = 0; offset < b.getNumSamples();) {
        auto pending = event;
        while (event != midi.end() && (*event).samplePosition <= offset) ++event;
        int count = juce::jmin(32, b.getNumSamples() - offset);
        if (event != midi.end()) count = juce::jmin(count, (*event).samplePosition - offset);
        controlPpq = blockPpq + offset * tempoBpm / (60 * getSampleRate());
        applyParameterRoutes(currentChain, count);
        modulation.sampleOrigin = 0;
        for (int n = 0; n < currentChain.size; ++n) if (currentChain.ids[n] >= 100) {
            const int node = SourceSettings::indexForId(currentChain.ids[n]);
            sourceEffects[node].configure(sourceFrames[node], false, latencyMode);
        }
        for (int i = 0; i < synth.getNumVoices(); ++i) {
            auto& voice = *static_cast<Voice*>(synth.getVoice(i));
            voice.configure(sourceFrames, false);
            auto& envelope = voice.envelope;
            const auto& current = envelope.getParameters();
            // Reapplying unchanged parameters resets JUCE's release slope after an early note-off.
            if (current.attack != next.attack || current.decay != next.decay
                || current.sustain != next.sustain || current.release != next.release)
                envelope.setParameters(next);
        }
        // Dispatch each event exactly at its sample; newly generated sources share
        // the existing per-note ADSR, mono priority and legato state.
        for (; pending != event; ++pending) {
            const auto message = (*pending).getMessage();
            const auto serial = synth.pitchSerial;
            synth.handleMidiEvent(message);
            for (int n = 0; n < currentChain.size; ++n) if (currentChain.ids[n] >= 100) {
                const int node = SourceSettings::indexForId(currentChain.ids[n]);
                if (sourceFrames[node].kind != 3) continue;
                if (message.isAllNotesOff() || message.isAllSoundOff())
                    sourceEffects[node].previousNote[message.getChannel() - 1] = -1;
                if (serial != synth.pitchSerial)
                    sourceEffects[node].note(synth.pitchChannel, synth.pitchNote, sourceFrames[node]);
            }
        }
        for (auto& source : sourceAudio) source.clear(0, count);
        juce::AudioBuffer<float> chunk(b.getArrayOfWritePointers(), 2, offset, count);
        synth.renderSources(chunk, count);
        // LFOs are independent control sources; every filter sees the same control frame.
        // Start silent. Sources add audio here; every processor receives exactly
        // the stream accumulated to its left. Histories stay with block identity.
        for (int n = 0; n < currentChain.size; ++n) {
            const int i = currentChain.ids[n];
            if (i >= 100) {
                const int node = SourceSettings::indexForId(i), kind = sourceFrames[node].kind;
                if (kind == 0 || kind == 4 || kind == 5) {
                    for (int c = 0; c < 2; ++c) chunk.addFrom(c, 0, sourceAudio[node], c, 0, count);
                } else if (kind != 6) sourceEffects[node].process(chunk);
                continue;
            }
            if (resetSlots[i].exchange(false)) slots[i].previousType = -1;
            slots[i].modulation = modulation;
            for (int d = 0; d < FxDetails::count; ++d) {
                const auto& definition = FxDetails::definitions[d];
                const float value = effectiveValue(detailFx[i][d]);
                // Normalised float round-trips can turn 0 dB into a tiny nonzero
                // gain or 12 ms into 11.999998 ms. Keep neutral defaults exact.
                const float tolerance = 4 * std::numeric_limits<float>::epsilon() * (definition.maximum - definition.minimum);
                slots[i].details[d] = std::abs(value - definition.initial) <= tolerance ? definition.initial : value;
            }
            if (static_cast<int>(types[i]->load()) == 8)
                for (int d = 0; d < EqSettings::count; ++d) {
                    const auto& def = EqSettings::definitions[d];
                    const float value = effectiveValue(eqFx[i][d]);
                    slots[i].eqValues[d] = std::abs(value - def.initial) < 0.00001f ? def.initial : value;
                }
            slots[i].extra = {effectiveValue(extraFx[i][0]), effectiveValue(extraFx[i][1]), effectiveValue(extraFx[i][2]), effectiveValue(extraFx[i][3]),
                              effectiveValue(extraFx[i][4]), effectiveValue(extraFx[i][5]), static_cast<int>(effectiveValue(extraFx[i][6]))};
            const float delayMs = delaySync[i]->load() >= .5f
                ? static_cast<float>(DelayTiming::milliseconds(tempoBpm, static_cast<int>(delayDivision[i]->load()), static_cast<int>(delayRhythm[i]->load())))
                : effectiveValue(delayTimes[i]);
            slots[i].process(chunk, static_cast<int>(types[i]->load()), effectiveValue(amounts[i]), bypasses[i]->load() >= 0.5f,
                             delayMs, effectiveValue(delayFeedbacks[i]), effectiveValue(reverbSizes[i]), effectiveValue(reverbDampings[i]),
                             effectiveValue(chorusRates[i]), effectiveValue(chorusDepths[i]), effectiveValue(driveStrengths[i]), effectiveValue(driveTones[i]));
        }
        advanceModulation(count);
        offset += count;
    }
    gain.setTargetValue(level->load());
    for (int n = 0; n < b.getNumSamples(); ++n) {
        const float g = gain.getNextValue();
        for (int c = 0; c < b.getNumChannels(); ++c) b.setSample(c, n, juce::jlimit(-0.98f, 0.98f, b.getSample(c, n) * g));
    }
}
void SineStack::getStateInformation(juce::MemoryBlock& data) { if (auto xml = state.copyState().createXml()) copyXmlToBinary(*xml, data); }
juce::File SineStack::getPresetFile() const {
    const auto path = state.state.getProperty("presetFile").toString();
    return juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File{};
}
int SineStack::getFactoryPresetIndex() const {
    const auto key = state.state.getProperty("factoryPreset").toString();
    for (int i = 0; i < FactoryPresets::count; ++i) if (key == FactoryPresets::presets[i].key) return i;
    return -1;
}
juce::Result SineStack::loadFactoryPreset(int index) {
    if (index < 0 || index >= FactoryPresets::count) return juce::Result::fail("Unknown factory preset.");
    const auto& preset = FactoryPresets::presets[index];
    juce::ValueTree settings("SineStack");
    settings.setProperty("chain", preset.chain, nullptr);
    settings.setProperty("factoryPreset", preset.key, nullptr);
    settings.setProperty("presetCategory", PresetCategories::canonical(preset.category), nullptr);
    // Start from every current default, never from the previous sound's hidden slots/routes.
    for (auto* parameter : getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        juce::ValueTree entry("PARAM");
        entry.setProperty("id", ranged->paramID, nullptr);
        entry.setProperty("value", ranged->convertFrom0to1(ranged->getDefaultValue()), nullptr);
        settings.appendChild(entry, nullptr);
    }
    for (const auto& value : preset.values) {
        auto* parameter = state.getParameter(value.id);
        if (!parameter) return juce::Result::fail("Unknown factory preset parameter.");
        const auto& range = parameter->getNormalisableRange();
        if (!std::isfinite(value.value) || value.value < range.start || value.value > range.end
            || (range.interval > 0 && std::abs(range.snapToLegalValue(value.value) - value.value) > .0001f))
            return juce::Result::fail("Invalid factory preset parameter.");
        settings.getChildWithProperty("id", value.id).setProperty("value", value.value, nullptr);
    }
    juce::MemoryBlock data;
    copyXmlToBinary(*settings.createXml(), data);
    setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    return juce::Result::ok();
}
juce::String SineStack::getPresetCategory() const {
    if (state.state.hasProperty("presetCategory"))
        return PresetCategories::canonical(state.state.getProperty("presetCategory").toString());
    const int factory = getFactoryPresetIndex();
    return factory >= 0 ? PresetCategories::canonical(FactoryPresets::presets[factory].category) : PresetCategories::names[0];
}
juce::Result SineStack::savePreset(const juce::File& file) { return savePreset(file, getPresetCategory()); }
juce::Result SineStack::savePreset(const juce::File& file, const juce::String& category) {
    auto preset = std::make_unique<juce::DynamicObject>();
    preset->setProperty("format", "SineStackPreset");
    preset->setProperty("category", PresetCategories::canonical(category));
    const auto currentChain = getChain();
    preset->setProperty("formatVersion", currentChain.explicitOrder ? 2 : 1);
    if (currentChain.explicitOrder) {
        juce::Array<juce::var> blocks;
        for (const auto& id : juce::StringArray::fromTokens(chainText(currentChain), ",", "")) blocks.add(id);
        preset->setProperty("chain", blocks);
    }
    juce::Array<juce::var> entries;
    for (const auto& child : state.copyState()) {
        const auto id = child.getProperty("id").toString();
        const auto* parameter = state.getParameter(id);
        if (!parameter) continue;
        auto entry = std::make_unique<juce::DynamicObject>();
        entry->setProperty("id", id);
        entry->setProperty("name", parameter->getName(100));
        entry->setProperty("value", static_cast<double>(child.getProperty("value")));
        juce::String unit = (id == "attack" || id == "decay" || id == "release" || id == "glide") ? "seconds"
            : id == "detune" ? "cents (+/-)"
            : id.endsWith("delayTime") ? "ms" : (id.endsWith("Rate") || id.endsWith("Cutoff")) ? "Hz"
            : dynamic_cast<const juce::AudioParameterBool*>(parameter) ? "0 = on, 1 = bypassed"
            : dynamic_cast<const juce::AudioParameterChoice*>(parameter) ? "zero-based choice index" : "ratio (0 to 1)";
        for (const auto& d : EqSettings::definitions) if (id.endsWith(d.suffix)) unit = d.unit;
        for (const auto& detail : FxDetails::definitions) if (id.endsWith(detail.suffix) && *detail.choices == '\0') unit = detail.unit;
        for (const auto& definition : SourceSettings::definitions)
            if ((id == definition.id || id.endsWith("_" + juce::String(definition.id))) && *definition.choices == '\0')
                unit = juce::String(definition.unit) == "%" ? "ratio" : definition.unit;
        if (id.endsWith("lfoBlock")) unit = "block ID (-1 = unassigned; FX 0..31, sources 100..106, synth 200..231)";
        if (id.endsWith("lfoRange")) unit = "normalized knob range (-1 to 1)";
        entry->setProperty("unit", unit);
        if (auto* choice = dynamic_cast<const juce::AudioParameterChoice*>(parameter)) {
            juce::Array<juce::var> options;
            for (const auto& option : choice->choices) options.add(option);
            entry->setProperty("choices", options);
        }
        entries.add(juce::var(entry.release()));
    }
    preset->setProperty("parameters", entries);
    const auto text = juce::JSON::toString(juce::var(preset.release()), false) + "\n";
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(text) || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Could not save the preset. Check the folder and write permissions.");
    state.state.setProperty("presetCategory", PresetCategories::canonical(category), nullptr);
    state.state.removeProperty("factoryPreset", nullptr);
    state.state.setProperty("presetFile", file.getFullPathName(), nullptr);
    sendChangeMessage();
    return juce::Result::ok();
}
juce::Result SineStack::loadPreset(const juce::File& file) {
    if (!file.existsAsFile() || file.getSize() > 4 * 1024 * 1024)
        return juce::Result::fail("The preset file is missing or too large.");
    juce::var preset;
    if (juce::JSON::parse(file.loadFileAsString(), preset).failed() || !preset.isObject()
        || preset["format"].toString() != "SineStackPreset" || !preset["formatVersion"].isInt()
        || (static_cast<int>(preset["formatVersion"]) != 1 && static_cast<int>(preset["formatVersion"]) != 2) || !preset["parameters"].isArray())
        return juce::Result::fail("This is not a supported Sine Stack preset.");
    auto settings = std::make_unique<juce::XmlElement>("SineStack");
    settings->setAttribute("presetCategory", PresetCategories::canonical(preset["category"].isString() ? preset["category"].toString() : juce::String{}));
    if (static_cast<int>(preset["formatVersion"]) == 2) {
        if (!preset["chain"].isArray()) return juce::Result::fail("The preset has no chain.");
        juce::StringArray ids;
        for (const auto& id : *preset["chain"].getArray()) {
            if (!id.isString() || id.toString().containsChar(',')) return juce::Result::fail("Invalid chain block.");
            ids.add(id.toString());
        }
        Chain decoded;
        const auto text = ids.joinIntoString(",");
        if (!decodeChain(text, decoded)) return juce::Result::fail("The preset contains an invalid chain.");
        settings->setAttribute("chain", text);
    }
    for (const auto& entry : *preset["parameters"].getArray()) {
        const auto value = entry["value"];
        if (!entry.isObject() || !entry["id"].isString() || !(value.isInt() || value.isInt64() || value.isDouble()))
            return juce::Result::fail("Each parameter must have an id and a numeric value.");
        auto* element = settings->createNewChildElement("PARAM");
        element->setAttribute("id", entry["id"].toString());
        element->setAttribute("value", value.toString());
    }
    juce::StringArray seen;
    for (const auto* entry : settings->getChildIterator()) {
        const auto id = entry->getStringAttribute("id");
        const auto* parameter = state.getParameter(id);
        if (!entry->hasTagName("PARAM") || !parameter || seen.contains(id) || !entry->hasAttribute("value"))
            return juce::Result::fail("The preset contains invalid or duplicate settings.");
        const auto text = entry->getStringAttribute("value").trim();
        char* end = nullptr;
        const auto value = std::strtod(text.toRawUTF8(), &end);
        const auto& range = parameter->getNormalisableRange();
        if (text.isEmpty() || end == text.toRawUTF8() || *end != '\0' || !std::isfinite(value)
            || static_cast<float>(value) < range.start || static_cast<float>(value) > range.end
            || (range.interval > 0 && std::abs(range.snapToLegalValue(static_cast<float>(value)) - value) > 0.0001))
            return juce::Result::fail("The preset contains an invalid parameter value.");
        seen.add(id);
    }
    for (const auto* id : {"attack", "release", "level", "fx0type", "fx0amount", "fx1type", "fx1amount", "fx2type", "fx2amount", "fx3type", "fx3amount"})
        if (!seen.contains(id)) return juce::Result::fail("The preset is incomplete.");
    if (settings->hasAttribute("chain")) {
        Chain validated; decodeChain(settings->getStringAttribute("chain"), validated);
        for (int n = 0; n < validated.size; ++n) if (validated.ids[n] < 100) {
            const auto prefix = "fx" + juce::String(validated.ids[n]);
            if (!seen.contains(prefix + "type") || !seen.contains(prefix + "amount"))
                return juce::Result::fail("The preset is missing a chain block's settings.");
        } else if (validated.ids[n] >= 200) {
            const auto prefix = "synth" + juce::String(validated.ids[n] - 200) + "_";
            if (!seen.contains(prefix + "kind")) return juce::Result::fail("The preset is missing a synthesis block's kind.");
            const auto* kindEntry = settings->getChildByAttribute("id", prefix + "kind");
            const int kind = kindEntry->getIntAttribute("value");
            for (const auto& d : SourceSettings::definitions) if (d.kind == kind && !seen.contains(prefix + d.id))
                return juce::Result::fail("The preset is missing a synthesis block's settings.");
        }
    }
    // Validate the entire file before touching the current sound. Existing migration
    // supplies defaults for parameters absent from earlier presets.
    juce::MemoryBlock data;
    copyXmlToBinary(*settings, data);
    setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    state.state.removeProperty("factoryPreset", nullptr);
    state.state.setProperty("presetFile", file.getFullPathName(), nullptr);
    sendChangeMessage();
    return juce::Result::ok();
}
void SineStack::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size)) if (xml->hasTagName(state.state.getType())) {
        auto restored = juce::ValueTree::fromXml(*xml);
        Chain restoredChain;
        if (restored.hasProperty("chain") && !decodeChain(restored.getProperty("chain").toString(), restoredChain)) return;
        for (const auto& id : {"decay", "sustain", "waveform", "fxOrder", "fx0bypass", "fx1bypass", "fx2bypass", "fx3bypass", "detune", "glide", "subLevel", "noiseLevel", "unison", "harmony", "harmonyMix", "lfoTarget", "lfoShape", "lfoRate", "lfoDepth"}) {
            if (!restored.getChildWithProperty("id", id).isValid()) {
                juce::ValueTree parameter("PARAM");
                parameter.setProperty("id", id, nullptr);
                const auto* definition = state.getParameter(id);
                parameter.setProperty("value", definition->convertFrom0to1(definition->getDefaultValue()), nullptr);
                restored.appendChild(parameter, nullptr);
            }
        }
        for (int i = 0; i < 4; ++i) {
            const auto prefix = "fx" + juce::String(i);
            for (const auto& suffix : {"delayTime", "delayFeedback", "reverbSize", "reverbDamping", "chorusRate", "chorusDepth", "driveStrength", "driveTone", "filterCutoff", "filterResonance", "tremoloRate", "tremoloDepth", "phaserRate", "phaserDepth", "filterMode"}) {
                const auto id = prefix + suffix;
                if (restored.getChildWithProperty("id", id).isValid()) continue;
                // Preserve legacy feedback/depth values, previously coupled to Amount.
                const auto oldAmount = restored.getChildWithProperty("id", prefix + "amount");
                const float mix = static_cast<float>(oldAmount.getProperty("value", 0.35f));
                juce::ValueTree parameter("PARAM");
                parameter.setProperty("id", id, nullptr);
                const auto* definition = state.getParameter(id);
                const float initial = juce::String(suffix) == "delayFeedback" ? 0.15f + juce::jlimit(0.0f, 1.0f, mix) * 0.55f
                    : juce::String(suffix) == "chorusDepth" ? juce::jlimit(0.0f, 1.0f, mix) * 0.7f
                    : juce::String(suffix) == "driveStrength" ? juce::jlimit(0.0f, 1.0f, mix)
                    : definition->convertFrom0to1(definition->getDefaultValue());
                parameter.setProperty("value", initial, nullptr);
                restored.appendChild(parameter, nullptr);
            }
        }
        for (auto* parameter : getParameters()) if (parameter->getVersionHint() >= 12) {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            if (!restored.getChildWithProperty("id", ranged->paramID).isValid()) {
                juce::ValueTree entry("PARAM");
                entry.setProperty("id", ranged->paramID, nullptr);
                entry.setProperty("value", ranged->convertFrom0to1(ranged->getDefaultValue()), nullptr);
                restored.appendChild(entry, nullptr);
            }
        }
        state.replaceState(restored);
        { const juce::SpinLock::ScopedLockType lock(chainLock); chain = restoredChain; }
        for (auto& reset : resetSlots) reset.store(true);
        for (auto& generation : sourceGenerations) generation.fetch_add(1);
        sendChangeMessage();
    }
}

namespace {
class EnvelopeGraph final : public juce::Component {
public:
    std::function<void(int, float)> onEdit;
    std::function<void(int, bool)> onGesture;
    void setEnvelope(float a, float d, float s, float r) {
        attack = a; decay = d; sustain = s; release = r;
        repaint();
    }
    void finishDrag() {
        if (dragged < 0) return;
        const int finished = dragged;
        dragged = -1;
        if (onGesture) onGesture(finished, false);
        repaint();
    }
    void mouseDown(const juce::MouseEvent& event) override {
        if (!event.mods.isLeftButtonDown()) return;
        finishDrag();
        const int hit = handleAt(event.position);
        if (hit < 0) return;
        dragScale = scale();
        dragged = hit;
        dragStart = event.position;
        dragValue = std::array<float, 4>{attack, decay, sustain, release}[static_cast<size_t>(hit)];
        if (onGesture) onGesture(hit, true);
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& event) override {
        if (dragged < 0) return;
        const float delta = dragged == 2 ? (dragStart.y - event.position.y) / (getHeight() - 42.0f)
                                         : (event.position.x - dragStart.x) / dragScale;
        if (onEdit) onEdit(dragged, dragValue + delta);
    }
    void mouseUp(const juce::MouseEvent&) override { finishDrag(); }
    void mouseMove(const juce::MouseEvent& event) override {
        hovered = handleAt(event.position);
        setMouseCursor(hovered < 0 ? juce::MouseCursor::NormalCursor
                                  : hovered == 2 ? juce::MouseCursor::UpDownResizeCursor
                                                 : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override { hovered = -1; repaint(); }
    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(uiColour(*this, DesignTheme::inset));
        g.fillRoundedRectangle(bounds, 6.0f);
        const float left = 10, right = bounds.getRight() - 10;
        const float top = 24, bottom = bounds.getBottom() - 18;
        // A/D/R share a linear, auto-scaled time axis. The held section is symbolic:
        // sustain is a level, and its real duration depends on when the key is released.
        const float holdWidth = (right - left) * 0.2f;
        const float timeScale = scale();
        const float peakX = left + attack * timeScale;
        const float sustainX = peakX + decay * timeScale;
        const float noteOffX = sustainX + holdWidth;
        const float sustainY = bottom - sustain * (bottom - top);
        const float endX = noteOffX + release * timeScale;
        g.setColour(uiColour(*this, DesignTheme::track));
        g.drawHorizontalLine(juce::roundToInt(bottom), left, right);
        const float dashes[] {2.0f, 3.0f};
        g.drawDashedLine({noteOffX, top, noteOffX, bottom}, dashes, 2);
        juce::Path curve;
        curve.startNewSubPath(left, bottom);
        curve.lineTo(peakX, top);
        curve.lineTo(sustainX, sustainY);
        curve.lineTo(noteOffX, sustainY);
        curve.lineTo(endX, bottom);
        auto fill = curve;
        fill.closeSubPath();
        g.setColour(uiColour(*this, DesignTheme::accent).withAlpha(0.09f)); g.fillPath(fill);
        g.setColour(uiColour(*this, DesignTheme::accent));
        g.strokePath(curve, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved));
        const auto points = handles();
        for (int i = 0; i < 4; ++i) {
            const auto point = points[static_cast<size_t>(i)];
            const float radius = (i == hovered || i == dragged) ? 5.5f : 4.0f;
            g.setColour((i == dragged ? uiColour(*this, DesignTheme::text) : uiColour(*this, DesignTheme::accent)));
            g.fillEllipse(point.x - radius, point.y - radius, radius * 2, radius * 2);
        }
        g.setFont(10.0f); g.setColour(uiColour(*this, DesignTheme::muted));
        const int active = dragged >= 0 ? dragged : hovered;
        const juce::String title = active < 0 ? "DRAG POINTS" : juce::StringArray{"ATTACK", "DECAY", "SUSTAIN", "RELEASE"}[active];
        g.drawText(title, 10, 3, 84, 16, juce::Justification::centredLeft);
        const int noteOffLabelX = juce::jlimit(96, getWidth() - 64, juce::roundToInt(noteOffX) - 27);
        g.drawText("NOTE OFF", noteOffLabelX, 3, 54, 16, juce::Justification::centred);
        g.drawText("HELD", juce::roundToInt(sustainX), juce::roundToInt(bottom) + 1,
                   juce::roundToInt(holdWidth), 16, juce::Justification::centred);
    }
private:
    float scale() const {
        // Reserve room to extend a duration, and freeze the axis while dragging so
        // the selected point follows the pointer without a moving time scale.
        return dragged >= 0 ? dragScale : (getWidth() - 20.0f) * 0.6f / (attack + decay + release);
    }
    std::array<juce::Point<float>, 4> handles() const {
        const float peakX = 10 + attack * scale();
        const float sustainX = peakX + decay * scale();
        const float noteOffX = sustainX + (getWidth() - 20.0f) * 0.2f;
        const float bottom = getHeight() - 18.0f;
        const float sustainY = bottom - sustain * (bottom - 24);
        return {{{peakX, 24}, {sustainX, sustainY}, {noteOffX, sustainY}, {noteOffX + release * scale(), bottom}}};
    }
    int handleAt(juce::Point<float> position) const {
        int closest = -1;
        float distance = 11.0f;
        const auto points = handles();
        for (int i = 0; i < 4; ++i) {
            const float candidate = points[static_cast<size_t>(i)].getDistanceFrom(position);
            if (candidate < distance) { closest = i; distance = candidate; }
        }
        return closest;
    }
    int dragged = -1, hovered = -1;
    float dragScale = 1, dragValue = 0;
    juce::Point<float> dragStart;
    float attack = 0.015f, decay = 0.1f, sustain = 1.0f, release = 0.45f;
};

#include "Editor.h"

}
juce::AudioProcessorEditor* SineStack::createEditor() { return new Editor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SineStack(); }
