#include "../Source/PresetCategories.h"
#include <CoreFoundation/CoreFoundation.h>
void checkPresetCategories() {
    struct Folder {
        juce::File path = juce::File::getCurrentWorkingDirectory().getChildFile("category-check-" + juce::Uuid().toString());
        ~Folder() { path.deleteRecursively(); }
    } folder;
    require(folder.path.createDirectory().wasOk(), "Could not create category fixture folder");
    SineStack p, restored;
    require(p.loadFactoryPreset(0).wasOk() && p.getPresetCategory() == "Bass", "Factory category missing");
    const auto bass = folder.path.getChildFile("Bass Test.sinestack");
    const auto lead = folder.path.getChildFile("Lead Test.sinestack");
    const auto legacy = folder.path.getChildFile("Legacy.sinestack");
    require(p.savePreset(bass, " bass ").wasOk() && p.getPresetCategory() == "Bass", "Category canonicalisation failed");
    require(juce::JSON::parse(bass)["category"].toString() == "Bass", "Category not stored in readable preset metadata");
    const auto beforeFailedSave = p.state.copyState().toXmlString();
    require(p.savePreset(folder.path.getChildFile("missing/fail.sinestack"), "Pad").failed()
            && beforeFailedSave == p.state.copyState().toXmlString(), "Failed save changed category or preset identity");
    require(p.savePreset(lead, "Lead").wasOk(), "Could not save Lead category");
    juce::MemoryBlock state; p.getStateInformation(state);
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.getPresetCategory() == "Lead", "Host state lost preset category");
    require(restored.loadPreset(bass).wasOk() && restored.getPresetCategory() == "Bass", "File load lost category");
    auto old = juce::JSON::parse(bass); old.getDynamicObject()->removeProperty("category");
    require(legacy.replaceWithText(juce::JSON::toString(old)), "Could not write legacy category fixture");
    require(restored.loadPreset(legacy).wasOk() && restored.getPresetCategory() == "Uncategorized", "Legacy preset inherited the previous category");
    for (const juce::var unknown : {juce::var(19), juce::var("Future category")}) {
        old.getDynamicObject()->setProperty("category", unknown);
        const auto malformed = folder.path.getChildFile("unknown.sinestack"); malformed.replaceWithText(juce::JSON::toString(old));
        require(restored.loadPreset(malformed).wasOk() && restored.getPresetCategory() == "Uncategorized", "Unknown optional metadata broke sound loading");
        malformed.deleteFile();
    }
    require(p.loadFactoryPreset(0).wasOk(), "Could not load browser fixture");
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    auto box = [&](const char* name) {
        auto* value = dynamic_cast<juce::ComboBox*>(findControl(*editor, name));
        require(value != nullptr, "Missing preset category browser control"); return value;
    };
    auto* bank = box("Preset bank"); auto* filter = box("Preset category filter"); auto* list = box("Preset list");
    auto* next = dynamic_cast<juce::TextButton*>(findControl(*editor, "Next preset"));
    auto* previous = dynamic_cast<juce::TextButton*>(findControl(*editor, "Previous preset"));
    auto* refresh = dynamic_cast<juce::TextButton*>(findControl(*editor, "Refresh presets"));
    auto selectCategory = [&](const char* category) { filter->setSelectedId(PresetCategories::index(category) + 2, juce::sendNotificationSync); };
    const auto unchanged = p.state.copyState().toXmlString();
    selectCategory("Bass");
    require(list->getNumItems() == 2 && p.state.copyState().toXmlString() == unchanged, "Category filtering changed the sound or count");
    next->onClick(); require(p.getFactoryPresetIndex() == 1, "Filtered Next selected the wrong underlying factory sound");
    next->onClick(); require(p.getFactoryPresetIndex() == 0 && list->getNumItems() == 2, "Filtered Next escaped Bass");
    previous->onClick(); require(p.getFactoryPresetIndex() == 1, "Filtered Previous failed to wrap");
    selectCategory("Pad"); require(list->getNumItems() == 2 && list->getSelectedId() == 0, "Pad bank does not contain both pads");
    list->setSelectedId(2, juce::sendNotificationSync);
    require(p.getFactoryPresetIndex() == 6 && p.getPresetCategory() == "Pad", "Filtered menu index was mistaken for factory index");
    const auto padState = p.state.copyState().toXmlString();
    selectCategory("FX");
    require(list->getNumItems() == 0 && !next->isEnabled() && !previous->isEnabled() && p.state.copyState().toXmlString() == padState,
            "Empty category changed sound or left navigation enabled");
    // External file loading reveals its result even when the previous filter excludes it.
    require(p.loadPreset(bass).wasOk(), "Could not load user browser fixture"); refresh->onClick();
    require(bank->getSelectedId() == 2 && filter->getSelectedId() == 1 && list->getText() == "Bass Test", "Loaded file remained hidden by the old category");
    selectCategory("Lead"); require(list->getNumItems() == 1 && list->getItemText(0) == "Lead Test", "User category filter failed");
    next->onClick(); require(p.getPresetFile() == lead && !next->isEnabled(), "Single filtered user preset navigation failed");
    selectCategory("Uncategorized"); require(list->getNumItems() == 1 && list->getItemText(0) == "Legacy", "Old user presets are not discoverable as Uncategorized");
    // Explicit Refresh must observe edits even if a filesystem preserves size/timestamp.
    selectCategory("Bass"); require(list->getNumItems() == 1, "Bass fixture missing");
    const auto stamp = bass.getLastModificationTime();
    auto changed = juce::JSON::parse(bass); changed.getDynamicObject()->setProperty("category", "Lead");
    bass.replaceWithText(juce::JSON::toString(changed, false) + "\n"); bass.setLastModificationTime(stamp);
    refresh->onClick(); require(list->getNumItems() == 0, "Refresh retained a stale category cache");
    selectCategory("Lead"); require(list->getNumItems() == 2, "Updated metadata is absent from its new category");
    bank->setSelectedId(1, juce::sendNotificationSync); selectCategory("Pad");
    list->setSelectedId(1, juce::sendNotificationSync);
    const auto preview = editor->createComponentSnapshot(editor->getLocalBounds());
    juce::FileOutputStream png(juce::File::getCurrentWorkingDirectory().getChildFile("preset-categories.png"));
    png.setPosition(0); png.truncate(); juce::PNGImageFormat().writeImageToStream(preview, png);
    require(bank->getRight() < filter->getX() && filter->getRight() < previous->getX()
            && previous->getRight() < list->getX() && list->getRight() < next->getX(), "Category controls overlap");
    std::cout << "PASS: Factory/User category filters, mapped navigation, legacy/unknown metadata, category persistence, cache refresh\n";
    // JUCE AlertWindow needs a connected display; CI can still exercise the offscreen browser.
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) {
        std::cout << "SKIP: Save dialog lifecycle requires a desktop display\n";
        return;
    }
    // Inspect the real save UI and cancel before opening the native file picker.
    auto* save = dynamic_cast<juce::TextButton*>(findControl(*editor, "Save Preset")); require(save, "Missing Save Preset");
    const auto beforeCancel = p.state.copyState().toXmlString(); save->onClick();
    auto* dialog = dynamic_cast<juce::AlertWindow*>(juce::Component::getCurrentlyModalComponent());
    require(dialog && dialog->getComboBoxComponent("category") && dialog->getComboBoxComponent("category")->getText() == "Pad",
            "Save dialog did not default to the current category");
    auto* category = dialog->getComboBoxComponent("category"); category->setSelectedItemIndex(PresetCategories::index("Lead"));
    juce::FileOutputStream dialogPng(juce::File::getCurrentWorkingDirectory().getChildFile("preset-category-save.png"));
    dialogPng.setPosition(0); dialogPng.truncate(); juce::PNGImageFormat().writeImageToStream(dialog->createComponentSnapshot(dialog->getLocalBounds()), dialogPng);
    dialog->exitModalState(0); CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.03, false);
    require(save->isEnabled() && p.state.copyState().toXmlString() == beforeCancel, "Cancelling category selection changed sound or left controls disabled");
    save->onClick(); editor.reset(); CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.03, false);
    require(juce::Component::getNumCurrentlyModalComponents() == 0, "Closing the editor left the category dialog alive");
    std::cout << "PASS: Save category dialog defaults, cancellation and editor lifetime\n";
}
