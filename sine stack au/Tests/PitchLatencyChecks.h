void checkPitchLatency() {
    // Measure the emitted impulse, not just the latency reported by the DSP.
    // Four serial wet processors must accumulate the measured stage latency.
    for (double rate : {44100.0, 48000.0, 96000.0}) for (int mode : {0, 1}) {
        std::array<AudioPitchShift, 4> stages;
        for (auto& stage : stages) stage.prepare(rate, mode);
        const int perStage = stages[0].latencySamples(mode);
        require(std::abs(perStage / rate - (mode == 0 ? .012 : .048)) < .0002,
                "Pitch latency does not match selected mode");
        const int impulseAt = static_cast<int>(rate * .06), total = static_cast<int>(rate * .4);
        float peak = 0, stray = 0; int peakAt = -1;
        for (int n = 0; n < total; ++n) {
            std::array<float, 2> sample {n == impulseAt ? .7f : 0, n == impulseAt ? -.3f : 0};
            for (auto& stage : stages) sample = stage.tick(sample, 0);
            if (std::abs(sample[0]) > peak) { peak = std::abs(sample[0]); peakAt = n; }
            if (n != impulseAt + 4 * perStage) stray = juce::jmax(stray, std::abs(sample[0]));
            require(std::abs(sample[1] + sample[0] * (3.0f / 7)) < .00001f, "Latency mode damaged stereo coherence");
        }
        if (!(peakAt - impulseAt == 4 * perStage && peak > .699f && stray < .001f))
            std::cerr << "Impulse: rate=" << rate << " mode=" << mode << " expected=" << 4*perStage << " actual=" << peakAt-impulseAt << " peak=" << peak << " stray=" << stray << "\n";
        // Four spectral passes may leave sub -60 dBFS phase/history residuals.
        // The impulse peak must still have exactly the predicted sample delay.
        require(peakAt - impulseAt == 4 * perStage && peak > .699f && stray < .001f,
                "Measured serial pitch latency or impulse reconstruction is incorrect");
        if (rate == 48000) std::cout << "LATENCY: " << (mode == 0 ? "Live" : "Detail") << " " << perStage
            << " samples/stage, four stages " << 4000 * perStage / rate << " ms\n";
    }
    // Detail is the previous implementation, sample-for-sample, including pitch.
    AudioPitchShift detail; detail.prepare(48000, 1);
    signalsmith::stretch::SignalsmithStretch<float> legacy {0x51a7};
    legacy.configure(2, 2304, 288, false); legacy.reset(); legacy.setTransposeSemitones(7);
    for (int n = 0; n < 18000; ++n) {
        const float input = .3f * std::sin(n * 220.0 * juce::MathConstants<double>::twoPi / 48000);
        float left = 0, right = 0; const float* in[] {&input, &input}; float* out[] {&left, &right};
        legacy.process(in, 1, out, 1); const auto actual = detail.tick({input, input}, 7);
        require(std::abs(left - actual[0]) < 1.0e-6f && std::abs(right - actual[1]) < 1.0e-6f,
                "Detail mode changed the previous pitch processing");
    }
    // A rapid return request during warming must eventually settle without a
    // reset/dropout in the currently audible processor or a hard output jump.
    AudioPitchShift changing, reference; changing.prepare(48000); reference.prepare(48000);
    float previous = 0;
    for (int n = 0; n < 24000; ++n) {
        if (n == 4000) changing.requestMode(1);
        if (n == 6000) changing.requestMode(0);
        const float input = .3f * std::sin(n * 440.0 * juce::MathConstants<double>::twoPi / 48000);
        const float value = changing.tick({input, input}, 7)[0], original = reference.tick({input, input}, 7)[0];
        require(std::isfinite(value) && std::abs(value) < 1 && std::abs(value - previous) < .12f,
                "Changing pitch latency produced unstable or discontinuous audio");
        if (n < 8500) require(std::abs(value - original) < 1.0e-6f, "Mode change reset the audible processor before its replacement was ready");
        previous = value;
    }
    require(!changing.isTransitioning(), "Rapid mode changes never settled");
    // The global control applies to every kind of pitch-processing block.
    for (int kind : {101, 102, 103}) {
        SineStack p; setupMono(p, .02f); const int id = p.addBlock(kind);
        const auto live = sourceRender(p, true);
        setParameter(p, "pitchLatency", 1); const auto precise = sourceRender(p, true);
        require(detailDifference(live, precise) > 1, "A pitch-processing block ignored latency selection");
        setParameter(p, p.blockBypassParameter(id).toRawUTF8(), 1);
        const auto bypass = sourceRender(p, true); setParameter(p, "pitchLatency", 0);
        requireSameAudio(bypass, sourceRender(p, true), "Pitch latency mode delayed bypassed audio");
    }
    SineStack p; p.addBlock(102);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto* choice = dynamic_cast<juce::ComboBox*>(findControl(*editor, "Pitch Latency"));
    require(choice && choice->getSelectedId() == 1, "Live latency is not the initial UI mode");
    const auto adsr = findControl(*editor, "ADSR envelope")->getBounds();
    choice->setSelectedId(2, juce::sendNotificationSync);
    require(p.state.getRawParameterValue("pitchLatency")->load() == 1, "Pitch latency selector did not update DSP parameter");
    const auto audio = sourceRender(p);
    juce::MemoryBlock state; p.getStateInformation(state);
    SineStack restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    requireSameAudio(audio, sourceRender(restored), "Host state lost pitch latency selection");
    struct TemporaryPreset { juce::File file = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("pitch-latency", ".sinestack"); ~TemporaryPreset() { file.deleteFile(); } } preset;
    const auto saved = p.savePreset(preset.file);
    require(saved.wasOk(), ("Latency preset save: " + saved.getErrorMessage()).toRawUTF8());
    const auto loaded = restored.loadPreset(preset.file);
    require(loaded.wasOk(), ("Latency preset load: " + loaded.getErrorMessage()).toRawUTF8());
    require(restored.state.getRawParameterValue("pitchLatency")->load() == 1, "Preset did not restore Detail mode");
    requireSameAudio(audio, sourceRender(restored), "Preset changed pitch latency sound");
    auto old = p.state.copyState(); old.removeChild(old.getChildWithProperty("id", "pitchLatency"), nullptr);
    juce::AudioProcessor::copyXmlToBinary(*old.createXml(), state);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(p.state.getRawParameterValue("pitchLatency")->load() == 0, "Old state inherited a previous latency selection");
    require(choice->getSelectedId() == 1, "Host restore did not update latency selector");
    require(adsr == findControl(*editor, "ADSR envelope")->getBounds(), "Latency setting moved the fixed ADSR");
    juce::FileOutputStream preview(juce::File::getCurrentWorkingDirectory().getChildFile("pitch-latency-preview.png"));
    preview.setPosition(0); preview.truncate();
    juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), preview);
    std::cout << "PASS: measured 12/48 ms latency, serial accumulation, Detail compatibility, mode crossfade, bypass, UI and presets\n";
}
