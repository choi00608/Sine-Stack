// Verify observable signal flow, not just the order stored in the UI.
int chainPosition(const SineStack& p, int id) {
    const auto chain = p.getChain();
    for (int i = 0; i < chain.size; ++i) if (chain.ids[i] == id) return i;
    return -1;
}
void checkOrderedChain() {
    juce::MidiBuffer sequence;
    sequence.addEvent(juce::MidiMessage::noteOn(1, 57, .8f), 0);
    sequence.addEvent(juce::MidiMessage::noteOn(1, 69, .8f), 6007);
    sequence.addEvent(juce::MidiMessage::noteOff(1, 69), 18013);
    sequence.addEvent(juce::MidiMessage::noteOff(1, 57), 21011);
    auto render = [&](SineStack& p) { return renderMonoMidi(p, sequence); };
    // Every source starts at its position, so a filter before the first source
    // must receive silence, while the same filter after it must alter the sound.
    for (int kind : {100, 104, 105}) {
        SineStack p; setupMono(p, .02f);
        if (kind != 100) { p.addBlock(kind); p.removeBlock(100); }
        const auto dry = render(p);
        const int filter = p.addBlock(5);
        setParameter(p, ("fx" + juce::String(filter) + "amount").toRawUTF8(), 1);
        setParameter(p, ("fx" + juce::String(filter) + "filterCutoff").toRawUTF8(), 80);
        require(detailDifference(dry, render(p)) > 5, "Filter after source did not process it");
        p.moveBlock(chainPosition(p, filter), 0);
        requireSameAudio(dry, render(p), "An earlier filter processed a later source");
    }
    // Unison, harmony and glide process their upstream stream.
    for (int kind : {101, 102, 103}) {
        SineStack p; setupMono(p, .02f);
        const auto dry = render(p); const int effect = p.addBlock(kind);
        const auto wet = render(p);
        require(detailDifference(dry, wet) > 1, "Ordered processor had no audible effect");
        p.moveBlock(chainPosition(p, effect), 0);
        requireSameAudio(dry, render(p), "Processor before first source affected the later source");
        p.moveBlock(0, 1);
        requireSameAudio(wet, render(p), "Restoring processor order did not restore audio");
        p.removeBlock(100);
        const auto silence = render(p);
        require(monoRms(silence, 0, static_cast<int>(silence.size())) == 0, "Audio processor generated its own MIDI oscillator");
    }
    // Two sources with a filter between: only the first source is filtered.
    SineStack combined, first, second;
    for (auto* p : {&combined, &first, &second}) { setupMono(*p, .02f); setParameter(*p, "level", .3f); }
    const int filter = combined.addBlock(5); first.addBlock(5);
    for (auto* p : {&combined, &first}) { setParameter(*p, "fx0amount", 1); setParameter(*p, "fx0filterCutoff", 120); }
    const int later = combined.duplicateBlock(100);
    combined.moveBlock(chainPosition(combined, later), combined.getChain().size - 1);
    setSourceParameter(combined, later, "oscTune", 7); setParameter(second, "oscTune", 7);
    const auto mix = render(combined), a = render(first), b = render(second);
    for (size_t n = 0; n < mix.size(); ++n) require(std::abs(mix[n] - a[n] - b[n]) < 1.0e-6f, "An FX processed a source added downstream");
    (void) filter;
    // A harmonizer after a delay must pitch-shift its echoes after ADSR is silent.
    SineStack tails; setupMono(tails, .02f);
    tails.addBlock(3); setParameter(tails, "fx0delayTime", 180); setParameter(tails, "fx0delayFeedback", 0); setParameter(tails, "fx0amount", 1);
    const int harmony = tails.addBlock(102); setSourceParameter(tails, harmony, "harmony", 3); setSourceParameter(tails, harmony, "harmonyMix", 1);
    juce::MidiBuffer shortNote; shortNote.addEvent(juce::MidiMessage::noteOn(1, 69, .8f), 0); shortNote.addEvent(juce::MidiMessage::noteOff(1, 69), 2400);
    const auto shiftedTail = renderMonoMidi(tails, shortNote);
    setParameter(tails, tails.blockBypassParameter(harmony).toRawUTF8(), 1);
    const auto dryTail = renderMonoMidi(tails, shortNote);
    require(monoTone(shiftedTail, 10500, 2200, 440 * std::exp2(7.0 / 12)) > monoTone(dryTail, 10500, 2200, 440 * std::exp2(7.0 / 12)) * 3,
            "Harmonizer did not shift the preceding delay tail");
    // LFO routes are independent of the audio-chain position.
    SineStack routed; setupMono(routed);
    const int f = routed.addBlock(5), lfo = routed.addBlock(106);
    setParameter(routed, "fx0amount", 1); setParameter(routed, "fx0filterCutoff", 500);
    setSourceParameter(routed, lfo, "lfoTarget", 0); const auto staticFilter = render(routed);
    setSourceParameter(routed, lfo, "lfoTarget", 3);
    const auto globalFilter = render(routed);
    require(detailDifference(staticFilter, globalFilter) > 1, "Independent LFO missed a filter");
    routed.moveBlock(chainPosition(routed, lfo), chainPosition(routed, f));
    requireSameAudio(globalFilter, render(routed), "Filter modulation depended on LFO placement");
    assignLfo(routed, lfo, f, "filterCutoff"); const auto assigned = render(routed);
    routed.moveBlock(chainPosition(routed, lfo), routed.getChain().size - 1);
    requireSameAudio(assigned, render(routed), "Specific parameter route depended on LFO placement");
    // A pair of equal shifts is applied successively, rather than summed as
    // independent oscillator layers. The +14-semitone component proves it.
    SineStack cascade; setupMono(cascade); setParameter(cascade, "sustain", 1);
    int h = cascade.addBlock(102); setSourceParameter(cascade, h, "harmony", 3); setSourceParameter(cascade, h, "harmonyMix", 1);
    juce::MidiBuffer sustained; sustained.addEvent(juce::MidiMessage::noteOn(1, 69, .8f), 0);
    const auto one = renderMonoMidi(cascade, sustained);
    cascade.duplicateBlock(h); const auto two = renderMonoMidi(cascade, sustained);
    const double hz = 440 * std::exp2(14.0 / 12);
    auto cascadeTone = [&](const std::vector<float>& audio) {
        double peak = 0;
        for (int cents = -20; cents <= 20; ++cents)
            peak = juce::jmax(peak, monoTone(audio, 12000, 24000, hz * std::exp2(cents / 1200.0)));
        return peak;
    };
    require(cascadeTone(two) > .02 && cascadeTone(one) < .001,
            "Duplicate harmonizers did not process each other's output");
    std::cout << "PASS: full ordered signal flow, source insertion, pitch FX tails/cascades, independent LFO routes\n";
}

void benchmarkOrderedChain() {
    for (int mode : {0, 1}) for (bool full : {false, true}) {
        SineStack p; setupMono(p, .02f); setParameter(p, "waveform", 2); setParameter(p, "pitchLatency", static_cast<float>(mode));
        if (full) {
            while (p.getChain().size < 32) {
                const int id = p.addBlock(102);
                setSourceParameter(p, id, "harmony", 5); setSourceParameter(p, id, "harmonyMix", .1f);
            }
        } else {
            p.addBlock(101); p.addBlock(1); p.addBlock(102); p.addBlock(3); p.addBlock(106); p.addBlock(5);
        }
        p.setRateAndBufferSizeDetails(48000, 256); p.prepareToPlay(48000, 256);
        juce::AudioBuffer<float> audio(2, 256); juce::MidiBuffer midi;
        double total = 0, worst = 0;
        for (int b = 0; b < 188; ++b) {
            midi.clear(); if (b == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 57, .3f), 0);
            const double start = juce::Time::getMillisecondCounterHiRes();
            p.processBlock(audio, midi);
            const double duration = juce::Time::getMillisecondCounterHiRes() - start;
            total += duration; worst = juce::jmax(worst, duration);
        }
        std::cout << "BENCH " << (mode == 0 ? "Live" : "Detail") << ": " << (full ? "32 blocks (31 harmonizers)" : "7-block working chain")
            << ", mean " << total / 188 << " ms, max " << worst << " ms per 5.33 ms callback\n";
    }
}
