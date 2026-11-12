#include "../Source/OutputScope.h"
#include <thread>
#include "ScopeCycleChecks.h"

void checkOutputScope() {
    checkScopeCycle();
    // Capture must contain exactly the host buffer after FX, level and clamping.
    SineStack monitored, reference;
    for (auto* p : {&monitored, &reference}) {
        setParameter(*p, "fx0type", 1); setParameter(*p, "fx0amount", .8f);
        setParameter(*p, "fx1type", 3); setParameter(*p, "fx1amount", .4f);
        setParameter(*p, "level", .23f);
        p->setRateAndBufferSizeDetails(48000, 257); p->prepareToPlay(48000, 257);
    }
    juce::AudioBuffer<float> a(2, 257), b(2, 257);
    for (int block = 0; block < 100; ++block) {
        monitored.outputCapture.setEnabled(block % 3 != 0);
        juce::MidiBuffer midiA, midiB;
        if (block == 0) {
            midiA.addEvent(juce::MidiMessage::noteOn(1, 48, .8f), 0);
            midiB = midiA;
        }
        monitored.processBlock(a, midiA); reference.processBlock(b, midiB);
        for (int c = 0; c < 2; ++c) for (int n = 0; n < a.getNumSamples(); ++n)
            require(a.getSample(c, n) == b.getSample(c, n), "Scope changed audio output");
        bool exact = true; int n = 0;
        const int read = monitored.outputCapture.read([&](const OutputCapture::Frame& f) {
            exact &= f.left == a.getSample(0, n) && f.right == a.getSample(1, n) && f.rate == 48000;
            ++n;
        });
        require(exact && read == (block % 3 != 0 ? 257 : 0), "Scope is not the final stereo output");
    }
    // Full FIFO drops samples; the next accepted frame exposes the discontinuity.
    OutputCapture capture; capture.setEnabled(true);
    juce::AudioBuffer<float> many(2, OutputCapture::capacity + 19); many.clear();
    capture.push(many, 44100);
    require(capture.read([](const auto&) {}) == OutputCapture::capacity - 1, "Scope overflow was not bounded");
    capture.push(a, 96000);
    std::uint64_t firstSequence = 0; int received = 0;
    capture.read([&](const auto& f) { if (received++ == 0) firstSequence = f.sequence; });
    require(firstSequence == static_cast<std::uint64_t>(many.getNumSamples()), "Scope overflow lost its sequence gap");

    // Stress concurrent publication across wrapping FIFO boundaries.
    std::atomic<bool> finished {false};
    std::thread producer([&] {
        juce::AudioBuffer<float> samples(2, 127);
        for (int block = 0; block < 1500; ++block) {
            for (int n = 0; n < 127; ++n) { samples.setSample(0, n, static_cast<float>(block * 127 + n)); samples.setSample(1, n, -samples.getSample(0, n)); }
            capture.push(samples, 48000);
        }
        finished.store(true);
    });
    bool coherent = true; std::uint64_t previous = 0; int frames = 0;
    auto consume = [&](const OutputCapture::Frame& f) {
        coherent &= f.left == -f.right && f.rate == 48000 && (frames == 0 || f.sequence > previous);
        previous = f.sequence; ++frames;
    };
    while (!finished.load()) { capture.read(consume); std::this_thread::yield(); }
    producer.join(); capture.read(consume);
    require(coherent && frames > 0, "Scope FIFO published incoherent stereo data");

    // Freeze, changing sample rates, drawing all themes and editor/window lifetime.
    DesignTheme theme; OutputScope scope(capture); scope.setLookAndFeel(&theme); scope.setActive(true);
    juce::AudioBuffer<float> wave(2, 8192);
    for (int n = 0; n < wave.getNumSamples(); ++n) {
        wave.setSample(0, n, .72f * std::sin(n * .035f));
        wave.setSample(1, n, .48f * std::sin(n * .035f + .8f));
    }
    capture.push(wave, 48000); scope.refresh();
    juce::ComboBox* view = nullptr;
    juce::ComboBox* timeRange = nullptr;
    juce::TextButton* trigger = nullptr;
    for (auto* child : scope.getChildren()) {
        if (child->getName() == "Scope view") view = dynamic_cast<juce::ComboBox*>(child);
        if (child->getName() == "Scope time range") timeRange = dynamic_cast<juce::ComboBox*>(child);
        if (child->getName() == "Scope trigger") trigger = dynamic_cast<juce::TextButton*>(child);
    }
    require(view && view->getSelectedId() == 1, "Single-cycle overlay is not the default");
    require(timeRange && trigger && !timeRange->isVisible() && !trigger->isVisible(), "Real-time controls leaked into cycle mode");
    require(scope.cycleAnalysis().state() == ScopeCycle::State::locked, "Scope UI did not analyse the captured cycle");
    auto before = scope.createComponentSnapshot(scope.getLocalBounds());
    juce::TextButton* freeze = nullptr;
    for (auto* child : scope.getChildren()) if (child->getName() == "Freeze output waveform") freeze = dynamic_cast<juce::TextButton*>(child);
    require(freeze != nullptr, "Missing scope freeze control");
    freeze->setToggleState(true, juce::dontSendNotification);
    before = scope.createComponentSnapshot(scope.getLocalBounds());
    wave.clear(); capture.push(wave, 96000); scope.refresh();
    auto frozen = scope.createComponentSnapshot(scope.getLocalBounds());
    bool same = true;
    for (int y = 76; y < 340; ++y) for (int x = 0; x < 720; ++x) same &= before.getPixelAt(x, y) == frozen.getPixelAt(x, y);
    require(same, "Freeze changed the displayed waveform");
    freeze->setToggleState(false, juce::dontSendNotification);
    capture.push(wave, 96000); scope.refresh();
    for (int i = 0; i < 7; ++i) { theme.select(i); scope.sendLookAndFeelChange(); require(scope.createComponentSnapshot(scope.getLocalBounds()).isValid(), "Scope theme paint failed"); }
    theme.select(0);
    for (int n = 0; n < wave.getNumSamples(); ++n) {
        wave.setSample(0, n, .72f * std::sin(n * .035f));
        wave.setSample(1, n, .48f * std::sin(n * .035f + .8f));
    }
    capture.push(wave, 48000); scope.refresh();
    {
        juce::FileOutputStream output(juce::File::getCurrentWorkingDirectory().getChildFile("scope-preview.png"));
        output.setPosition(0); output.truncate();
        juce::PNGImageFormat().writeImageToStream(scope.createComponentSnapshot(scope.getLocalBounds(), true, 2), output);
    }
    view->setSelectedId(2, juce::sendNotificationSync);
    require(timeRange->isVisible() && trigger->isVisible(), "Real-time mode controls are missing");
    for (int milliseconds : {5, 20, 50, 100}) {
        timeRange->setSelectedId(milliseconds, juce::sendNotificationSync);
        require(scope.createComponentSnapshot(scope.getLocalBounds()).isValid(), "Real-time view failed to render");
    }
    {
        juce::FileOutputStream output(juce::File::getCurrentWorkingDirectory().getChildFile("scope-realtime-preview.png"));
        output.setPosition(0); output.truncate();
        juce::PNGImageFormat().writeImageToStream(scope.createComponentSnapshot(scope.getLocalBounds(), true, 2), output);
    }
    view->setSelectedId(1, juce::sendNotificationSync);
    require(scope.cycleAnalysis().state() == ScopeCycle::State::locked, "Switching back did not restore cycle view");
    scope.setSize(600, 320);
    require(scope.createComponentSnapshot(scope.getLocalBounds()).isValid(), "Minimum scope size failed");
    scope.setActive(false); scope.setLookAndFeel(nullptr);
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(monitored.createEditor());
        juce::TextButton* open = nullptr;
        for (auto* child : editor->getChildren()) if (child->getName() == "Open output scope") open = dynamic_cast<juce::TextButton*>(child);
        require(open != nullptr, "Missing Output Scope menu button");
        open->onClick(); require(monitored.outputCapture.isEnabled(), "Opening scope did not enable capture");
        OutputScopeWindow* window = nullptr;
        for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
            if (auto* w = dynamic_cast<OutputScopeWindow*>(juce::TopLevelWindow::getTopLevelWindow(i))) window = w;
        require(window != nullptr, "Scope window did not open");
        window->closeButtonPressed(); require(!monitored.outputCapture.isEnabled(), "Closing scope left capture enabled");
        open->onClick(); require(monitored.outputCapture.isEnabled(), "Scope did not reopen");
    }
    require(!monitored.outputCapture.isEnabled(), "Editor destruction left scope capture active");
    std::cout << "PASS: exact final output, unchanged audio, bounded concurrent FIFO, stable cycles, stereo phase, view switching, freeze, rates, themes and scope lifetime\n";
}
