// Simulate real device restart/failure through JUCE's AudioDeviceManager, without
// touching the user's audio hardware or global device settings.
struct TestAudioState { bool reject48 = false, ignore48 = false; int starts = 0; };
class TestAudioDevice final : public juce::AudioIODevice {
    TestAudioState& state;
    bool opened = false, playing = false;
    double rate = 44100;
    juce::AudioIODeviceCallback* callback = nullptr;
public:
    explicit TestAudioDevice(TestAudioState& s) : AudioIODevice("Test output", "Test"), state(s) {}
    juce::StringArray getOutputChannelNames() override { return {"Left", "Right"}; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return {44100, 48000, 96000}; }
    juce::Array<int> getAvailableBufferSizes() override { return {256}; }
    int getDefaultBufferSize() override { return 256; }
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double next, int) override {
        if (next == 48000 && state.reject48) return "Simulated device busy";
        opened = true; rate = next == 48000 && state.ignore48 ? 44100 : next; return {};
    }
    void close() override { stop(); opened = false; }
    bool isOpen() override { return opened; }
    void start(juce::AudioIODeviceCallback* next) override {
        callback = next; playing = true; ++state.starts;
        if (callback) callback->audioDeviceAboutToStart(this);
    }
    void stop() override { if (callback) callback->audioDeviceStopped(); callback = nullptr; playing = false; }
    bool isPlaying() override { return playing; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 256; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { juce::BigInteger channels; channels.setRange(0, 2, true); return channels; }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};
class TestAudioType final : public juce::AudioIODeviceType {
    TestAudioState& state;
public:
    explicit TestAudioType(TestAudioState& s) : AudioIODeviceType("Test"), state(s) {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool = false) const override { return {"Test output"}; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice(const juce::String&, const juce::String&) override { return new TestAudioDevice(state); }
};
class TestAudioManager final : public juce::AudioDeviceManager {
public:
    TestAudioState testState;
    void createAudioDeviceTypes(juce::OwnedArray<juce::AudioIODeviceType>& types) override { types.add(new TestAudioType(testState)); }
};
void checkSampleRates() {
    TestAudioManager manager;
    juce::AudioDeviceManager::AudioDeviceSetup setup; setup.sampleRate = 44100; setup.bufferSize = 256;
    require(manager.initialise(0, 2, nullptr, false, {}, &setup).isEmpty(), "Test device failed to open");
    require(AudioSettings::rates(&manager).contains(48000), "Missing 48 kHz option");
    require(AudioSettings::setRate(manager, 48000).wasOk(), "48 kHz could not be selected");
    require(manager.getCurrentAudioDevice()->getCurrentSampleRate() == 48000 && manager.testState.starts >= 2, "Device did not restart at 48 kHz");
    require(AudioSettings::setRate(manager, 88200).failed(), "Unsupported rate was accepted");
    require(manager.getCurrentAudioDevice()->getCurrentSampleRate() == 48000, "Unsupported rate changed device");
    require(AudioSettings::setRate(manager, 44100).wasOk(), "44.1 kHz could not be restored");
    manager.testState.reject48 = true;
    require(AudioSettings::setRate(manager, 48000).failed(), "Failed device opening was not reported");
    require(manager.getCurrentAudioDevice() && manager.getCurrentAudioDevice()->getCurrentSampleRate() == 44100 && manager.getCurrentAudioDevice()->isOpen(), "Failed rate did not restore previous device");
    manager.testState.reject48 = false; manager.testState.ignore48 = true;
    require(AudioSettings::setRate(manager, 48000).failed(), "Silently ignored rate was accepted");
    manager.testState.ignore48 = false;
    SineStack p; p.standaloneDevices = &manager;
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto* box = dynamic_cast<juce::ComboBox*>(findControl(*editor, "Sample rate"));
    require(box && box->isEnabled(), "Standalone sample rate selector is disabled");
    box->setSelectedId(48000, juce::sendNotificationSync);
    require(manager.getCurrentAudioDevice()->getCurrentSampleRate() == 48000 && box->getText() == "48000 Hz", "Sample rate UI did not update the real device setup");
    std::cout << "PASS: sample rate selection, device restart, unsupported rates and failure rollback\n";
}
