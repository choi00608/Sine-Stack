void setSourceParameter(SineStack& p, int id, const char* setting, float value) {
    setParameter(p, p.sourceParameter(id, setting).toRawUTF8(), value);
}
void checkSourceInstances() {
    SineStack p;
    std::array<int, 7> originals, copies;
    for (int kind = 0; kind < 7; ++kind) {
        const int original = kind == 0 ? SineStack::oscillatorBlock : p.addBlock(100 + kind);
        originals[kind] = original;
        for (const auto& d : SourceSettings::definitions) if (d.kind == kind) {
            auto* parameter = p.state.getParameter(p.sourceParameter(original, d.id));
            parameter->setValueNotifyingHost(0.72f);
        }
        setParameter(p, p.blockBypassParameter(original).toRawUTF8(), 1);
        const int copy = p.duplicateBlock(original); copies[kind] = copy;
        require(copy >= 200 && p.blockKind(copy) == 100 + kind, "Source duplicate lost its kind");
        auto chain = p.getChain();
        int position = -1; for (int n = 0; n < chain.size; ++n) if (chain.ids[n] == original) position = n;
        require(position >= 0 && chain.ids[position + 1] == copy, "Source copy is not adjacent to the original");
        require(p.state.getRawParameterValue(p.blockBypassParameter(copy))->load() == 1, "Source duplicate lost bypass state");
        for (const auto& d : SourceSettings::definitions) if (d.kind == kind) {
            auto* from = p.state.getParameter(p.sourceParameter(original, d.id));
            auto* to = p.state.getParameter(p.sourceParameter(copy, d.id));
            require(from->getValue() == to->getValue(), "Source copy lost a setting");
            const float unchanged = from->getValue(); to->setValueNotifyingHost(0.13f);
            require(from->getValue() == unchanged, "Source instances share a parameter");
        }
        const int added = p.addBlock(100 + kind);
        require(added != original && added != copy && p.blockKind(added) == 100 + kind, "Repeated Add did not create an independent source");
        p.removeBlock(added);
    }
    struct File { juce::File path = juce::File::getCurrentWorkingDirectory().getChildFile("sources-" + juce::Uuid().toString() + ".sinestack"); ~File() { path.deleteFile(); } } file;
    require(p.savePreset(file.path).wasOk(), "Could not save source instances");
    const auto json = juce::JSON::parse(file.path.loadFileAsString());
    require(file.path.loadFileAsString().contains("synth0"), "Preset does not expose readable instance IDs");
    SineStack restored; require(restored.loadPreset(file.path).wasOk(), "Could not load source instances");
    require(p.getChain().ids == restored.getChain().ids && p.getChain().size == restored.getChain().size, "Preset lost source identities or order");
    for (int kind = 0; kind < 7; ++kind) for (const auto& d : SourceSettings::definitions) if (d.kind == kind)
        require(std::abs(p.state.getParameter(p.sourceParameter(copies[kind], d.id))->getValue()
            - restored.state.getParameter(restored.sourceParameter(copies[kind], d.id))->getValue()) < 0.00001f, "Preset lost an independent source setting");
    // Missing instance data must not silently turn a saved LFO into an oscillator.
    auto invalid = juce::JSON::parse(file.path.loadFileAsString());
    auto* parameters = invalid["parameters"].getArray();
    const auto missing = "synth" + juce::String(copies[6] - 200) + "_kind";
    for (int n = parameters->size(); --n >= 0;) if (parameters->getReference(n)["id"].toString() == missing) parameters->remove(n);
    file.path.replaceWithText(juce::JSON::toString(invalid));
    const auto unchanged = restored.state.copyState().toXmlString();
    require(restored.loadPreset(file.path).failed() && restored.state.copyState().toXmlString() == unchanged, "Incomplete source preset changed the sound");
    const int removed = copies[1]; p.removeBlock(removed);
    const int reused = p.addBlock(SineStack::unisonBlock);
    require(reused == removed && p.state.getRawParameterValue(p.sourceParameter(reused, "detune"))->load() == 10,
            "Reused synthesis slot inherited deleted settings");
    while (p.getChain().size < 32) require(p.addBlock(SineStack::lfoBlock) >= 0, "Could not fill a chain with synthesis blocks");
    require(p.addBlock(SineStack::lfoBlock) == -1 && p.duplicateBlock(originals[1]) == -1, "Source instances exceeded the chain limit");
}
std::vector<float> sourceRender(SineStack& p, bool twoNotes = false) {
    p.setRateAndBufferSizeDetails(48000, 128); p.prepareToPlay(48000, 128);
    std::vector<float> result;
    for (int block = 0; block < 300; ++block) {
        juce::AudioBuffer<float> audio(2, 128); juce::MidiBuffer midi;
        if (block == 0 || (twoNotes && block == 100)) midi.addEvent(juce::MidiMessage::noteOn(1, block == 0 ? 57 : 69, 0.7f), 0);
        if (block == 80 && twoNotes) midi.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
        if (block == 200) midi.addEvent(juce::MidiMessage::noteOff(1, twoNotes ? 69 : 57), 0);
        p.processBlock(audio, midi);
        for (int n = 0; n < 128; ++n) for (int c = 0; c < 2; ++c) {
            const float sample = audio.getSample(c, n);
            require(std::isfinite(sample) && std::abs(sample) <= 0.98f, "Source instances produced invalid audio");
            result.push_back(sample);
        }
    }
    return result;
}
void checkSourceInstanceAudio() {
    const std::array<const char*, 7> changed {"oscTune", "detune", "harmonyShift", "glide", "subFine", "noiseColour", "lfoRate"};
    const std::array<float, 7> values {7, 37, -12, 0.6f, 35, -0.8f, 7};
    for (int kind = 0; kind < 7; ++kind) {
        SineStack p;
        setParameter(p, "release", 0.02f);
        const int original = kind == 0 ? 100 : p.addBlock(100 + kind);
        if (kind == 6) setSourceParameter(p, original, "lfoTarget", 2);
        const auto before = sourceRender(p, kind == 3);
        const int copy = p.duplicateBlock(original);
        setSourceParameter(p, copy, changed[kind], values[kind]);
        const auto layered = sourceRender(p, kind == 3);
        require(detailDifference(before, layered) > 0.1, "An independent source instance did not change audio");
        setParameter(p, p.blockBypassParameter(copy).toRawUTF8(), 1);
        requireSameAudio(before, sourceRender(p, kind == 3), "Bypassing a source copy changed the original");
        setParameter(p, p.blockBypassParameter(copy).toRawUTF8(), 0);
        p.moveBlock(p.getChain().size - 1, 0);
        const auto moved = sourceRender(p, kind == 3);
        if (kind == 0 || kind == 4 || kind == 5 || kind == 6)
            requireSameAudio(layered, moved, "Adding independent sources changed with summation order");
        else require(detailDifference(layered, moved) > .1, "Moving an audio block before the source did not change processing");
        juce::MemoryBlock state; p.getStateInformation(state);
        SineStack restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        requireSameAudio(moved, sourceRender(restored, kind == 3), "Source instances failed to restore audio");
        p.removeBlock(copy);
        requireSameAudio(before, sourceRender(p, kind == 3), "Removing a source copy changed the original");
    }
    // Every new control has an audible purpose. Noise colour frequency needs a coloured signal.
    for (const auto& d : SourceSettings::definitions) if (!d.legacy) {
        SineStack p;
        const int id = d.kind == 0 ? 100 : p.addBlock(100 + d.kind);
        if (d.kind == 2) setSourceParameter(p, id, "harmony", 5);
        if (d.kind == 5) setSourceParameter(p, id, "noiseColour", -0.8f);
        if (d.kind == 6) {
            setSourceParameter(p, id, "lfoTarget", juce::String(d.id) == "lfoFilterRange" ? 3 : 1);
            if (juce::String(d.id) == "lfoFilterRange") p.addBlock(5);
        }
        const auto before = sourceRender(p, d.kind == 3);
        setSourceParameter(p, id, d.id, d.initial == d.high ? d.low : d.high);
        // Phase 360 is deliberately identical to zero; test a quarter cycle.
        if (juce::String(d.id) == "lfoPhase") setSourceParameter(p, id, d.id, 90);
        const auto after = sourceRender(p, d.kind == 3);
        if (detailDifference(before, after) <= 0.05) std::cerr << "Inactive source setting: " << d.id << '\n';
        require(detailDifference(before, after) > 0.05, "A new synthesis setting did not affect audio");
    }
    // Simultaneous LFOs target separate routes and keep separate rates/phases.
    SineStack p; const int first = p.addBlock(SineStack::lfoBlock), second = p.addBlock(SineStack::lfoBlock);
    setSourceParameter(p, first, "lfoTarget", 1); setSourceParameter(p, first, "lfoRate", 3);
    setSourceParameter(p, second, "lfoTarget", 3); setSourceParameter(p, second, "lfoRate", 6);
    p.addBlock(5);
    const auto both = sourceRender(p);
    setParameter(p, p.blockBypassParameter(second).toRawUTF8(), 1);
    require(detailDifference(both, sourceRender(p)) > 1, "LFO copies did not modulate independently");
}
void checkSourceEditor() {
    SineStack p;
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto* duplicate = dynamic_cast<juce::TextButton*>(findControl(*editor, "Duplicate selected block"));
    const auto adsrBounds = findControl(*editor, "ADSR envelope")->getBounds();
    for (int kind = 0; kind < 7; ++kind) {
        const int original = kind == 0 ? 100 : p.addBlock(100 + kind);
        if (kind == 6) setSourceParameter(p, original, "lfoTarget", 1);
        p.sendSynchronousChangeMessage();
        auto* tile = findControl(*editor, (kind == 6 ? "Modulation block " : "Chain block ") + juce::String(original));
        tile->mouseDown(chainMouse(*tile, {20, 20})); tile->mouseUp(chainMouse(*tile, {20, 20}));
        require(duplicate->isEnabled(), "Source duplicate button is disabled");
        duplicate->onClick();
        const auto chain = p.getChain(); int copy = -1;
        for (int n = 0; n + 1 < chain.size; ++n) if (chain.ids[n] == original) copy = chain.ids[n + 1];
        require(copy >= 200, "UI did not duplicate the selected source");
        for (const auto& d : SourceSettings::definitions) if (d.kind == kind) {
            auto* parameter = p.state.getParameter(p.sourceParameter(copy, d.id));
            auto* control = findControl(*editor, parameter->getName(100));
            require(control && control->isVisible() && juce::Rectangle<int>(361,104,631,316).contains(editor->getLocalArea(control, control->getLocalBounds())), "Source control is missing or outside the inspector");
            const float originalValue = p.state.getParameter(p.sourceParameter(original, d.id))->getValue();
            if (auto* knob = dynamic_cast<juce::Slider*>(control)) {
                knob->setValue(d.high, juce::sendNotificationSync);
                require(std::abs(parameter->convertFrom0to1(parameter->getValue()) - d.high) < 0.01f, "Source knob is not attached");
                parameter->setValueNotifyingHost(parameter->convertTo0to1(d.initial));
                require(std::abs(knob->getValue() - d.initial) < 0.01, "Source automation did not update its own knob");
            } else if (auto* choice = dynamic_cast<juce::ComboBox*>(control)) {
                choice->setSelectedItemIndex(static_cast<int>(d.high), juce::sendNotificationSync);
                require(parameter->convertFrom0to1(parameter->getValue()) == d.high, "Source choice is not attached");
            }
            require(p.state.getParameter(p.sourceParameter(original, d.id))->getValue() == originalValue, "Copy inspector edited original source");
        }
        auto* bypass = dynamic_cast<juce::TextButton*>(findControl(*editor, (kind == 6 ? "Modulation bypass " : "Chain bypass ") + juce::String(copy)));
        bypass->setToggleState(true, juce::sendNotificationSync);
        auto* inspectorBypass = dynamic_cast<juce::TextButton*>(findControl(*editor, "Selected block bypass"));
        require(inspectorBypass->getToggleState() && p.state.getRawParameterValue(p.blockBypassParameter(original))->load() == 0,
                "Source copy bypass affected the original or failed to sync");
        bypass->setToggleState(false, juce::sendNotificationSync);
        require(findControl(*editor, "ADSR envelope")->getBounds() == adsrBounds, "Source controls moved the ADSR");
        for (int n = 0; n < 12; ++n) { juce::Timer::callPendingTimersSynchronously(); juce::Thread::sleep(20); }
        juce::FileOutputStream out(juce::File::getCurrentWorkingDirectory().getChildFile("source-details-" + juce::String(kind) + ".png"));
        out.setPosition(0); out.truncate(); juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1), out);
    }
}
void checkSourceMigrationAndStress() {
    SineStack reference;
    for (int kind = 101; kind <= 106; ++kind) reference.addBlock(kind);
    setParameter(reference, "detune", 22); setParameter(reference, "harmony", 5);
    setParameter(reference, "lfoTarget", 1); setParameter(reference, "lfoShape", 1);
    auto old = reference.state.copyState();
    for (int n = old.getNumChildren(); --n >= 0;) {
        const auto id = old.getChild(n).getProperty("id").toString();
        if (reference.state.getParameter(id)->getVersionHint() == 14) old.removeChild(n, nullptr);
    }
    juce::MemoryBlock state; juce::AudioProcessor::copyXmlToBinary(*old.createXml(), state);
    SineStack migrated;
    for (auto* parameter : migrated.getParameters()) if (parameter->getVersionHint() == 14) parameter->setValueNotifyingHost(0.8f);
    migrated.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (auto* parameter : migrated.getParameters()) if (parameter->getVersionHint() == 14)
        require(std::abs(parameter->getValue() - parameter->getDefaultValue()) < 0.000001f, "Old state retained edited source details");
    for (const auto& child : reference.state.copyState()) {
        const auto id = child.getProperty("id").toString();
        require(std::abs(reference.state.getParameter(id)->getValue() - migrated.state.getParameter(id)->getValue()) < 0.000001f,
                "Old synthesis preset changed a parameter after migration");
    }
    // Skewed parameters make a normalised round trip during state restoration.
    // On x86, lfoRate differs by one float ULP (1.19e-7 Hz); the serial pitch
    // processors amplify this into a peak sample delta of ~2.1e-5. Keep a
    // bounded -86 dBFS tolerance here, with the strict parameter check above.
    requireSameAudio(sourceRender(reference, true), sourceRender(migrated, true),
                     "Old synthesis preset changed sound after migration", 0.00005f);
    SineStack sine; const int first = sine.addBlock(SineStack::lfoBlock); setSourceParameter(sine, first, "lfoTarget", 1);
    const auto baseline = sourceRender(sine);
    for (int shape = 2; shape <= 4; ++shape) {
        setSourceParameter(sine, first, "lfoShape", static_cast<float>(shape));
        require(detailDifference(baseline, sourceRender(sine)) > 1, "New LFO waveform had no audible effect");
    }
    // Exercise all eight voices while editing a full synthesis chain in real time.
    for (double rate : {44100.0, 96000.0}) {
        SineStack p;
        while (p.getChain().size < 32) p.addBlock(100 + p.getChain().size % 7);
        p.setRateAndBufferSizeDetails(rate, 64); p.prepareToPlay(rate, 64);
        for (int block = 0; block < 36; ++block) {
            if (block == 8 || block == 16) {
                const auto chain = p.getChain();
                for (int n = 0; n < chain.size; ++n) {
                    const int id = chain.ids[n], kind = p.blockKind(id) - 100;
                    for (const auto& d : SourceSettings::definitions) if (d.kind == kind)
                        setSourceParameter(p, id, d.id, block == 8 ? d.high : d.low);
                }
            }
            if (block == 20) {
                const int removed = p.getChain().ids[7]; p.removeBlock(removed); p.addBlock(SineStack::lfoBlock);
                p.moveBlock(31, 1);
            }
            if (block == 25) {
                const auto chain = p.getChain(); for (int n = 0; n < chain.size; ++n)
                    setParameter(p, p.blockBypassParameter(chain.ids[n]).toRawUTF8(), 1);
            }
            juce::AudioBuffer<float> audio(2, 257); juce::MidiBuffer midi;
            if (block == 0) for (int note = 48; note < 56; ++note) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.2f), note - 48);
            if (block == 18) for (int note = 48; note < 56; ++note) {
                midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                midi.addEvent(juce::MidiMessage::noteOn(1, note + 12, 0.2f), 0);
            }
            p.processBlock(audio, midi);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < audio.getNumSamples(); ++n)
                require(std::isfinite(audio.getSample(c, n)) && std::abs(audio.getSample(c, n)) <= 0.98f, "Unstable source instance automation");
            if (block == 35) require(audio.getMagnitude(0, audio.getNumSamples()) == 0, "Bypassed synthesis chain leaked sound");
        }
    }
}
