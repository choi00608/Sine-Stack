void checkRhythms() {
    struct Case { int division, rhythm; double hz, ms; };
    const std::array<Case, 6> cases {{{2, 1, 4.0/3, 750}, {3, 1, 8.0/3, 375}, {4, 1, 16.0/3, 187.5},
                                    {2, 2, 3, 1000.0/3}, {3, 2, 6, 500.0/3}, {4, 2, 12, 250.0/3}}};
    for (double sr : {44100.0, 48000.0}) for (const auto& c : cases) {
        SineStack synced, free;
        setupTempoLfo(synced, true, c.division, 19); setParameter(synced, "lfoRhythm", static_cast<float>(c.rhythm));
        setupTempoLfo(free, false, c.division, c.hz);
        TempoTestPlayHead clock; clock.position.setBpm(120); clock.position.setIsPlaying(true);
        requireSameAudio(renderTempo(free, nullptr, sr, 257), renderTempo(synced, &clock, sr, 257,
            [&](int n) { clock.position.setPpqPosition(n / (sr * .5)); }), "Dotted/triplet LFO speed is incorrect");
        checkSyncedEcho(sr, 120, c.division, c.ms, 0, true, c.rhythm);
    }
    checkSyncedEcho(48000, 120, 3, 375, .5f, true, 1);
    checkSyncedEcho(48000, 120, 3, 500.0/3, .5f, true, 2);
    checkSyncedEcho(48000, 10, 0, 8000, 0, true, 1);
    // Triplet LFOs must also follow transport loops, not just run at the right Hz.
    SineStack synced, free;
    const int lfo = setupTempoLfo(synced, true, 3, 19); setParameter(synced, "lfoRhythm", 2);
    setupTempoLfo(free, false, 3, 6);
    TempoTestPlayHead clock; clock.position.setBpm(120); clock.position.setIsPlaying(true);
    const auto looped = renderTempo(synced, &clock, 48000, 240, [&](int n) {
        clock.position.setPpqPosition((n < 12000 ? n : n - 12000) / 24000.0);
    });
    requireSameAudio(looped, renderTempo(free, nullptr, 48000, 240, [&](int n) {
        if (n == 12000) setParameter(free, "lfoPhase", 180);
    }), "Triplet LFO lost phase on loop restart");
    const int delay = synced.addBlock(3);
    setParameter(synced, "fx0delaySync", 1); setParameter(synced, "fx0delayDivision", 3); setParameter(synced, "fx0delayRhythm", 1);
    // Preserve original host automation IDs and normalized 0..6 range.
    for (const auto* id : {"lfoDivision", "fx0delayDivision"}) {
        auto* p = synced.state.getParameter(id);
        require(p->getNormalisableRange().end == 6 && p->convertTo0to1(3) == .5f,
            "Extending beat choices changed existing automation scaling");
    }
    const int lfoCopy = synced.duplicateBlock(lfo), delayCopy = synced.duplicateBlock(delay);
    require(synced.state.getRawParameterValue(synced.sourceParameter(lfoCopy, "lfoRhythm"))->load() == 2
        && synced.state.getRawParameterValue("fx" + juce::String(delayCopy) + "delayRhythm")->load() == 1, "Duplicate lost dotted/triplet modifier");
    setSourceParameter(synced, lfoCopy, "lfoRhythm", 1);
    require(synced.state.getRawParameterValue("lfoRhythm")->load() == 2, "Duplicated rhythm shares state");
    juce::MemoryBlock data; synced.getStateInformation(data);
    free.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    requireSameAudio(renderDetailedFx(synced), renderDetailedFx(free), "Dotted/triplet host-state round-trip changed sound");
    struct Preset { juce::File file = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("rhythms", ".sinestack"); ~Preset(){file.deleteFile();} } preset;
    require(synced.savePreset(preset.file).wasOk() && free.loadPreset(preset.file).wasOk(), "Rhythm preset round-trip failed");
    requireSameAudio(renderDetailedFx(synced), renderDetailedFx(free), "Rhythm preset changed sound");
    auto old = synced.state.copyState();
    for (int n = old.getNumChildren(); --n >= 0;) if (synced.state.getParameter(old.getChild(n).getProperty("id").toString())->getVersionHint() == 23) old.removeChild(n, nullptr);
    juce::AudioProcessor::copyXmlToBinary(*old.createXml(), data); free.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    for (const auto& id : juce::StringArray{"lfoRhythm", synced.sourceParameter(lfoCopy, "lfoRhythm"), "fx0delayRhythm", "fx" + juce::String(delayCopy) + "delayRhythm"})
        require(free.state.getRawParameterValue(id)->load() == 0, "Old preset inherited a rhythm modifier");
    require(free.state.getRawParameterValue("lfoDivision")->load() == 3 && free.state.getRawParameterValue("fx0delayDivision")->load() == 3,
        "Old preset no longer selects straight eighth notes");
    // The single BEAT picker controls two stable host parameters and follows automation.
    std::unique_ptr<juce::AudioProcessorEditor> editor(synced.createEditor());
    auto sync = [&] { synced.sendSynchronousChangeMessage(); };
    auto select = [&](int id, bool mod) {
        auto* tile = findControl(*editor, (mod ? "Modulation block " : "Chain block ") + juce::String(id));
        tile->mouseDown(chainMouse(*tile, {20,20})); tile->mouseUp(chainMouse(*tile, {20,20}));
    };
    auto snapshot = [&](const char* name) {
        juce::FileOutputStream preview(juce::File::getCurrentWorkingDirectory().getChildFile(name));
        preview.setPosition(0); preview.truncate(); juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), preview);
    };
    for (bool mod : {true, false}) {
        select(mod ? lfo : delay, mod);
        const auto divisionId = mod ? "lfoDivision" : "fx0delayDivision";
        const auto rhythmId = mod ? "lfoRhythm" : "fx0delayRhythm";
        const auto name = mod ? "LFO Beat" : "FX 1 Delay Beat";
        auto beat = [&] { return dynamic_cast<juce::ComboBox*>(findControl(*editor, name)); };
        require(beat() && beat()->getNumItems() == 21, "Beat picker does not include all straight/dotted/triplet notes");
        beat()->setSelectedId(11, juce::sendNotificationSync); sync(); // Eighth dotted.
        require(beat()->getText() == "1/8 Dotted" && synced.state.getRawParameterValue(divisionId)->load() == 3
            && synced.state.getRawParameterValue(rhythmId)->load() == 1, "Picker did not select dotted eighth");
        auto* rhythmParameter = synced.state.getParameter(rhythmId);
        GestureCheck gesture; rhythmParameter->addListener(&gesture);
        beat()->setSelectedId(18, juce::sendNotificationSync); sync();
        require(gesture.starts == 1 && gesture.ends == 1 && beat()->getText() == "1/8 Triplet", "Triplet selection did not send a complete host gesture");
        rhythmParameter->removeListener(&gesture);
        setParameter(synced, divisionId, 4); setParameter(synced, rhythmId, 1);
        require(beat()->getText() == "1/16 Dotted", "Beat picker did not follow host automation"); sync();
        beat()->setSelectedId(4, juce::sendNotificationSync); sync();
        require(synced.state.getRawParameterValue(rhythmId)->load() == 0 && beat()->getText() == "1/8", "Choosing a straight note retained a modifier");
        beat()->setSelectedId(mod ? 18 : 11, juce::sendNotificationSync); sync();
        snapshot(mod ? "lfo-rhythm-preview.png" : "delay-rhythm-preview.png");
    }
    synced.removeBlock(lfo); synced.addBlock(106); synced.removeBlock(delay); synced.addBlock(3);
    require(synced.state.getRawParameterValue("lfoRhythm")->load() == 0 && synced.state.getRawParameterValue("fx0delayRhythm")->load() == 0,
        "Reused block retained dotted/triplet timing");
    std::cout << "PASS: dotted/triplet LFO and delay timing, loops, feedback, limit, automation compatibility, duplication/presets, migration and combined pickers\n";
}
