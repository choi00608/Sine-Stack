#pragma once

namespace AudioSettings {
inline juce::Array<double> rates(juce::AudioDeviceManager* manager) {
    juce::Array<double> result {44100.0, 48000.0};
    if (manager != nullptr) if (auto* device = manager->getCurrentAudioDevice())
        for (double rate : device->getAvailableSampleRates()) result.addIfNotAlreadyThere(rate);
    std::sort(result.begin(), result.end());
    return result;
}
inline juce::Result setRate(juce::AudioDeviceManager& manager, double rate) {
    auto* device = manager.getCurrentAudioDevice();
    if (device == nullptr || !device->getAvailableSampleRates().contains(rate))
        return juce::Result::fail("This audio device does not support " + juce::String(rate, 0) + " Hz.");
    const auto previous = manager.getAudioDeviceSetup();
    auto next = previous; next.sampleRate = rate;
    auto error = manager.setAudioDeviceSetup(next, true);
    device = manager.getCurrentAudioDevice();
    if (error.isEmpty() && device != nullptr && std::abs(device->getCurrentSampleRate() - rate) < 1)
        return juce::Result::ok();
    const auto rollback = manager.setAudioDeviceSetup(previous, true);
    if (error.isEmpty()) error = "The audio device could not apply this sample rate.";
    if (rollback.isNotEmpty()) error += " Restore failed: " + rollback;
    return juce::Result::fail(error);
}
}
