#include "../Source/AudioSettings.h"
#include <CoreFoundation/CoreFoundation.h>

// Measure a sustained sine well below the output limiter, after filter settling.
double eqLevel(double rate, int band, float frequency, float gain, float q, int note = 69) {
    SineStack p;
    setParameter(p, "level", 0.02f); setParameter(p, "attack", 0.005f);
    setParameter(p, "fx0type", 8); setParameter(p, "fx0amount", 1);
    for (auto item : {std::pair<int, float>{0, frequency}, {1, gain}, {2, q}})
        setParameter(p, ("fx0" + juce::String(EqSettings::definitions[band * 3 + item.first].suffix)).toRawUTF8(), item.second);
    p.setRateAndBufferSizeDetails(rate, 257); p.prepareToPlay(rate, 257);
    juce::AudioBuffer<float> audio(2, 257); double energy = 0; int samples = 0;
    for (int block = 0; block < 160; ++block) {
        juce::MidiBuffer midi;
        if (block == 0) midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.5f), 0);
        p.processBlock(audio, midi);
        if (block > 80) for (int n = 0; n < 257; ++n) { energy += std::pow(audio.getSample(0, n), 2); ++samples; }
    }
    return std::sqrt(energy / samples);
}
void checkEq() {
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        SineStack dry, flat; setupDetailedFx(dry, 0); setupDetailedFx(flat, 8);
        requireSameAudio(renderDetailedFx(dry, rate, 257), renderDetailedFx(flat, rate, 257), "Flat EQ changed the sound");
        for (int band = 0; band < 3; ++band) {
            const int note = 45 + band * 24;
            const float frequency = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(note));
            const double base = eqLevel(rate, band, frequency, 0, 1, note);
            for (float db : {-12.0f, 12.0f}) {
                const double measured = 20 * std::log10(eqLevel(rate, band, frequency, db, 1, note) / base);
                require(std::abs(measured - db) < 0.1, "EQ centre gain is inaccurate");
            }
            require(eqLevel(rate, band, frequency * 2, 12, 0.3f, note) > eqLevel(rate, band, frequency * 2, 12, 8, note) * 1.5,
                    "EQ Q did not change bandwidth");
        }
    }
    SineStack p; const int id = p.addBlock(8); require(id >= 0 && p.blockName(id) == "EQ", "Cannot add EQ");
    const auto prefix = "fx" + juce::String(id);
    require(p.state.getRawParameterValue(prefix + "amount")->load() == 1, "New EQ is not fully wet");
    setupDetailedFx(p, 8);
    const auto baseline = renderDetailedFx(p);
    for (const auto& d : EqSettings::definitions) setParameter(p, (prefix + d.suffix).toRawUTF8(), d.maximum);
    const auto effected = renderDetailedFx(p);
    require(detailDifference(baseline, effected) > 0.1, "EQ is inaudible");
    setParameter(p, (prefix + "bypass").toRawUTF8(), 1);
    requireSameAudio(baseline, renderDetailedFx(p), "EQ bypass did not restore dry audio");
    setParameter(p, (prefix + "bypass").toRawUTF8(), 0);
    const int copy = p.duplicateBlock(id); require(copy >= 0, "Cannot duplicate EQ");
    for (const auto& d : EqSettings::definitions)
        require(p.state.getRawParameterValue("fx" + juce::String(copy) + d.suffix)->load() == p.state.getRawParameterValue(prefix + d.suffix)->load(), "EQ copy lost settings");
    setParameter(p, ("fx" + juce::String(copy) + "eqLowGain").toRawUTF8(), -9);
    require(p.state.getRawParameterValue(prefix + "eqLowGain")->load() > 17, "EQ copies share settings");
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("eq-test.sinestack");
    require(p.savePreset(file).wasOk(), "EQ preset save failed");
    SineStack restored; require(restored.loadPreset(file).wasOk(), "EQ preset load failed"); file.deleteFile();
    for (const auto& d : EqSettings::definitions)
        require(std::abs(restored.state.getRawParameterValue(prefix + d.suffix)->load() - p.state.getRawParameterValue(prefix + d.suffix)->load()) < 0.001, "EQ preset lost settings");
    p.removeBlock(copy);
    for (const auto& d : EqSettings::definitions) setParameter(p, (prefix + d.suffix).toRawUTF8(), d.initial);
    const int lfo = p.addBlock(SineStack::lfoBlock);
    setSourceParameter(p, lfo, "lfoTarget", 0);
    const auto still = renderDetailedFx(p);
    assignLfo(p, lfo, id, "eqMidGain", 0.5f);
    const auto moving = renderDetailedFx(p);
    require(detailDifference(still, moving) > 0.1, "LFO cannot modulate EQ");
    require(p.state.getRawParameterValue(prefix + "eqMidGain")->load() == 0, "LFO changed saved EQ gain");
    // Existing source destination indices must not shift when EQ is appended.
    for (int d = 0; d < SourceSettings::count; ++d)
        require(p.modulationParameterIndex(SourceSettings::definitions[d].id, true) == 16 + FxDetails::count + d, "EQ shifted legacy LFO routing IDs");
    for (const auto& d : EqSettings::definitions) {
        assignLfo(p, lfo, id, d.suffix, 1);
        setSourceParameter(p, lfo, "lfoRate", 20); setSourceParameter(p, lfo, "lfoDepth", 1);
        for (const auto& other : EqSettings::definitions) setParameter(p, (prefix + other.suffix).toRawUTF8(), other.maximum);
        renderDetailedFx(p, 48000, 64);
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto* tile = findControl(*editor, "Chain block " + juce::String(id)); require(tile != nullptr, "Missing EQ tile");
    tile->mouseDown(chainMouse(*tile, {20, 20})); tile->mouseUp(chainMouse(*tile, {20, 20}));
    auto* band = dynamic_cast<juce::ComboBox*>(findControl(*editor, "EQ band")); require(band, "Missing EQ band selector");
    band->setSelectedId(2, juce::sendNotificationSync);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.03, false);
    auto* knob = dynamic_cast<juce::Slider*>(findControl(*editor, "FX " + juce::String(id + 1) + " EQ Mid Gain"));
    require(knob != nullptr, "EQ band selection did not show mid settings");
    knob->setValue(-6, juce::sendNotificationSync);
    require(std::abs(p.state.getRawParameterValue(prefix + "eqMidGain")->load() + 6) < 0.001, "EQ knob did not bind");
    auto* rate = dynamic_cast<juce::ComboBox*>(findControl(*editor, "Sample rate"));
    require(rate && rate->getText() == "48000 Hz" && !rate->isEnabled(), "AU did not show host sample rate");
    juce::FileOutputStream preview(juce::File::getCurrentWorkingDirectory().getChildFile("eq-preview.png"));
    preview.setPosition(0); preview.truncate();
    juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds(), true, 2), preview);
    std::cout << "PASS: EQ response at 44.1/48/96k, bandwidth, bypass, duplicate, preset, LFO and editor\n";
}
