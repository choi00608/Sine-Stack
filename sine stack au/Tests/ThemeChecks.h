#include "../Source/DesignTheme.h"
void checkDesignThemes() {
    struct PreferencesScope {
        juce::File folder = juce::File::getCurrentWorkingDirectory().getChildFile("theme-check-" + juce::Uuid().toString());
        bool hadOverride = std::getenv("SINESTACK_APPEARANCE_FILE") != nullptr;
        std::string previous = hadOverride ? std::getenv("SINESTACK_APPEARANCE_FILE") : "";
        PreferencesScope() { setenv("SINESTACK_APPEARANCE_FILE", folder.getChildFile("appearance.json").getFullPathName().toRawUTF8(), 1); }
        ~PreferencesScope() {
            if (hadOverride) setenv("SINESTACK_APPEARANCE_FILE", previous.c_str(), 1); else unsetenv("SINESTACK_APPEARANCE_FILE");
            folder.deleteRecursively();
        }
    } preferences;
    SineStack p, other;
    const int eq = p.addBlock(8);
    p.addBlock(3); p.addBlock(106);
    setParameter(p, ("fx" + juce::String(eq) + "eqLowGain").toRawUTF8(), 6);
    const auto sound = sourceRender(p);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor()), independent(other.createEditor());
    auto selector = [](juce::Component& root) {
        auto* box = dynamic_cast<juce::ComboBox*>(findControl(root, "Design preset"));
        require(box != nullptr, "Missing design preset menu"); return box;
    };
    auto* menu = selector(*editor);
    require(menu->getNumItems() == 7 && menu->getSelectedId() == 1, "New users must start with Synthwave and seven themes");
    auto* tile = findControl(*editor, "Chain block " + juce::String(eq));
    require(tile != nullptr, "Missing theme test EQ tile");
    tile->mouseDown(chainMouse(*tile, {20, 20})); tile->mouseUp(chainMouse(*tile, {20, 20}));
    const auto originalState = p.state.copyState().toXmlString();
    for (int index = 0; index < 7; ++index) {
        menu->setSelectedId(index + 1, juce::sendNotificationSync);
        if (index == 0) menu->onChange();
        require(selector(*independent)->getSelectedId() == 1, "Theme selection leaked into an existing editor");
        require(p.state.copyState().toXmlString() == originalState, "Appearance changed sound parameters or chain");
        require(DesignTheme::savedIndex() == index, "Design choice was not saved");
        const auto accent = juce::Colour(DesignTheme::presets[index].colours[DesignTheme::accent]);
        auto* bypass = findControl(*editor, "Chain bypass " + juce::String(eq));
        require(bypass && bypass->findColour(juce::TextButton::buttonColourId) == accent, "Tile bypass colour failed to update");
        require(editor->getLookAndFeel().findColour(juce::PopupMenu::highlightedBackgroundColourId) == accent,
                "Popup menu did not follow design");
        for (int tick = 0; tick < 3; ++tick) juce::Timer::callPendingTimersSynchronously();
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1);
        if (index != 0) require(image.getPixelAt(5, 5) == juce::Colour(DesignTheme::presets[index].colours[DesignTheme::background]),
                                "Editor background did not change");
        juce::FileOutputStream output(juce::File::getCurrentWorkingDirectory().getChildFile("design-" + juce::String(DesignTheme::presets[index].key) + ".png"));
        output.setPosition(0); output.truncate(); juce::PNGImageFormat().writeImageToStream(image, output);
        auto* graph = findControl(*editor, "EQ frequency response");
        require(graph && graph->isVisible(), "Theme switching hid the EQ graph");
        require(menu->getBounds().getRight() <= findControl(*editor, "Save Preset")->getX(), "Design menu overlaps preset actions");
    }
    requireSameAudio(sound, sourceRender(p), "Theme selection changed rendered audio");
    editor.reset(); editor.reset(p.createEditor());
    require(selector(*editor)->getSelectedId() == 7, "Reopening the editor lost the design choice");
    const auto preset = preferences.folder.getChildFile("sound.sinestack");
    require(p.savePreset(preset).wasOk(), "Could not save theme independence fixture");
    selector(*editor)->setSelectedId(2, juce::sendNotificationSync);
    require(p.loadPreset(preset).wasOk() && selector(*editor)->getSelectedId() == 2, "Loading sound changed the design");
    // Malformed or unknown preferences fall back safely to the default.
    require(DesignTheme::preferencesFile().replaceWithText("{\"design\":\"future-theme\"}"), "Could not write unknown design");
    require(DesignTheme::savedIndex() == 0, "Unknown design did not fall back to Synthwave");
    require(DesignTheme::preferencesFile().replaceWithText("broken json"), "Could not write malformed design");
    require(DesignTheme::savedIndex() == 0, "Malformed preferences did not fall back to Synthwave");
    std::cout << "PASS: seven designs, live tiles/graphs/menus, editor isolation, preference persistence, unchanged audio/presets, safe fallback\n";
}
