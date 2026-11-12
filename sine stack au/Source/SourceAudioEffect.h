// Included outside Plugin.cpp's anonymous namespace.
void SineStack::SourceEffect::prepare(double rate, int latencyMode) {
    sampleRate = rate;
    for (auto& head : heads) head.prepare(rate, latencyMode);
    for (auto& control : controls) control.reset(rate, .02);
    for (auto& weight : harmonyWeights) weight.reset(rate, .02);
    wet.reset(rate, .01); pitchMix.reset(rate, .02); kind = -1;
}
void SineStack::SourceEffect::configure(const SourceSettings::Frame& frame, bool initialise, int latencyMode) {
    for (auto& head : heads) head.requestMode(latencyMode);
    const bool fresh = kind != frame.kind || generation != frame.generation;
    if (fresh) {
        kind = frame.kind; generation = frame.generation;
        for (auto& head : heads) head.reset();
        harmonyRunning.fill(false);
        previousNote.fill(-1); glideStart = 0; glidePosition = 1;
        wet.setCurrentAndTargetValue(initialise && frame.active ? 1 : 0);
        pitchMix.setCurrentAndTargetValue(0);
    }
    if (frame.kind == 2) {
        const int interval = static_cast<int>(frame.values[SourceSettings::harmony]);
        const bool minor = interval == 1 || interval == 4, major = interval == 2 || interval == 5, fifth = interval >= 3;
        const float scale = 1.0f / juce::jmax(1, (minor || major ? 1 : 0) + (fifth ? 1 : 0));
        const std::array<float, 3> weights {minor ? scale : 0, major ? scale : 0, fifth ? scale : 0};
        for (int h = 0; h < 3; ++h) {
            if (fresh) harmonyWeights[h].setCurrentAndTargetValue(weights[h]);
            else harmonyWeights[h].setTargetValue(weights[h]);
        }
    }
    wet.setTargetValue(frame.active ? 1 : 0);
    for (int d = 0; d < SourceSettings::count; ++d) {
        if (fresh) controls[d].setCurrentAndTargetValue(frame.values[d]);
        else controls[d].setTargetValue(frame.values[d]);
    }
}
void SineStack::SourceEffect::note(int channel, int note, const SourceSettings::Frame& frame) {
    if (channel < 1 || channel > 16 || kind != 3) return;
    const double currentShift = glideStart * (1 - std::pow(glidePosition, glideCurve));
    const double previous = previousNote[channel - 1];
    const double time = frame.values[SourceSettings::glide];
    glideStart = previous >= 0 && time > 0 ? (previous + currentShift - note) * frame.values[SourceSettings::glideAmount] : 0;
    glideCurve = frame.values[SourceSettings::glideCurve]; glidePosition = 0;
    glideStep = time > 0 ? 1 / (time * sampleRate) : 1;
    previousNote[channel - 1] = note;
}
void SineStack::SourceEffect::process(juce::AudioBuffer<float>& audio) {
    using namespace SourceSettings;
    std::array<float, SourceSettings::count> v {};
    auto pan = [](float value, int channel) { return juce::jmin(1.0f, 1 + (channel == 0 ? -value : value)); };
    for (int n = 0; n < audio.getNumSamples(); ++n) {
        for (int d = 0; d < SourceSettings::count; ++d) if (definitions[d].kind == kind) v[d] = controls[d].getNextValue();
        const std::array<float, 2> input {audio.getSample(0, n), audio.getSample(1, n)};
        auto output = input;
        if (kind == 1) {
            const float bank = v[voices], cents = v[SourceSettings::detune];
            const float active = juce::jlimit(0.0f, 1.0f, bank) * juce::jlimit(0.0f, 1.0f, cents / .01f);
            const float five = juce::jlimit(0.0f, 1.0f, bank - 1);
            std::array<float, 2> shifted {};
            for (int h = 0; h < 4; ++h) {
                const double pitch = (h % 2 == 0 ? -1 : 1) * cents * (h < 2 ? .01 : .005);
                const auto sample = heads[h].tick(input, pitch);
                const float weight = h < 2 ? .25f - five * .125f : five * .125f;
                const float position = juce::jlimit(-1.0f, 1.0f, v[unisonPan] + (h % 2 == 0 ? -v[unisonWidth] : v[unisonWidth]));
                for (int c = 0; c < 2; ++c) shifted[c] += sample[c] * weight * pan(position, c);
            }
            const float mix = v[unisonMix] * active;
            for (int c = 0; c < 2; ++c) output[c] = input[c] + mix * (shifted[c] - input[c] * .5f);
        } else if (kind == 2) {
            for (int h = 0; h < 3; ++h) {
                const double pitch = (h == 0 ? 3 : h == 1 ? 4 : 7) + v[harmonyShift] + v[harmonyFine] * .01;
                const float weight = harmonyWeights[h].getNextValue();
                if (weight == 0 && !harmonyWeights[h].isSmoothing()) {
                    // An unselected interval needs no spectral transform. Clear
                    // once so selecting it later cannot replay old buffered audio.
                    if (harmonyRunning[h]) heads[h].reset();
                    harmonyRunning[h] = false; continue;
                }
                harmonyRunning[h] = true;
                const auto sample = heads[h].tick(input, pitch);
                const float position = juce::jlimit(-1.0f, 1.0f, v[harmonyPan] + (h == 2 ? v[harmonyWidth] : -v[harmonyWidth]));
                for (int c = 0; c < 2; ++c) output[c] += sample[c] * v[SourceSettings::harmonyMix] * weight * pan(position, c);
            }
        } else if (kind == 3) {
            glidePosition = juce::jmin(1.0, glidePosition + glideStep);
            const double shift = glideStart * (1 - std::pow(glidePosition, glideCurve));
            pitchMix.setTargetValue(std::abs(shift) > .00001 ? 1.0f : 0.0f);
            const auto shifted = heads[0].tick(input, shift); const float mix = pitchMix.getNextValue();
            for (int c = 0; c < 2; ++c) output[c] = input[c] + mix * (shifted[c] - input[c]);
        }
        const float amount = wet.getNextValue();
        const float mix = amount;
        for (int c = 0; c < 2; ++c) audio.setSample(c, n, mix == 0 ? input[c] : input[c] + mix * (output[c] - input[c]));
    }
}
