// Included inside Plugin.cpp's private namespace.
struct Voice final : juce::SynthesiserVoice {
    struct Unit {
        int kind = -1;
        unsigned generation = 0;
        juce::SmoothedValue<float> gain;
        std::array<juce::SmoothedValue<float>, SourceSettings::count> controls;
        std::array<juce::SmoothedValue<float>, 3> waveMix;
        std::array<double, 4> phase {};
        std::array<float, 2> low {};
        juce::Random noise, noiseRight;
        std::array<float, SourceSettings::count> values {};
        void advance() {
            for (int d = 0; d < SourceSettings::count; ++d)
                if (SourceSettings::definitions[d].kind == kind) values[d] = controls[d].getNextValue();
        }
    };
    std::array<Unit, SourceSettings::capacity> units;
    std::array<juce::AudioBuffer<float>, SourceSettings::capacity>* sourceAudio = nullptr;
    const SineStack::Modulation* modulation = nullptr;
    int voiceIndex = 0;
    double frequency = 440, bend = 1;
    float velocity = 0;
    juce::ADSR envelope;
    juce::SmoothedValue<float> handover;
    bool retiring = false, hasRendered = false, legatoTransition = false;
    void retireForMono() {
        if (!isVoiceActive() || retiring) return;
        if (!hasRendered) { stopNote(0, false); return; }
        retiring = true;
        handover.setTargetValue(0);
    }
    explicit Voice(int index) : voiceIndex(index) {}
    void prepare(double sampleRate, int) {
        legatoTransition = false;
        handover.reset(sampleRate, 0.005); handover.setCurrentAndTargetValue(1); retiring = false; hasRendered = false;
        envelope.setSampleRate(sampleRate); envelope.setParameters(envelope.getParameters());
        for (int i = 0; i < SourceSettings::capacity; ++i) {
            auto& u = units[i]; u.kind = -1; u.phase.fill(0); u.low.fill(0);
            const juce::int64 seed = 0x51a7 + voiceIndex * 7919 + (i == 5 ? 0 : (i + 1) * 104729);
            u.noise.setSeed(seed); u.noiseRight.setSeed(seed + 15485863);
            u.gain.reset(sampleRate, 0.02); u.gain.setCurrentAndTargetValue(0);
            for (auto& s : u.controls) s.reset(sampleRate, 0.02);
            for (auto& s : u.waveMix) s.reset(sampleRate, 0.01);
        }
    }
    void configure(const std::array<SourceSettings::Frame, SourceSettings::capacity>& frames,
                   bool initialise) {
        using namespace SourceSettings;
        for (int i = 0; i < capacity; ++i) {
            const auto& f = frames[i]; auto& u = units[i];
            const bool fresh = initialise || u.kind != f.kind || u.generation != f.generation;
            u.kind = f.kind; u.generation = f.generation;
            if (fresh) { u.phase.fill(0); u.low.fill(0); }
            if (f.kind != 0 && f.kind != 4 && f.kind != 5) { u.gain.setTargetValue(0); continue; }
            for (int d = 0; d < count; ++d) {
                if (fresh) u.controls[d].setCurrentAndTargetValue(f.values[d]);
                else u.controls[d].setTargetValue(f.values[d]);
            }
            float target = f.active ? 1.0f : 0.0f;
            if (f.kind == 0) target *= f.values[oscLevel];
            if (f.kind == 4) target *= f.values[subLevel];
            if (f.kind == 5) target *= f.values[noiseLevel];
            if (initialise) u.gain.setCurrentAndTargetValue(target);
            else { if (fresh) u.gain.setCurrentAndTargetValue(0); u.gain.setTargetValue(target); }
            const int waveType = static_cast<int>(f.values[f.kind == 4 ? subWave : wave]);
            for (int j = 0; j < 3; ++j) {
                if (fresh) u.waveMix[j].setCurrentAndTargetValue(j == waveType ? 1.0f : 0.0f);
                else u.waveMix[j].setTargetValue(j == waveType ? 1.0f : 0.0f);
            }
        }
    }
    static double polyBlep(double t, double increment) {
        if (t < increment) { t /= increment; return 2 * t - t * t - 1; }
        if (t > 1 - increment) { t = (t - 1) / increment; return t * t + 2 * t + 1; }
        return 0;
    }
    static double oscillator(double position, double increment, const std::array<float, 3>& mix) {
        const double edge = polyBlep(position, increment);
        const double half = position < 0.5 ? position + 0.5 : position - 0.5;
        return std::sin(position * juce::MathConstants<double>::twoPi) * mix[0]
            + ((position < 0.5 ? 1.0 : -1.0) + edge - polyBlep(half, increment)) * mix[1]
            + (2 * position - 1 - edge) * mix[2];
    }
    static void advancePhase(double& phase, double increment) { phase += increment; phase -= std::floor(phase); }
    static float panGain(float pan, int channel) { return juce::jmin(1.0f, 1 + (channel == 0 ? -pan : pan)); }
    bool canPlaySound(juce::SynthesiserSound* s) override { return dynamic_cast<Sound*>(s) != nullptr; }
    void startNote(int note, float v, juce::SynthesiserSound*, int wheel) override {
        if (legatoTransition) {
            legatoTransition = false;
            frequency = juce::MidiMessage::getMidiNoteInHertz(note); pitchWheelMoved(wheel);
            // Keep envelope, all oscillator phases, filter/noise histories and
            // the phrase's first-note velocity. Only its pitch/identity changes.
            return;
        }
        handover.setCurrentAndTargetValue(1); retiring = false; hasRendered = false;
        frequency = juce::MidiMessage::getMidiNoteInHertz(note);
        pitchWheelMoved(wheel); velocity = v * 0.16f;
        for (auto& u : units) {
            u.phase.fill(0);
            for (auto& w : u.waveMix) w.setCurrentAndTargetValue(w.getTargetValue());
        }
        envelope.reset(); envelope.noteOn();
    }
    void stopNote(float, bool tail) override {
        // startVoice updates JUCE's private MIDI identity during a legato transfer.
        // Its preliminary stop must not reset the sounding voice.
        if (legatoTransition) return;
        envelope.noteOff();
        if (!tail) { envelope.reset(); clearCurrentNote(); }
    }
    void pitchWheelMoved(int w) override { bend = std::pow(2.0f, (w - 8192) / 8192.0f * 2.0f / 12.0f); }
    void controllerMoved(int, int) override {}
    void renderNextBlock(juce::AudioBuffer<float>&, int start, int count) override {
        using namespace SourceSettings;
        if (count > 0) hasRendered = true;
        for (int n = 0; n < count; ++n) {
            const float envelopeGain = velocity * envelope.getNextSample() * handover.getNextValue() * modulation->volumeAt(start + n);
            const double hz = frequency * std::exp2(modulation->pitchAt(start + n) / 12.0);
            for (int i = 0; i < capacity; ++i) {
                auto& u = units[i]; if (u.kind != 0) continue;
                if (u.gain.getCurrentValue() == 0 && u.gain.getTargetValue() == 0) continue;
                u.advance(); const auto& v = u.values;
                const float gain = u.gain.getNextValue();
                const std::array<float, 3> wave {u.waveMix[0].getNextValue(), u.waveMix[1].getNextValue(), u.waveMix[2].getNextValue()};
                const double tunedHz = hz * std::exp2(v[oscTune] / 12.0 + v[oscFine] / 1200.0);
                const double increment = juce::jmin(0.49, tunedHz * bend / getSampleRate());
                const float sample = static_cast<float>(oscillator(u.phase[0], increment, wave));
                advancePhase(u.phase[0], increment);
                for (int c = 0; c < 2; ++c) (*sourceAudio)[i].addSample(c, start + n, sample * gain * panGain(v[oscPan], c) * envelopeGain);
            }
            for (int i = 0; i < capacity; ++i) {
                auto& u = units[i];
                if ((u.kind != 4 && u.kind != 5) || (u.gain.getCurrentValue() == 0 && u.gain.getTargetValue() == 0)) continue;
                u.advance(); const auto& v = u.values; const float gain = u.gain.getNextValue();
                if (u.kind == 4) {
                    const std::array<float, 3> wave {u.waveMix[0].getNextValue(), u.waveMix[1].getNextValue(), u.waveMix[2].getNextValue()};
                    const double increment = juce::jmin(0.49, hz * bend / getSampleRate() * std::exp2(-1.0 - v[subOctave] + v[subFine] / 1200.0));
                    const float sample = static_cast<float>(oscillator(u.phase[0], increment, wave)); advancePhase(u.phase[0], increment);
                    for (int c = 0; c < 2; ++c) {
                        const float layer = sample * gain * panGain(v[subPan], c);
                        (*sourceAudio)[i].addSample(c, start + n, layer * envelopeGain);
                    }
                } else if (u.kind == 5) {
                    const float left = u.noise.nextFloat() * 2 - 1, right = u.noiseRight.nextFloat() * 2 - 1;
                    const float coefficient = static_cast<float>(1 - std::exp(-juce::MathConstants<double>::twoPi * juce::jmin(static_cast<double>(v[noiseCutoff]), getSampleRate() * 0.45) / getSampleRate()));
                    for (int c = 0; c < 2; ++c) {
                        const float input = c == 0 ? left : left + v[noiseWidth] * (right - left);
                        u.low[c] += coefficient * (input - u.low[c]);
                        const float coloured = v[noiseColour] < 0 ? input + (-v[noiseColour]) * (u.low[c] - input) : input - v[noiseColour] * u.low[c];
                        const float layer = coloured * gain * panGain(v[noisePan], c);
                        (*sourceAudio)[i].addSample(c, start + n, layer * envelopeGain);
                    }
                }
            }
            if (!envelope.isActive() || (retiring && !handover.isSmoothing())) { envelope.reset(); clearCurrentNote(); break; }
        }
    }
};
