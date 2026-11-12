#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Clock final : juce::AudioPlayHead {
    double ppq = 0, bpm = 90;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p; p.setBpm(bpm); p.setPpqPosition(ppq); p.setIsPlaying(true); return p;
    }
};
juce::AudioProcessorParameter& parameter(juce::AudioPluginInstance& p, const juce::String& name) {
    for (auto* candidate : p.getParameters()) if (candidate->getName(100) == name) return *candidate;
    throw std::runtime_error(("Missing hosted parameter: " + name).toStdString());
}
void set(juce::AudioPluginInstance& p, const juce::String& name, float normalized) {
    parameter(p, name).setValueNotifyingHost(normalized);
}
}
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        require(argc == 2, "Pass the built .vst3 bundle path");
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile(descriptions, juce::File(argv[1]).getFullPathName());
        require(descriptions.size() == 1 && descriptions[0]->isInstrument, "VST3 scan did not find one instrument");
        require(descriptions[0]->name == "Sine Stack", "VST3 identity is incorrect");
        auto plist = juce::XmlDocument::parse(juce::File(argv[1]).getChildFile("Contents/Info.plist"));
        require(plist != nullptr && plist->getChildByName("dict") != nullptr, "VST3 version metadata is missing");
        juce::String bundleVersion;
        for (auto* element : plist->getChildByName("dict")->getChildIterator())
            if (element->hasTagName("key") && element->getAllSubText() == "CFBundleShortVersionString")
                if (auto* value = element->getNextElement()) bundleVersion = value->getAllSubText();
        require(bundleVersion.isNotEmpty() && descriptions[0]->version == bundleVersion,
                "Compiled VST3 host version differs from its bundle metadata");
        std::cout << "PASS: actual VST3 reports version " << descriptions[0]->version << '\n';
        auto create = [&] {
            juce::String error;
            auto p = format.createInstanceFromDescription(*descriptions[0], 48000, 127, error);
            if (!p) throw std::runtime_error(error.toStdString());
            require(p->getTotalNumOutputChannels() == 2 && p->getTotalNumInputChannels() == 0, "VST3 bus layout is incorrect");
            require(p->hasEditor(), "VST3 is missing its editor");
            return p;
        };
        auto dry = create(), effect = create();
        // Test the built VST3 adapter, not a directly-linked processor. This checks
        // host parameter IDs, MIDI routing and tempo context through the VST3 ABI.
        set(*effect, "FX 1", 3.0f / 8); // Delay
        set(*effect, "FX 1 Amount", .6f);
        set(*effect, "FX 1 Delay Feedback", 0);
        set(*effect, "FX 1 Delay Clock", 1);
        set(*effect, "FX 1 Delay Beat", .5f); // Eighth note
        set(*effect, "FX 1 Delay Rhythm", .5f); // Dotted eighth note
        Clock clock;
        for (double sr : {44100.0, 48000.0}) {
            clock.bpm = sr == 44100 ? 90 : 150; // Distinguish real host tempo from the 120 BPM fallback.
            for (auto* p : {dry.get(), effect.get()}) {
                set(*p, "Attack", 0); set(*p, "Release", 0);
                p->setPlayHead(&clock); p->setRateAndBufferSizeDetails(sr, 127); p->prepareToPlay(sr, 127);
            }
            const double delay = sr * .75 * 60 / clock.bpm;
            const int total = static_cast<int>(sr * .65);
            std::vector<float> original(static_cast<size_t>(total + 128));
            juce::AudioBuffer<float> a(2, 127), b(2, 127);
            double echoEnergy = 0, sourceEnergy = 0;
            for (int offset = 0; offset < total; offset += 127) {
                clock.ppq = offset * clock.bpm / (60 * sr);
                for (auto pair : {std::make_pair(dry.get(), &a), std::make_pair(effect.get(), &b)}) {
                    pair.second->clear(); juce::MidiBuffer midi;
                    if (offset == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 69, .8f), 0);
                    const int off = static_cast<int>(sr * .01);
                    if (off >= offset && off < offset + 127) midi.addEvent(juce::MidiMessage::noteOff(1, 69), off - offset);
                    pair.first->processBlock(*pair.second, midi);
                }
                for (int n = 0; n < 127; ++n) {
                    const int sample = offset + n; original[sample] = a.getSample(0, n);
                    double expected = original[sample];
                    if (sample >= delay) {
                        const double previous = sample - delay; const int index = static_cast<int>(previous);
                        expected += .6 * .65 * (original[index] + (previous - index) * (original[index + 1] - original[index]));
                    }
                    for (int c = 0; c < 2; ++c)
                        require(std::isfinite(b.getSample(c, n)) && std::abs(b.getSample(c, n) - expected) < .0001,
                            "Hosted VST3 audio/MIDI/tempo parameters differ from expected dotted echoes");
                    sourceEnergy += std::abs(original[sample]);
                    echoEnergy += std::abs(b.getSample(0, n) - original[sample]);
                }
            }
            require(sourceEnergy > 1 && echoEnergy > .1, "Hosted VST3 did not produce source audio and echoes");
            for (auto* p : {dry.get(), effect.get()}) { p->releaseResources(); p->setPlayHead(nullptr); }
        }
        juce::MemoryBlock state; effect->getStateInformation(state);
        require(state.getSize() > 0, "Hosted VST3 did not save state");
        auto restored = create(); restored->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        for (const auto* name : {"FX 1", "FX 1 Delay Clock", "FX 1 Delay Beat", "FX 1 Delay Rhythm", "FX 1 Amount"})
            require(std::abs(parameter(*restored, name).getValue() - parameter(*effect, name).getValue()) < .00001f,
                "Hosted VST3 state round-trip lost a parameter");
        std::cout << "PASS: built VST3 scan/identity, stereo MIDI synthesis at 44.1/48 kHz, host tempo/dotted delay, parameters and state restoration\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
