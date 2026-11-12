void assignLfo(SineStack& p, int lfo, int target, const char* setting, float range = 0.25f) {
    const int parameter = p.modulationParameterIndex(setting, target >= 100);
    require(parameter > 0 && p.canModulate(target, parameter), "Unsupported modulation test target");
    p.materialiseChain();
    setSourceParameter(p, lfo, "lfoBlock", static_cast<float>(target));
    setSourceParameter(p, lfo, "lfoParameter", static_cast<float>(parameter));
    setSourceParameter(p, lfo, "lfoRange", range);
    setSourceParameter(p, lfo, "lfoTarget", 4);
}
void checkParameterRoutingAudio() {
    const std::array<const char*, 7> fields {"driveTone", "chorusRate", "delayTime", "reverbSize", "filterCutoff", "tremoloRate", "phaserCentre"};
    for (int type = 1; type <= 7; ++type) {
        SineStack p; setupDetailedFx(p, type);
        const int lfo = p.addBlock(SineStack::lfoBlock);
        setSourceParameter(p, lfo, "lfoTarget", 0);
        const auto dry = renderDetailedFx(p);
        assignLfo(p, lfo, 0, fields[type - 1]);
        if (type == 3) require(p.getTailLengthSeconds() >= 2.0, "Modulated delay time was omitted from the host tail estimate");
        const auto state = p.state.copyState().toXmlString();
        const auto moving = renderDetailedFx(p);
        require(detailDifference(dry, moving) > 0.1, "An FX target did not receive LFO modulation");
        require(state == p.state.copyState().toXmlString(), "LFO wrote its output into saved parameters");
        setSourceParameter(p, lfo, "lfoRange", 0);
        const auto zeroRange = renderDetailedFx(p);
        requireSameAudio(dry, zeroRange, "Zero Range did not restore the base sound");
        setSourceParameter(p, lfo, "lfoRange", 0.25f);
        setParameter(p, p.blockBypassParameter(lfo).toRawUTF8(), 1);
        requireSameAudio(dry, renderDetailedFx(p), "Bypassed LFO still modulated the target");
        setParameter(p, p.blockBypassParameter(lfo).toRawUTF8(), 0);
        setSourceParameter(p, lfo, "lfoRange", -0.25f);
        require(detailDifference(moving, renderDetailedFx(p)) > 0.1, "Negative Range did not reverse modulation");
    }
    SineStack p;
    const int unison = p.addBlock(SineStack::unisonBlock), copy = p.duplicateBlock(unison);
    const int lfo = p.addBlock(SineStack::lfoBlock);
    setSourceParameter(p, lfo, "lfoTarget", 0);
    const auto baseline = sourceRender(p);
    assignLfo(p, lfo, copy, "detune", 0.4f);
    const auto modulated = sourceRender(p);
    require(detailDifference(baseline, modulated) > 1, "Unison Detune did not receive modulation");
    require(p.state.getRawParameterValue("detune")->load() == 10 && p.state.getRawParameterValue(p.sourceParameter(copy, "detune"))->load() == 10,
            "Routing changed the source knob baseline");
    setParameter(p, p.blockBypassParameter(copy).toRawUTF8(), 1);
    const auto copyOff = sourceRender(p);
    setSourceParameter(p, lfo, "lfoTarget", 0);
    requireSameAudio(copyOff, sourceRender(p), "LFO assigned to a bypassed copy leaked into the original");
    // Equal, opposite routes to the same parameter cancel before conversion/clamping.
    setParameter(p, p.blockBypassParameter(copy).toRawUTF8(), 0);
    assignLfo(p, lfo, copy, "detune", 0.4f);
    const int opposing = p.duplicateBlock(lfo);
    setSourceParameter(p, opposing, "lfoRange", -0.4f);
    requireSameAudio(baseline, sourceRender(p), "Multiple routes were not summed before clamping");
    // A second LFO can drive the first LFO's rate without recursive evaluation.
    assignLfo(p, opposing, lfo, "lfoRate", 0.3f);
    const auto nested = sourceRender(p);
    require(detailDifference(modulated, nested) > 1, "LFO-to-LFO rate modulation had no effect");
    const auto nestedChain = p.getChain(); int lfoPosition = -1;
    for (int i = 0; i < nestedChain.size; ++i) if (nestedChain.ids[i] == opposing) lfoPosition = i;
    p.moveBlock(lfoPosition, nestedChain.size - 1);
    requireSameAudio(nested, sourceRender(p), "LFO-to-LFO connection depended on registry position");
    require(p.state.getRawParameterValue(p.sourceParameter(opposing, "lfoBlock"))->load() == lfo,
            "Moving an LFO discarded its saved destination");
}
void checkRoutingStateAndUi() {
    SineStack p;
    const int delay = p.addBlock(3), second = p.addBlock(3), unison = p.addBlock(SineStack::unisonBlock);
    const int lfo = p.addBlock(SineStack::lfoBlock);
    p.moveBlock(p.getChain().size - 1, 1);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto* tile = findControl(*editor, "Modulation block " + juce::String(lfo));
    tile->mouseDown(chainMouse(*tile, {20,20})); tile->mouseUp(chainMouse(*tile, {20,20}));
    auto sync = [&] { p.sendSynchronousChangeMessage(); };
    auto target = [&] { return dynamic_cast<juce::ComboBox*>(findControl(*editor, p.state.getParameter(p.sourceParameter(lfo, "lfoTarget"))->getName(100))); };
    auto parameter = [&] { return dynamic_cast<juce::ComboBox*>(findControl(*editor, "LFO destination parameter")); };
    require(target() && parameter(), "LFO routing selectors are missing");
    target()->setSelectedId(1000 + second, juce::sendNotificationSync); sync();
    const int time = p.modulationParameterIndex("delayTime", false);
    parameter()->setSelectedId(time + 1, juce::sendNotificationSync); sync();
    require(p.state.getRawParameterValue(p.sourceParameter(lfo, "lfoBlock"))->load() == second
            && p.state.getRawParameterValue(p.sourceParameter(lfo, "lfoParameter"))->load() == time
            && p.state.getRawParameterValue(p.sourceParameter(lfo, "lfoTarget"))->load() == 4,
            "Routing UI did not select the requested delay instance and time");
    require(findControl(*editor, p.state.getParameter(p.sourceParameter(lfo, "lfoRange"))->getName(100)) != nullptr,
            "Parameter routing has no Range knob");
    const auto graphBounds = findControl(*editor, "ADSR envelope")->getBounds();
    p.moveBlock(3, 0); sync();
    require(parameter()->isEnabled(), "Moving the target disabled a position-independent route");
    require(target()->getSelectedId() == 1000 + second && parameter()->getSelectedId() == time + 1,
            "Reordering reassigned the LFO by position");
    require(target()->getText().startsWith("01 / Delay"), "Target label did not update its chain position");
    auto savePreview = [&](const char* name) {
        for (int n = 0; n < 12; ++n) { juce::Timer::callPendingTimersSynchronously(); juce::Thread::sleep(20); }
        juce::FileOutputStream out(juce::File::getCurrentWorkingDirectory().getChildFile(name)); out.setPosition(0); out.truncate();
        juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1), out);
    };
    savePreview("lfo-delay-routing.png");
    target()->setSelectedId(1000 + unison, juce::sendNotificationSync); sync();
    const int detune = p.modulationParameterIndex("detune", true);
    parameter()->setSelectedId(detune + 1, juce::sendNotificationSync); sync();
    require(parameter()->getSelectedId() == detune + 1 && !p.canModulate(unison, time), "Routing parameter choices did not follow block type");
    require(!p.canModulate(unison, p.modulationParameterIndex("unison", true)), "A discrete voice-count selector was offered as a continuous target");
    require(findControl(*editor, "ADSR envelope")->getBounds() == graphBounds, "Routing inspector moved ADSR");
    savePreview("lfo-unison-routing.png");
    const int lfoCopy = p.duplicateBlock(lfo);
    require(p.state.getRawParameterValue(p.sourceParameter(lfoCopy, "lfoBlock"))->load() == unison
            && p.state.getRawParameterValue(p.sourceParameter(lfoCopy, "lfoParameter"))->load() == detune,
            "Duplicating an LFO lost its assignment");
    struct File { juce::File path = juce::File::getCurrentWorkingDirectory().getChildFile("route-" + juce::Uuid().toString() + ".sinestack"); ~File(){path.deleteFile();} } file;
    require(p.savePreset(file.path).wasOk(), "Could not save routed preset");
    SineStack restored; require(restored.loadPreset(file.path).wasOk(), "Could not load routed preset");
    for (const auto& child : p.state.copyState()) {
        const auto id = child.getProperty("id").toString();
        require(std::abs(p.state.getParameter(id)->getValue() - restored.state.getParameter(id)->getValue()) < 0.000001f,
                "Restoring LFO routing changed a parameter");
    }
    // Skewed parameter round trips can differ by one float ULP on x86.
    requireSameAudio(sourceRender(p), sourceRender(restored), "Preset did not restore LFO routing sound", 0.00001f);
    juce::MemoryBlock state; p.getStateInformation(state); restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (const auto& child : p.state.copyState()) {
        const auto id = child.getProperty("id").toString();
        require(std::abs(p.state.getParameter(id)->getValue() - restored.state.getParameter(id)->getValue()) < 0.000001f,
                "Restoring LFO routing changed a parameter");
    }
    // Skewed parameter round trips can differ by one float ULP on x86.
    requireSameAudio(sourceRender(p), sourceRender(restored), "Host state did not restore LFO routing sound", 0.00001f);
    // Deletion clears both copies' links; recycling the target slot leaves them disconnected.
    p.removeBlock(unison); const int recycled = p.addBlock(SineStack::unisonBlock); sync();
    require(recycled == unison && p.state.getRawParameterValue(p.sourceParameter(lfo, "lfoBlock"))->load() == -1
            && p.state.getRawParameterValue(p.sourceParameter(lfoCopy, "lfoBlock"))->load() == -1,
            "Recycled source inherited incoming LFO links");
    assignLfo(p, lfo, delay, "delayTime");
    setParameter(p, ("fx" + juce::String(delay) + "type").toRawUTF8(), 1); sync();
    require(!p.canModulate(delay, time), "Changing effect type kept an incompatible assignment active");
}
void checkRoutingMigrationAndSafety() {
    SineStack p; const int effect = p.addBlock(3), lfo = p.addBlock(SineStack::lfoBlock);
    setSourceParameter(p, lfo, "lfoTarget", 1);
    auto old = p.state.copyState();
    for (int n = old.getNumChildren(); --n >= 0;) if (p.state.getParameter(old.getChild(n).getProperty("id").toString())->getVersionHint() == 15) old.removeChild(n, nullptr);
    juce::MemoryBlock data; juce::AudioProcessor::copyXmlToBinary(*old.createXml(), data);
    const auto oldSound = sourceRender(p);
    assignLfo(p, lfo, effect, "delayTime");
    p.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    require(p.state.getRawParameterValue("lfoBlock")->load() == -1 && p.state.getRawParameterValue("lfoParameter")->load() == 0,
            "Old preset inherited a previous routing assignment");
    requireSameAudio(oldSound, sourceRender(p), "Old global LFO preset changed sound");
    for (double rate : {44100.0, 96000.0}) {
        SineStack synth;
        const int delay = synth.addBlock(3), unison = synth.addBlock(SineStack::unisonBlock);
        const int first = synth.addBlock(SineStack::lfoBlock), second = synth.addBlock(SineStack::lfoBlock);
        assignLfo(synth, first, delay, "delayTime", 1); assignLfo(synth, second, unison, "detune", -1);
        setSourceParameter(synth, first, "lfoRate", 20); setSourceParameter(synth, second, "lfoShape", 4);
        synth.setRateAndBufferSizeDetails(rate, 64); synth.prepareToPlay(rate, 64);
        for (int block = 0; block < 80; ++block) {
            if (block == 30) { synth.removeBlock(delay); synth.addBlock(3); }
            if (block == 40) assignLfo(synth, first, second, "lfoRate", 1);
            if (block == 50) assignLfo(synth, second, first, "lfoDepth", 1);
            juce::AudioBuffer<float> audio(2, 257); juce::MidiBuffer midi;
            if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 73);
            if (block == 60) midi.addEvent(juce::MidiMessage::noteOff(1, 60), 117);
            synth.processBlock(audio, midi);
            for (int c = 0; c < 2; ++c) for (int n = 0; n < audio.getNumSamples(); ++n) {
                require(std::isfinite(audio.getSample(c,n)) && std::abs(audio.getSample(c,n)) <= 0.98f, "Unstable routed synthesis");
                if (block == 0 && n < 73) require(audio.getSample(c,n) == 0, "Control slices moved MIDI before its sample offset");
            }
        }
    }
}
