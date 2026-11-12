std::vector<float> renderMonoMidi(SineStack& p, const juce::MidiBuffer& sequence, double rate = 48000, int size = 128, double seconds = 0.9) {
    p.setRateAndBufferSizeDetails(rate, size); p.prepareToPlay(rate, size);
    const int total = juce::roundToInt(rate * seconds);
    std::vector<float> result(total);
    for (int start = 0; start < total; start += size) {
        const int count = juce::jmin(size, total - start);
        juce::AudioBuffer<float> audio(2, count); juce::MidiBuffer midi;
        midi.addEvents(sequence, start, count, -start); p.processBlock(audio, midi);
        for (int n = 0; n < count; ++n) {
            result[start + n] = audio.getSample(0, n);
            require(std::isfinite(result[start + n]), "Mono produced invalid samples");
        }
    }
    return result;
}
double monoRms(const std::vector<float>& audio, int start, int count) {
    double energy = 0;
    for (int n = start; n < start + count; ++n) energy += audio[n] * audio[n];
    return std::sqrt(energy / count);
}
double monoTone(const std::vector<float>& audio, int start, int count, double hz, double rate = 48000) {
    std::complex<double> sum {};
    for (int n = 0; n < count; ++n) sum += static_cast<double>(audio[start + n]) * std::polar(1.0, -juce::MathConstants<double>::twoPi * hz * n / rate);
    return std::abs(sum) * 2 / count;
}
void setupMono(SineStack& p, float release = 0.45f) {
    setParameter(p, "voiceMode", 1); setParameter(p, "attack", 0.005f); setParameter(p, "release", release);
}
void checkMono() {
    double worstError = 0;
    for (double rate : {44100.0, 48000.0, 96000.0}) for (int size : {64, 257}) {
        const int second = juce::roundToInt(rate * 0.125);
        juce::MidiBuffer isolated, overlap;
        isolated.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), second);
        overlap.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
        overlap.addEvent(juce::MidiMessage::noteOff(1, 57), juce::roundToInt(rate * 0.1));
        overlap.addEvents(isolated, 0, -1, 0);
        SineStack p; setupMono(p);
        const auto reference = renderMonoMidi(p, isolated, rate, size);
        const auto actual = renderMonoMidi(p, overlap, rate, size);
        const int start = second + juce::roundToInt(rate * .02), count = juce::roundToInt(rate * .04);
        for (int n = start; n < start + count; ++n) worstError = juce::jmax(worstError, std::abs(static_cast<double>(reference[n] - actual[n])));
        require(worstError < 1.0e-6, "Mono still has release overlap cancellation");
        // A fade should not introduce a step near a handover, even with the new note in opposite phase.
        double maxStep = 0;
        for (int n = second; n < second + juce::roundToInt(rate * .006); ++n)
            maxStep = juce::jmax(maxStep, std::abs(static_cast<double>(actual[n] - actual[n - 1])));
        require(maxStep < 0.012, "Mono handover introduced an abrupt step");
    }
    SineStack p; setupMono(p);
    juce::MidiBuffer legato;
    legato.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
    legato.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 4800);
    legato.addEvent(juce::MidiMessage::noteOff(1, 60), 9600);
    legato.addEvent(juce::MidiMessage::noteOff(1, 57), 14400);
    const auto notes = renderMonoMidi(p, legato);
    require(monoTone(notes, 6000, 2400, 261.625565) > monoTone(notes, 6000, 2400, 220) * 5, "Mono did not select the latest note");
    require(monoTone(notes, 10800, 2400, 220) > monoTone(notes, 10800, 2400, 261.625565) * 5, "Mono did not return to held note");
    require(monoRms(notes, 16000, 2000) > 0.01, "Mono cut the final note's ADSR release");
    require(monoRms(notes, 40000, 2000) < 1.0e-7, "Mono left a stuck final note");
    // MIDI events must stay sample-accurate with varied block sizes and LFO routing slices.
    requireSameAudio(notes, renderMonoMidi(p, legato, 48000, 257), "Mono timing depends on host block size");
    const int lfo = p.addBlock(SineStack::lfoBlock);
    setSourceParameter(p, lfo, "lfoDepth", 0); assignLfo(p, lfo, SineStack::oscillatorBlock, "oscPan");
    requireSameAudio(notes, renderMonoMidi(p, legato, 48000, 257), "LFO slicing changed Mono note timing");
    p.removeBlock(lfo);
    for (int controller : {64, 66}) {
        setupMono(p, 0.02f); juce::MidiBuffer pedal;
        pedal.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
        pedal.addEvent(juce::MidiMessage::controllerEvent(1, controller, 127), 2400);
        pedal.addEvent(juce::MidiMessage::noteOff(1, 57), 3840);
        pedal.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 4800);
        pedal.addEvent(juce::MidiMessage::noteOff(1, 60), 7200);
        pedal.addEvent(juce::MidiMessage::controllerEvent(1, controller, 0), 9600);
        const auto audio = renderMonoMidi(p, pedal);
        const double expected = controller == 64 ? 261.625565 : 220;
        require(monoTone(audio, 8160, 1200, expected) > 0.05, "Mono pedal did not sustain/recall the correct note");
        require(monoRms(audio, 11000, 2000) < 1.0e-7, "Mono pedal release left stuck notes");
    }
    // Same-offset chords resolve to the last note without accumulating unheard voices.
    juce::MidiBuffer chord, single;
    for (int note = 40; note <= 70; ++note) chord.addEvent(juce::MidiMessage::noteOn(1, note, 0.8f), 0);
    single.addEvent(juce::MidiMessage::noteOn(1, 70, 0.8f), 0);
    requireSameAudio(renderMonoMidi(p, chord), renderMonoMidi(p, single), "Simultaneous notes accumulated in Mono");
    chord.addEvent(juce::MidiMessage::allSoundOff(1), 4800);
    const auto panic = renderMonoMidi(p, chord);
    require(monoRms(panic, 4800, 5000) == 0, "Mono All Sound Off failed");
    // The FX chain remains global: Mono must not clear delay history at the next note.
    SineStack wet; setupMono(wet, .02f); const int delay = wet.addBlock(3);
    const auto prefix = "fx" + juce::String(delay);
    setParameter(wet, (prefix + "amount").toRawUTF8(), 1); setParameter(wet, (prefix + "delayTime").toRawUTF8(), 100);
    juce::MidiBuffer shortNotes;
    shortNotes.addEvent(juce::MidiMessage::noteOn(1, 57, 0.8f), 0);
    shortNotes.addEvent(juce::MidiMessage::noteOff(1, 57), 960);
    shortNotes.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 4800);
    shortNotes.addEvent(juce::MidiMessage::noteOff(1, 60), 5760);
    const auto echo = renderMonoMidi(wet, shortNotes);
    require(monoRms(echo, 10000, 1000) > 0.001, "Mono cleared FX tails");
    // Choice bindings, host state, presets, and backward-compatible defaults.
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto* mode = dynamic_cast<juce::ComboBox*>(findControl(*editor, "Voice Mode")); require(mode, "Voice Mode selector missing");
    mode->setSelectedId(1, juce::sendNotificationSync); require(p.state.getRawParameterValue("voiceMode")->load() == 0, "Poly selection not bound");
    mode->setSelectedId(2, juce::sendNotificationSync); require(p.state.getRawParameterValue("voiceMode")->load() == 1, "Mono selection not bound");
    juce::MemoryBlock state; p.getStateInformation(state);
    SineStack restored; restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.state.getRawParameterValue("voiceMode")->load() == 1, "Host state lost Mono mode");
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("mono-check.sinestack");
    require(p.savePreset(file).wasOk() && restored.loadPreset(file).wasOk(), "Mono preset round trip failed"); file.deleteFile();
    require(restored.state.getRawParameterValue("voiceMode")->load() == 1, "Preset lost Mono mode");
    auto old = p.state.copyState(); old.removeChild(old.getChildWithProperty("id", "voiceMode"), nullptr);
    juce::AudioProcessor::copyXmlToBinary(*old.createXml(), state);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.state.getRawParameterValue("voiceMode")->load() == 0, "Old presets did not retain Poly behavior");
    juce::FileOutputStream preview(juce::File::getCurrentWorkingDirectory().getChildFile("mono-preview.png"));
    preview.setPosition(0); preview.truncate();
    juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds(), true, 2), preview);
    std::cout << "PASS: Mono overlap removed (max error " << worstError << "), handover, note priority, release, pedals, MIDI timing, FX tails and presets\n";
}
