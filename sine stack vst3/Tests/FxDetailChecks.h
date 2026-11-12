// Offline stereo renders for detailed FX checks and comparison with earlier builds.
std::vector<float> renderDetailedFx(SineStack& p, double rate = 48000, int blockSize = 128) {
    p.setRateAndBufferSizeDetails(rate, blockSize); p.prepareToPlay(rate, blockSize);
    juce::AudioBuffer<float> audio(2, blockSize);
    std::vector<float> result;
    const int total = static_cast<int>(rate * 1.2);
    for (int start = 0; start < total; start += blockSize) {
        juce::MidiBuffer midi;
        if (start == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
        const int off = static_cast<int>(rate * 0.3);
        if (off >= start && off < start + blockSize) midi.addEvent(juce::MidiMessage::noteOff(1, 57), off - start);
        p.processBlock(audio, midi);
        for (int n = 0; n < std::min(blockSize, total - start); ++n) for (int c = 0; c < 2; ++c) {
            const float sample = audio.getSample(c, n);
            require(std::isfinite(sample) && std::abs(sample) <= 0.98f, "Detailed FX render produced invalid samples");
            result.push_back(sample);
        }
    }
    return result;
}
void setupDetailedFx(SineStack& p, int type) {
    setParameter(p, "waveform", 2); setParameter(p, "attack", 0.005f); setParameter(p, "release", 0.02f);
    setParameter(p, "fx0type", static_cast<float>(type)); setParameter(p, "fx0amount", 0.8f);
}
void dumpDefaultFx(const juce::File& file) {
    juce::FileOutputStream out(file); require(out.openedOk(), "Could not open reference render");
    out.setPosition(0); out.truncate();
    for (double rate : {44100.0, 48000.0}) for (int type = 1; type <= 7; ++type) {
        SineStack p; setupDetailedFx(p, type);
        for (float sample : renderDetailedFx(p, rate)) out.writeFloat(sample);
    }
}
double detailDifference(const std::vector<float>& a, const std::vector<float>& b) {
    require(a.size() == b.size(), "Audio length mismatch");
    double difference = 0;
    for (size_t n = 0; n < a.size(); ++n) difference += std::abs(a[n] - b[n]);
    return difference;
}
void checkDetailedFxAudio() {
    for (int type = 1; type <= 7; ++type) {
        SineStack original; setupDetailedFx(original, type);
        const auto baseline = renderDetailedFx(original);
        for (const auto& detail : FxDetails::definitions) if (detail.effect == type) {
            const auto id = "fx0" + juce::String(detail.suffix);
            std::vector<float> values {detail.minimum, detail.maximum};
            if (*detail.choices != '\0') for (int choice = 1; choice < static_cast<int>(detail.maximum); ++choice) values.push_back(static_cast<float>(choice));
            for (float changed : values) if (changed != detail.initial) {
                SineStack modified; setupDetailedFx(modified, type); setParameter(modified, id.toRawUTF8(), changed);
                const auto audio = renderDetailedFx(modified);
                const double difference = detailDifference(baseline, audio);
                if (difference <= 0.1) std::cerr << "Inaudible parameter: " << detail.suffix << " difference=" << difference << '\n';
                require(difference > 0.1, "A new effect parameter did not change audio");
            }
        }
        SineStack quieter; setupDetailedFx(quieter, type); setParameter(quieter, "fx0outputGain", -9);
        const auto quiet = renderDetailedFx(quieter);
        const float gain = juce::Decibels::decibelsToGain(-9.0f);
        for (size_t n = 0; n < quiet.size(); ++n)
            require(std::abs(quiet[n] - baseline[n] * gain) < 0.000001f, "Per-FX output gain is not applied correctly");
        SineStack dry, bypassed;
        setupDetailedFx(dry, 0); setupDetailedFx(bypassed, type); setParameter(bypassed, "fx0bypass", 1);
        for (const auto& detail : FxDetails::definitions) if (detail.effect == type || detail.effect == 0)
            setParameter(bypassed, ("fx0" + juce::String(detail.suffix)).toRawUTF8(), detail.maximum);
        requireSameAudio(renderDetailedFx(dry), renderDetailedFx(bypassed), "New FX controls altered bypassed audio");
    }
    // Stereo controls must have stereo consequences even for a mono oscillator.
    for (int type : {3, 4, 6}) {
        SineStack p; setupDetailedFx(p, type);
        if (type == 3) setParameter(p, "fx0delayMode", 1);
        if (type == 4) setParameter(p, "fx0reverbWidth", 0);
        if (type == 6) setParameter(p, "fx0tremoloPhase", 180);
        const auto audio = renderDetailedFx(p);
        double side = 0;
        for (size_t n = 0; n < audio.size(); n += 2) side += std::abs(audio[n] - audio[n + 1]);
        require(type == 4 ? side < 0.01 : side > 1, "Stereo FX control did not change channel relationship");
    }
    for (int type : {2, 7}) {
        SineStack p; setupDetailedFx(p, type);
        const auto initialTail = p.getTailLengthSeconds();
        setParameter(p, type == 2 ? "fx0chorusFeedback" : "fx0phaserFeedback", -0.9f);
        require(p.getTailLengthSeconds() > initialTail, "Strong feedback did not extend the reported tail");
    }
    // Pre-delay delays the room, never the dry attack.
    SineStack noPre, withPre; setupDetailedFx(noPre, 4); setupDetailedFx(withPre, 4);
    setParameter(withPre, "fx0reverbPreDelay", 200);
    const auto a = renderDetailedFx(noPre), b = renderDetailedFx(withPre);
    for (size_t n = 0; n < 1000; ++n) require(a[n] == b[n], "Reverb pre-delay shifted the dry attack");
    require(withPre.getTailLengthSeconds() >= noPre.getTailLengthSeconds() + 0.199, "Pre-delay is missing from tail length");
    // Filters and feedback remain bounded at both parameter endpoints, including
    // automation and host buffers larger than the prepared scratch buffers.
    for (double rate : {44100.0, 96000.0}) for (int type = 1; type <= 7; ++type) {
        SineStack p; setupDetailedFx(p, type);
        p.setRateAndBufferSizeDetails(rate, 64); p.prepareToPlay(rate, 64);
        for (int block = 0; block < 45; ++block) {
            if (block % 9 == 0) {
                for (const auto& detail : FxDetails::definitions) if (detail.effect == type || detail.effect == 0)
                    setParameter(p, ("fx0" + juce::String(detail.suffix)).toRawUTF8(), block % 18 == 0 ? detail.maximum : detail.minimum);
                setParameter(p, "fx0filterResonance", 1); setParameter(p, "fx0filterCutoff", 20000);
            }
            juce::AudioBuffer<float> audio(2, 257); juce::MidiBuffer midi;
            if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
            p.processBlock(audio, midi);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < audio.getNumSamples(); ++n)
                require(std::isfinite(audio.getSample(c, n)) && std::abs(audio.getSample(c, n)) <= 0.98f, "Unstable detailed FX automation");
        }
    }
}
void checkDetailMigration() {
    SineStack modern;
    for (int type = 1; type <= 7; ++type) modern.addBlock(type);
    auto old = modern.state.copyState();
    for (int n = old.getNumChildren(); --n >= 0;) {
        const auto id = old.getChild(n).getProperty("id").toString();
        if (modern.state.getParameter(id)->getVersionHint() == 13) old.removeChild(n, nullptr);
    }
    juce::MemoryBlock data; juce::AudioProcessor::copyXmlToBinary(*old.createXml(), data);
    SineStack restored;
    // Loading old state must reset details that were previously edited.
    for (auto* parameter : restored.getParameters()) if (parameter->getVersionHint() == 13) parameter->setValueNotifyingHost(0.87f);
    restored.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    for (auto* parameter : restored.getParameters()) if (parameter->getVersionHint() == 13)
        require(std::abs(parameter->getValue() - parameter->getDefaultValue()) < 0.000001f, "Old state retained edited FX details");
    requireSameAudio(renderDetailedFx(modern), renderDetailedFx(restored), "Old state did not restore the original sound");
    std::unique_ptr<juce::AudioProcessorEditor> editor(restored.createEditor());
    for (int type = 1; type <= 7; ++type) {
        const int id = type - 1;
        auto* tile = findControl(*editor, "Chain block " + juce::String(id));
        tile->mouseDown(chainMouse(*tile, {20, 20})); tile->mouseUp(chainMouse(*tile, {20, 20}));
        for (const auto& detail : FxDetails::definitions) if (detail.effect == type || detail.effect == 0) {
            auto* parameter = restored.state.getParameter("fx" + juce::String(id) + detail.suffix);
            auto* control = findControl(*editor, parameter->getName(100));
            require(control && control->isVisible() && juce::Rectangle<int>(361, 104, 631, 316).contains(control->getBounds()),
                    "New effect control is missing or outside the inspector");
            if (auto* knob = dynamic_cast<juce::Slider*>(control)) {
                knob->setValue(detail.maximum, juce::sendNotificationSync);
                require(std::abs(parameter->convertFrom0to1(parameter->getValue()) - detail.maximum) < 0.01f, "New knob is not attached");
                require(std::abs(knob->getValueFromText(knob->getTextFromValue(detail.maximum)) - detail.maximum) < 0.01, "New knob unit does not round-trip");
                parameter->setValueNotifyingHost(parameter->convertTo0to1(detail.initial));
                require(std::abs(knob->getValue() - detail.initial) < 0.01, "Host automation did not update the knob");
            } else if (auto* choice = dynamic_cast<juce::ComboBox*>(control)) {
                choice->setSelectedItemIndex(static_cast<int>(detail.maximum), juce::sendNotificationSync);
                require(parameter->convertFrom0to1(parameter->getValue()) == detail.maximum, "New choice is not attached");
            } else require(false, "Unexpected detailed FX control");
        }
        juce::FileOutputStream out(juce::File::getCurrentWorkingDirectory().getChildFile("fx-details-" + juce::String(type) + ".png"));
        out.setPosition(0); out.truncate();
        juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1), out);
    }
}

void dumpSourceDefaults(const juce::File& file) {
    juce::FileOutputStream out(file); require(out.openedOk(), "Could not open source reference"); out.setPosition(0); out.truncate();
    for (int test = 0; test < 12; ++test) {
        SineStack p; setupDetailedFx(p, test == 10 ? 5 : 0);
        if (test < 2) { setParameter(p, "detune", 20); setParameter(p, "unison", test + 1); }
        if (test >= 2 && test < 7) setParameter(p, "harmony", test - 1);
        if (test == 7) { setParameter(p, "subLevel", 0.4f); setParameter(p, "noiseLevel", 0.1f); }
        if (test >= 8 && test < 11) setParameter(p, "lfoTarget", test - 7);
        if (test == 11) { setParameter(p, "lfoTarget", 1); setParameter(p, "lfoShape", 1); }
        for (float sample : renderDetailedFx(p)) out.writeFloat(sample);
    }
}
