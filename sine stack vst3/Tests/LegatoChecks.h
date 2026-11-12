void setupLegato(SineStack& p) { setupMono(p); setParameter(p, "monoLegato", 1); }
void checkLegato() {
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        SineStack p; setupLegato(p);
        setParameter(p, "attack", .2f); setParameter(p, "decay", .2f); setParameter(p, "sustain", .35f); setParameter(p, "release", .15f);
        juce::MidiBuffer single, overlap;
        single.addEvent(juce::MidiMessage::noteOn(1, 57, .8f), 0);
        single.addEvent(juce::MidiMessage::noteOff(1, 57), juce::roundToInt(rate * .65));
        overlap.addEvents(single, 0, -1, 0);
        // Same pitch makes resets of ADSR, phase or phrase velocity directly observable.
        for (double time : {.08, .27, .49}) overlap.addEvent(juce::MidiMessage::noteOn(1, 57, .2f), juce::roundToInt(rate * time));
        requireSameAudio(renderMonoMidi(p, single, rate, 64), renderMonoMidi(p, overlap, rate, 257),
                         "Legato restarted attack/decay/sustain, phase or first-note velocity");
    }
    SineStack p; setupLegato(p);
    setParameter(p, "attack", .3f); setParameter(p, "decay", .2f); setParameter(p, "sustain", .4f); setParameter(p, "release", .2f);
    juce::MidiBuffer notes;
    const auto first = juce::MidiMessage::noteOn(1, 57, .8f);
    notes.addEvent(first, 0);
    notes.addEvent(juce::MidiMessage::noteOn(1, 60, .2f), 5760);
    notes.addEvent(juce::MidiMessage::noteOff(1, 60), 10560);
    notes.addEvent(juce::MidiMessage::noteOff(1, 57), 19200);
    auto audio = renderMonoMidi(p, notes, 48000, 257);
    // Independent continuous oscillator/envelope reference: C4 changes pitch at
    // 120 ms, releasing it at 220 ms returns to A3 without a new gate.
    juce::ADSR envelope; envelope.setSampleRate(48000);
    auto value = [&](const char* id) { return p.state.getRawParameterValue(id)->load(); };
    envelope.setParameters({value("attack"), value("decay"), value("sustain"), value("release")}); envelope.noteOn();
    double phase = 0, maximumError = 0;
    for (int n = 0; n < static_cast<int>(audio.size()); ++n) {
        if (n == 19200) envelope.noteOff();
        const double hz = juce::MidiMessage::getMidiNoteInHertz(n >= 5760 && n < 10560 ? 60 : 57);
        const float expected = static_cast<float>(std::sin(phase * juce::MathConstants<double>::twoPi))
            * (first.getFloatVelocity() * .16f * envelope.getNextSample()) * value("level");
        maximumError = juce::jmax(maximumError, std::abs(static_cast<double>(audio[n] - expected)));
        phase += hz / 48000; phase -= std::floor(phase);
    }
    require(maximumError < 1.0e-6, "Legato pitch changes or fallback did not preserve continuous ADSR/phase");
    const int lfo = p.addBlock(SineStack::lfoBlock);
    setSourceParameter(p, lfo, "lfoDepth", 0); assignLfo(p, lfo, SineStack::oscillatorBlock, "oscPan");
    requireSameAudio(audio, renderMonoMidi(p, notes, 48000, 64), "Legato changed under LFO routing slices");
    p.removeBlock(lfo);
    // A gap, even while release is audible, starts a fresh envelope like Retrigger.
    juce::MidiBuffer separated;
    separated.addEvent(first, 0); separated.addEvent(juce::MidiMessage::noteOff(1, 57), 4800);
    separated.addEvent(juce::MidiMessage::noteOn(1, 60, .6f), 9600);
    separated.addEvent(juce::MidiMessage::noteOff(1, 60), 14400);
    const auto detached = renderMonoMidi(p, separated);
    setParameter(p, "monoLegato", 0);
    requireSameAudio(detached, renderMonoMidi(p, separated), "Separated Legato notes did not retrigger");
    setParameter(p, "monoLegato", 1);
    // Pedal-up can return to a still-held earlier key. It must not start release
    // before transferring the current voice back to that key.
    for (int controller : {64, 66}) {
        juce::MidiBuffer pedal;
        pedal.addEvent(first, 0); pedal.addEvent(juce::MidiMessage::controllerEvent(1, controller, 127), 2400);
        pedal.addEvent(juce::MidiMessage::noteOn(1, 60, .2f), 5760);
        pedal.addEvent(juce::MidiMessage::noteOff(1, 60), 8160);
        pedal.addEvent(juce::MidiMessage::controllerEvent(1, controller, 0), 10560);
        pedal.addEvent(juce::MidiMessage::noteOff(1, 57), 19200);
        const auto actual = renderMonoMidi(p, pedal);
        auto expectedNotes = notes;
        if (controller == 66) { // Only A3 was captured; C4 returns immediately on key-up.
            expectedNotes.clear(); expectedNotes.addEvent(first, 0);
            expectedNotes.addEvent(juce::MidiMessage::noteOn(1, 60, .2f), 5760);
            expectedNotes.addEvent(juce::MidiMessage::noteOff(1, 60), 8160);
            expectedNotes.addEvent(juce::MidiMessage::noteOff(1, 57), 19200);
        }
        requireSameAudio(actual, renderMonoMidi(p, expectedNotes), "Legato pedal return restarted or released the envelope");
    }
    // Changed MIDI identity must ignore the previous key's later Note Off and
    // route pitch-wheel / Note Off on the destination channel to the same voice.
    juce::MidiBuffer crossChannel;
    crossChannel.addEvent(first, 0);
    crossChannel.addEvent(juce::MidiMessage::noteOn(2, 60, .2f), 5760);
    crossChannel.addEvent(juce::MidiMessage::noteOff(1, 57), 8160);
    crossChannel.addEvent(juce::MidiMessage::pitchWheel(2, 16383), 10000);
    crossChannel.addEvent(juce::MidiMessage::noteOff(2, 60), 19200);
    const auto channels = renderMonoMidi(p, crossChannel);
    require(monoRms(channels, 15000, 2000) > .01, "Stale Note Off stopped a legato destination");
    require(monoTone(channels, 15000, 3000, 293.6606) > monoTone(channels, 15000, 3000, 261.6256) * 3,
            "Legato MIDI channel did not receive pitch bend");
    require(monoRms(channels, 33000, 3000) < 1.0e-7, "Legato left a stuck note on the destination channel");
    // A new target during curved glide must start at the current audible pitch.
    SineStack gliding; setupLegato(gliding); const int glide = gliding.addBlock(SineStack::glideBlock);
    setSourceParameter(gliding, glide, "glide", .2f); setSourceParameter(gliding, glide, "glideCurve", 2);
    juce::MidiBuffer moving;
    moving.addEvent(first, 0); moving.addEvent(juce::MidiMessage::noteOn(1, 69, .8f), 4800);
    moving.addEvent(juce::MidiMessage::noteOn(1, 72, .8f), 7680);
    const auto glideAudio = renderMonoMidi(gliding, moving, 48000, 257);
    // Glide now processes the complete upstream audio through a streaming spectral pitch shifter.
    // Its render must remain independent of host buffer partitioning, including
    // a new legato target before the previous glide has finished.
    const auto partitioned = renderMonoMidi(gliding, moving, 48000, 61);
    double glideError = 0;
    for (size_t n = 0; n < glideAudio.size(); ++n) glideError = juce::jmax(glideError, std::abs(double(glideAudio[n] - partitioned[n])));
    require(glideError < .00001, "Audio glide depended on host buffer partitioning");
    setParameter(gliding, gliding.blockBypassParameter(glide).toRawUTF8(), 1);
    const auto direct = renderMonoMidi(gliding, moving, 48000, 257);
    require(detailDifference(glideAudio, direct) > 1, "Audio glide did not process the legato stream");
    require(monoTone(glideAudio, 25000, 5000, juce::MidiMessage::getMidiNoteInHertz(72)) > .01,
            "Audio glide did not arrive at the latest legato pitch");
    // Keep existing host automation values and presets compatible.
    require(p.state.getParameter("voiceMode")->getNormalisableRange().end == 1, "Legato changed existing Voice Mode automation range");
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto* box = dynamic_cast<juce::ComboBox*>(findControl(*editor, "Voice Mode")); require(box && box->getSelectedId() == 3, "Legato mode missing from editor");
    box->setSelectedId(2, juce::sendNotificationSync); require(value("voiceMode") == 1 && value("monoLegato") == 0, "Retrigger mode mapping failed");
    box->setSelectedId(3, juce::sendNotificationSync); require(value("voiceMode") == 1 && value("monoLegato") == 1, "Legato mode mapping failed");
    setParameter(p, "voiceMode", 0); require(box->getSelectedId() == 1, "Host Poly automation not reflected");
    setParameter(p, "voiceMode", 1); require(box->getSelectedId() == 3, "Host Mono automation not reflected");
    setParameter(p, "monoLegato", 0); require(box->getSelectedId() == 2, "Host Legato automation not reflected");
    box->setSelectedId(3, juce::sendNotificationSync);
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("legato-check.sinestack");
    require(p.savePreset(file).wasOk(), "Could not save Legato preset");
    SineStack restored; require(restored.loadPreset(file).wasOk(), "Could not load Legato preset"); file.deleteFile();
    require(restored.state.getRawParameterValue("voiceMode")->load() == 1 && restored.state.getRawParameterValue("monoLegato")->load() == 1, "Preset lost Legato mode");
    auto old = p.state.copyState(); old.removeChild(old.getChildWithProperty("id", "monoLegato"), nullptr);
    juce::MemoryBlock data; juce::AudioProcessor::copyXmlToBinary(*old.createXml(), data);
    restored.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    require(restored.state.getRawParameterValue("voiceMode")->load() == 1 && restored.state.getRawParameterValue("monoLegato")->load() == 0,
            "Old Mono preset changed to Legato");
    p.keyboard.reset();
    juce::FileOutputStream preview(juce::File::getCurrentWorkingDirectory().getChildFile("legato-preview.png"));
    preview.setPosition(0); preview.truncate();
    juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds(), true, 2), preview);
    std::cout << "PASS: Legato continuous ADSR/phase (error " << maximumError << "), gaps, pedals, MIDI identity, curved glide (error " << glideError << "), presets and automation\n";
}
