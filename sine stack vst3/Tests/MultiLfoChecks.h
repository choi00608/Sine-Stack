void setLfoConnection(SineStack& p, int lfo, int route, int block, const char* field, float depth) {
    auto set = [&](const char* key, float value) { setParameter(p, p.sourceParameter(lfo, SineStack::routeSetting(route, key)).toRawUTF8(), value); };
    set("lfoBlock", static_cast<float>(block));
    set("lfoParameter", static_cast<float>(p.modulationParameterIndex(field, block >= 100)));
    set("lfoRange", depth);
    if (route == 0) setSourceParameter(p, lfo, "lfoTarget", 4); else set("enabled", 1);
}
void checkMultiLfo() {
    SineStack p;
    setParameter(p, "level", .3f);
    const int delay = p.addBlock(3), lfo = p.addBlock(106);
    // An exactly representable range endpoint survives both state restoration
    // and duplication, isolating routing equivalence from skew round-off.
    setParameter(p, "lfoRate", 20);
    const auto dry = sourceRender(p);
    setLfoConnection(p, lfo, 0, delay, "delayTime", .13f);
    const auto firstOnly = sourceRender(p);
    setLfoConnection(p, lfo, 1, 100, "oscPan", -.31f);
    const auto both = sourceRender(p);
    require(detailDifference(both, firstOnly) > 1, "Second LFO destination had no effect");
    juce::MemoryBlock saved; p.getStateInformation(saved);
    SineStack reference; reference.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    setParameter(reference, "route2_enabled", 0);
    const int secondLfo = reference.duplicateBlock(lfo);
    setLfoConnection(reference, secondLfo, 0, 100, "oscPan", -.31f);
    requireSameAudio(both, sourceRender(reference), "One LFO with two routes differs from synchronized single-target LFOs");
    require(p.state.getRawParameterValue("oscPan")->load() == 0, "Multi-route modulation overwrote the saved knob baseline");
    setParameter(p, "route2_lfoRange", 0);
    requireSameAudio(firstOnly, sourceRender(p), "Zero route depth affected another destination");
    setParameter(p, "route2_lfoRange", -.31f); setParameter(p, "lfoRange", 0);
    const auto secondOnly = sourceRender(p);
    require(detailDifference(secondOnly, dry) > 1 && detailDifference(secondOnly, both) > 1, "Independent route depths are coupled");
    setParameter(p, "lfoRange", .13f);
    setParameter(p, p.blockBypassParameter(lfo).toRawUTF8(), 1);
    requireSameAudio(dry, sourceRender(p), "LFO bypass failed to disable every route");
    setParameter(p, p.blockBypassParameter(lfo).toRawUTF8(), 0);
    setParameter(p, "lfoDepth", 0);
    requireSameAudio(dry, sourceRender(p), "Master Depth failed to disable every route");
    setParameter(p, "lfoDepth", .5f);
    // Opposing connections sum before clamping; the last supported slot also runs.
    setLfoConnection(p, lfo, 0, 100, "oscPan", .4f);
    setLfoConnection(p, lfo, 1, 100, "oscPan", -.4f);
    requireSameAudio(dry, sourceRender(p), "Opposing routes from one LFO failed to cancel");
    setLfoConnection(p, lfo, 7, delay, "delayTime", .13f);
    requireSameAudio(firstOnly, sourceRender(p), "Eighth route did not process independently");
    require(p.getTailLengthSeconds() >= 2, "Extra routes are missing from host tail estimation");
    // Backward/self/other-LFO rules apply equally to extra routes.
    setLfoConnection(p, lfo, 7, lfo, "lfoRate", .4f);
    requireSameAudio(dry, sourceRender(p), "Extra route permitted self-modulation");
    p.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    const int copy = p.duplicateBlock(lfo);
    for (int r = 0; r < SineStack::maxLfoRoutes; ++r) for (const auto* key : {"lfoBlock", "lfoParameter", "lfoRange", "enabled"}) {
        if (r == 0 && juce::String(key) == "enabled") continue;
        const auto name = SineStack::routeSetting(r, key);
        require(p.state.getRawParameterValue(p.sourceParameter(lfo, name))->load() == p.state.getRawParameterValue(p.sourceParameter(copy, name))->load(),
            "Duplicating an LFO lost an extra route");
    }
    setParameter(p, p.sourceParameter(copy, "route2_lfoRange").toRawUTF8(), .22f);
    require(std::abs(p.state.getRawParameterValue("route2_lfoRange")->load() + .31f) < .00001f, "Duplicated route depth shares state");
    juce::MemoryBlock all; p.getStateInformation(all);
    reference.setStateInformation(all.getData(), static_cast<int>(all.getSize()));
    requireSameAudio(sourceRender(p), sourceRender(reference), "Multiple destinations failed host-state round-trip");
    struct Preset { juce::File file = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile("multi-lfo", ".sinestack"); ~Preset(){file.deleteFile();} } preset;
    require(p.savePreset(preset.file).wasOk() && reference.loadPreset(preset.file).wasOk(), "Multi-route preset round-trip failed");
    requireSameAudio(sourceRender(p), sourceRender(reference), "Multiple destinations failed preset audio round-trip");
    // A previous-version single route must not inherit live extra routes.
    auto old = p.state.copyState();
    for (int i = old.getNumChildren(); --i >= 0;) {
        const auto id = old.getChild(i).getProperty("id").toString();
        if (p.state.getParameter(id)->getVersionHint() == 20) old.removeChild(i, nullptr);
    }
    juce::MemoryBlock legacy; juce::AudioProcessor::copyXmlToBinary(*old.createXml(), legacy);
    reference.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
    require(reference.state.getRawParameterValue("route2_enabled")->load() == 0
        && reference.state.getRawParameterValue(reference.sourceParameter(copy, "route2_enabled"))->load() == 0,
        "Old preset retained extra connections from current sound");
    p.removeBlock(100); p.addBlock(100);
    require(p.state.getRawParameterValue("route2_lfoBlock")->load() == -1
        && p.state.getRawParameterValue(p.sourceParameter(copy, "route2_lfoBlock"))->load() == -1, "Recycled block inherited extra modulation links");
    p.removeBlock(lfo); p.addBlock(106);
    require(p.state.getRawParameterValue("route2_enabled")->load() == 0, "Recycled LFO retained extra destinations");

    // Exercise the actual editor controls, scrolling, automation and removal.
    p.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto* tile = findControl(*editor, "Modulation block " + juce::String(lfo));
    tile->mouseDown(chainMouse(*tile, {20,20})); tile->mouseUp(chainMouse(*tile, {20,20}));
    auto sync = [&] { p.sendSynchronousChangeMessage(); };
    const auto adsr = findControl(*editor, "ADSR envelope")->getBounds();
    auto* add = dynamic_cast<juce::TextButton*>(findControl(*editor, "Add LFO target"));
    require(add != nullptr, "Multi-target add button missing");
    add->onClick(); sync();
    auto* target = dynamic_cast<juce::ComboBox*>(findControl(*editor, "LFO target block 3"));
    require(target != nullptr, "Add target did not create an independent row");
    target->setSelectedId(1000 + delay, juce::sendNotificationSync); sync();
    auto* parameter = dynamic_cast<juce::ComboBox*>(findControl(*editor, "LFO destination parameter 3"));
    parameter->setSelectedId(p.modulationParameterIndex("delayFeedback", false) + 1, juce::sendNotificationSync); sync();
    auto depth = [&] { return dynamic_cast<juce::Slider*>(findControl(*editor, p.state.getParameter("route3_lfoRange")->getName(100))); };
    require(depth() != nullptr, "Connection has no independent depth slider");
    depth()->setValue(-.18f, juce::sendNotificationSync);
    require(std::abs(p.state.getRawParameterValue("route3_lfoRange")->load() + .18f) < .00001f, "Depth slider did not update host parameter");
    setParameter(p, "route3_lfoRange", .42f);
    require(std::abs(depth()->getValue() - .42) < .00001, "Depth slider did not follow host automation");
    auto* viewport = dynamic_cast<juce::Viewport*>(findControl(*editor, "LFO connections"));
    require(viewport && viewport->getViewPositionY() > 0, "New connection was not revealed in the scrolling list");
    for (int n = 3; n < SineStack::maxLfoRoutes; ++n) { add->onClick(); sync(); }
    require(!add->isEnabled() && findControl(*editor, "LFO target block 8"), "Route capacity is not represented in the UI");
    auto* remove = dynamic_cast<juce::TextButton*>(findControl(*editor, "Remove LFO target 2"));
    remove->onClick(); sync();
    require(add->isEnabled() && !findControl(*editor, "LFO target block 2")
        && std::abs(p.state.getRawParameterValue("route3_lfoRange")->load() - .42f) < .00001f, "Removing a route changed another route");
    add->onClick(); sync();
    require(p.state.getRawParameterValue("route2_lfoBlock")->load() == -1 && p.state.getRawParameterValue("route2_lfoRange")->load() == .25f,
        "Reusing a route silently reconnected the old target");
    require(findControl(*editor, "ADSR envelope")->getBounds() == adsr, "Multi-target editor moved ADSR");
    // Show a useful pair of routes for visual inspection.
    p.setStateInformation(saved.getData(), static_cast<int>(saved.getSize())); sync(); viewport->setViewPosition(0, 0);
    juce::FileOutputStream preview(juce::File::getCurrentWorkingDirectory().getChildFile("multi-lfo-preview.png"));
    preview.setPosition(0); preview.truncate();
    juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), preview);
    std::cout << "PASS: eight LFO destinations, independent signed depths, cancellation, bypass, state/preset migration, duplication, deletion, UI and automation\n";
}
