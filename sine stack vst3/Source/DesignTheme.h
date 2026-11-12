#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>

class DesignTheme final : public juce::LookAndFeel_V4 {
public:
    enum Role { background, panel, inset, graph, control, selected, border, track,
                accent, secondary, tertiary, text, muted, disabled, indicator, roleCount };
    struct Preset { const char* key; const char* name; std::array<juce::uint32, roleCount> colours; };
    inline static constexpr std::array<Preset, 7> presets {{
        {"synthwave", "Synthwave", {0xff100b1b,0xff1c122d,0xff140d22,0xff160e25,0xff2a1b40,0xff442451,0xff69477f,0xff493453,0xfff879d5,0xffbb95ff,0xff70e4ed,0xfff5eaff,0xffc2accf,0xff8d7b9a,0xffa17ac0}},
        {"classic-green", "Classic Green", {0xff141c18,0xff202a24,0xff18211b,0xff17231b,0xff29322e,0xff33442d,0xff46524a,0xff3c4541,0xffb4e78a,0xff82cce7,0xffe9b97b,0xffeef3e9,0xffaebbb2,0xff859187,0xff819b70}},
        {"arctic", "Arctic Ice", {0xff090f1c,0xff111f30,0xff0a1524,0xff0d1929,0xff1c2d42,0xff1b4554,0xff3f6077,0xff2b4358,0xff7ee7fa,0xffaabfff,0xffffcf8b,0xffedf8ff,0xffacc3d6,0xff7b93a8,0xff609eb8}},
        {"solar", "Solar Flare", {0xff1b1010,0xff2a1b19,0xff211211,0xff241512,0xff3b2722,0xff573628,0xff7f5644,0xff553b31,0xffffb15d,0xffff7f8a,0xffffdf9b,0xfffff1df,0xffd5b7a3,0xffa58a79,0xffbb8353}},
        {"ocean", "Ocean Depths", {0xff07191e,0xff102a32,0xff0a2028,0xff0c242c,0xff183b45,0xff1e5059,0xff3d6872,0xff2b4d58,0xff62e2c4,0xff70baff,0xffffc7a1,0xffe6f8f7,0xff9fc5ce,0xff70969f,0xff479eaa}},
        {"rose", "Rose Quartz", {0xfff5edf3,0xfffff9fd,0xffeee3ed,0xfffffafe,0xfff0e4ef,0xffe7cfe2,0xffb18da8,0xffdac5d6,0xff972d75,0xff6744a5,0xff0b7b86,0xff39273c,0xff73586e,0xff8f7c8d,0xff9b5b89}},
        {"mono", "Graphite", {0xff111214,0xff1c1e22,0xff15171a,0xff17191d,0xff292c31,0xff3e424a,0xff5b606b,0xff3a3e46,0xffe3e6ed,0xffa5b1c9,0xffcbbfac,0xfff5f6f8,0xffb3b7c0,0xff818692,0xff9299a6}}
    }};
    DesignTheme() { select(0); }
    int index() const { return current; }
    juce::Colour colour(Role role) const { return juce::Colour(presets[current].colours[role]); }
    void select(int index) {
        current = juce::jlimit(0, static_cast<int>(presets.size()) - 1, index);
        using namespace juce;
        setColour(Slider::rotarySliderFillColourId, colour(accent));
        setColour(Slider::rotarySliderOutlineColourId, colour(track));
        setColour(Slider::thumbColourId, colour(text));
        setColour(Slider::trackColourId, colour(accent));
        setColour(Slider::backgroundColourId, colour(track));
        setColour(Slider::textBoxTextColourId, colour(text));
        setColour(Slider::textBoxBackgroundColourId, colour(inset));
        setColour(Slider::textBoxOutlineColourId, colour(border));
        setColour(Slider::textBoxHighlightColourId, colour(accent).withAlpha(.35f));
        setColour(ComboBox::backgroundColourId, colour(control));
        setColour(ComboBox::outlineColourId, colour(border));
        setColour(ComboBox::textColourId, colour(text));
        setColour(ComboBox::arrowColourId, colour(accent));
        setColour(ComboBox::focusedOutlineColourId, colour(accent));
        setColour(Label::textColourId, colour(muted));
        setColour(TextButton::buttonColourId, colour(control));
        setColour(TextButton::buttonOnColourId, colour(selected));
        setColour(TextButton::textColourOffId, colour(text));
        setColour(TextButton::textColourOnId, colour(accent));
        setColour(PopupMenu::backgroundColourId, colour(panel));
        setColour(PopupMenu::textColourId, colour(text));
        setColour(PopupMenu::headerTextColourId, colour(muted));
        setColour(PopupMenu::highlightedBackgroundColourId, colour(accent));
        setColour(PopupMenu::highlightedTextColourId, colour(inset));
        setColour(ScrollBar::thumbColourId, colour(indicator));
        setColour(TextEditor::backgroundColourId, colour(inset));
        setColour(TextEditor::textColourId, colour(text));
        setColour(TextEditor::highlightColourId, colour(accent).withAlpha(.35f));
        setColour(TextEditor::highlightedTextColourId, colour(text));
        setColour(TextEditor::outlineColourId, colour(border));
        setColour(TextEditor::focusedOutlineColourId, colour(accent));
        setColour(CaretComponent::caretColourId, colour(accent));
        setColour(TooltipWindow::backgroundColourId, colour(panel));
        setColour(TooltipWindow::textColourId, colour(text));
        setColour(TooltipWindow::outlineColourId, colour(border));
        setColour(AlertWindow::backgroundColourId, colour(panel));
        setColour(AlertWindow::textColourId, colour(text));
        setColour(AlertWindow::outlineColourId, colour(border));
        setColour(ResizableWindow::backgroundColourId, colour(background));
        setColour(MidiKeyboardComponent::keyDownOverlayColourId, colour(accent).withAlpha(.7f));
        setColour(MidiKeyboardComponent::mouseOverKeyOverlayColourId, colour(secondary).withAlpha(.35f));
    }
    static juce::File preferencesFile() {
        const auto overridePath = juce::SystemStats::getEnvironmentVariable("SINESTACK_APPEARANCE_FILE", {});
        if (overridePath.isNotEmpty()) return juce::File(overridePath);
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support/SineStack/Appearance.settings");
    }
    static int savedIndex() {
        const auto key = juce::JSON::parse(preferencesFile())["design"].toString();
        for (size_t i = 0; i < presets.size(); ++i) if (key == presets[i].key) return static_cast<int>(i);
        return 0;
    }
    static bool saveIndex(int index) {
        if (index < 0 || index >= static_cast<int>(presets.size())) return false;
        const auto file = preferencesFile();
        if (file.getParentDirectory().createDirectory().failed()) return false;
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty("design", presets[index].key);
        return file.replaceWithText(juce::JSON::toString(juce::var(object.release())));
    }
private:
    int current = 0;
};
inline juce::Colour uiColour(const juce::Component& component, DesignTheme::Role role) {
    if (auto* theme = dynamic_cast<DesignTheme*>(&component.getLookAndFeel())) return theme->colour(role);
    return juce::Colour(DesignTheme::presets[0].colours[role]);
}
