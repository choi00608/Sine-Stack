#pragma once
#include <juce_dsp/juce_dsp.h>
#include "ThirdParty/signalsmith-stretch.h"

// Live mode keeps 48 ms of analysis history but skews the analysis/synthesis
// windows toward the present: only about 12 ms of round-trip wet latency.
// Both configurations are prepared off the audio thread. A mode change warms
// the destination and crossfades, without allocation or resetting the dry path.
class AudioPitchShift {
public:
    enum Mode { live = 0, detail = 1 };
    void prepare(double rate, int mode = live) {
        const int hop = juce::jmax(64, juce::roundToInt(rate * .006));
        processors[live].configure(2, hop * 8, hop, false, .75f);
        processors[detail].configure(2, hop * 8, hop, false);
        warmSamples = hop * 16;
        fadeSamples = juce::jmax(1, juce::roundToInt(rate * .02));
        wanted = juce::jlimit(0, 1, mode); reset();
    }
    void reset() {
        for (auto& processor : processors) processor.reset();
        active = wanted; destination = -1; warmRemaining = fadePosition = 0;
    }
    void requestMode(int mode) { wanted = juce::jlimit(0, 1, mode); }
    int latencySamples(int mode) const {
        const auto& p = processors[juce::jlimit(0, 1, mode)];
        return p.inputLatency() + p.outputLatency();
    }
    bool isTransitioning() const { return destination >= 0 || wanted != active; }
    std::array<float, 2> tick(std::array<float, 2> input, double semitones) {
        if (destination < 0 && wanted != active) {
            destination = wanted; processors[destination].reset();
            warmRemaining = warmSamples; fadePosition = 0;
        }
        const float pitch = static_cast<float>(juce::jlimit(-48.0, 48.0, semitones));
        auto output = process(active, input, pitch);
        if (destination >= 0) {
            const auto next = process(destination, input, pitch);
            if (warmRemaining > 0) --warmRemaining;
            else {
                const float mix = static_cast<float>(++fadePosition) / fadeSamples;
                for (int c = 0; c < 2; ++c) output[c] += mix * (next[c] - output[c]);
                if (fadePosition >= fadeSamples) { active = destination; destination = -1; }
            }
        }
        return output;
    }
private:
    std::array<signalsmith::stretch::SignalsmithStretch<float>, 2> processors {
        signalsmith::stretch::SignalsmithStretch<float>{0x51a7},
        signalsmith::stretch::SignalsmithStretch<float>{0x51a7}
    };
    int active = live, wanted = live, destination = -1;
    int warmSamples = 4608, fadeSamples = 960, warmRemaining = 0, fadePosition = 0;
    std::array<float, 2> process(int mode, const std::array<float, 2>& input, float pitch) {
        auto& processor = processors[mode];
        processor.setTransposeSemitones(pitch);
        std::array<float, 2> output {};
        const std::array<const float*, 2> in {&input[0], &input[1]};
        const std::array<float*, 2> out {&output[0], &output[1]};
        processor.process(in, 1, out, 1);
        return output;
    }
};
