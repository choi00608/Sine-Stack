struct TempoTestPlayHead final : juce::AudioPlayHead {
    PositionInfo position;
    bool available = true;
    juce::Optional<PositionInfo> getPosition() const override { return available ? juce::makeOptional(position) : juce::Optional<PositionInfo>(); }
};
std::vector<float> renderTempo(SineStack& p, TempoTestPlayHead* clock, double sampleRate, int blockSize,
                              const std::function<void(int)>& beforeBlock = {}) {
    p.setPlayHead(clock);
    p.setRateAndBufferSizeDetails(sampleRate, blockSize); p.prepareToPlay(sampleRate, blockSize);
    std::vector<float> result;
    for (int offset = 0, total = static_cast<int>(sampleRate * .5); offset < total; offset += blockSize) {
        if (beforeBlock) beforeBlock(offset);
        const int count = juce::jmin(blockSize, total - offset);
        juce::AudioBuffer<float> audio(2, count); juce::MidiBuffer midi;
        if (offset == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 57, .7f), 0);
        p.processBlock(audio, midi);
        for (int n = 0; n < count; ++n) for (int c = 0; c < 2; ++c) {
            require(std::isfinite(audio.getSample(c, n)), "Tempo sync produced invalid audio");
            result.push_back(audio.getSample(c, n));
        }
    }
    p.setPlayHead(nullptr);
    return result;
}
int setupTempoLfo(SineStack& p, bool sync, int division, double rate) {
    const int lfo = p.addBlock(106);
    setSourceParameter(p, lfo, "lfoTarget", 2); // Direct source-volume path, evaluated per sample.
    setSourceParameter(p, lfo, "lfoDepth", .8f);
    setSourceParameter(p, lfo, "lfoRate", static_cast<float>(rate));
    setSourceParameter(p, lfo, "lfoSync", sync ? 1 : 0);
    setSourceParameter(p, lfo, "lfoDivision", static_cast<float>(division));
    return lfo;
}
void checkTempoSync() {
    // Compare rendered audio against known Hz rates, including different sample rates
    // and non-multiple-of-32 buffers. Quarter/eighth/sixteenth notes at 120 BPM are 2/4/8 Hz.
    for (double sr : {44100.0, 48000.0}) for (double bpm : {60.0, 120.0}) for (int division : {2, 3, 4}) {
        SineStack synced, free;
        setupTempoLfo(synced, true, division, 19);
        setupTempoLfo(free, false, division, bpm / (60 * LfoTiming::beats[division]));
        TempoTestPlayHead clock; clock.position.setBpm(bpm); clock.position.setIsPlaying(true);
        const auto expected = renderTempo(free, nullptr, sr, 257);
        const auto actual = renderTempo(synced, &clock, sr, 257, [&](int offset) { clock.position.setPpqPosition(offset * bpm / (60 * sr)); });
        requireSameAudio(expected, actual, "Tempo division does not match its expected frequency");
    }
    for (int division : {0, 1, 5, 6}) {
        SineStack synced, free;
        setupTempoLfo(synced, true, division, 19);
        setupTempoLfo(free, false, division, 1 / LfoTiming::beats[division]);
        TempoTestPlayHead clock; clock.position.setBpm(60); clock.position.setIsPlaying(true);
        requireSameAudio(renderTempo(free, nullptr, 48000, 128), renderTempo(synced, &clock, 48000, 128,
            [&](int n) { clock.position.setPpqPosition(n / 48000.0); }), "Long/short note division is incorrect");
    }
    // Missing host data defaults to 120 BPM; missing PPQ and stopped transports still run.
    SineStack synced, free;
    const int lfo = setupTempoLfo(synced, true, 3, 19);
    setupTempoLfo(free, false, 3, 4);
    const auto fourHz = renderTempo(free, nullptr, 48000, 240);
    requireSameAudio(fourHz, renderTempo(synced, nullptr, 48000, 240), "Standalone sync fallback is not 120 BPM");
    TempoTestPlayHead clock; clock.position.setBpm(120); clock.position.setIsPlaying(false); clock.position.setPpqPosition(7);
    requireSameAudio(fourHz, renderTempo(synced, &clock, 48000, 240), "Stopped host froze synced LFO");
    clock.position.setIsPlaying(true); clock.position.setPpqPosition({});
    requireSameAudio(fourHz, renderTempo(synced, &clock, 48000, 240), "Missing PPQ froze synced LFO");
    requireSameAudio(fourHz, renderTempo(synced, &clock, 48000, 240, [&](int n) {
        clock.position.setBpm(n == 0 ? 120 : std::numeric_limits<double>::quiet_NaN());
    }), "Invalid host tempo did not retain last valid BPM");
    // Song position locks phase, including negative preroll and loop/seek boundaries.
    setParameter(synced, "lfoDivision", 2); setParameter(free, "lfoRate", 2); setParameter(free, "lfoPhase", 270);
    clock.position.setBpm(120);
    const auto negative = renderTempo(synced, &clock, 48000, 240, [&](int n) { clock.position.setPpqPosition(-.25 + n / 24000.0); });
    requireSameAudio(renderTempo(free, nullptr, 48000, 240), negative, "Preroll musical phase is incorrect");
    setParameter(free, "lfoPhase", 0);
    const auto looped = renderTempo(synced, &clock, 48000, 240, [&](int n) { clock.position.setPpqPosition((n >= 12000 ? n - 12000 : n) / 24000.0); });
    const auto expectedLoop = renderTempo(free, nullptr, 48000, 240, [&](int n) { if (n == 12000) setParameter(free, "lfoPhase", 180); });
    requireSameAudio(expectedLoop, looped, "Loop/seek did not realign synced LFO phase");
    // Tempo changes while playing should change speed without losing musical phase.
    setParameter(free, "lfoPhase", 0);
    const auto changing = renderTempo(synced, &clock, 48000, 240, [&](int n) {
        clock.position.setBpm(n < 12000 ? 120 : 60);
        clock.position.setPpqPosition(n < 12000 ? n / 24000.0 : .5 + (n - 12000) / 48000.0);
    });
    const auto expectedChange = renderTempo(free, nullptr, 48000, 240, [&](int n) { if (n == 12000) setParameter(free, "lfoRate", 1); });
    requireSameAudio(expectedChange, changing, "Live tempo change produced incorrect LFO timing");
    // Every connection from a synced LFO uses the same musical clock.
    setLfoConnection(synced, lfo, 0, 100, "oscPan", .3f);
    setLfoConnection(synced, lfo, 1, 100, "oscFine", -.08f);
    juce::MemoryBlock data; synced.getStateInformation(data);
    free.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    setParameter(free, "lfoSync", 0); setParameter(free, "lfoRate", 2);
    clock.position.setBpm(120); clock.position.setIsPlaying(true);
    requireSameAudio(renderTempo(free, nullptr, 48000, 240), renderTempo(synced, &clock, 48000, 240,
        [&](int n) { clock.position.setPpqPosition(n / 24000.0); }), "Multi-target routes do not share synced timing");
    const int copy = synced.duplicateBlock(lfo);
    require(synced.state.getRawParameterValue(synced.sourceParameter(copy, "lfoSync"))->load() == 1
        && synced.state.getRawParameterValue(synced.sourceParameter(copy, "lfoDivision"))->load() == 2, "Duplicate lost LFO sync settings");
    setSourceParameter(synced, copy, "lfoDivision", 4);
    struct Preset { juce::File file = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("tempo", ".sinestack"); ~Preset(){file.deleteFile();} } preset;
    require(synced.savePreset(preset.file).wasOk() && free.loadPreset(preset.file).wasOk(), "Tempo preset round-trip failed");
    require(free.state.getRawParameterValue(free.sourceParameter(copy, "lfoDivision"))->load() == 4, "Preset lost independent beat division");
    auto old = synced.state.copyState();
    for (int n = old.getNumChildren(); --n >= 0;) if (synced.state.getParameter(old.getChild(n).getProperty("id").toString())->getVersionHint() == 21) old.removeChild(n, nullptr);
    juce::AudioProcessor::copyXmlToBinary(*old.createXml(), data); free.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    require(free.state.getRawParameterValue("lfoSync")->load() == 0 && free.state.getRawParameterValue(free.sourceParameter(copy, "lfoSync"))->load() == 0,
        "Legacy preset inherited tempo sync from current sound");
    std::unique_ptr<juce::AudioProcessorEditor> editor(synced.createEditor());
    auto* tile = findControl(*editor, "Modulation block " + juce::String(lfo));
    tile->mouseDown(chainMouse(*tile, {20,20})); tile->mouseUp(chainMouse(*tile, {20,20}));
    auto sync = [&] { synced.sendSynchronousChangeMessage(); };
    auto mode = [&] { return dynamic_cast<juce::ComboBox*>(findControl(*editor, "LFO Clock")); };
    auto beat = [&] { return dynamic_cast<juce::ComboBox*>(findControl(*editor, "LFO Beat")); };
    auto rate = [&] { return dynamic_cast<juce::Slider*>(findControl(*editor, "LFO Rate")); };
    require(mode() && beat() && rate() && !rate()->isEnabled() && beat()->isEnabled(), "Sync UI does not select musical rate");
    beat()->setSelectedId(5, juce::sendNotificationSync); sync();
    require(synced.state.getRawParameterValue("lfoDivision")->load() == 4, "UI did not select a sixteenth note");
    mode()->setSelectedId(1, juce::sendNotificationSync); sync();
    require(rate()->isEnabled() && !beat()->isEnabled() && std::abs(synced.state.getRawParameterValue("lfoRate")->load() - 19) < .0001f, "Free mode lost Hz value or enabled wrong control");
    setParameter(synced, "lfoSync", 1); sync();
    require(mode()->getSelectedId() == 2 && !rate()->isEnabled(), "Clock UI did not follow host automation");
    juce::FileOutputStream preview(juce::File::getCurrentWorkingDirectory().getChildFile("tempo-sync-preview.png"));
    preview.setPosition(0); preview.truncate(); juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), preview);
    synced.removeBlock(lfo); synced.addBlock(106);
    require(synced.state.getRawParameterValue("lfoSync")->load() == 0, "Recycled LFO retained tempo mode");
    std::cout << "PASS: beat rates at 44.1/48 kHz, host tempo/phase/loop, missing host data, multi-target sync, duplicate/preset migration and UI\n";
}
void checkTileDeletion() {
    SineStack p;
    const int delay = p.addBlock(3), drive = p.addBlock(1), lfo = p.addBlock(106);
    setLfoConnection(p, lfo, 0, delay, "delayTime", .2f);
    setLfoConnection(p, lfo, 1, delay, "delayFeedback", .1f);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto sync = [&] { p.sendSynchronousChangeMessage(); };
    auto tileFor = [&](int id) { return findControl(*editor, (p.blockKind(id) == 106 ? "Modulation block " : "Chain block ") + juce::String(id)); };
    auto contains = [&](int id) { const auto c = p.getChain(); for (int n = 0; n < c.size; ++n) if (c.ids[n] == id) return true; return false; };
    auto* tile = tileFor(delay);
    tile->mouseDown(chainMouse(*tile, {20,20})); tile->mouseUp(chainMouse(*tile, {20,20}));
    auto* removeDrive = dynamic_cast<juce::TextButton*>(findControl(*editor, "Chain delete " + juce::String(drive)));
    require(removeDrive && removeDrive->getParentComponent()->getComponentAt(removeDrive->getBounds().getCentre()) == removeDrive, "Tile delete has no independent hit area");
    removeDrive->onClick(); sync();
    auto* title = dynamic_cast<juce::Label*>(findControl(*editor, "Selected block title"));
    require(!contains(drive) && contains(delay) && title->getText() == "Delay", "Deleting another tile changed the selected block");
    // A small slip below the cards but inside the panel still cancels, preserving edge scrolling.
    const auto order = p.getAudioChain();
    tile->mouseDown(chainMouse(*tile, {20,20})); tile->mouseDrag(chainMouse(*tile, {70,120}, true)); tile->mouseUp(chainMouse(*tile, {70,120}, true)); sync();
    require(p.getAudioChain().ids == order.ids && contains(delay), "Small vertical drag slip deleted a block");
    // Exit the whole panel: preview only until release. Escape cancels even if the mouse is still held.
    tile->mouseDown(chainMouse(*tile, {20,20})); tile->mouseDrag(chainMouse(*tile, {40,-120}, true));
    require(contains(delay), "Drag removal committed before release");
    require(editor->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)), "Escape did not cancel drag removal");
    tile->mouseDrag(chainMouse(*tile, {40,-120}, true)); tile->mouseUp(chainMouse(*tile, {40,-120}, true)); sync();
    require(contains(delay), "Cancelled drag restarted or deleted the block");
    // Use an editor-coordinate point so the animation's transient tile position is irrelevant.
    auto dragOutside = [&](int id, juce::Point<int> point, bool release) {
        auto* t = tileFor(id); t->mouseDown(chainMouse(*t, {20,20}));
        const auto local = t->getLocalPoint(editor.get(), point).toFloat();
        t->mouseDrag(chainMouse(*t, local, true));
        if (release) t->mouseUp(chainMouse(*t, local, true));
    };
    dragOutside(delay, {500,350}, false);
    {
        juce::FileOutputStream preview(juce::File::getCurrentWorkingDirectory().getChildFile("chain-delete-preview.png"));
        preview.setPosition(0); preview.truncate(); juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), preview);
    }
    auto local = tile->getLocalPoint(editor.get(), juce::Point<int>(500,350)).toFloat();
    tile->mouseUp(chainMouse(*tile, local, true)); sync();
    require(!contains(delay) && p.state.getRawParameterValue("lfoBlock")->load() == -1 && p.state.getRawParameterValue("route2_lfoBlock")->load() == -1,
        "Drag deletion failed or retained incoming LFO routes");
    const int replacement = p.addBlock(3); sync();
    require(replacement == delay && p.state.getRawParameterValue("lfoBlock")->load() == -1, "Reused deleted slot inherited modulation");
    dragOutside(replacement, {500,650}, true); sync(); require(!contains(replacement), "Drag below chain did not remove block");
    const int horizontal = p.addBlock(1); sync(); dragOutside(horizontal, {1015,510}, true); sync();
    require(!contains(horizontal), "Drag past right panel edge did not remove block");
    auto* removeLfo = dynamic_cast<juce::TextButton*>(findControl(*editor, "Modulation delete " + juce::String(lfo)));
    require(removeLfo != nullptr, "Modulation tile is missing direct deletion"); removeLfo->onClick(); sync();
    require(!contains(lfo), "Modulation tile deletion failed");
    auto* removeOsc = dynamic_cast<juce::TextButton*>(findControl(*editor, "Chain delete 100")); removeOsc->onClick(); sync();
    require(p.getChain().size == 0 && title->getText() == "No block selected", "Deleting the final block left a dangling inspector");
    for (float sample : sourceRender(p)) require(sample == 0, "Empty chain still generates audio");
    juce::MemoryBlock saved; p.getStateInformation(saved); SineStack restored; restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    require(restored.getChain().size == 0, "Deleted blocks reappeared after state restore");
    std::cout << "PASS: direct tile deletion, drag-out preview/removal, Escape, edge margin, route cleanup, empty chain and state\n";
}
