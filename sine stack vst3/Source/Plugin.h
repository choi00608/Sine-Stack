#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "FxDetails.h"
#include "SourceSettings.h"
#include "ModTargets.h"
#include "AudioPitchShift.h"
#include "LfoTiming.h"
#include "DelayTiming.h"
#include "OutputCapture.h"

class SineStack final : public juce::AudioProcessor, public juce::ChangeBroadcaster {
public:
    static constexpr int maxBlocks = 32, maxLfoRoutes = 8;
    static juce::String routeSetting(int route, const juce::String& key) {
        return route == 0 ? key : "route" + juce::String(route + 1) + "_" + key;
    }
    enum SourceBlock { oscillatorBlock = 100, unisonBlock, harmonyBlock, glideBlock, subBlock, noiseBlock, lfoBlock };
    struct Chain {
        std::array<int, maxBlocks> ids {};
        int size = 0;
        bool explicitOrder = false;
    };
    // Stable preset membership registry, including legacy in-chain LFO IDs.
    // Audio and control paths use the filtered views, never registry position.
    Chain getChain() const;
    Chain getAudioChain() const;
    Chain getModulators() const;
    void moveAudioBlock(int from, int to);
    int addBlock(int kind); // 1..8: audio FX; 100..106: sources, pitch FX and modulation
    int duplicateBlock(int id); // Independent block copy, immediately after the original.
    void removeBlock(int id);
    void moveBlock(int from, int to);
    void materialiseChain();
    juce::String blockName(int id) const;
    juce::String blockBypassParameter(int id) const;
    int blockKind(int id) const;
    juce::String sourceParameter(int id, const juce::String& setting) const;
    bool canModulate(int blockId, int parameterIndex) const;
    int modulationParameterIndex(const juce::String& setting, bool source) const;
    juce::String modulationParameterId(int blockId, int parameterIndex) const;
    struct LfoLane {
        double phase = 0, step = 0;
        int shape = 0;
        std::array<float, 3> start {}, target {};
        float slew = 1;
        float phaseOffset = 0, offset = 0, pitchRange = 2, filterRange = 4;
        float depth(int route, int sample) const;
        float wave(int sample) const;
    };
    struct Modulation : LfoLane {
        int sampleOrigin = 0;
        std::array<LfoLane, SourceSettings::poolSize> extra;
        float filterAt(int sample) const;
        float pitchAt(int sample) const;
        float volumeAt(int sample) const;
    };
    SineStack();
    const juce::String getName() const override { return "Sine Stack"; }
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;
    double getTempoBpm() const { return reportedTempo.load(std::memory_order_relaxed); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Init"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    std::array<int, 4> getFxOrder() const;
    void moveFx(int from, int to);
    juce::Result loadFactoryPreset(int index);
    int getFactoryPresetIndex() const;
    juce::String getPresetCategory() const;
    juce::Result savePreset(const juce::File&);
    juce::Result savePreset(const juce::File&, const juce::String& category);
    juce::Result loadPreset(const juce::File&);
    juce::File getPresetFile() const;
    juce::AudioProcessorValueTreeState state;
    juce::MidiKeyboardState keyboard;
    OutputCapture outputCapture;
    // Message-thread-only device access, owned by the standalone wrapper. Null in AU hosts.
    juce::AudioDeviceManager* standaloneDevices = nullptr;
private:
    struct Slot {
        juce::dsp::Chorus<float> chorus;
        juce::dsp::Phaser<float> phaser;
        juce::dsp::StateVariableTPTFilter<float> filter;
        juce::dsp::StateVariableTPTFilter<float> secondFilter;
        std::array<float, EqSettings::count> eqValues {};
        std::array<juce::SmoothedValue<double>, EqSettings::count> eqSmoothers;
        std::array<std::array<std::array<double, 2>, 2>, 3> eqHistory {};
        std::array<float, FxDetails::count> details {};
        std::array<juce::SmoothedValue<float>, FxDetails::count> detailSmoothers;
        std::array<juce::SmoothedValue<float>, 3> driveModes, tremoloShapes;
        std::array<float, 2> echoLow {}, echoHigh {}, roomLow {}, driveDCInput {}, driveDCOutput {};
        juce::SmoothedValue<float> roomDry;
        juce::AudioBuffer<float> preDelay;
        juce::AudioBuffer<float> roomInput;
        int preWrite = 0;
        struct Extra {
            float cutoff = 2000, resonance = 0.1f, tremoloRate = 4, tremoloDepth = 0.5f;
            float phaserRate = 0.4f, phaserDepth = 0.5f;
            int filterMode = 0;
        } extra;
        Modulation modulation;
        juce::SmoothedValue<float> cutoff, resonance, tremoloRate, tremoloDepth;
        double tremoloPhase = 0;
        juce::Reverb reverb;
        juce::AudioBuffer<float> delay;
        juce::AudioBuffer<float> dry;
        int write = 0, previousType = -1;
        juce::SmoothedValue<float> amount, wetGain, delaySamples, feedback;
        juce::SmoothedValue<float> driveStrength, driveTone;
        std::array<float, 2> toneLow {};
        float toneCoefficient = 0;
        double sampleRate = 48000;
        void prepare(double, int, bool);
        void process(juce::AudioBuffer<float>&, int, float, bool, float, float, float, float, float, float, float, float);
        void processEffect(juce::AudioBuffer<float>&, int, float, float, float, float, float, float, float, float, float, int);
    };
    struct SourceEffect {
        std::array<AudioPitchShift, 4> heads;
        std::array<juce::SmoothedValue<float>, SourceSettings::count> controls;
        juce::SmoothedValue<float> wet, pitchMix;
        std::array<juce::SmoothedValue<float>, 3> harmonyWeights;
        std::array<bool, 3> harmonyRunning {};
        std::array<double, 16> previousNote {};
        double sampleRate = 48000, glideStart = 0, glidePosition = 1, glideStep = 1, glideCurve = 1;
        int kind = -1; unsigned generation = 0;
        void prepare(double, int latencyMode);
        void configure(const SourceSettings::Frame&, bool initialise = false, int latencyMode = 0);
        void note(int channel, int midiNote, const SourceSettings::Frame&);
        void process(juce::AudioBuffer<float>&);
    };
    std::array<SourceEffect, SourceSettings::capacity> sourceEffects;
    std::array<juce::AudioBuffer<float>, SourceSettings::capacity> sourceAudio;
    struct Synth final : juce::Synthesiser {
        unsigned pitchSerial = 0;
        int pitchChannel = 1, pitchNote = 60;
        void recordPitch(int channel, int note) { ++pitchSerial; pitchChannel = channel; pitchNote = note; }
        void renderSources(juce::AudioBuffer<float>& audio, int count) { const juce::ScopedLock guard(lock); renderVoices(audio, 0, count); }
        struct HeldNote {
            juce::uint64 order = 0;
            float velocity = 0;
            bool key = false, sustained = false, captured = false;
            bool active() const { return key || sustained || captured; }
        };
        std::array<HeldNote, 16 * 128> held {};
        std::array<bool, 16> sustainDown {}, sostenutoDown {};
        juce::uint64 noteOrder = 0;
        int monoCurrent = -1;
        bool mono = false, legato = false;
        void setMono(bool, bool);
        int latestHeld() const;
        void playMono(int);
        void updateMonoPriority();
        void syncMonoVoiceFlags();
        void noteOn(int, int, float) override;
        void noteOff(int, int, float, bool) override;
        void handleSustainPedal(int, bool) override;
        void handleSostenutoPedal(int, bool) override;
        void handleMidiEvent(const juce::MidiMessage&) override;
        void allNotesOff(int, bool) override;
    } synth;
    std::array<Slot, maxBlocks> slots;
    std::array<std::atomic<float>*, maxBlocks> types {}, amounts {}, bypasses {};
    std::array<std::atomic<float>*, maxBlocks> delayTimes {}, delayFeedbacks {}, delaySync {}, delayDivision {}, delayRhythm {};
    std::array<std::atomic<float>*, maxBlocks> reverbSizes {}, reverbDampings {};
    std::array<std::atomic<float>*, maxBlocks> chorusRates {}, chorusDepths {};
    std::array<std::atomic<float>*, maxBlocks> driveStrengths {}, driveTones {};
    std::array<std::array<std::atomic<float>*, 7>, maxBlocks> extraFx {};
    std::array<std::array<std::atomic<float>*, FxDetails::count>, maxBlocks> detailFx {};
    std::array<std::array<std::atomic<float>*, EqSettings::count>, maxBlocks> eqFx {};
    std::array<std::atomic<float>*, 7> sourceBypasses {};
    std::array<std::array<std::atomic<float>*, SourceSettings::count>, SourceSettings::capacity> sourceValues {};
    std::array<std::atomic<float>*, SourceSettings::poolSize> sourceKinds {}, sourceMutes {};
    std::array<std::atomic<unsigned>, SourceSettings::capacity> sourceGenerations {};
    std::array<SourceSettings::Frame, SourceSettings::capacity> sourceFrames {};
    void updateSources(const Chain&, bool initialise);
    void configureLfo(int node, bool initialise, bool reset);
    void advanceModulation(int samples);
    void syncLfoClock(int node);
    std::array<std::atomic<float>*, SourceSettings::capacity> lfoSync {}, lfoDivision {}, lfoRhythm {};
    double tempoBpm = 120, controlPpq = 0;
    std::atomic<double> reportedTempo {120};
    bool hostPhaseAvailable = false;
    struct TargetBinding { std::atomic<float>* value = nullptr; juce::RangedAudioParameter* parameter = nullptr; };
    std::array<std::array<TargetBinding, ModTargets::count>, ModTargets::blocks> targetBindings {};
    struct ParameterRoute {
        std::atomic<float> *block = nullptr, *parameter = nullptr, *range = nullptr, *enabled = nullptr;
        juce::SmoothedValue<float> strength;
        int boundBlock = -1, boundParameter = 0;
        unsigned generation = 0;
    };
    std::array<std::array<ParameterRoute, maxLfoRoutes>, SourceSettings::capacity> parameterRoutes;
    struct Override { std::atomic<float>* parameter = nullptr; float delta = 0, value = 0; int block = -1, setting = 0; };
    std::array<Override, SourceSettings::capacity * maxLfoRoutes> overrides;
    int overrideCount = 0;
    void applyParameterRoutes(const Chain&, int samples);
    float effectiveValue(std::atomic<float>*) const;
    std::array<std::atomic<bool>, maxBlocks> resetSlots {};
    mutable juce::SpinLock chainLock;
    Chain chain;
    void storeChain(const Chain&);
    static bool decodeChain(const juce::String&, Chain&);
    std::atomic<float> *attack, *decay, *sustain, *release, *level, *waveform, *fxOrder, *voiceMode, *monoLegato, *pitchLatency;
    std::atomic<float> *detune, *glide, *subLevel, *noiseLevel;
    std::atomic<float> *unison, *harmony, *harmonyMix, *lfoRate, *lfoDepth, *lfoShape, *lfoTarget;
    Modulation modulation;
    juce::SmoothedValue<float> gain;
};
