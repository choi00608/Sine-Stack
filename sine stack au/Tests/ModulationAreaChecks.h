void checkModulationArea() {
    SineStack p;
    const int delay = p.addBlock(3);
    const auto base = sourceRender(p);
    const int lfo = p.addBlock(SineStack::lfoBlock);
    require(p.getAudioChain().size == 2 && p.getModulators().size == 1 && p.getModulators().ids[0] == lfo,
            "An LFO was included in the audio-chain view");
    requireSameAudio(base, sourceRender(p), "A newly added, unassigned LFO changed the sound");
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto sync = [&] { p.sendSynchronousChangeMessage(); };
    const auto envelopeBounds = findControl(*editor, "ADSR envelope")->getBounds();
    auto* tile = findControl(*editor, "Modulation block " + juce::String(lfo));
    require(tile && tile->getParentComponent()->getName() == "Modulation contents"
            && findControl(*editor, "Chain block " + juce::String(lfo)) == nullptr,
            "LFO tile is not confined to the separate MODULATION area");
    auto* audioView = findControl(*editor, "FX chain viewport");
    auto* modView = dynamic_cast<juce::Viewport*>(findControl(*editor, "Modulation viewport"));
    require(modView && modView->getY() > audioView->getBottom(), "MODULATION area is not below the audio chain");
    tile->mouseDown(chainMouse(*tile, {20, 20})); tile->mouseUp(chainMouse(*tile, {20, 20}));
    auto target = [&] { return dynamic_cast<juce::ComboBox*>(findControl(*editor, "LFO Target")); };
    require(target() && target()->isItemEnabled(1000 + delay) && target()->isItemEnabled(1100)
            && !target()->isItemEnabled(1000 + lfo), "Destination picker retained position restrictions or allows self-modulation");
    target()->setSelectedId(1000 + delay, juce::sendNotificationSync); sync();
    auto* parameter = dynamic_cast<juce::ComboBox*>(findControl(*editor, "LFO destination parameter"));
    parameter->setSelectedId(p.modulationParameterIndex("delayTime", false) + 1, juce::sendNotificationSync); sync();
    const auto assigned = sourceRender(p);
    require(detailDifference(base, assigned) > 1, "LFO did not modulate a previously earlier block");
    // Old preset membership order is retained for IDs, but no longer routes LFOs.
    const auto initial = p.getChain();
    p.moveBlock(initial.size - 1, 0); sync();
    requireSameAudio(assigned, sourceRender(p), "Changing legacy LFO position changed its control route");
    auto* routeDisplay = dynamic_cast<juce::Label*>(findControl(*editor, "Signal chain order"));
    require(routeDisplay && !routeDisplay->getText().contains("LFO"), "Audio path display includes a control source");
    auto* bypass = dynamic_cast<juce::TextButton*>(findControl(*editor, "Modulation bypass " + juce::String(lfo)));
    require(bypass != nullptr, "Modulation tile has no bypass");
    bypass->setToggleState(true, juce::sendNotificationSync); sync();
    requireSameAudio(base, sourceRender(p), "LFO bypass did not restore the target's saved base value");
    bypass->setToggleState(false, juce::sendNotificationSync); sync();
    auto* duplicate = dynamic_cast<juce::TextButton*>(findControl(*editor, "Duplicate selected block"));
    duplicate->onClick(); sync();
    const auto mods = p.getModulators(); require(mods.size == 2 && p.getAudioChain().size == 2, "Duplicating an LFO added an audio block");
    const int copy = mods.ids[1];
    require(p.state.getRawParameterValue(p.sourceParameter(copy, "lfoBlock"))->load() == delay
            && p.state.getRawParameterValue(p.sourceParameter(copy, "lfoParameter"))->load() == p.modulationParameterIndex("delayTime", false),
            "LFO duplication lost its routing settings");
    p.moveAudioBlock(1, 0); sync();
    require(p.getModulators().ids == mods.ids && p.getAudioChain().ids[0] == delay,
            "Audio reorder moved or discarded a modulator");
    p.moveAudioBlock(0, 1); sync();
    require(p.state.getRawParameterValue(p.sourceParameter(copy, "lfoBlock"))->load() == delay, "Audio reorder reassigned an LFO target");
    const auto both = sourceRender(p);
    juce::MemoryBlock data; p.getStateInformation(data);
    SineStack restored; restored.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    require(restored.getModulators().ids == mods.ids && restored.getAudioChain().ids == p.getAudioChain().ids,
            "Host state did not separate legacy LFO IDs into the modulation area");
    requireSameAudio(both, sourceRender(restored), "Restored modulation routing changed the sound");
    struct Preset { juce::File file = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("mod-area", ".sinestack"); ~Preset() { file.deleteFile(); } } preset;
    require(p.savePreset(preset.file).wasOk() && restored.loadPreset(preset.file).wasOk(), "Modulation preset round-trip failed");
    requireSameAudio(both, sourceRender(restored), "Modulation preset lost sound or connections");
    // Loading a prior version-2 chain with an interleaved LFO works unchanged.
    auto old = p.state.copyState(); old.setProperty("chain", "oscillator,lfo,fx0,synth0", nullptr);
    juce::AudioProcessor::copyXmlToBinary(*old.createXml(), data);
    restored.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    require(restored.getAudioChain().size == 2 && restored.getModulators().size == 2, "Interleaved old LFOs were not separated");
    requireSameAudio(both, sourceRender(restored), "Old LFO position changed migrated routing");
    // Add button and horizontal overflow belong to the modulation area alone.
    auto* add = dynamic_cast<juce::TextButton*>(findControl(*editor, "Add LFO"));
    require(add && add->getY() > audioView->getBottom(), "Missing separate Add LFO button");
    for (int n = 0; n < 6; ++n) { add->onClick(); sync(); }
    require(p.getAudioChain().size == 2 && modView->getViewedComponent()->getWidth() > modView->getWidth(),
            "Adding LFOs changed audio order or failed to extend modulation scrolling");
    require(modView->getViewPositionX() > 0, "Adding an LFO did not reveal it in the modulation row");
    require(envelopeBounds == findControl(*editor, "ADSR envelope")->getBounds(), "Separating modulators moved the fixed ADSR");
    // Review a connected LFO rather than an unassigned one.
    tile = findControl(*editor, "Modulation block " + juce::String(lfo));
    modView->setViewPosition(0, 0); tile->mouseDown(chainMouse(*tile, {20,20})); tile->mouseUp(chainMouse(*tile, {20,20}));
    juce::FileOutputStream preview(juce::File::getCurrentWorkingDirectory().getChildFile("modulation-area-preview.png"));
    preview.setPosition(0); preview.truncate();
    juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), preview);
    p.removeBlock(delay); const int reused = p.addBlock(3);
    require(reused == delay && p.state.getRawParameterValue(p.sourceParameter(lfo, "lfoBlock"))->load() == -1
            && p.state.getRawParameterValue(p.sourceParameter(copy, "lfoBlock"))->load() == -1,
            "Deleting/reusing a target retained stale modulation links");

    // A constant source-pitch LFO is sample-identical to tuning the oscillator,
    // including the note onset. There is no 12/48 ms audio-processing stage.
    SineStack pitch, tuned;
    for (auto* synth : {&pitch, &tuned}) setupMono(*synth, .02f);
    const int pitchLfo = pitch.addBlock(106);
    setSourceParameter(pitch, pitchLfo, "lfoTarget", 1); setSourceParameter(pitch, pitchLfo, "lfoDepth", 1);
    setSourceParameter(pitch, pitchLfo, "lfoPitchRange", 12); setSourceParameter(pitch, pitchLfo, "lfoPhase", 90);
    setSourceParameter(pitch, pitchLfo, "lfoOffset", 1); setSourceParameter(pitch, pitchLfo, "lfoRate", 1);
    setParameter(tuned, "oscTune", 12);
    juce::MidiBuffer note; note.addEvent(juce::MidiMessage::noteOn(1, 57, .8f), 73);
    const auto expected = renderMonoMidi(tuned, note, 48000, 257, .1);
    for (int quality : {0, 1}) {
        setParameter(pitch, "pitchLatency", static_cast<float>(quality));
        requireSameAudio(expected, renderMonoMidi(pitch, note, 48000, 257, .1), "Source pitch LFO still uses audio pitch-shifter latency");
    }
    require(pitch.getTailLengthSeconds() == tuned.getTailLengthSeconds(), "LFO added a spectral tail to host rendering");
    // Self-routes from automation or an older file are inert; another LFO may
    // target this LFO using the previous control frame, independent of position.
    setSourceParameter(pitch, pitchLfo, "lfoTarget", 0); const auto unmodulated = sourceRender(pitch);
    assignLfo(pitch, pitchLfo, pitchLfo, "lfoRate", 1);
    requireSameAudio(unmodulated, sourceRender(pitch), "A self-assigned LFO changed its own output");
    std::cout << "PASS: separate MODULATION area, unrestricted target position, duplicates/bypass, scrolling, old presets, source-pitch latency and deletion safety\n";
}
