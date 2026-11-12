#include "../Source/Plugin.h"
#include <iostream>
#include <stdexcept>
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void setParameter(SineStack& p, const char* id, float value) {
    auto* parameter = p.state.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
void checkPresetFiles(SineStack& p, juce::AudioProcessorEditor& editor) {
    struct TestFolder {
        juce::File path = juce::File::getCurrentWorkingDirectory().getChildFile("preset-check-" + juce::Uuid().toString());
        ~TestFolder() { path.deleteRecursively(); }
    } folder;
    require(folder.path.createDirectory().wasOk(), "Could not create preset test folder");
    const auto file = folder.path.getChildFile(juce::String::fromUTF8("테스트 소리.sinestack"));
    std::vector<float> savedValues;
    int index = 0;
    for (auto* parameter : p.getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        const float value = ranged->convertFrom0to1(static_cast<float>((index++ % 9) + 1) / 10);
        parameter->setValueNotifyingHost(ranged->convertTo0to1(value));
        savedValues.push_back(parameter->getValue());
    }
    require(p.savePreset(file).wasOk() && file.existsAsFile(), "Preset file save failed");
    SineStack reference;
    require(reference.loadPreset(file).wasOk(), "Preset file load failed");
    for (auto* parameter : p.getParameters()) parameter->setValueNotifyingHost(parameter->getDefaultValue());
    require(p.loadPreset(file).wasOk(), "Preset reload failed");
    index = 0;
    for (auto* parameter : p.getParameters()) {
        if (std::abs(parameter->getValue() - savedValues[index]) >= 0.00001f)
            std::cerr << parameter->getName(100) << ": saved " << savedValues[index] << ", loaded " << parameter->getValue() << '\n';
        require(std::abs(parameter->getValue() - savedValues[index++]) < 0.00001f, "Preset lost a parameter");
    }
    for (auto* child : editor.getChildren()) {
        if (child->getName() == "ATTACK")
            require(std::abs(dynamic_cast<juce::Slider*>(child)->getValue() - p.state.getRawParameterValue("attack")->load()) < 0.0001,
                    "Preset load did not update knobs");
        if (child->getName() == "Waveform")
            require(dynamic_cast<juce::ComboBox*>(child)->getSelectedId() == static_cast<int>(p.state.getRawParameterValue("waveform")->load()) + 1,
                    "Preset load did not update waveform selector");
    }
    // A newly constructed processor and the open editor's processor must render the same sound.
    for (auto* processor : {&p, &reference}) {
        processor->setRateAndBufferSizeDetails(48000, 128); processor->prepareToPlay(48000, 128);
    }
    for (int block = 0; block < 100; ++block) {
        juce::AudioBuffer<float> a(2, 128), b(2, 128);
        for (auto pair : {std::make_pair(&p, &a), std::make_pair(&reference, &b)}) {
            juce::MidiBuffer midi;
            if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            pair.first->processBlock(*pair.second, midi);
        }
        for (int c = 0; c < 2; ++c) for (int n = 0; n < 128; ++n)
            require(std::abs(a.getSample(c, n) - b.getSample(c, n)) < 0.000001f, "Preset sound does not round-trip");
    }
    const auto validText = file.loadFileAsString();
    require(validText.contains("\n") && validText.contains("\"name\"") && validText.contains("\"unit\""), "Preset is not readable text");
    const auto invalid = folder.path.getChildFile("invalid.sinestack");
    auto mustReject = [&] {
        const auto before = p.state.copyState().toXmlString();
        require(p.loadPreset(invalid).failed(), "Invalid preset was accepted");
        require(p.state.copyState().toXmlString() == before, "Invalid preset changed current sound");
    };
    invalid.replaceWithText("not a preset"); mustReject();
    for (const auto& badValue : {juce::var("nan"), juce::var("garbage"), juce::var(9999)}) {
        auto bad = juce::JSON::parse(validText);
        bad["parameters"].getArray()->getReference(0).getDynamicObject()->setProperty("value", badValue);
        invalid.replaceWithText(juce::JSON::toString(bad)); mustReject();
    }
    auto duplicate = juce::JSON::parse(validText);
    auto* settings = duplicate["parameters"].getArray();
    settings->add((*settings)[0]);
    invalid.replaceWithText(juce::JSON::toString(duplicate)); mustReject();
    invalid.replaceWithText("{\"format\":\"SineStackPreset\",\"formatVersion\":1,\"parameters\":[]}"); mustReject();
    // Editing a numeric value in a text editor must affect the loaded sound.
    auto edited = juce::JSON::parse(validText);
    for (auto& entry : *edited["parameters"].getArray())
        if (entry["id"].toString() == "attack") entry.getDynamicObject()->setProperty("value", 0.25);
    invalid.replaceWithText(juce::JSON::toString(edited, false));
    require(p.loadPreset(invalid).wasOk() && std::abs(p.state.getRawParameterValue("attack")->load() - 0.25f) < 0.00001f,
            "Text-edited preset did not load");
    require(p.loadPreset(folder.path.getChildFile("missing.sinestack")).failed(), "Missing preset was accepted");
    require(p.savePreset(folder.path.getChildFile("missing-folder/file.sinestack")).failed(), "Invalid save path reported success");
    // Overwrite and values at the exact parameter endpoints must remain loadable.
    for (float endpoint : {0.0f, 1.0f}) {
        for (auto* parameter : p.getParameters()) parameter->setValueNotifyingHost(endpoint);
        require(p.savePreset(file).wasOk() && reference.loadPreset(file).wasOk(), "Preset overwrite or endpoint loading failed");
    }
    file.replaceWithText(validText);
    require(p.loadPreset(file).wasOk(), "Final preset restore failed");
    for (auto* child : editor.getChildren()) if (auto* slider = dynamic_cast<juce::Slider*>(child))
        if (slider->isVisible() && slider->getX() >= 361)
            for (auto* part : slider->getChildren()) if (auto* label = dynamic_cast<juce::Label*>(part))
                require(label->getText() == slider->getTextFromValue(slider->getValue()), "Preset load left stale knob text");
}
void checkPresetBrowser(SineStack& p, juce::AudioProcessorEditor& editor) {
    struct TestFolder {
        juce::File path = juce::File::getCurrentWorkingDirectory().getChildFile("browser-check-" + juce::Uuid().toString());
        ~TestFolder() { path.deleteRecursively(); }
    } folder;
    require(folder.path.createDirectory().wasOk(), "Preset browser test folder failed");
    juce::ComboBox* list = nullptr;
    juce::TextButton *refresh = nullptr, *previous = nullptr, *next = nullptr;
    for (auto* child : editor.getChildren()) {
        if (child->getName() == "Preset list") list = dynamic_cast<juce::ComboBox*>(child);
        if (child->getName() == "Refresh presets") refresh = dynamic_cast<juce::TextButton*>(child);
        if (child->getName() == "Previous preset") previous = dynamic_cast<juce::TextButton*>(child);
        if (child->getName() == "Next preset") next = dynamic_cast<juce::TextButton*>(child);
    }
    require(list && refresh && previous && next, "Missing preset browser controls");
    const auto alpha = folder.path.getChildFile("Alpha.sinestack");
    const auto zeta = folder.path.getChildFile("Zeta.sinestack");
    setParameter(p, "attack", 0.25f); require(p.savePreset(alpha).wasOk(), "Could not save Alpha");
    setParameter(p, "attack", 0.75f); require(p.savePreset(zeta).wasOk(), "Could not save Zeta");
    folder.path.getChildFile("notes.txt").replaceWithText("not a preset");
    refresh->onClick();
    require(list->getNumItems() == 2 && list->getItemText(0) == "Alpha" && list->getItemText(1) == "Zeta", "Preset list filter/sort failed");
    require(list->getText() == "Zeta", "Saved preset was not selected");
    auto checkSelection = [&](const juce::File& file, float attack) {
        require(p.getPresetFile() == file && list->getText() == file.getFileNameWithoutExtension()
                && std::abs(p.state.getRawParameterValue("attack")->load() - attack) < 0.0001f,
                "Preset navigation did not update sound and selection together");
    };
    require(previous->isEnabled() && next->isEnabled(), "Preset navigation is disabled");
    previous->onClick(); checkSelection(alpha, 0.25f);
    previous->onClick(); checkSelection(zeta, 0.75f);
    next->onClick(); checkSelection(alpha, 0.25f);
    next->onClick(); checkSelection(zeta, 0.75f);
    list->setSelectedId(list->getItemId(0), juce::sendNotificationSync);
    require(std::abs(p.state.getRawParameterValue("attack")->load() - 0.25f) < 0.0001f && p.getPresetFile() == alpha,
            "Selecting a preset did not load it");
    const auto broken = folder.path.getChildFile("Broken.sinestack");
    broken.replaceWithText("broken preset");
    refresh->onClick();
    require(list->getNumItems() == 3, "Refresh did not find added preset");
    list->setSelectedId(list->getItemId(1), juce::sendNotificationSync);
    require(list->getText() == "Alpha" && std::abs(p.state.getRawParameterValue("attack")->load() - 0.25f) < 0.0001f,
            "Broken preset changed the sound or selected name");
    next->onClick(); checkSelection(zeta, 0.75f);
    previous->onClick(); checkSelection(alpha, 0.25f);
    zeta.deleteFile(); refresh->onClick();
    require(list->getNumItems() == 2 && list->getText() == "Alpha", "Deleted preset was not removed");
    // Opening the editor must restore the folder/list without reloading and losing edits.
    setParameter(p, "attack", 0.4f);
    next->onClick(); checkSelection(alpha, 0.4f);
    previous->onClick(); checkSelection(alpha, 0.4f);
    std::unique_ptr<juce::AudioProcessorEditor> reopened(p.createEditor());
    bool found = false;
    for (auto* child : reopened->getChildren()) if (child->getName() == "Preset list") {
        auto* other = dynamic_cast<juce::ComboBox*>(child);
        found = other && other->getNumItems() == 2 && other->getText() == "Alpha";
    }
    require(found && std::abs(p.state.getRawParameterValue("attack")->load() - 0.4f) < 0.0001f, "Reopening preset browser lost edits/location");
    juce::MemoryBlock hostState; p.getStateInformation(hostState);
    SineStack restored; restored.setStateInformation(hostState.getData(), static_cast<int>(hostState.getSize()));
    require(restored.getPresetFile() == alpha, "Host state lost preset folder");
    require(!alpha.loadFileAsString().contains(folder.path.getFullPathName()), "Portable preset leaked local folder path");
    broken.deleteFile(); refresh->onClick();
    require(!previous->isEnabled() && !next->isEnabled(), "Single current preset should disable navigation");
    // A deleted current file leaves no selection: next starts at first, previous at last.
    p.state.state.setProperty("presetFile", folder.path.getChildFile("Missing.sinestack").getFullPathName(), nullptr);
    refresh->onClick();
    require(list->getSelectedId() == 0 && next->isEnabled(), "Unselected preset cannot be navigated");
    next->onClick(); checkSelection(alpha, 0.25f);
    setParameter(p, "attack", 0.75f); require(p.savePreset(zeta).wasOk(), "Could not restore Zeta");
    p.state.state.setProperty("presetFile", folder.path.getChildFile("Missing.sinestack").getFullPathName(), nullptr);
    previous->onClick(); checkSelection(zeta, 0.75f);
    auto preview = editor.createComponentSnapshot(editor.getLocalBounds(), true, 2.0f);
    juce::FileOutputStream image(juce::File::getCurrentWorkingDirectory().getChildFile("preset-browser-preview.png"));
    image.setPosition(0); image.truncate(); juce::PNGImageFormat().writeImageToStream(preview, image);
    alpha.deleteFile(); zeta.deleteFile(); refresh->onClick();
    require(list->getNumItems() == 0 && !list->isEnabled() && !previous->isEnabled() && !next->isEnabled(), "Empty preset folder was not handled");
}
double pitchedSpectrum(const std::vector<float>& audio, double hz);
void checkSourceLayers() {
    auto render = [](const char* id, float value, bool twoNotes = false, int channel = 1) {
        SineStack p;
        setParameter(p, "attack", 0.005f); setParameter(p, "release", 0.02f);
        if (id) setParameter(p, id, value);
        p.setRateAndBufferSizeDetails(48000, 240); p.prepareToPlay(48000, 240);
        std::vector<float> samples;
        for (int block = 0; block < 240; ++block) {
            juce::AudioBuffer<float> audio(2, 240);
            juce::MidiBuffer midi;
            if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, twoNotes ? 57 : 69, 1.0f), 0);
            if (twoNotes && block == 20) midi.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
            if (twoNotes && block == 50) midi.addEvent(juce::MidiMessage::noteOn(channel, 69, 1.0f), 0);
            if (block == 200) midi.addEvent(juce::MidiMessage::noteOff(channel, 69), 0);
            p.processBlock(audio, midi);
            for (int n = 0; n < 240; ++n) {
                const auto sample = audio.getSample(0, n);
                require(std::isfinite(sample) && std::abs(sample) < 0.98f, "Source layers produced unstable audio");
                if (block >= 235) require(std::abs(sample) < 1.0e-7f, "Sources or pitch FX leaked past ADSR and spectral tail");
                samples.push_back(sample);
            }
        }
        return samples;
    };
    const auto plain = render(nullptr, 0);
    auto amplitude = [](const std::vector<float>& audio, double hz) {
        double real = 0, imaginary = 0;
        for (int n = 4800; n < 48000; ++n) {
            const double phase = juce::MathConstants<double>::twoPi * hz * n / 48000;
            real += audio[n] * std::cos(phase); imaginary += audio[n] * std::sin(phase);
        }
        return std::hypot(real, imaginary) * 2 / 43200;
    };
    const auto sub = render("subLevel", 1);
    require(amplitude(sub, 220) > 0.1 && amplitude(plain, 220) < 0.00001,
            "Sub oscillator is not one octave below the note");
    require(std::abs(amplitude(sub, 440) - amplitude(plain, 440)) < 0.00001,
            "Sub level changed the original oscillator level");
    const auto detuned = render("detune", 50);
    for (double hz : {440 * std::exp2(-50.0 / 1200), 440 * std::exp2(50.0 / 1200)})
        require(pitchedSpectrum(detuned, hz) > 0.02 && amplitude(plain, hz) < 0.004,
                "Detune did not add symmetric pitch-shifted audio");
    const auto noisy = render("noiseLevel", 1);
    double energy = 0, correlation = 0;
    for (int n = 4801; n < 48000; ++n) {
        const double noise = noisy[n] - plain[n];
        energy += noise * noise;
        correlation += noise * (noisy[n - 1] - plain[n - 1]);
    }
    require(energy > 100 && std::abs(correlation / energy) < 0.05, "Noise source is not broadband white noise");
    auto estimatePitch = [](const std::vector<float>& audio, int start, int length) {
        int first = -1, last = -1, crossings = 0;
        for (int n = start + 1; n < start + length; ++n) if (audio[n - 1] <= 0 && audio[n] > 0) {
            if (first < 0) first = n;
            last = n; ++crossings;
        }
        return crossings > 1 ? (crossings - 1) * 48000.0 / (last - first) : 0;
    };
    const auto sliding = render("glide", 0.5f, true);
    require(std::abs(estimatePitch(sliding, 2400, 2400) - 220) < 2, "First note should not glide");
    require(estimatePitch(sliding, 14400, 2400) > 230 && estimatePitch(sliding, 14400, 2400) < 260,
            "Glide did not start from the previous note");
    require(std::abs(estimatePitch(sliding, 38400, 4800) - 440) < 2, "Glide did not reach target pitch on time");
    const auto instant = render("glide", 0, true);
    const auto separateChannel = render("glide", 0.5f, true, 2);
    for (const auto* audio : {&instant, &separateChannel})
        require(std::abs(estimatePitch(*audio, 14400, 2400) - 440) < 2, "Glide off or MIDI channel isolation failed");
    // Older states must reset the new controls, even after editing a new sound.
    SineStack p;
    auto old = p.state.copyState();
    for (const auto* id : {"detune", "glide", "subLevel", "noiseLevel"}) {
        old.removeChild(old.getChildWithProperty("id", id), nullptr);
        setParameter(p, id, 1);
    }
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(*old.createXml(), state);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (const auto* id : {"detune", "glide", "subLevel", "noiseLevel"})
        require(p.state.getRawParameterValue(id)->load() == 0, "Old preset enabled new source layers");
}
std::vector<float> renderAnalog(std::initializer_list<std::pair<const char*, float>> settings, int blockSize = 257, double sampleRate = 48000) {
    SineStack p;
    for (const auto& setting : settings) setParameter(p, setting.first, setting.second);
    p.setRateAndBufferSizeDetails(sampleRate, 64); p.prepareToPlay(sampleRate, 64);
    std::vector<float> result;
    const int total = static_cast<int>(sampleRate);
    for (int start = 0; start < total; start += blockSize) {
        const int count = juce::jmin(blockSize, total - start);
        juce::AudioBuffer<float> audio(2, count);
        juce::MidiBuffer midi;
        if (start == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
        p.processBlock(audio, midi);
        for (int n = 0; n < count; ++n) {
            require(std::isfinite(audio.getSample(0, n)) && std::abs(audio.getSample(0, n)) <= 0.98f,
                    "Analog features produced invalid audio");
            result.push_back(audio.getSample(0, n));
        }
    }
    return result;
}
double spectrum(const std::vector<float>& audio, double hz) {
    double real = 0, imaginary = 0, windowSum = 0;
    for (int n = 4800; n < 48000; ++n) {
        const double window = 0.5 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * (n - 4800) / 43199);
        const double phase = juce::MathConstants<double>::twoPi * hz * n / 48000;
        real += window * audio[n] * std::cos(phase); imaginary += window * audio[n] * std::sin(phase); windowSum += window;
    }
    return std::hypot(real, imaginary) * 2 / windowSum;
}
// Spectral audio shifters distribute energy around each sinusoid. Verify a
// narrow +/-12-cent band, rather than requiring oscillator-exact FFT phase.
// Adjacent musical intervals and the dry frequency remain outside this band.
double pitchedSpectrum(const std::vector<float>& audio, double hz) {
    double result = 0;
    for (int cents = -12; cents <= 12; ++cents) result = juce::jmax(result, spectrum(audio, hz * std::exp2(cents / 1200.0)));
    return result;
}
void checkAnalogAudio() {
    const auto dry = renderAnalog({});
    auto difference = [](const std::vector<float>& a, const std::vector<float>& b) {
        double difference = 0;
        for (size_t i = 0; i < a.size(); ++i) difference += std::abs(a[i] - b[i]);
        return difference;
    };
    for (int type = 5; type <= 7; ++type)
        require(difference(dry, renderAnalog({{"fx0type", static_cast<float>(type)}, {"fx0amount", 0}})) == 0,
                "New effect at zero amount leaked wet audio");
    const auto single = renderAnalog({{"unison", 0}, {"detune", 50}});
    require(difference(dry, single) < 0.00001, "Single-voice unison changed the original sound");
    const auto triple = renderAnalog({{"unison", 1}, {"detune", 50}});
    const auto quint = renderAnalog({{"unison", 2}, {"detune", 50}});
    const double innerPitch = 440 * std::exp2(25.0 / 1200);
    require(pitchedSpectrum(quint, innerPitch) > 0.01 && pitchedSpectrum(triple, innerPitch) < 0.001,
            "Five-voice unison did not add the inner pair of voices");
    for (int mode = 1; mode <= 5; ++mode) {
        const auto audio = renderAnalog({{"harmony", static_cast<float>(mode)}, {"harmonyMix", 1}});
        for (int interval : {3, 4, 7}) {
            const bool expected = interval == 3 ? (mode == 1 || mode == 4)
                : interval == 4 ? (mode == 2 || mode == 5) : mode >= 3;
            const double amount = pitchedSpectrum(audio, 440 * std::exp2(interval / 12.0));
            require(expected ? amount > 0.05 : amount < 0.001, "Harmonizer played the wrong third/fifth interval");
        }
    }
    require(difference(dry, renderAnalog({{"harmony", 5}, {"harmonyMix", 0}})) == 0, "Harmony zero mix is not dry");
    const auto lowClosed = renderAnalog({{"fx0type", 5}, {"fx0amount", 1}, {"fx0filterCutoff", 100}, {"fx0filterResonance", 0}});
    const auto lowOpen = renderAnalog({{"fx0type", 5}, {"fx0amount", 1}, {"fx0filterCutoff", 5000}, {"fx0filterResonance", 0}});
    const auto high = renderAnalog({{"fx0type", 5}, {"fx0amount", 1}, {"fx0filterCutoff", 5000}, {"fx0filterMode", 1}, {"fx0filterResonance", 0}});
    require(spectrum(lowClosed, 440) < 0.01 && spectrum(lowOpen, 440) > 0.1 && spectrum(high, 440) < 0.003,
            "Filter low/high-pass response is incorrect");
    const auto band = renderAnalog({{"fx0type", 5}, {"fx0amount", 1}, {"fx0filterCutoff", 440}, {"fx0filterMode", 2}});
    require(spectrum(band, 440) > 0.07, "Band-pass filter did not pass its centre frequency");
    const auto resonant = renderAnalog({{"fx0type", 5}, {"fx0amount", 1}, {"fx0filterCutoff", 440}, {"fx0filterResonance", 1}});
    require(spectrum(resonant, 440) > 0.4, "Filter resonance did not emphasise the cutoff");
    const auto tremolo = renderAnalog({{"fx0type", 6}, {"fx0amount", 1}, {"fx0tremoloRate", 4}, {"fx0tremoloDepth", 0.8f}});
    for (size_t n = 0; n < dry.size(); ++n) {
        const double gain = 1 - 0.4 * (1 + std::sin(juce::MathConstants<double>::twoPi * 4 * n / 48000));
        require(std::abs(tremolo[n] - dry[n] * gain) < 0.000001, "Tremolo rate/depth is incorrect");
    }
    require(difference(dry, renderAnalog({{"fx0type", 6}, {"fx0amount", 1}, {"fx0tremoloDepth", 0}})) == 0,
            "Tremolo at zero depth is not dry");
    const auto phaseSlow = renderAnalog({{"waveform", 2}, {"fx0type", 7}, {"fx0amount", 1}, {"fx0phaserRate", 0.1f}, {"fx0phaserDepth", 1}});
    const auto phaseFast = renderAnalog({{"waveform", 2}, {"fx0type", 7}, {"fx0amount", 1}, {"fx0phaserRate", 3}, {"fx0phaserDepth", 1}});
    const auto phaseStill = renderAnalog({{"waveform", 2}, {"fx0type", 7}, {"fx0amount", 1}, {"fx0phaserDepth", 0}});
    require(difference(phaseSlow, phaseFast) > 10 && difference(phaseSlow, phaseStill) > 10, "Phaser rate/depth did not change audio");
    const auto volumeLfo = renderAnalog({{"lfoTarget", 2}, {"lfoRate", 2}, {"lfoDepth", 1}});
    for (size_t n = 0; n < dry.size(); ++n) {
        const double gain = 0.5 - 0.5 * std::sin(juce::MathConstants<double>::twoPi * 2 * n / 48000);
        require(std::abs(volumeLfo[n] - dry[n] * gain) < 0.000001, "LFO volume route or phase continuity is incorrect");
    }
    const auto smallBlocks = renderAnalog({{"lfoTarget", 2}, {"lfoRate", 2}, {"lfoDepth", 1}}, 64);
    require(difference(volumeLfo, smallBlocks) < 0.001, "LFO depends on host block size");
    const auto pitchLfo = renderAnalog({{"lfoTarget", 1}, {"lfoRate", 2}, {"lfoDepth", 1}});
    auto pitchAt = [](const std::vector<float>& audio, int start) {
        int first = -1, last = -1, crossings = 0;
        for (int n = start; n < start + 1200; ++n) if (audio[n - 1] <= 0 && audio[n] > 0) {
            if (first < 0) first = n;
            last = n; ++crossings;
        }
        return (crossings - 1) * 48000.0 / (last - first);
    };
    require(pitchAt(pitchLfo, 5400) > 480 && pitchAt(pitchLfo, 17400) < 405, "LFO pitch route did not bend both directions");
    const auto cutoffLfo = renderAnalog({{"fx0type", 5}, {"fx0amount", 1}, {"fx0filterCutoff", 440},
                                        {"fx0filterResonance", 0}, {"lfoTarget", 3}, {"lfoRate", 2}, {"lfoDepth", 1}});
    auto energyAt = [](const std::vector<float>& audio, int start) {
        double energy = 0;
        for (int n = start; n < start + 1200; ++n) energy += audio[n] * audio[n];
        return energy;
    };
    require(energyAt(cutoffLfo, 5400) > energyAt(cutoffLfo, 17400) * 10, "LFO did not open/close the filter");
    const auto triangle = renderAnalog({{"lfoTarget", 2}, {"lfoRate", 2}, {"lfoDepth", 1}, {"lfoShape", 1}});
    require(difference(triangle, volumeLfo) > 100, "LFO shape selector had no effect");
    require(difference(dry, renderAnalog({{"lfoTarget", 0}, {"lfoDepth", 1}})) == 0, "LFO Off changed the sound");
    for (const double sr : {44100.0, 96000.0}) for (int mode = 0; mode < 3; ++mode)
        renderAnalog({{"fx0type", 5}, {"fx0amount", 1}, {"fx0filterMode", static_cast<float>(mode)},
                      {"fx0filterCutoff", 20000}, {"fx0filterResonance", 1}, {"lfoTarget", 3}, {"lfoDepth", 1}, {"lfoRate", 20}}, 257, sr);
}
void checkWaveforms() {
    for (int type = 0; type < 3; ++type) {
        SineStack p;
        setParameter(p, "waveform", static_cast<float>(type));
        p.setRateAndBufferSizeDetails(48000, 240); p.prepareToPlay(48000, 240);
        juce::AudioBuffer<float> audio(2, 240);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
        for (int i = 0; i < 10; ++i) { p.processBlock(audio, midi); midi.clear(); }
        std::array<double, 3> real {}, imaginary {};
        for (int block = 0; block < 20; ++block) {
            p.processBlock(audio, midi);
            for (int n = 0; n < 240; ++n) {
                const float sample = audio.getSample(0, n);
                require(std::isfinite(sample) && std::abs(sample) < 0.12f, "Invalid oscillator output");
                for (int harmonic = 1; harmonic <= 3; ++harmonic) {
                    const double phase = juce::MathConstants<double>::twoPi * 440 * harmonic * (block * 240 + n) / 48000;
                    real[harmonic - 1] += sample * std::cos(phase);
                    imaginary[harmonic - 1] += sample * std::sin(phase);
                }
            }
        }
        std::array<double, 3> amplitude {};
        for (int i = 0; i < 3; ++i) amplitude[i] = 2 * std::hypot(real[i], imaginary[i]) / 4800;
        require(amplitude[0] > 0.04, "Missing oscillator fundamental");
        if (type == 0) require(amplitude[1] < 0.0001 && amplitude[2] < 0.0001, "Sine contains unexpected harmonics");
        if (type == 1) require(amplitude[1] < 0.0001 && std::abs(amplitude[2] / amplitude[0] - 1.0 / 3) < 0.02,
                               "Square harmonic spectrum failed");
        if (type == 2) require(std::abs(amplitude[1] / amplitude[0] - 0.5) < 0.02, "Saw harmonic spectrum failed");
        setParameter(p, "waveform", static_cast<float>((type + 1) % 3));
        for (int i = 0; i < 10; ++i) p.processBlock(audio, midi);
        require(audio.getMagnitude(0, 240) > 0.01f, "Waveform change interrupted held note");
        juce::MemoryBlock saved; p.getStateInformation(saved);
        SineStack restored; restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        require(restored.state.getRawParameterValue("waveform")->load() == (type + 1) % 3, "Waveform state restore failed");
        auto legacy = p.state.copyState();
        legacy.removeChild(legacy.getChildWithProperty("id", "waveform"), nullptr);
        juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(), saved);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        require(restored.state.getRawParameterValue("waveform")->load() == 0, "Legacy preset must restore sine");
    }
}
struct GestureCheck : juce::AudioProcessorParameter::Listener {
    int starts = 0, ends = 0;
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool starting) override { if (starting) ++starts; else ++ends; }
};
void checkBypassAudio() {
    for (int type = 1; type <= 7; ++type) {
        SineStack toggled, wet, dry;
        const auto prefix = "fx" + juce::String((type - 1) % 4);
        for (auto* p : {&toggled, &wet}) {
            setParameter(*p, (prefix + "type").toRawUTF8(), static_cast<float>(type));
            setParameter(*p, (prefix + "amount").toRawUTF8(), 0.8f);
        }
        for (auto* p : {&toggled, &wet, &dry}) {
            p->setRateAndBufferSizeDetails(48000, 64); p->prepareToPlay(48000, 64);
        }
        double wetDifference = 0;
        // Variable blocks larger than the prepare hint exercise preallocated scratch chunking.
        for (int block = 0; block < 240; ++block) {
            if (block == 80) setParameter(toggled, (prefix + "bypass").toRawUTF8(), 1);
            if (block == 140) setParameter(toggled, (prefix + "bypass").toRawUTF8(), 0);
            juce::AudioBuffer<float> a(2, 257), b(2, 257), c(2, 257);
            for (auto pair : {std::make_pair(&toggled, &a), std::make_pair(&wet, &b), std::make_pair(&dry, &c)}) {
                juce::MidiBuffer midi;
                if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
                pair.first->processBlock(*pair.second, midi);
            }
            for (int channel = 0; channel < 2; ++channel) for (int n = 0; n < 257; ++n) {
                const float sample = a.getSample(channel, n);
                require(std::isfinite(sample) && std::abs(sample) <= 0.98f, "Invalid bypass transition output");
                if (block >= 82 && block < 140)
                    require(sample == c.getSample(channel, n), "Bypassed FX is not exact dry passthrough");
                if (block < 80 || block >= 142)
                    require(std::abs(sample - b.getSample(channel, n)) < 0.000001f, "Enabling FX lost settings or effect history");
                wetDifference += std::abs(b.getSample(channel, n) - c.getSample(channel, n));
            }
        }
        require(wetDifference > 1, "Bypass test effect was inaudible");
        require(toggled.state.getRawParameterValue(prefix + "type")->load() == type
                && std::abs(toggled.state.getRawParameterValue(prefix + "amount")->load() - 0.8f) < 0.00001f,
                "Bypass changed effect settings");
        // Loading/preparing with bypass already on must start dry, without a wet burst.
        setParameter(toggled, (prefix + "bypass").toRawUTF8(), 1);
        toggled.prepareToPlay(48000, 64); dry.prepareToPlay(48000, 64);
        juce::AudioBuffer<float> a(2, 64), b(2, 64);
        for (auto pair : {std::make_pair(&toggled, &a), std::make_pair(&dry, &b)}) {
            juce::MidiBuffer midi; midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
            pair.first->processBlock(*pair.second, midi);
        }
        for (int n = 0; n < 64; ++n) require(a.getSample(0, n) == b.getSample(0, n), "Initial bypass leaked wet audio");
    }
}
void checkDelayTiming(double sr, float timeMs, float feedback) {
    SineStack effect, dry;
    setParameter(effect, "fx0type", 3); setParameter(effect, "fx0amount", 0.6f);
    setParameter(effect, "fx0delayTime", timeMs); setParameter(effect, "fx0delayFeedback", feedback);
    for (auto* p : {&effect, &dry}) {
        setParameter(*p, "attack", 0.005f); setParameter(*p, "release", 0.02f);
        p->setRateAndBufferSizeDetails(sr, 127); p->prepareToPlay(sr, 127);
    }
    const int delaySamples = juce::roundToInt(sr * timeMs * 0.001);
    const int total = delaySamples * 3 + static_cast<int>(sr * 0.1);
    std::vector<float> source(static_cast<size_t>(total + 127));
    juce::AudioBuffer<float> a(2, 127), b(2, 127);
    double echoEnergy = 0;
    for (int start = 0; start < total; start += 127) {
        for (auto pair : {std::make_pair(&effect, &a), std::make_pair(&dry, &b)}) {
            juce::MidiBuffer midi;
            if (start == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
            const int off = static_cast<int>(sr * 0.01);
            if (off >= start && off < start + 127) midi.addEvent(juce::MidiMessage::noteOff(1, 69), off - start);
            pair.first->processBlock(*pair.second, midi);
        }
        for (int n = 0; n < 127; ++n) {
            const int sample = start + n;
            source[sample] = b.getSample(0, n);
            double expected = source[sample], gain = 0.6 * 0.65;
            for (int previous = sample - delaySamples; previous >= 0; previous -= delaySamples) {
                expected += gain * source[previous]; gain *= feedback;
            }
            require(std::abs(a.getSample(0, n) - expected) < 0.00005, "Delay time or feedback gain is incorrect");
            if (sample >= delaySamples) echoEnergy += std::abs(a.getSample(0, n));
        }
    }
    require(echoEnergy > 1, "Delay timing test produced no echoes");
    require(effect.getTailLengthSeconds() >= timeMs * 0.001, "Reported delay tail is too short");
}
std::array<double, 2> measureReverb(float size, float damping) {
    SineStack p;
    setParameter(p, "waveform", 2); setParameter(p, "attack", 0.005f); setParameter(p, "release", 0.02f);
    setParameter(p, "fx0type", 4); setParameter(p, "fx0amount", 1);
    setParameter(p, "fx0reverbSize", size); setParameter(p, "fx0reverbDamping", damping);
    p.setRateAndBufferSizeDetails(48000, 240); p.prepareToPlay(48000, 240);
    juce::AudioBuffer<float> audio(2, 240);
    double energy = 0, lateEnergy = 0, differenceEnergy = 0;
    float previous = 0;
    for (int block = 0; block < 300; ++block) {
        juce::MidiBuffer midi;
        if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 84, 1.0f), 0);
        if (block == 4) midi.addEvent(juce::MidiMessage::noteOff(1, 84), 0);
        p.processBlock(audio, midi);
        for (int n = 0; n < 240; ++n) {
            const float sample = audio.getSample(0, n);
            require(std::isfinite(sample) && std::abs(sample) < 0.98f, "Unstable reverb output");
            if (block >= 40) {
                energy += sample * sample;
                differenceEnergy += (sample - previous) * (sample - previous);
            }
            if (block >= 150) lateEnergy += sample * sample;
            previous = sample;
        }
    }
    return {lateEnergy, differenceEnergy / juce::jmax(energy, 1.0e-20)};
}
void checkReverbAudio() {
    const auto small = measureReverb(0, 0.5f);
    const auto large = measureReverb(1, 0.5f);
    require(large[0] > small[0] * 2 && large[0] > 0.00001, "Room Size did not extend reverb decay");
    const auto bright = measureReverb(0.72f, 0);
    const auto dark = measureReverb(0.72f, 1);
    require(dark[1] < bright[1] * 0.9, "Damping did not reduce high-frequency tail energy");
}
std::vector<float> renderChorus(float rate, float depth) {
    SineStack p;
    setParameter(p, "fx0type", 2); setParameter(p, "fx0amount", 0.8f);
    setParameter(p, "fx0chorusRate", rate); setParameter(p, "fx0chorusDepth", depth);
    p.setRateAndBufferSizeDetails(48000, 128); p.prepareToPlay(48000, 128);
    juce::AudioBuffer<float> audio(2, 128);
    std::vector<float> result;
    for (int block = 0; block < 750; ++block) {
        juce::MidiBuffer midi;
        if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
        p.processBlock(audio, midi);
        for (int n = 0; n < 128; ++n) {
            const float sample = audio.getSample(0, n);
            require(std::isfinite(sample) && std::abs(sample) < 0.98f, "Unstable chorus output");
            if (block >= 100) result.push_back(sample);
        }
    }
    return result;
}
void checkChorusAudio() {
    const auto zeroSlow = renderChorus(0.05f, 0), zeroFast = renderChorus(10, 0);
    const auto deepSlow = renderChorus(0.05f, 1), deepFast = renderChorus(10, 1);
    double depthDifference = 0, rateDifference = 0;
    for (size_t i = 0; i < zeroSlow.size(); ++i) {
        require(std::abs(zeroSlow[i] - zeroFast[i]) < 0.000001f, "Rate modulates chorus at zero depth");
        depthDifference += std::abs(deepSlow[i] - zeroSlow[i]);
        rateDifference += std::abs(deepSlow[i] - deepFast[i]);
    }
    require(depthDifference > 10 && rateDifference > 10, "Chorus Rate/Depth do not change audio");
}
std::vector<float> renderDrive(float strength, float tone, float mix = 1) {
    SineStack p;
    setParameter(p, "fx0type", 1); setParameter(p, "fx0amount", mix);
    setParameter(p, "fx0driveStrength", strength); setParameter(p, "fx0driveTone", tone);
    p.setRateAndBufferSizeDetails(48000, 240); p.prepareToPlay(48000, 240);
    juce::AudioBuffer<float> audio(2, 240);
    std::vector<float> result;
    for (int block = 0; block < 30; ++block) {
        juce::MidiBuffer midi;
        if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
        p.processBlock(audio, midi);
        for (int n = 0; n < 240; ++n) {
            require(std::isfinite(audio.getSample(0, n)) && std::abs(audio.getSample(0, n)) <= 0.98f, "Unstable Drive output");
            if (block >= 10) result.push_back(audio.getSample(0, n));
        }
    }
    return result;
}
void checkDriveAudio() {
    auto harmonicRatio = [](const std::vector<float>& audio) {
        std::array<double, 2> amplitude {};
        for (int k = 0; k < 2; ++k) {
            double real = 0, imaginary = 0;
            for (size_t n = 0; n < audio.size(); ++n) {
                const double phase = juce::MathConstants<double>::twoPi * 440 * (k == 0 ? 1 : 3) * n / 48000;
                real += audio[n] * std::cos(phase); imaginary += audio[n] * std::sin(phase);
            }
            amplitude[k] = std::hypot(real, imaginary);
        }
        return amplitude[1] / amplitude[0];
    };
    require(harmonicRatio(renderDrive(1, 0.5f)) > harmonicRatio(renderDrive(0, 0.5f)) * 5,
            "Drive Strength did not increase harmonic distortion");
    require(harmonicRatio(renderDrive(1, 1)) > harmonicRatio(renderDrive(1, 0)) * 1.3,
            "Tone did not change brightness");
    const auto neutral = renderDrive(0.7f, 0.5f, 0.7f);
    const auto dry = renderDrive(1, 1, 0);
    for (size_t n = 0; n < neutral.size(); ++n) {
        const double input = 0.16 * std::sin(juce::MathConstants<double>::twoPi * 440 * n / 48000);
        const double legacy = (input + 0.7 * (0.5 * std::tanh(input * (1 + 24 * 0.7)) - input)) * 0.7;
        require(std::abs(neutral[n] - legacy) < 0.00001, "Neutral tone changed original drive sound");
        require(std::abs(dry[n] - input * 0.7) < 0.00001, "Drive Amount zero is not dry");
    }
}
void checkFxOrderAudio() {
    SineStack moved, reference, original;
    for (auto* p : {&moved, &original}) {
        setParameter(*p, "fx0type", 1); setParameter(*p, "fx0amount", 0.7f);
        setParameter(*p, "fx1type", 3); setParameter(*p, "fx1amount", 0.6f);
    }
    moved.moveFx(0, 1);
    setParameter(reference, "fx0type", 3); setParameter(reference, "fx0amount", 0.6f);
    setParameter(reference, "fx1type", 1); setParameter(reference, "fx1amount", 0.7f);
    for (auto* p : {&moved, &reference, &original}) {
        setParameter(*p, "release", 0.02f);
        p->setRateAndBufferSizeDetails(48000, 256); p->prepareToPlay(48000, 256);
    }
    double changedOrderDifference = 0, tailEnergy = 0;
    for (int block = 0; block < 200; ++block) {
        // Moving an empty slot ahead of the chain must preserve existing delay memory.
        if (block == 100) moved.moveFx(3, 0);
        juce::AudioBuffer<float> a(2, 256), b(2, 256), c(2, 256);
        for (auto pair : {std::make_pair(&moved, &a), std::make_pair(&reference, &b), std::make_pair(&original, &c)}) {
            juce::MidiBuffer midi;
            if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
            if (block == 10) midi.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
            pair.first->processBlock(*pair.second, midi);
        }
        for (int n = 0; n < 256; ++n) {
            require(std::abs(a.getSample(0, n) - b.getSample(0, n)) < 0.000001f, "FX processing order or retained tail mismatch");
            changedOrderDifference += std::abs(a.getSample(0, n) - c.getSample(0, n));
            if (block > 100) tailEnergy += std::abs(a.getSample(0, n));
        }
    }
    require(changedOrderDifference > 1 && tailEnergy > 0.1, "FX reorder test did not exercise audible order/tails");
}
void checkGraphDrags(SineStack& processor, juce::AudioProcessorEditor& editor, juce::Component& graph) {
    const std::array<const char*, 4> ids {"attack", "decay", "sustain", "release"};
    const std::array<juce::Point<float>, 4> points {{{65.2f, 24}, {120.4f, 52}, {175.6f, 52}, {230.8f, 80}}};
    auto eventAt = [&](juce::Point<float> position, juce::Point<float> down, bool dragging) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position,
                               juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0,
                               &graph, &graph, juce::Time::getCurrentTime(), down,
                               juce::Time::getCurrentTime(), 1, dragging);
    };
    for (int index = 0; index < 4; ++index) {
        for (const auto* id : ids) setParameter(processor, id, 0.5f);
        auto* parameter = processor.state.getParameter(ids[index]);
        GestureCheck gestures;
        parameter->addListener(&gestures);
        const auto start = points[index];
        graph.mouseDown(eventAt(start, start, false));
        const auto end = start + (index == 2 ? juce::Point<float>{0, -14} : juce::Point<float>{20, 0});
        graph.mouseDrag(eventAt(end, start, true));
        const float expected = index == 2 ? 0.75f : 0.5f + 20.0f / 110.4f;
        require(std::abs(processor.state.getRawParameterValue(ids[index])->load() - expected) < 0.0001f,
                "Graph drag did not update ADSR parameter");
        for (auto* child : editor.getChildren())
            if (auto* knob = dynamic_cast<juce::Slider*>(child))
                if (knob->getName().toLowerCase() == ids[index])
                    require(std::abs(knob->getValue() - expected) < 0.0001, "Graph drag did not sync knob");
        for (int other = 0; other < 4; ++other)
            if (other != index) require(std::abs(processor.state.getRawParameterValue(ids[other])->load() - 0.5f) < 0.0001f,
                                        "Graph drag changed an unrelated ADSR parameter");
        // Drag outside the graph and verify legal parameter bounds are respected.
        graph.mouseDrag(eventAt(start + (index == 2 ? juce::Point<float>{0, -10000} : juce::Point<float>{10000, 0}), start, true));
        require(parameter->getValue() == 1.0f, "Graph drag exceeded maximum");
        graph.mouseDrag(eventAt(start + (index == 2 ? juce::Point<float>{0, 10000} : juce::Point<float>{-10000, 0}), start, true));
        require(parameter->getValue() == 0.0f, "Graph drag exceeded minimum");
        graph.mouseUp(eventAt(end, start, true));
        parameter->removeListener(&gestures);
        require(gestures.starts == 1 && gestures.ends == 1, "Graph automation gesture was not balanced");
    }
    for (const auto* id : ids) setParameter(processor, id, 0.5f);
}
void checkEnvelope(double sampleRate, int blockSize, double noteOff, float sustain) {
    SineStack p;
    setParameter(p, "attack", 0.1f); setParameter(p, "decay", 0.2f);
    setParameter(p, "sustain", sustain); setParameter(p, "release", 0.2f);
    p.setRateAndBufferSizeDetails(sampleRate, blockSize); p.prepareToPlay(sampleRate, blockSize);
    const int offSample = juce::roundToInt(noteOff * sampleRate);
    auto heldLevel = [sustain](double time) {
        if (time < 0.1) return time / 0.1;
        if (time < 0.3) return 1.0 - (1.0 - sustain) * (time - 0.1) / 0.2;
        return static_cast<double>(sustain);
    };
    juce::AudioBuffer<float> audio(2, blockSize);
    for (int start = 0; start < sampleRate * 0.85; start += blockSize) {
        juce::MidiBuffer events;
        if (start == 0) events.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
        if (offSample >= start && offSample < start + blockSize)
            events.addEvent(juce::MidiMessage::noteOff(1, 69), offSample - start);
        p.processBlock(audio, events);
        for (int i = 0; i < blockSize; ++i) {
            const int sample = start + i;
            const double time = (sample + 1) / sampleRate;
            const double envelope = sample < offSample ? heldLevel(time)
                : heldLevel(noteOff) * juce::jmax(0.0, 1.0 - (time - noteOff) / 0.2);
            const double expected = std::sin(sample * 440.0 / sampleRate * juce::MathConstants<double>::twoPi)
                                    * 0.16 * 0.7 * envelope;
            require(std::abs(audio.getSample(0, i) - expected) < 0.0006, "ADSR amplitude/timing mismatch");
        }
    }
}
void checkEnvelopeState() {
    SineStack source;
    setParameter(source, "attack", 0.25f); setParameter(source, "decay", 0.75f);
    setParameter(source, "sustain", 0.35f); setParameter(source, "release", 1.5f);
    juce::MemoryBlock saved; source.getStateInformation(saved);
    SineStack restored; restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    for (const auto* id : {"attack", "decay", "sustain", "release"})
        require(std::abs(source.state.getRawParameterValue(id)->load() - restored.state.getRawParameterValue(id)->load()) < 0.00001f,
                "ADSR state restore failed");
    auto oldState = source.state.copyState();
    oldState.removeChild(oldState.getChildWithProperty("id", "decay"), nullptr);
    oldState.removeChild(oldState.getChildWithProperty("id", "sustain"), nullptr);
    juce::MemoryBlock oldSaved;
    juce::AudioProcessor::copyXmlToBinary(*oldState.createXml(), oldSaved);
    restored.setStateInformation(oldSaved.getData(), static_cast<int>(oldSaved.getSize()));
    require(std::abs(restored.state.getRawParameterValue("decay")->load() - 0.1f) < 0.00001f
            && restored.state.getRawParameterValue("sustain")->load() == 1.0f, "Old preset compatibility failed");
}
#include "ChainChecks.h"
#include "FxDetailChecks.h"
#include "SourceChecks.h"
#include "LfoRoutingChecks.h"
#include "EqChecks.h"
#include "EqGraphChecks.h"
#include "AudioSettingsChecks.h"
#include "MonoChecks.h"
#include "LegatoChecks.h"
#include "OrderedChainChecks.h"
#include "PitchLatencyChecks.h"
#include "ModulationAreaChecks.h"
#include "MultiLfoChecks.h"
#include "TempoAndDeleteChecks.h"
#include "DelaySyncChecks.h"
#include "RhythmChecks.h"
#include "ThemeChecks.h"
#include "FactoryChecks.h"
#include "PresetCategoryChecks.h"
#include "OutputScopeChecks.h"

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    try {
        if (argc == 2 && juce::String(argv[1]) == "--check-output-scope") { checkOutputScope(); return 0; }
        if (argc == 2 && juce::String(argv[1]) == "--check-categories") { checkPresetCategories(); return 0; }
        if (argc == 2 && juce::String(argv[1]) == "--check-factory") { checkFactoryPresets(); return 0; }
        if (argc == 3 && juce::String(argv[1]) == "--export-factory") { checkFactoryPresets(juce::File(argv[2])); return 0; }
        if (argc == 2 && juce::String(argv[1]) == "--check-themes") { checkDesignThemes(); return 0; }
        if (argc == 2 && juce::String(argv[1]) == "--check-source-migration") { checkSourceMigrationAndStress(); return 0; }
        if (argc == 2 && juce::String(argv[1]) == "--check-rhythms") { checkRhythms(); return 0; }
        if (argc == 2 && juce::String(argv[1]) == "--check-delay-sync") { checkDelaySync(); return 0; }
        if (argc == 2 && juce::String(argv[1]) == "--check-tempo-delete") { checkTempoSync(); checkTileDeletion(); return 0; }
        if (argc == 2 && juce::String(argv[1]) == "--check-multi-lfo") { checkMultiLfo(); return 0; }
        if (argc == 2 && juce::String(argv[1]) == "--check-modulation-area") { checkModulationArea(); return 0; }
        if (argc == 2 && juce::String(argv[1]) == "--check-pitch-latency") { checkPitchLatency(); return 0; }
        if (argc == 2 && juce::String(argv[1]) == "--benchmark-ordered") { benchmarkOrderedChain(); return 0; }
        if (argc == 3 && juce::String(argv[1]) == "--dump-fx-defaults") { dumpDefaultFx(juce::File(argv[2])); return 0; }
        if (argc == 3 && juce::String(argv[1]) == "--dump-source-defaults") { dumpSourceDefaults(juce::File(argv[2])); return 0; }
        checkOutputScope();
        checkPresetCategories();
        checkFactoryPresets();
        checkDesignThemes();
        checkWaveforms();
        checkSourceLayers();
        checkAnalogAudio();
        checkFxOrderAudio();
        checkBypassAudio();
        checkDelayTiming(48000, 100, 0);
        checkDelayTiming(48000, 320, 0.5f);
        checkDelayTiming(44100, 2000, 0.95f);
        checkReverbAudio();
        checkChorusAudio();
        checkDriveAudio();
        for (double sampleRate : {44100.0, 48000.0}) {
            for (int blockSize : {64, 257}) {
                checkEnvelope(sampleRate, blockSize, 0.5, 0.25f);
                checkEnvelope(sampleRate, blockSize, 0.05, 0.25f);
                checkEnvelope(sampleRate, blockSize, 0.15, 0.25f);
                checkEnvelope(sampleRate, blockSize, 0.5, 0.0f);
                checkEnvelope(sampleRate, blockSize, 0.05, 0.0f);
            }
        }
        checkEnvelopeState();
        checkChainModel();
        checkChainDuplication();
        checkChainEditor();
        checkDetailedFxAudio();
        checkDetailMigration();
        checkSourceInstances();
        checkSourceInstanceAudio();
        checkSourceEditor();
        checkSourceMigrationAndStress();
        checkParameterRoutingAudio();
        checkRoutingStateAndUi();
        checkRoutingMigrationAndSafety();
        checkEq();
        checkEqGraph();
        checkSampleRates();
        checkMono();
        checkLegato();
        checkOrderedChain();
        checkPitchLatency();
        checkModulationArea();
        checkMultiLfo();
        checkTempoSync(); checkTileDeletion();
        checkDelaySync();
        checkRhythms();
        SineStack p; p.setRateAndBufferSizeDetails(48000, 256); p.prepareToPlay(48000, 256);
        juce::AudioBuffer<float> b(2, 256); juce::MidiBuffer midi;
        p.processBlock(b, midi); require(b.getMagnitude(0, 256) == 0, "Initial silence failed");
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0); p.processBlock(b, midi); midi.clear();
        for (int i = 0; i < 10; ++i) p.processBlock(b, midi);
        require(b.getMagnitude(0, 256) > 0.01f, "MIDI produces no audio");
        for (int type = 1; type <= 7; ++type) {
            for (int slot = 0; slot < 4; ++slot) {
                auto* parameter = p.state.getParameter("fx" + juce::String(slot) + "type");
                parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(type)));
                p.state.getParameter("fx" + juce::String(slot) + "amount")->setValueNotifyingHost(1);
            }
            for (int block = 0; block < 250; ++block) {
                p.processBlock(b, midi);
                for (int c = 0; c < 2; ++c) for (int n = 0; n < 256; ++n)
                    require(std::isfinite(b.getSample(c, n)) && std::abs(b.getSample(c, n)) <= 0.981f, "Invalid FX output");
            }
        }
        juce::MemoryBlock state; p.getStateInformation(state);
        SineStack restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        require(restored.state.getRawParameterValue("fx3type")->load() == 7, "State restore failed");
        for (int slot = 0; slot < 4; ++slot) p.state.getParameter("fx" + juce::String(slot) + "type")->setValueNotifyingHost(0);
        midi.addEvent(juce::MidiMessage::noteOff(1, 69), 0); p.processBlock(b, midi); midi.clear();
        for (int i = 0; i < 1000; ++i) p.processBlock(b, midi);
        require(b.getMagnitude(0, 256) < 0.000001f, "Note release failed");
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        juce::Component* graph = nullptr;
        for (auto* child : editor->getChildren())
            if (child->getName() == "ADSR envelope") graph = child;
        require(graph != nullptr, "Missing ADSR graph");
        auto graphPixelsChanged = [](const juce::Image& before, const juce::Image& after) {
            for (int y = 0; y < before.getHeight(); ++y)
                for (int x = 0; x < before.getWidth(); ++x)
                    if (before.getPixelAt(x, y) != after.getPixelAt(x, y)) return true;
            return false;
        };
        for (auto* child : editor->getChildren()) {
            if (auto* knob = dynamic_cast<juce::Slider*>(child)) {
                const auto id = knob->getName().toLowerCase();
                if (id == "attack" || id == "decay" || id == "sustain" || id == "release") {
                    const auto before = graph->createComponentSnapshot(graph->getLocalBounds());
                    knob->setValue(0.3, juce::sendNotificationSync);
                    require(std::abs(p.state.getRawParameterValue(id)->load() - 0.3f) < 0.0001f, "ADSR knob binding failed");
                    require(graphPixelsChanged(before, graph->createComponentSnapshot(graph->getLocalBounds())),
                            "ADSR graph did not respond to knob");
                    const auto afterKnob = graph->createComponentSnapshot(graph->getLocalBounds());
                    setParameter(p, id.toRawUTF8(), 0.5f);
                    require(graphPixelsChanged(afterKnob, graph->createComponentSnapshot(graph->getLocalBounds())),
                            "ADSR graph did not respond to host parameter");
                }
            }
        }
        checkGraphDrags(p, *editor, *graph);
        checkPresetFiles(p, *editor);
        checkPresetBrowser(p, *editor);
        auto preview = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream previewFile(juce::File::getCurrentWorkingDirectory().getChildFile("preview.png"));
        previewFile.setPosition(0); previewFile.truncate();
        juce::PNGImageFormat().writeImageToStream(preview, previewFile);
        std::cout << "PASS: sine/square/saw spectra, waveform switching and state, ADSR, graph editing, MIDI, FX\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
