#include "Plugin.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace {
class Holder final : public juce::StandalonePluginHolder {
public:
    Holder(juce::PropertySet* settings, const juce::AudioDeviceManager::AudioDeviceSetup& preferred)
        : StandalonePluginHolder(settings, false, {}, &preferred) { connect(); }
    void createPlugin() override { StandalonePluginHolder::createPlugin(); connect(); }
private:
    void connect() {
        if (auto* synth = dynamic_cast<SineStack*>(processor.get())) synth->standaloneDevices = &deviceManager;
    }
};
class SineStackApp final : public juce::JUCEApplication {
    juce::ApplicationProperties properties;
    std::unique_ptr<juce::StandaloneFilterWindow> window;
public:
    SineStackApp() {
        juce::PropertiesFile::Options options;
        options.applicationName = "Sine Stack"; options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters(options);
    }
    const juce::String getApplicationName() override { return "Sine Stack"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}
    void initialise(const juce::String&) override {
        juce::AudioDeviceManager::AudioDeviceSetup preferred;
        preferred.sampleRate = 48000;
        auto holder = std::make_unique<Holder>(properties.getUserSettings(), preferred);
        window = std::make_unique<juce::StandaloneFilterWindow>(getApplicationName(), juce::Colour(0xff171d1a), std::move(holder));
        window->setVisible(true);
    }
    void shutdown() override { window.reset(); properties.saveIfNeeded(); }
    void systemRequestedQuit() override {
        if (window) window->pluginHolder->savePluginState();
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents()) {
            juce::Timer::callAfterDelay(100, [] { if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit(); });
        } else quit();
    }
};
}
JUCE_CREATE_APPLICATION_DEFINE(SineStackApp)
