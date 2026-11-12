// Chain model, persistence, audio routing, and interaction checks.
juce::Component* findControl(juce::Component& parent, const juce::String& name) {
    if (parent.getName() == name) return &parent;
    for (auto* child : parent.getChildren()) if (auto* result = findControl(*child, name)) return result;
    return nullptr;
}
juce::MouseEvent chainMouse(juce::Component& component, juce::Point<float> point, bool drag = false) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0, &component, &component,
        juce::Time::getCurrentTime(), {20, 20}, juce::Time::getCurrentTime(), 1, drag);
}
std::vector<float> renderChain(SineStack& p) {
    p.setRateAndBufferSizeDetails(48000, 128); p.prepareToPlay(48000, 128);
    std::vector<float> result;
    for (int block = 0; block < 240; ++block) {
        juce::AudioBuffer<float> audio(2, 128); juce::MidiBuffer midi;
        if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 69, 0.8f), 0);
        if (block == 30) midi.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
        p.processBlock(audio, midi);
        for (int n = 0; n < 128; ++n) {
            require(std::isfinite(audio.getSample(0, n)) && std::abs(audio.getSample(0, n)) <= 0.98f, "Invalid chain output");
            result.push_back(audio.getSample(0, n));
        }
    }
    return result;
}
void requireSameAudio(const std::vector<float>& a, const std::vector<float>& b, const char* error, float tolerance = 0.000001f) {
    require(a.size() == b.size(), error);
    float maximum = 0; size_t worst = 0;
    for (size_t n = 0; n < a.size(); ++n) {
        require(std::isfinite(a[n]) && std::isfinite(b[n]), error);
        const float difference = std::abs(a[n] - b[n]);
        if (difference > maximum) { maximum = difference; worst = n; }
    }
    if (maximum >= tolerance)
        std::cerr << error << ": max difference=" << maximum << ", sample=" << worst
                  << ", expected=" << a[worst] << ", actual=" << b[worst] << std::endl;
    require(maximum < tolerance, error);
}
void checkChainModel() {
    SineStack legacy, modern, reversed;
    for (auto* p : {&legacy, &modern}) {
        setParameter(*p, "fx0type", 1); setParameter(*p, "fx0amount", 0.7f);
        setParameter(*p, "fx1type", 3); setParameter(*p, "fx1amount", 0.6f);
        setParameter(*p, "detune", 12); setParameter(*p, "harmony", 5); setParameter(*p, "harmonyMix", 0.25f);
        setParameter(*p, "subLevel", 0.2f); setParameter(*p, "glide", 0.12f); setParameter(*p, "release", 0.02f);
    }
    modern.materialiseChain();
    const auto oldAudio = renderChain(legacy);
    requireSameAudio(oldAudio, renderChain(modern), "Migrating the old layout changed the sound");
    // Source-derived processors now receive the audio at their actual position.
    modern.moveBlock(1, modern.getChain().size - 1);
    const auto movedAudio = renderChain(modern); double movedDifference = 0;
    for (size_t n = 0; n < movedAudio.size(); ++n) movedDifference += std::abs(movedAudio[n] - oldAudio[n]);
    require(movedDifference > 1, "Moving unison past the FX did not change the sound");
    modern.moveBlock(modern.getChain().size - 1, 1);
    auto position = [](const SineStack::Chain& chain, int id) {
        for (int n = 0; n < chain.size; ++n) if (chain.ids[n] == id) return n;
        return -1;
    };
    modern.moveBlock(position(modern.getChain(), 0), position(modern.getChain(), 1));
    juce::MemoryBlock state; legacy.getStateInformation(state); reversed.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    reversed.moveFx(0, 1);
    const auto reordered = renderChain(modern);
    requireSameAudio(reordered, renderChain(reversed), "Visible audio FX order does not match processing order");
    double difference = 0;
    for (size_t n = 0; n < reordered.size(); ++n) difference += std::abs(reordered[n] - oldAudio[n]);
    require(difference > 1, "Chain reorder test did not exercise a sound change");

    SineStack full;
    for (int n = 0; n < 31; ++n) require(full.addBlock(n % 7 + 1) >= 0, "Could not fill the large chain");
    require(full.getChain().size == 32 && full.addBlock(3) == -1, "Chain limit is not enforced");
    const int moved = full.getChain().ids[31];
    setParameter(full, ("fx" + juce::String(moved) + "amount").toRawUTF8(), 0.83f);
    full.moveBlock(31, 1);
    require(full.getChain().ids[1] == moved && std::abs(full.state.getRawParameterValue("fx" + juce::String(moved) + "amount")->load() - 0.83f) < 0.00001,
            "Reordering lost block identity or settings");
    full.removeBlock(moved); require(full.getChain().size == 31, "Removing a block failed");
    const int recycled = full.addBlock(3);
    require(recycled == moved && full.state.getRawParameterValue("fx" + juce::String(recycled) + "type")->load() == 3,
            "A removed slot could not be reused");
    require(std::abs(full.state.getRawParameterValue("fx" + juce::String(recycled) + "amount")->load() - 0.35f) < 0.00001,
            "A new block inherited deleted settings");

    struct TestFile {
        juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile("chain-check-" + juce::Uuid().toString() + ".sinestack");
        ~TestFile() { file.deleteFile(); }
    } temporary;
    require(full.savePreset(temporary.file).wasOk(), "Could not save the 32-block chain");
    const auto text = temporary.file.loadFileAsString();
    const auto json = juce::JSON::parse(text);
    require(static_cast<int>(json["formatVersion"]) == 2 && json["chain"].getArray()->size() == 32, "Preset did not save chain structure");
    require(!text.contains(temporary.file.getFullPathName()), "Preset leaked local path");
    SineStack restored;
    require(restored.loadPreset(temporary.file).wasOk(), "Could not load the large chain");
    require(restored.getChain().ids == full.getChain().ids && restored.getChain().size == 32, "Preset changed chain order");
    requireSameAudio(renderChain(full), renderChain(restored), "Large-chain audio failed to round-trip");
    full.getStateInformation(state); restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.getChain().ids == full.getChain().ids, "Host state lost chain order");
    const auto before = restored.state.copyState().toXmlString();
    for (const auto& invalidId : {"fx99", "oscillator", "unknown"}) {
        auto invalid = juce::JSON::parse(text);
        invalid["chain"].getArray()->getReference(1) = invalidId;
        temporary.file.replaceWithText(juce::JSON::toString(invalid));
        require(restored.loadPreset(temporary.file).failed() && restored.state.copyState().toXmlString() == before,
                "Invalid chain modified the current sound");
    }
    const auto current = full.getChain();
    for (int n = 0; n < current.size; ++n) full.removeBlock(current.ids[n]);
    require(full.getChain().size == 0, "The chain could not be emptied");
    for (float sample : renderChain(full)) require(sample == 0, "Empty chain should be silent");
    require(full.savePreset(temporary.file).wasOk() && restored.loadPreset(temporary.file).wasOk() && restored.getChain().size == 0,
            "Empty chain failed to round-trip");
    require(full.addBlock(SineStack::oscillatorBlock) == SineStack::oscillatorBlock, "Oscillator could not be re-added");
    double energy = 0; for (float sample : renderChain(full)) energy += sample * sample;
    require(energy > 1, "Re-adding oscillator did not restore sound");
}
void checkChainDuplication() {
    SineStack p;
    for (int type = 1; type <= 7; ++type) {
        const int original = p.addBlock(type);
        const auto prefix = "fx" + juce::String(original);
        std::vector<std::pair<juce::String, float>> expected;
        for (auto* parameter : p.getParameters()) {
            auto* value = dynamic_cast<juce::RangedAudioParameter*>(parameter);
            if (!value->paramID.startsWith(prefix) || juce::CharacterFunctions::isDigit(value->paramID[prefix.length()])) continue;
            const auto suffix = value->paramID.substring(prefix.length());
            if (suffix != "type") value->setValueNotifyingHost(suffix == "bypass" ? 1.0f : 0.73f);
            expected.emplace_back(suffix, value->getValue());
        }
        const auto before = p.getChain();
        const int copy = p.duplicateBlock(original);
        require(copy >= 0 && copy != original && p.getChain().size == before.size + 1, "Could not duplicate FX independently");
        require(p.getChain().ids[before.size - 1] == original && p.getChain().ids[before.size] == copy,
                "Duplicate is not immediately after its original");
        for (const auto& [suffix, value] : expected)
            require(std::abs(p.state.getParameter("fx" + juce::String(copy) + suffix)->getValue() - value) < 0.00001f,
                    "Duplicate did not copy every parameter");
        const float originalAmount = p.state.getParameter(prefix + "amount")->getValue();
        setParameter(p, ("fx" + juce::String(copy) + "amount").toRawUTF8(), 0.12f);
        require(p.state.getParameter(prefix + "amount")->getValue() == originalAmount, "Editing copy changed original");
    }
    // fx1 must not accidentally match fx10, fx11, etc.; insert within the chain.
    const auto before = p.getChain();
    const int copy = p.duplicateBlock(1);
    require(p.getChain().ids[2] == 1 && p.getChain().ids[3] == copy && p.getChain().ids[4] == before.ids[3],
            "Duplicate did not insert beside the selected FX");
    require(p.state.getParameter("fx1amount")->getValue() == p.state.getParameter("fx" + juce::String(copy) + "amount")->getValue(),
            "Slot prefix collision changed copied settings");
    juce::MemoryBlock state; p.getStateInformation(state);
    SineStack restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.getChain().ids == p.getChain().ids, "Host state lost duplicates");
    requireSameAudio(renderChain(p), renderChain(restored), "Duplicated chain failed to restore audio");
    struct PresetFile {
        juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile("duplicate-check-" + juce::Uuid().toString() + ".sinestack");
        ~PresetFile() { file.deleteFile(); }
    } preset;
    require(p.savePreset(preset.file).wasOk() && restored.loadPreset(preset.file).wasOk()
            && restored.getChain().ids == p.getChain().ids, "Preset lost duplicated chain");
    require(restored.state.getParameter("fx" + juce::String(copy) + "amount")->getValue()
            == p.state.getParameter("fx1amount")->getValue(), "Preset lost duplicate settings");
    const auto unchanged = p.state.copyState().toXmlString();
    for (int invalid : std::array<int, 4>{-1, 31, 199, 231})
        require(p.duplicateBlock(invalid) == -1 && p.state.copyState().toXmlString() == unchanged,
                "Invalid or singleton duplicate changed state");
    while (p.getChain().size < SineStack::maxBlocks) p.addBlock(3);
    const auto full = p.state.copyState().toXmlString();
    require(p.duplicateBlock(0) == -1 && p.state.copyState().toXmlString() == full, "Duplicate exceeded chain capacity");
}
void checkChainEditor() {
    SineStack p;
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto sync = [&] { p.sendSynchronousChangeMessage(); };
    auto settleAnimations = [] {
        for (int n = 0; n < 12; ++n) {
            juce::Timer::callPendingTimersSynchronously();
            juce::Thread::sleep(20);
        }
        juce::Timer::callPendingTimersSynchronously();
    };
    auto* graph = findControl(*editor, "ADSR envelope"); require(graph != nullptr, "Missing fixed ADSR");
    const auto graphBounds = graph->getBounds();
    const int unison = p.addBlock(SineStack::unisonBlock);
    require(p.getChain().size == 2, "Source block was not added");
    const int harmony = p.addBlock(SineStack::harmonyBlock);
    const int lfo = p.addBlock(SineStack::lfoBlock);
    const int delay = p.addBlock(3);
    for (int n = 0; n < 10; ++n) require(p.addBlock(n % 7 + 1) >= 0, "Could not add repeated effects");
    sync();
    auto* viewport = dynamic_cast<juce::Viewport*>(findControl(*editor, "FX chain viewport"));
    require(viewport && viewport->getViewedComponent()->getWidth() > viewport->getWidth(), "Chain does not scroll horizontally");
    viewport->setViewPosition(600, 0); require(viewport->getViewPositionX() > 0, "Horizontal scroll did not move");
    viewport->setViewPosition(0, 0);
    auto select = [&](int id) {
        auto* tile = findControl(*editor, (p.blockKind(id) == SineStack::lfoBlock ? "Modulation block " : "Chain block ") + juce::String(id)); require(tile != nullptr, "Missing block tile");
        tile->mouseDown(chainMouse(*tile, {20, 20})); tile->mouseUp(chainMouse(*tile, {20, 20}));
        auto* title = dynamic_cast<juce::Label*>(findControl(*editor, "Selected block title"));
        require(title && title->getText() == p.blockName(id), "Tile selection did not update inspector");
        require(graph->getBounds() == graphBounds, "Selecting a block moved the ADSR");
    };
    auto* duplicate = dynamic_cast<juce::TextButton*>(findControl(*editor, "Duplicate selected block"));
    require(duplicate != nullptr, "Duplicate button is missing");
    select(unison);
    require(duplicate->isEnabled(), "Source block should allow duplication");
    auto* detune = dynamic_cast<juce::Slider*>(findControl(*editor, "Detune")); require(detune != nullptr, "Unison has no Detune control");
    detune->setValue(22, juce::sendNotificationSync); require(p.state.getRawParameterValue("detune")->load() == 22, "Inspector knob did not update sound");
    select(harmony); require(findControl(*editor, "Harmony") != nullptr && findControl(*editor, "Detune") == nullptr, "Inspector retained controls from another block");
    select(lfo); require(findControl(*editor, "LFO Target") && findControl(*editor, "LFO Rate"), "LFO settings missing from chain inspector");
    select(delay);
    auto* time = dynamic_cast<juce::Slider*>(findControl(*editor, "FX " + juce::String(delay + 1) + " Delay Time"));
    require(time != nullptr, "Selected Delay has no time control");
    time->setValue(475, juce::sendNotificationSync);
    require(p.state.getRawParameterValue("fx" + juce::String(delay) + "delayTime")->load() == 475, "Delay inspector edited the wrong block");
    auto* bypass = dynamic_cast<juce::TextButton*>(findControl(*editor, "Selected block bypass"));
    bypass->setToggleState(true, juce::sendNotificationSync);
    require(p.state.getRawParameterValue(p.blockBypassParameter(delay))->load() == 1, "Inspector bypass is not connected");
    bypass->setToggleState(false, juce::sendNotificationSync);
    auto tileBypass = [&](int id) {
        auto* button = dynamic_cast<juce::TextButton*>(findControl(*editor, (p.blockKind(id) == SineStack::lfoBlock ? "Modulation bypass " : "Chain bypass ") + juce::String(id)));
        require(button != nullptr && button->isEnabled(), "Chain tile is missing a bypass button");
        auto* tile = button->getParentComponent();
        require(tile->getComponentAt(button->getBounds().getCentre()) == button,
                "Tile bypass hit area is captured by the drag/select surface");
        return button;
    };
    const auto chainBeforeBypass = p.getChain();
    auto* selectedTitle = dynamic_cast<juce::Label*>(findControl(*editor, "Selected block title"));
    tileBypass(unison)->setToggleState(true, juce::sendNotificationSync);
    require(p.state.getRawParameterValue(p.blockBypassParameter(unison))->load() == 1
            && selectedTitle->getText() == "Delay" && !bypass->getToggleState(),
            "Bypassing an unselected tile changed selection or the wrong block");
    require(p.state.getRawParameterValue("detune")->load() == 22 && p.getChain().ids == chainBeforeBypass.ids,
            "Tile bypass changed settings or chain order");
    GestureCheck bypassGesture;
    auto* bypassParameter = p.state.getParameter(p.blockBypassParameter(delay));
    bypassParameter->addListener(&bypassGesture);
    tileBypass(delay)->setToggleState(true, juce::sendNotificationSync);
    require(bypass->getToggleState() && bypass->getButtonText() == "BYPASSED"
            && p.state.getRawParameterValue("fx" + juce::String(delay) + "delayTime")->load() == 475,
            "Tile bypass did not sync the inspector or preserve settings");
    require(bypassGesture.starts == 1 && bypassGesture.ends == 1, "Tile bypass did not send one complete automation gesture");
    bypassParameter->removeListener(&bypassGesture);
    bypass->setToggleState(false, juce::sendNotificationSync);
    require(!tileBypass(delay)->getToggleState() && tileBypass(delay)->getButtonText() == "ON", "Inspector bypass did not sync the tile");
    setParameter(p, p.blockBypassParameter(delay).toRawUTF8(), 1);
    require(tileBypass(delay)->getToggleState() && bypass->getToggleState(), "Host bypass did not sync both controls");
    auto* tile = findControl(*editor, "Chain block " + juce::String(delay));
    auto* routing = dynamic_cast<juce::Label*>(findControl(*editor, "Signal chain order"));
    require(routing != nullptr, "Missing audio order display");
    const int from = 3, to = 1;
    const auto beforeDrag = p.getChain();
    tile->mouseDown(chainMouse(*tile, {20, 20}));
    tile->mouseDrag(chainMouse(*tile, {20.0f + (to - from) * 170, 20}, true));
    require(p.getChain().ids == beforeDrag.ids && tile->getAlpha() == 0,
            "Drag should preview placement without committing audio order");
    settleAnimations();
    require(findControl(*editor, "Chain block " + juce::String(unison))->getX() == 2 * 170,
            "Neighbour did not animate into the insertion gap");
    {
        auto preview = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream output(juce::File::getCurrentWorkingDirectory().getChildFile("chain-drag-preview.png"));
        output.setPosition(0); output.truncate(); juce::PNGImageFormat().writeImageToStream(preview, output);
    }
    tile->mouseUp(chainMouse(*tile, {20.0f + (to - from) * 170, 20}, true)); sync();
    settleAnimations();
    require(tile->getX() == 170 && tile->getY() == 0 && tile->getAlpha() == 1,
            "Dropped tile did not settle into the chain");
    require(p.getChain().ids[1] == delay && p.state.getRawParameterValue("fx" + juce::String(delay) + "delayTime")->load() == 475,
            "Dragging a block lost order or settings");
    require(tileBypass(delay)->getToggleState() && tileBypass(delay)->getButtonText() == "BYPASSED", "Rebuilding moved tiles lost bypass state");
    tileBypass(delay)->setToggleState(false, juce::sendNotificationSync);
    sync();
    require(routing->getText().startsWith("CHAIN:  SILENCE  >  01 Oscillator  >  02 Delay  >  03 Unison / Detune")
            && routing->getText().contains("Unison") && routing->getText().endsWith("OUT"),
            "Displayed FX order disagrees with the actual audio route");
    // A small slip below the cards, still inside the chain panel, cancels and restores the layout.
    const auto beforeCancel = p.getChain();
    tile->mouseDown(chainMouse(*tile, {20, 20}));
    tile->mouseDrag(chainMouse(*tile, {220, 120}, true));
    tile->mouseUp(chainMouse(*tile, {220, 120}, true));
    settleAnimations();
    require(p.getChain().ids == beforeCancel.ids && tile->getX() == 170 && tile->getY() == 0,
            "Cancelled drag changed order or left a misplaced tile");
    tile->mouseDown(chainMouse(*tile, {20, 20}));
    // Keep the pointer inside the visible panel edge; farther outside now previews deletion.
    const auto edgePoint = tile->getLocalPoint(viewport, juce::Point<int>(viewport->getWidth() - 8, 20)).toFloat();
    tile->mouseDrag(chainMouse(*tile, edgePoint, true));
    settleAnimations();
    require(viewport->getViewPositionX() > 0 && p.getChain().ids == beforeCancel.ids,
            "Dragging at the edge did not scroll or committed order prematurely");
    const auto cancelPoint = tile->getLocalPoint(viewport, juce::Point<int>(viewport->getWidth() - 8, 120)).toFloat();
    tile->mouseUp(chainMouse(*tile, cancelPoint, true));
    settleAnimations(); viewport->setViewPosition(0, 0);
    require(duplicate->isEnabled(), "Audio effect should allow duplication");
    duplicate->onClick();
    const int copy = p.getChain().ids[2];
    require(copy != delay && p.getChain().size == beforeCancel.size + 1, "Duplicate button failed");
    auto* copyTime = dynamic_cast<juce::Slider*>(findControl(*editor, "FX " + juce::String(copy + 1) + " Delay Time"));
    require(copyTime && copyTime->getValue() == 475, "Duplicate was not selected with copied settings");
    copyTime->setValue(650, juce::sendNotificationSync);
    require(p.state.getRawParameterValue("fx" + juce::String(delay) + "delayTime")->load() == 475,
            "Copy inspector changed original Delay");
    require(routing->getText().startsWith("CHAIN:  SILENCE  >  01 Oscillator  >  02 Delay  >  03 Delay"),
            "Route did not include duplicated effect");
    settleAnimations();
    // Generate a reviewable image of the new selected-effect layout.
    auto preview = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    juce::FileOutputStream output(juce::File::getCurrentWorkingDirectory().getChildFile("chain-preview.png"));
    output.setPosition(0); output.truncate(); juce::PNGImageFormat().writeImageToStream(preview, output);
    select(delay);
    auto* remove = dynamic_cast<juce::TextButton*>(findControl(*editor, "Remove selected block"));
    remove->onClick();
    require(findControl(*editor, "Chain block " + juce::String(delay)) == nullptr, "Remove button did not remove the selected block");
    for (int kind = SineStack::glideBlock; kind <= SineStack::noiseBlock; ++kind) { p.addBlock(kind); sync(); select(kind); }
    for (int type = 1; type <= 7; ++type) {
        const int id = p.addBlock(type); sync(); select(id);
        int controls = 0;
        for (auto* child : editor->getChildren()) if (dynamic_cast<juce::Slider*>(child) && child->getName().startsWith("FX " + juce::String(id + 1) + " ")) ++controls;
        int expected = 4; // Amount, two core controls, Output.
        for (const auto& detail : FxDetails::definitions) if (detail.effect == type && *detail.choices == '\0') ++expected;
        require(controls == expected, "An audio effect is missing detailed inspector controls");
    }
    while (p.getChain().size < 32) p.addBlock(3);
    sync(); auto* add = dynamic_cast<juce::TextButton*>(findControl(*editor, "Add block"));
    require(add && !add->isEnabled() && !duplicate->isEnabled(), "Add and Duplicate should disable at the chain limit");
    const auto chain = p.getChain(); for (int n = 0; n < chain.size; ++n) p.removeBlock(chain.ids[n]); sync();
    require(add->isEnabled() && !remove->isVisible(), "Empty-chain inspector is not usable");
    p.addBlock(SineStack::oscillatorBlock); sync(); select(SineStack::oscillatorBlock);
    require(findControl(*editor, "Waveform") != nullptr, "Oscillator waveform is not in the inspector");
}
