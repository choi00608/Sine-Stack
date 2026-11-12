void checkSyncedEcho(double sr, double bpm, int division, double expectedMs, float feedback = 0, bool hostAvailable = true, int rhythm = 0) {
    SineStack effect, dry;
    TempoTestPlayHead clock; clock.position.setBpm(bpm); clock.available = hostAvailable;
    effect.setPlayHead(&clock);
    setParameter(effect, "fx0type", 3); setParameter(effect, "fx0amount", .6f);
    setParameter(effect, "fx0delayTime", 173); setParameter(effect, "fx0delayFeedback", feedback);
    setParameter(effect, "fx0delaySync", 1); setParameter(effect, "fx0delayDivision", static_cast<float>(division));
    setParameter(effect, "fx0delayRhythm", static_cast<float>(rhythm));
    for (auto* p : {&effect, &dry}) {
        setParameter(*p, "attack", .005f); setParameter(*p, "release", .02f);
        p->setRateAndBufferSizeDetails(sr, 127); p->prepareToPlay(sr, 127);
    }
    const double delay = sr * expectedMs / 1000;
    const int total = static_cast<int>(std::ceil(delay * (feedback > 0 ? 2 : 1) + sr * .08));
    std::vector<float> source(static_cast<size_t>(total + 127));
    juce::AudioBuffer<float> a(2, 127), b(2, 127);
    double echoEnergy = 0;
    for (int start = 0; start < total; start += 127) {
        for (auto pair : {std::make_pair(&effect, &a), std::make_pair(&dry, &b)}) {
            juce::MidiBuffer midi;
            if (start == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 69, .8f), 0);
            const int off = static_cast<int>(sr * .01);
            if (off >= start && off < start + 127) midi.addEvent(juce::MidiMessage::noteOff(1, 69), off - start);
            pair.first->processBlock(*pair.second, midi);
        }
        for (int n = 0; n < 127; ++n) {
            const int sample = start + n; source[sample] = b.getSample(0, n);
            double expected = source[sample], gain = .6 * .65;
            for (double previous = sample - delay; previous >= 0; previous -= delay) {
                const int index = static_cast<int>(previous); const double fraction = previous - index;
                expected += gain * (source[index] + fraction * (source[index + 1] - source[index]));
                if (feedback == 0) break;
                gain *= feedback;
            }
            require(std::isfinite(a.getSample(0, n)) && std::abs(a.getSample(0, n) - expected) < .00005,
                "Synced delay echo time or feedback is incorrect");
            echoEnergy += std::abs(a.getSample(0, n) - b.getSample(0, n));
        }
    }
    require(echoEnergy > .1, "Synced delay produced no echo");
    require(effect.getTailLengthSeconds() >= expectedMs * .001, "Synced delay tail is too short");
    effect.setPlayHead(nullptr);
}
std::vector<float> renderChangingDelay(SineStack& p, TempoTestPlayHead* clock, const std::function<void(int)>& step) {
    p.setPlayHead(clock); p.setRateAndBufferSizeDetails(48000, 240); p.prepareToPlay(48000, 240);
    std::vector<float> result;
    for (int offset = 0; offset < 67200; offset += 240) {
        step(offset);
        juce::AudioBuffer<float> audio(2, 240); juce::MidiBuffer midi;
        if (offset == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 57, .7f), 0);
        if (offset == 4800) midi.addEvent(juce::MidiMessage::noteOff(1, 57), 0);
        p.processBlock(audio, midi);
        for (int n = 0; n < 240; ++n) for (int c = 0; c < 2; ++c) result.push_back(audio.getSample(c,n));
    }
    p.setPlayHead(nullptr); return result;
}
void checkDelaySync() {
    for (double sr : {44100.0, 48000.0}) for (double bpm : {60.0, 120.0}) {
        checkSyncedEcho(sr, bpm, 2, 60000 / bpm);
        checkSyncedEcho(sr, bpm, 3, 30000 / bpm);
        checkSyncedEcho(sr, bpm, 4, 15000 / bpm);
    }
    checkSyncedEcho(48000, 120, 3, 250, .5f);
    checkSyncedEcho(48000, 60, 0, 4000); // Longer than the original two-second buffer.
    checkSyncedEcho(48000, 10, 0, 8000); // Eight-second capacity limit.
    checkSyncedEcho(48000, 1000, 6, 3.75); // Synced notes shorter than the free-time minimum.
    checkSyncedEcho(48000, 60, 2, 500, 0, false); // No host clock: 120 BPM.
    SineStack synced, free;
    const int delay = synced.addBlock(3);
    setParameter(synced, "fx0amount", .6f); setParameter(synced, "fx0delaySync", 1);
    setParameter(synced, "fx0delayFeedback", .5f); setParameter(synced, "fx0delayTime", 475);
    juce::MemoryBlock data; synced.getStateInformation(data); free.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    setParameter(free, "fx0delaySync", 0); setParameter(free, "fx0delayTime", 500);
    TempoTestPlayHead clock;
    const auto moving = renderChangingDelay(synced, &clock, [&](int n) {
        clock.position.setBpm(n < 33600 ? 120 : 60);
        clock.position.setPpqPosition(n < 33600 ? n / 24000.0 : 0); // Loop does not erase echoes.
        clock.position.setIsPlaying(true);
    });
    const auto expected = renderChangingDelay(free, nullptr, [&](int n) { if (n == 33600) setParameter(free, "fx0delayTime", 1000); });
    for (size_t n = 0; n < expected.size(); ++n) require(std::abs(expected[n] - moving[n]) < .00005f,
        "Tempo changes reset the delay tail or differ from smoothed time changes");
    require(synced.getTempoBpm() == 60 && synced.getTailLengthSeconds() > 10, "Tempo or feedback tail is not reported correctly");
    // The saved free-time knob and its LFO connection remain available after returning to Free.
    const int lfo = synced.addBlock(106); setLfoConnection(synced, lfo, 0, delay, "delayTime", .4f);
    const auto syncedAudio = renderDetailedFx(synced);
    setParameter(synced, "lfoRange", 0);
    requireSameAudio(syncedAudio, renderDetailedFx(synced), "Time LFO broke the selected beat while synced");
    setParameter(synced, "fx0delaySync", 0); const auto plainFree = renderDetailedFx(synced);
    setParameter(synced, "lfoRange", .4f);
    require(detailDifference(plainFree, renderDetailedFx(synced)) > 1, "Time LFO did not resume in Free mode");
    require(std::abs(synced.state.getRawParameterValue("fx0delayTime")->load() - 475) < .001f, "Sync overwrote the free-time value");
    setParameter(synced, "fx0delaySync", 1); setParameter(synced, "fx0delayDivision", 3);
    const int copy = synced.duplicateBlock(delay);
    require(synced.state.getRawParameterValue("fx" + juce::String(copy) + "delaySync")->load() == 1
        && synced.state.getRawParameterValue("fx" + juce::String(copy) + "delayDivision")->load() == 3, "Delay duplicate lost sync settings");
    setParameter(synced, ("fx" + juce::String(copy) + "delayDivision").toRawUTF8(), 4);
    synced.moveAudioBlock(2, 1);
    require(synced.state.getRawParameterValue("fx0delayDivision")->load() == 3, "Duplicate timing or reorder changed original delay");
    synced.getStateInformation(data); free.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    requireSameAudio(renderDetailedFx(synced), renderDetailedFx(free), "Synced delays failed host-state round-trip");
    struct Preset { juce::File file = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("delay-sync", ".sinestack"); ~Preset(){file.deleteFile();} } preset;
    require(synced.savePreset(preset.file).wasOk() && free.loadPreset(preset.file).wasOk(), "Synced delay preset round-trip failed");
    requireSameAudio(renderDetailedFx(synced), renderDetailedFx(free), "Synced delay preset lost timing");
    auto old = synced.state.copyState();
    for (int n = old.getNumChildren(); --n >= 0;) if (synced.state.getParameter(old.getChild(n).getProperty("id").toString())->getVersionHint() == 22) old.removeChild(n, nullptr);
    juce::AudioProcessor::copyXmlToBinary(*old.createXml(), data); free.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    require(free.state.getRawParameterValue("fx0delaySync")->load() == 0
        && free.state.getRawParameterValue("fx" + juce::String(copy) + "delaySync")->load() == 0, "Old preset retained tempo sync");
    std::unique_ptr<juce::AudioProcessorEditor> editor(synced.createEditor());
    auto* tile = findControl(*editor, "Chain block " + juce::String(delay));
    tile->mouseDown(chainMouse(*tile, {20,20})); tile->mouseUp(chainMouse(*tile, {20,20}));
    auto sync = [&] { synced.sendSynchronousChangeMessage(); };
    auto* mode = dynamic_cast<juce::ComboBox*>(findControl(*editor, "FX 1 Delay Clock"));
    auto* beat = dynamic_cast<juce::ComboBox*>(findControl(*editor, "FX 1 Delay Beat"));
    auto* time = dynamic_cast<juce::Slider*>(findControl(*editor, "FX 1 Delay Time"));
    require(mode && beat && time && !time->isEnabled() && beat->isEnabled(), "Delay sync controls are missing or enabled incorrectly");
    for (auto* control : {mode, beat}) require(juce::Rectangle<int>(361,104,631,316).contains(control->getBounds()), "Delay timing selector is outside inspector");
    beat->setSelectedId(5, juce::sendNotificationSync); sync();
    require(synced.state.getRawParameterValue("fx0delayDivision")->load() == 4, "Delay UI did not select sixteenth note");
    mode->setSelectedId(1, juce::sendNotificationSync); sync();
    require(time->isEnabled() && !beat->isEnabled() && std::abs(time->getValue() - 475) < .001, "Free mode lost saved milliseconds");
    setParameter(synced, "fx0delaySync", 1); sync();
    require(mode->getSelectedId() == 2 && !time->isEnabled(), "Sync UI did not follow automation");
    clock.position.setBpm(120); synced.setPlayHead(&clock);
    juce::AudioBuffer<float> empty(2, 128); juce::MidiBuffer none; synced.processBlock(empty, none); synced.setPlayHead(nullptr); sync();
    juce::FileOutputStream preview(juce::File::getCurrentWorkingDirectory().getChildFile("delay-sync-preview.png"));
    preview.setPosition(0); preview.truncate(); juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), preview);
    synced.removeBlock(delay); const int reused = synced.addBlock(3);
    require(reused == delay && synced.state.getRawParameterValue("fx0delaySync")->load() == 0, "Recycled delay retained sync mode");
    std::cout << "PASS: delay musical echoes, fractional taps, 8-second buffer, tempo changes/tails, LFO/Free interaction, duplication, migration and UI\n";
}
