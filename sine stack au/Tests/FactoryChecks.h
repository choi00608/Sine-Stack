#include "../Source/FactoryPresets.h"
juce::AudioBuffer<float> renderFactory(SineStack& p, int index, double rate, bool stress = false) {
    TempoTestPlayHead clock; clock.position.setBpm(120); clock.position.setIsPlaying(true);
    p.setPlayHead(&clock); p.setRateAndBufferSizeDetails(rate, 256); p.prepareToPlay(rate, 256);
    const auto& preset = FactoryPresets::presets[index];
    const int total = static_cast<int>(rate * (stress ? 4 : 8));
    juce::AudioBuffer<float> output(2, total); output.clear();
    juce::MidiBuffer events;
    const bool chord = preset.chord || stress;
    const int voices = stress ? 8 : chord ? 4 : 1;
    const std::array<int, 8> intervals {0, 4, 7, 11, 12, 16, 19, 23};
    for (int phrase = 0; phrase < (chord ? 1 : 4); ++phrase) {
        const double start = phrase * .5;
        const double hold = chord ? 3 : index == 2 ? .54 : .37;
        const int root = preset.demoNote + (phrase == 2 ? 7 : phrase == 3 ? 10 : 0);
        for (int v = 0; v < voices; ++v) {
            const int note = juce::jmin(108, root + intervals[v]);
            events.addEvent(juce::MidiMessage::noteOn(1, note, stress ? 1.0f : .82f), juce::roundToInt(start * rate));
            events.addEvent(juce::MidiMessage::noteOff(1, note), juce::roundToInt((start + hold) * rate));
        }
    }
    for (int offset = 0; offset < total; offset += 256) {
        const int count = juce::jmin(256, total - offset);
        clock.position.setPpqPosition(offset / rate * 2);
        juce::AudioBuffer<float> audio(2, count); juce::MidiBuffer midi;
        midi.addEvents(events, offset, count, -offset); p.processBlock(audio, midi);
        for (int channel = 0; channel < 2; ++channel) output.copyFrom(channel, offset, audio, channel, 0, count);
    }
    p.releaseResources(); p.setPlayHead(nullptr);
    return output;
}
void checkFactoryPresets(const juce::File& exportFolder = {}) {
    struct Folder {
        juce::File path = juce::File::getCurrentWorkingDirectory().getChildFile("factory-check-" + juce::Uuid().toString());
        ~Folder() { path.deleteRecursively(); }
    } temporary;
    require(temporary.path.createDirectory().wasOk(), "Could not create factory test folder");
    const bool exporting = exportFolder != juce::File{};
    const auto folder = exporting ? exportFolder : temporary.path;
    require(folder.createDirectory().wasOk(), "Could not create factory export folder");
    if (exporting) require(folder.getChildFile("Audio Previews").createDirectory().wasOk(), "Could not create audio preview folder");
    bool hasHeadroom = true;
    for (int index = 0; index < FactoryPresets::count; ++index) {
        SineStack p, clean;
        for (auto* parameter : p.getParameters()) {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            // Respect discrete/bool steps when constructing a heavily edited patch.
            parameter->setValueNotifyingHost(ranged->convertTo0to1(ranged->convertFrom0to1(.81f)));
        }
        require(p.loadFactoryPreset(index).wasOk() && clean.loadFactoryPreset(index).wasOk(), "Factory recipe failed validation");
        require(p.getChain().ids == clean.getChain().ids && p.getChain().size == clean.getChain().size, "Factory inherited a previous chain");
        for (auto* parameter : p.getParameters()) {
            const auto id = dynamic_cast<juce::RangedAudioParameter*>(parameter)->paramID;
            const float difference = std::abs(parameter->getValue() - clean.state.getParameter(id)->getValue());
            if (difference >= .000001f) std::cerr << "Factory " << index << " parameter " << id << ": dirty=" << parameter->getValue()
                << " clean=" << clean.state.getParameter(id)->getValue() << " delta=" << difference << std::endl;
            require(difference < .000001f, "Factory inherited previous hidden parameters/routes");
        }
        require(p.getFactoryPresetIndex() == index && p.getPresetFile() == juce::File{}, "Factory identity is not portable");
        const auto before = p.state.copyState().toXmlString();
        require(p.loadFactoryPreset(-1).failed() && p.loadFactoryPreset(10).failed() && before == p.state.copyState().toXmlString(), "Invalid factory selection mutated sound");
        juce::MemoryBlock state; p.getStateInformation(state);
        clean.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        require(clean.getFactoryPresetIndex() == index, "DAW state lost factory selection");
        for (double rate : {44100.0, 48000.0}) {
            auto audio = renderFactory(p, index, rate);
            float peak = 0; double squares = 0;
            for (int channel = 0; channel < 2; ++channel) for (int n = 0; n < audio.getNumSamples(); ++n) {
                const float value = audio.getSample(channel, n);
                require(std::isfinite(value), "Factory sound produced non-finite audio");
                if (std::abs(value) >= .97f) hasHeadroom = false;
                peak = juce::jmax(peak, std::abs(value)); squares += value * value;
            }
            const double rms = std::sqrt(squares / (2 * audio.getNumSamples()));
            std::cout << FactoryPresets::presets[index].name << " / " << rate << " Hz: peak=" << peak << ", RMS=" << rms << std::endl;
            require(peak > .015f && rms > .002, "Factory sound is silent or too quiet");
            if (exporting && rate == 48000) {
                audio.applyGainRamp(0, juce::roundToInt(rate * .008), 0, 1);
                const int fade = juce::roundToInt(rate * .25);
                audio.applyGainRamp(audio.getNumSamples() - fade, fade, 1, 0);
                const auto file = folder.getChildFile("Audio Previews").getChildFile(juce::String(FactoryPresets::presets[index].name) + ".wav");
                auto stream = std::make_unique<juce::FileOutputStream>(file); require(stream->openedOk(), "Preview file unavailable");
                stream->setPosition(0); stream->truncate();
                juce::WavAudioFormat format;
                std::unique_ptr<juce::OutputStream> outputStream(std::move(stream));
                auto writer = format.createWriterFor(outputStream, juce::AudioFormatWriterOptions{}
                    .withSampleRate(rate).withNumChannels(2).withBitsPerSample(24));
                require(writer != nullptr, "Could not create preview writer");
                require(writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()), "Could not write preview audio");
            }
        }
        const float level = p.state.getRawParameterValue("level")->load();
        // Measure unclipped peaks below the final limiter, then compensate the
        // linear output gain. This exposes overloads instead of hiding them at .98.
        setParameter(p, "level", level * .1f);
        const auto stress = renderFactory(p, index, 48000, true);
        setParameter(p, "level", level);
        for (int c = 0; c < 2; ++c) for (int n = 0; n < stress.getNumSamples(); ++n)
            require(std::isfinite(stress.getSample(c, n)), "Factory polyphony produced non-finite audio");
        const float stressPeak = stress.getMagnitude(0, stress.getNumSamples()) * 10;
        std::cout << FactoryPresets::presets[index].name << " / 8-note full-velocity peak=" << stressPeak << std::endl;
        if (stressPeak >= .9f) hasHeadroom = false;
        const auto file = folder.getChildFile(juce::String(FactoryPresets::presets[index].name) + ".sinestack");
        require(p.savePreset(file).wasOk() && p.getFactoryPresetIndex() == -1, "Saving factory edits did not create a User preset");
        require(clean.loadPreset(file).wasOk() && clean.getFactoryPresetIndex() == -1 && clean.getChain().ids == p.getChain().ids,
                "Factory export is not compatible with the user preset format");
        for (auto* parameter : p.getParameters()) {
            const auto id = dynamic_cast<juce::RangedAudioParameter*>(parameter)->paramID;
            require(std::abs(parameter->getValue() - clean.state.getParameter(id)->getValue()) < .00001f, "Factory export lost parameters");
        }
    }
    require(hasHeadroom, "Factory bank needs output-level adjustment for polyphony headroom");
    SineStack p;
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto* bank = dynamic_cast<juce::ComboBox*>(findControl(*editor, "Preset bank"));
    auto* list = dynamic_cast<juce::ComboBox*>(findControl(*editor, "Preset list"));
    auto* previous = dynamic_cast<juce::TextButton*>(findControl(*editor, "Previous preset"));
    auto* next = dynamic_cast<juce::TextButton*>(findControl(*editor, "Next preset"));
    require(bank && list && previous && next && bank->getSelectedId() == 1 && list->getNumItems() == 10, "Fresh installation has no factory bank");
    const auto unchanged = p.state.copyState().toXmlString();
    bank->setSelectedId(2, juce::sendNotificationSync); bank->setSelectedId(1, juce::sendNotificationSync);
    require(p.state.copyState().toXmlString() == unchanged, "Browsing banks changed the sound");
    next->onClick(); require(p.getFactoryPresetIndex() == 0 && list->getSelectedId() == 1, "Factory Next failed");
    previous->onClick(); require(p.getFactoryPresetIndex() == 9 && list->getSelectedId() == 10, "Factory Previous wrap failed");
    list->setSelectedId(6, juce::sendNotificationSync); require(p.getFactoryPresetIndex() == 5, "Factory menu selection failed");
    setParameter(p, "attack", .35f);
    editor.reset(); editor.reset(p.createEditor());
    list = dynamic_cast<juce::ComboBox*>(findControl(*editor, "Preset list"));
    require(list->getSelectedId() == 6 && std::abs(p.state.getRawParameterValue("attack")->load() - .35f) < .00001f,
            "Reopening the factory browser lost edits/selection");
    const auto saved = temporary.path.getChildFile("My Afterglow.sinestack");
    require(p.savePreset(saved).wasOk(), "Could not save an edited factory preset");
    dynamic_cast<juce::TextButton*>(findControl(*editor, "Refresh presets"))->onClick();
    bank = dynamic_cast<juce::ComboBox*>(findControl(*editor, "Preset bank"));
    require(bank->getSelectedId() == 2 && list->getText() == "My Afterglow", "Saving did not show the user copy");
    bank->setSelectedId(1, juce::sendNotificationSync); list->setSelectedId(6, juce::sendNotificationSync);
    require(std::abs(p.state.getRawParameterValue("attack")->load() - .7f) < .00001f, "User edits overwrote the factory original");
    auto* active = findControl(*editor, "Chain bypass 100");
    require(active && active->findColour(juce::TextButton::buttonColourId)
            == editor->getLookAndFeel().findColour(juce::Slider::rotarySliderFillColourId),
            "New factory-chain tiles did not inherit the selected design");
    const auto image = editor->createComponentSnapshot(editor->getLocalBounds());
    juce::FileOutputStream png(juce::File::getCurrentWorkingDirectory().getChildFile("factory-browser.png"));
    png.setPosition(0); png.truncate(); juce::PNGImageFormat().writeImageToStream(image, png);
    std::cout << "PASS: 10 embedded factory sounds, 44.1/48 kHz rendering, polyphony headroom, clean reset, host state, exports, fresh browser/navigation and editable user copies\n";
}
