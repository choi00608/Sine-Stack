#include "../Source/EqGraph.h"

void checkEqGraph() {
    SineStack p; p.setRateAndBufferSizeDetails(48000, 128);
    const int id = p.addBlock(8), other = p.addBlock(8);
    const auto prefix = "fx" + juce::String(id);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto select = [&](int block) {
        auto* tile = findControl(*editor, "Chain block " + juce::String(block)); require(tile, "Missing EQ graph test tile");
        tile->mouseDown(chainMouse(*tile, {20, 20})); tile->mouseUp(chainMouse(*tile, {20, 20}));
    };
    select(id);
    auto* graph = dynamic_cast<EqGraph*>(findControl(*editor, "EQ frequency response"));
    require(graph && graph->isVisible(), "Missing EQ response graph");
    const auto envelopeBounds = findControl(*editor, "ADSR envelope")->getBounds();
    auto set = [&](const char* key, float value) { setParameter(p, (prefix + key).toRawUTF8(), value); };
    auto reset = [&] { for (const auto& d : EqSettings::definitions) set(d.suffix, d.initial); };
    auto knob = [&](int band, int setting) {
        auto* parameter = p.state.getParameter(prefix + EqSettings::definitions[band * 3 + setting].suffix);
        auto* result = dynamic_cast<juce::Slider*>(findControl(*editor, parameter->getName(100)));
        require(result, "Selected EQ band's knob was not shown"); return result;
    };
    for (int band = 0; band < 3; ++band) {
        reset();
        const auto start = graph->bandPoint(band);
        auto* frequency = p.state.getParameter(prefix + EqSettings::definitions[band * 3].suffix);
        auto* gain = p.state.getParameter(prefix + EqSettings::definitions[band * 3 + 1].suffix);
        GestureCheck freqGestures, gainGestures; frequency->addListener(&freqGestures); gain->addListener(&gainGestures);
        graph->mouseDown(chainMouse(*graph, start));
        const float targetFrequency = std::array<float, 3>{180, 1600, 9000}[band];
        const float targetGain = std::array<float, 3>{7, -9, 4}[band];
        const auto target = graph->pointFor(targetFrequency, targetGain);
        graph->mouseDrag(chainMouse(*graph, target, true));
        require(std::abs(knob(band, 0)->getValue() - targetFrequency) < 0.1, "EQ graph frequency did not sync knob");
        require(std::abs(knob(band, 1)->getValue() - targetGain) < 0.001, "EQ graph gain did not sync knob");
        require(graph->bandPoint(band).getDistanceFrom(target) < 0.01f, "EQ handle does not follow pointer");
        require(std::abs(knob(band, 2)->getValue() - 0.707) < 0.001, "Dragging EQ changed Q");
        for (int n = 0; n < EqSettings::count; ++n) if (n / 3 != band)
            require(std::abs(p.state.getRawParameterValue(prefix + EqSettings::definitions[n].suffix)->load() - EqSettings::definitions[n].initial) < 0.01f, "EQ drag changed another band");
        graph->mouseDrag(chainMouse(*graph, {10000, -10000}, true));
        require(frequency->getValue() == 1 && gain->getValue() == 1, "EQ drag upper bounds failed");
        graph->mouseDrag(chainMouse(*graph, {-10000, 10000}, true));
        require(frequency->getValue() == 0 && gain->getValue() == 0, "EQ drag lower bounds failed");
        graph->mouseUp(chainMouse(*graph, target, true));
        frequency->removeListener(&freqGestures); gain->removeListener(&gainGestures);
        require(freqGestures.starts == 1 && freqGestures.ends == 1 && gainGestures.starts == 1 && gainGestures.ends == 1,
                "EQ graph did not balance host automation gestures");
        knob(band, 0)->setValue(targetFrequency, juce::sendNotificationSync);
        knob(band, 1)->setValue(targetGain, juce::sendNotificationSync);
        require(graph->bandPoint(band).getDistanceFrom(target) < 0.01f, "EQ knobs did not move the handle");
    }
    reset(); set("eqMidGain", 12);
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        p.setRateAndBufferSizeDetails(rate, 128); graph->refreshSampleRate();
        require(std::abs(graph->responseDb(1000) - 12) < 0.001, "EQ graph disagrees with centre gain");
        const double centre = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(69));
        set("eqMidFrequency", 880); set("eqMidQ", 2);
        const double measured = 20 * std::log10(eqLevel(rate, 1, 880, 12, 2) / eqLevel(rate, 1, 880, 0, 2));
        require(std::abs(graph->responseDb(centre) - measured) < 0.1, "EQ graph disagrees with rendered audio response");
        set("eqMidFrequency", 1000); set("eqMidQ", 0.707f);
    }
    const double wide = graph->responseDb(700); set("eqMidQ", 8);
    require(graph->responseDb(700) < wide - 1, "EQ Q did not update response graph");
    set("eqMidFrequency", 1000); set("amount", 0.5f);
    require(std::abs(graph->responseDb(1000) - 20 * std::log10(0.5 + 0.5 * std::pow(10, 12.0 / 20))) < 0.001, "EQ graph ignores Amount");
    const double beforeOutput = graph->responseDb(700); set("outputGain", -6);
    require(std::abs(graph->responseDb(700) - beforeOutput + 6) < 0.001, "EQ graph ignores Output");
    set("bypass", 1); require(graph->responseDb(1000) == 0, "Bypassed EQ response is not flat");
    set("bypass", 0); set("amount", 1); set("outputGain", 0);
    // Automated changes arriving off the message thread must also refresh graph attachments.
    std::thread host([&] { set("eqHighFrequency", 7500); set("eqHighGain", -5); }); host.join();
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.03, false);
    require(graph->bandPoint(2).getDistanceFrom(graph->pointFor(7500, -5)) < 0.01f, "Host automation did not update EQ graph");
    // Switching blocks during a drag must end the old gesture and detach its parameters.
    GestureCheck abandoned;
    auto* oldFrequency = p.state.getParameter(prefix + "eqLowFrequency"); oldFrequency->addListener(&abandoned);
    graph->mouseDown(chainMouse(*graph, graph->bandPoint(0)));
    select(other);
    graph->mouseDrag(chainMouse(*graph, {400, 50}, true)); graph->mouseUp(chainMouse(*graph, {400, 50}, true));
    oldFrequency->removeListener(&abandoned);
    require(abandoned.starts == 1 && abandoned.ends == 1, "Switching EQ blocks leaked a host gesture");
    require(std::abs(graph->responseDb(1000)) < 0.001, "EQ graph retained previous block settings");
    select(SineStack::oscillatorBlock); require(!graph->isVisible(), "EQ graph stayed visible on a source block");
    require(findControl(*editor, "ADSR envelope")->getBounds() == envelopeBounds, "EQ graph moved the fixed ADSR");
    select(id);
    reset(); set("eqLowGain", 5); set("eqMidGain", -6); set("eqMidFrequency", 1300); set("eqHighGain", 4);
    auto* bandBox = dynamic_cast<juce::ComboBox*>(findControl(*editor, "EQ band"));
    bandBox->setSelectedId(2, juce::sendNotificationSync);
    require(knob(1, 1)->getValue() < -5.99 && graph->bandPoint(1).getDistanceFrom(graph->pointFor(1300, -6)) < 0.01f, "Band menu broke EQ graph bindings");
    // Reopen and restore host state after graph editing.
    juce::MemoryBlock state; p.getStateInformation(state); set("eqMidGain", 10);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize())); p.sendSynchronousChangeMessage();
    require(graph->bandPoint(1).getDistanceFrom(graph->pointFor(1300, -6)) < 0.01f, "State restore did not refresh EQ graph");
    juce::FileOutputStream out(juce::File::getCurrentWorkingDirectory().getChildFile("eq-graph-preview.png"));
    out.setPosition(0); out.truncate();
    juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds(), true, 2), out);
    GestureCheck closing; oldFrequency->addListener(&closing);
    graph->mouseDown(chainMouse(*graph, graph->bandPoint(0))); editor.reset();
    oldFrequency->removeListener(&closing);
    require(closing.starts == 1 && closing.ends == 1, "Closing EQ editor leaked a host gesture");
    std::cout << "PASS: EQ graph response, all band drags, knob/host sync, bounds, block isolation, state and gestures\n";
}
