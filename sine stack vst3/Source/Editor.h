#include "AudioSettings.h"
// Included in Plugin.cpp's private editor namespace.
constexpr int chainTilePitch = 170;
class ChainSurface final : public juce::Component {
public:
    int count = 0; bool arrows = true;
    void paint(juce::Graphics& g) override {
        if (!arrows) return;
        g.setColour(uiColour(*this, DesignTheme::indicator));
        for (int n = 0; n + 1 < count; ++n) {
            const float x = static_cast<float>(n * chainTilePitch + 153);
            g.drawLine(x, 36, x + 11, 36, 1.4f);
            g.drawLine(x + 7, 32, x + 11, 36, 1.4f);
            g.drawLine(x + 7, 40, x + 11, 36, 1.4f);
        }
    }
};
class ChainTile final : public juce::Component {
    juce::TextButton bypassButton {"ON"}, deleteButton {"x"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
public:
    int id = -1, position = 0;
    bool selected = false, bypassed = false, movable = true;
    juce::String title, category;
    std::function<void()> onSelect, onRemove;
    std::function<void(juce::Point<float>, bool)> onDrag;
    juce::Point<float> origin, grabPoint;
    bool dragging = false, dragCancelled = false;
    ChainTile(SineStack& processor, int blockId) : id(blockId) {
        addAndMakeVisible(bypassButton); addAndMakeVisible(deleteButton);
        setWantsKeyboardFocus(true);
        deleteButton.setName((processor.blockKind(id) == SineStack::lfoBlock ? "Modulation delete " : "Chain delete ") + juce::String(id));
        deleteButton.setTooltip("Remove this block");
        deleteButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        deleteButton.onClick = [this] { if (onRemove) onRemove(); };
        bypassButton.setName((processor.blockKind(id) == SineStack::lfoBlock ? "Modulation bypass " : "Chain bypass ") + juce::String(id));
        bypassButton.setClickingTogglesState(true);
        bypassButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        bypassButton.onStateChange = [this] {
            bypassed = bypassButton.getToggleState();
            bypassButton.setButtonText(bypassed ? "BYPASSED" : "ON");
            bypassButton.setTooltip(bypassed ? "Enable this block" : "Bypass this block");
            repaint();
        };
        bypassButton.setTooltip("Bypass this block");
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor.state, processor.blockBypassParameter(id), bypassButton);
    }
    void parentHierarchyChanged() override { lookAndFeelChanged(); }
    void lookAndFeelChanged() override {
        bypassButton.setColour(juce::TextButton::buttonColourId, uiColour(*this, DesignTheme::accent));
        bypassButton.setColour(juce::TextButton::textColourOffId, uiColour(*this, DesignTheme::inset));
        bypassButton.setColour(juce::TextButton::buttonOnColourId, uiColour(*this, DesignTheme::track));
        bypassButton.setColour(juce::TextButton::textColourOnId, uiColour(*this, DesignTheme::muted));
    }
    void resized() override { bypassButton.setBounds(10, 48, 94, 20); deleteButton.setBounds(120, 5, 22, 20); }
    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat().reduced(1);
        g.setColour((selected ? uiColour(*this, DesignTheme::selected) : uiColour(*this, DesignTheme::control))); g.fillRoundedRectangle(bounds, 8);
        g.setColour((selected ? uiColour(*this, DesignTheme::accent) : uiColour(*this, DesignTheme::border))); g.drawRoundedRectangle(bounds, 8, selected ? 2.0f : 1.0f);
        g.setFont(10.0f); g.setColour(uiColour(*this, DesignTheme::muted));
        g.drawText(juce::String(position + 1).paddedLeft('0', 2) + " / " + category, 12, 7, getWidth() - 48, 16, juce::Justification::centredLeft);
        g.setFont(14.0f); g.setColour((bypassed ? uiColour(*this, DesignTheme::disabled) : uiColour(*this, DesignTheme::text)));
        g.drawFittedText(title, 12, 26, getWidth() - 24, 21, juce::Justification::centredLeft, 1);
        g.setFont(10.0f); g.setColour((bypassed ? uiColour(*this, DesignTheme::disabled) : uiColour(*this, DesignTheme::accent)));
        if (movable) for (int row = 0; row < 3; ++row) g.drawHorizontalLine(53 + row * 4, getWidth() - 25.0f, getWidth() - 13.0f);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!e.mods.isLeftButtonDown()) return;
        origin = e.getEventRelativeTo(getParentComponent()).position; grabPoint = e.position; dragging = false; dragCancelled = false;
        if (onSelect) onSelect();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!movable || dragCancelled) return;
        const auto point = e.getEventRelativeTo(getParentComponent()).position;
        if (point.getDistanceFrom(origin) > 5) dragging = true;
        if (dragging && onDrag) onDrag(point, false);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (dragging && onDrag) onDrag(e.getEventRelativeTo(getParentComponent()).position, true);
        dragging = false;
    }
};

class Editor final : public juce::AudioProcessorEditor, private juce::ChangeListener, private juce::Timer {
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };
    struct Choice {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboAttachment> attachment;
        std::unique_ptr<juce::ParameterAttachment> divisionAttachment, rhythmAttachment;
    };
    struct RouteRow final : juce::Component {
        juce::ComboBox target, parameter;
        juce::Slider depth;
        juce::TextButton remove {"x"};
        juce::Label number;
        int slot = 0;
        std::unique_ptr<SliderAttachment> attachment;
        RouteRow() {
            for (auto* c : std::initializer_list<juce::Component*>{&target, &parameter, &depth, &remove, &number}) addAndMakeVisible(c);
        }
        void resized() override {
            number.setBounds(0, 8, 22, 24); target.setBounds(24, 7, 184, 28);
            parameter.setBounds(214, 7, 174, 28); depth.setBounds(394, 5, 142, 32); remove.setBounds(544, 7, 28, 28);
        }
    };
    juce::Viewport routeViewport;
    juce::Component routeContent;
    std::vector<std::unique_ptr<RouteRow>> routeRows;
    juce::TextButton addRouteButton {"+ Target"};
    juce::Label routeHeading, routeParameterHeading, routeDepthHeading;
    int revealRoute = -1;
    DesignTheme theme;
    juce::ComboBox designList;
    juce::Label designLabel, buildLabel;
    juce::TextButton scopeButton {"Output Scope"};
    std::unique_ptr<OutputScopeWindow> scopeWindow;
    SineStack& processor;
    EnvelopeGraph envelopeGraph;
    EqGraph eqGraph;
    std::array<Knob, 5> fixedKnobs;
    std::vector<std::unique_ptr<Knob>> inspectorKnobs;
    std::vector<std::unique_ptr<Choice>> inspectorChoices;
    juce::Label inspectorTitle, inspectorCategory, inspectorDescription, routingLabel;
    juce::TextButton bypassButton {"ON"}, duplicateButton {"Duplicate"}, removeButton {"Remove"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    juce::Viewport chainViewport;
    ChainSurface chainContent;
    juce::Viewport modViewport;
    ChainSurface modContent;
    std::vector<std::unique_ptr<ChainTile>> modTiles;
    juce::TextButton addLfoButton {"+ LFO"};
    juce::String modSignature;
    juce::ComponentAnimator animator;
    std::vector<std::unique_ptr<ChainTile>> tiles;
    juce::TextButton addButton {"+ Add"};
    int selectedId = -1, inspectorType = -1, dragTarget = -1;
    int dragId = -1, dragFrom = -1, animationTick = 0;
    juce::Point<float> dragPointer, dragGrab;
    juce::Image dragImage;
    juce::String dragSignature;
    juce::String chainSignature, lfoInspectorSignature;
    juce::MidiKeyboardComponent keyboard;
    juce::TooltipWindow tooltips {this, 600};
    juce::TextButton savePresetButton {"Save Preset"}, loadPresetButton {"Load Preset"};
    juce::TextButton refreshPresetButton {"Refresh"}, previousPresetButton {"<"}, nextPresetButton {">"};
    juce::ComboBox pitchLatencyList;
    std::unique_ptr<ComboAttachment> pitchLatencyAttachment;
    juce::ComboBox voiceModeList;
    std::unique_ptr<juce::ParameterAttachment> voiceModeAttachment, legatoAttachment;
    bool monoSelected = false, legatoSelected = false;
    juce::ComboBox sampleRateList;
    juce::Label sampleRateLabel;
    juce::String sampleRateSignature;
    int eqBand = 0;
    juce::ComboBox presetList, presetBank, presetCategory;
    juce::Array<int> visibleFactoryPresets;
    PresetCategories::Cache categoryCache;
    std::unique_ptr<juce::AlertWindow> presetCategoryDialog;
    int lastFactoryPreset = -1;
    juce::File userPresetFolder;
    juce::Array<juce::File> presetFiles;
    bool presetBusy = false;
    juce::Label presetStatus;
    juce::File lastPresetFile;
    std::unique_ptr<juce::FileChooser> presetChooser;
public:
    explicit Editor(SineStack& p) : AudioProcessorEditor(p), processor(p), eqGraph(p), keyboard(p.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard) {
        theme.select(DesignTheme::savedIndex());
        setLookAndFeel(&theme);
        for (auto* component : std::initializer_list<juce::Component*>{&savePresetButton, &loadPresetButton, &refreshPresetButton,
                &previousPresetButton, &nextPresetButton, &presetList, &presetBank, &presetCategory, &presetStatus, &scopeButton, &envelopeGraph, &designList, &designLabel, &buildLabel,
                &inspectorTitle, &inspectorCategory, &inspectorDescription, &bypassButton, &duplicateButton, &removeButton,
                &routeViewport, &addRouteButton, &routeHeading, &routeParameterHeading, &routeDepthHeading, &chainViewport, &addButton, &modViewport, &addLfoButton, &routingLabel, &keyboard, &sampleRateList, &sampleRateLabel, &voiceModeList, &pitchLatencyList})
            addAndMakeVisible(component);
        scopeButton.setName("Open output scope");
        scopeButton.setTooltip("View the final stereo output waveform while editing your sound.");
        scopeButton.onClick = [this] {
            if (!scopeWindow) scopeWindow = std::make_unique<OutputScopeWindow>(processor.outputCapture, theme);
            scopeWindow->show();
        };
        buildLabel.setName("Version and build date");
        buildLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
        buildLabel.setJustificationType(juce::Justification::centredRight);
        const auto builtAt = juce::Time::fromISO8601(BuildInfo::timestampUTC());
        buildLabel.setText("v" + juce::String(BuildInfo::version()) + "  |  Built "
            + builtAt.formatted("%Y-%m-%d %H:%M:%S %Z"), juce::dontSendNotification);
        buildLabel.setTooltip("This running build: v" + juce::String(BuildInfo::version())
            + " / " + BuildInfo::timestampUTC()
            + "\nBuild time shown in your local time zone. Reopen the app or DAW after an update.");
        designLabel.setText("DESIGN", juce::dontSendNotification);
        designLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
        designList.setName("Design preset");
        for (size_t i = 0; i < DesignTheme::presets.size(); ++i)
            designList.addItem(DesignTheme::presets[i].name, static_cast<int>(i) + 1);
        designList.setSelectedId(theme.index() + 1, juce::dontSendNotification);
        designList.setTooltip("Appearance only. Remembered for new windows across AU, VST3 and Standalone.");
        designList.onChange = [this] {
            if (designList.getSelectedId() <= 0) return;
            if (dragId >= 0) finishDrag(false);
            theme.select(designList.getSelectedId() - 1);
            updateDesignColours();
            sendLookAndFeelChange(); repaint();
            if (scopeWindow) { scopeWindow->sendLookAndFeelChange(); scopeWindow->repaint(); }
            const bool saved = DesignTheme::saveIndex(theme.index());
            designList.setTooltip(saved ? "Appearance saved for new windows. Sound presets are unchanged."
                                        : "Appearance changed for this window, but the preference file could not be saved.");
        };
        pitchLatencyList.setName("Pitch Latency");
        pitchLatencyList.addItemList({"Live / 12 ms", "Detail / 48 ms"}, 1);
        pitchLatencyList.setTooltip("Wet latency per Unison, Harmonizer or Glide. LFO control signals add no audio delay. Live responds faster; Detail favours low notes and dense chords. Serial pitch paths add latency. Dry audio and other LFO routes stay immediate. Mode changes crossfade without stopping notes.");
        pitchLatencyAttachment = std::make_unique<ComboAttachment>(processor.state, "pitchLatency", pitchLatencyList);
        voiceModeList.setName("Voice Mode");
        voiceModeList.addItemList({"Poly (8 voices)", "Mono (Retrigger)", "Mono (Legato)"}, 1);
        voiceModeList.setTooltip("Retrigger restarts ADSR on each note. Legato keeps ADSR, oscillator phases and first-note velocity while notes overlap, "
            "including pedal-held notes; Glide moves from the current pitch. A gap starts a new envelope. FX tails remain in both modes.");
        auto refreshMode = [this] { voiceModeList.setSelectedId(monoSelected ? (legatoSelected ? 3 : 2) : 1, juce::dontSendNotification); };
        voiceModeAttachment = std::make_unique<juce::ParameterAttachment>(*processor.state.getParameter("voiceMode"),
            [this, refreshMode](float value) { monoSelected = value >= 0.5f; refreshMode(); });
        legatoAttachment = std::make_unique<juce::ParameterAttachment>(*processor.state.getParameter("monoLegato"),
            [this, refreshMode](float value) { legatoSelected = value >= 0.5f; refreshMode(); });
        voiceModeAttachment->sendInitialUpdate(); legatoAttachment->sendInitialUpdate();
        voiceModeList.onChange = [this] {
            const int selection = voiceModeList.getSelectedId();
            legatoAttachment->setValueAsCompleteGesture(selection == 3 ? 1.0f : 0.0f);
            voiceModeAttachment->setValueAsCompleteGesture(selection == 1 ? 0.0f : 1.0f);
        };
        addChildComponent(eqGraph);
        eqGraph.onBeforeDrag = [this] { processor.materialiseChain(); refreshChain(); };
        eqGraph.onBandSelected = [this](int band) { selectEqBand(band); };
        sampleRateLabel.setText("SAMPLE RATE", juce::dontSendNotification);
        sampleRateLabel.setFont(juce::Font(juce::FontOptions(10.0f)));

        sampleRateList.setName("Sample rate");
        sampleRateList.onChange = [this] {
            if (processor.standaloneDevices == nullptr || sampleRateList.getSelectedId() <= 0) return;
            const auto result = AudioSettings::setRate(*processor.standaloneDevices, sampleRateList.getSelectedId());
            sampleRateSignature.clear(); refreshSampleRate();
            if (result.failed()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Sample rate", result.getErrorMessage());
        };
        refreshSampleRate();
        presetCategory.setName("Preset category filter");
        presetCategory.addItem("All categories", 1);
        presetCategory.addItemList(PresetCategories::choices(), 2);
        presetCategory.setSelectedId(1, juce::dontSendNotification);
        presetCategory.setTooltip("Filter the current bank by sound category. Arrows stay inside the filtered list.");
        presetCategory.onChange = [this] { refreshPresetList(); };
        presetBank.setName("Preset bank");
        presetBank.addItemList({"Factory", "User"}, 1);
        presetBank.setSelectedId(processor.getPresetFile() == juce::File{} ? 1 : 2, juce::dontSendNotification);
        presetBank.setTooltip("Factory: 10 built-in sounds. User: presets in the current folder.");
        presetBank.onChange = [this] { refreshPresetList(); };
        presetList.setName("Preset list"); refreshPresetButton.setName("Refresh presets");
        previousPresetButton.setName("Previous preset"); nextPresetButton.setName("Next preset");
        previousPresetButton.setTooltip("Previous preset"); nextPresetButton.setTooltip("Next preset");
        presetList.setTextWhenNothingSelected("Select a preset...");
        presetList.setTextWhenNoChoicesAvailable("No matching presets");
        refreshPresetButton.onClick = [this] { categoryCache.clear(); refreshPresetList(); };
        previousPresetButton.onClick = [this] { stepPreset(-1); }; nextPresetButton.onClick = [this] { stepPreset(1); };
        presetList.onChange = [this] {
            const int index = presetList.getSelectedId() - 1;
            if (index < 0 || index >= presetCount()) return;
            const auto name = presetList.getText();
            const auto result = loadListedPreset(index);
            refreshPresetList(); refreshChain();
            if (result.failed()) {
                presetStatus.setText("Could not load: " + name, juce::dontSendNotification);
                presetStatus.setTooltip(result.getErrorMessage());
            }
        };
        savePresetButton.onClick = [this] { choosePreset(true); }; loadPresetButton.onClick = [this] { choosePreset(false); };
         presetStatus.setFont(juce::Font(juce::FontOptions(12.0f)));
        envelopeGraph.setName("ADSR envelope");
        for (int i = 0; i < 5; ++i) {
            const auto id = juce::StringArray{"attack", "decay", "sustain", "release", "level"}[i];
            setupKnob(fixedKnobs[i], id, juce::StringArray{"ATTACK", "DECAY", "SUSTAIN", "RELEASE", "OUTPUT"}[i]);
            if (i < 4) {
                fixedKnobs[i].slider.setName(id.toUpperCase());
                fixedKnobs[i].slider.onValueChange = [this] { updateEnvelopeGraph(); };
            }
        }
        updateEnvelopeGraph();
        envelopeGraph.onEdit = [this](int index, float value) {
            auto* parameter = processor.state.getParameter(juce::StringArray{"attack", "decay", "sustain", "release"}[index]);
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        };
        envelopeGraph.onGesture = [this](int index, bool begin) {
            auto* parameter = processor.state.getParameter(juce::StringArray{"attack", "decay", "sustain", "release"}[index]);
            if (begin) parameter->beginChangeGesture(); else parameter->endChangeGesture();
        };
        inspectorTitle.setFont(juce::Font(juce::FontOptions(24.0f))); inspectorTitle.setName("Selected block title");
        inspectorCategory.setFont(juce::Font(juce::FontOptions(11.0f))); inspectorCategory.setColour(juce::Label::textColourId, uiColour(*this, DesignTheme::accent));
        inspectorDescription.setFont(juce::Font(juce::FontOptions(12.0f)));
        bypassButton.setName("Selected block bypass"); bypassButton.setClickingTogglesState(true);
        bypassButton.onStateChange = [this] { updateBypassAppearance(); };
        removeButton.setName("Remove selected block");
        removeButton.onClick = [this] { processor.removeBlock(selectedId); refreshChain(); };
        duplicateButton.setName("Duplicate selected block");
        duplicateButton.onClick = [this] {
            const int copy = processor.duplicateBlock(selectedId);
            if (copy < 0) return;
            selectedId = copy; refreshChain(); revealSelected();
        };
        routingLabel.setName("Signal chain order"); routingLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
        routingLabel.setMinimumHorizontalScale(1.0f);

        chainViewport.setName("FX chain viewport"); chainContent.setName("FX chain contents");
        routeViewport.setViewedComponent(&routeContent, false);
        routeViewport.setName("LFO connections"); routeContent.setName("LFO connection rows");
        routeViewport.setScrollBarsShown(true, false); routeViewport.setScrollBarThickness(10);
        routeHeading.setText("TARGET BLOCK", juce::dontSendNotification);
        routeParameterHeading.setText("PARAMETER", juce::dontSendNotification);
        routeDepthHeading.setText("DEPTH", juce::dontSendNotification);
        for (auto* label : {&routeHeading, &routeParameterHeading, &routeDepthHeading}) {
            label->setFont(juce::Font(juce::FontOptions(11.0f)));

        }
        addRouteButton.setName("Add LFO target");
        addRouteButton.onClick = [this] {
            if (processor.blockKind(selectedId) != SineStack::lfoBlock) return;
            for (int r = 1; r < SineStack::maxLfoRoutes; ++r) {
                if (processor.state.getRawParameterValue(processor.sourceParameter(selectedId, SineStack::routeSetting(r, "enabled")))->load() >= 0.5f) continue;
                setRouteParameter(selectedId, SineStack::routeSetting(r, "lfoBlock"), -1);
                setRouteParameter(selectedId, SineStack::routeSetting(r, "lfoParameter"), 0);
                setRouteParameter(selectedId, SineStack::routeSetting(r, "lfoRange"), .25f);
                setRouteParameter(selectedId, SineStack::routeSetting(r, "enabled"), 1);
                revealRoute = r; processor.sendChangeMessage(); break;
            }
        };
        chainViewport.setViewedComponent(&chainContent, false); chainViewport.setScrollBarsShown(false, true);
        chainViewport.setScrollBarThickness(10);
        modViewport.setName("Modulation viewport"); modContent.setName("Modulation contents"); modContent.arrows = false;
        modViewport.setViewedComponent(&modContent, false); modViewport.setScrollBarsShown(false, true); modViewport.setScrollBarThickness(10);
        addLfoButton.setName("Add LFO");
        addLfoButton.onClick = [this] {
            const int id = processor.addBlock(SineStack::lfoBlock);
            if (id < 0) return;
            selectedId = id; refreshChain(); revealSelected();
        };
        addButton.setName("Add block"); addButton.onClick = [this] { showAddMenu(); };
        keyboard.setAvailableRange(36, 96); keyboard.setLowestVisibleKey(36); keyboard.setKeyWidth(968.0f / 36);
        processor.addChangeListener(this);
        updateDesignColours();
        setSize(1016, 844); refreshPresetList(); refreshChain(); startTimerHz(60);
    }
    ~Editor() override {
        scopeWindow.reset();
        stopTimer(); animator.cancelAllAnimations(false); processor.removeChangeListener(this); envelopeGraph.finishDrag(); eqGraph.finishDrag();
        routeViewport.setViewedComponent(nullptr, false); chainViewport.setViewedComponent(nullptr, false); modViewport.setViewedComponent(nullptr, false); setLookAndFeel(nullptr);
    }
private:
    void updateDesignColours() {
        inspectorTitle.setColour(juce::Label::textColourId, theme.colour(DesignTheme::text));
        inspectorCategory.setColour(juce::Label::textColourId, theme.colour(DesignTheme::accent));
    }
    void refreshSampleRate() {
        eqGraph.refreshSampleRate();
        auto* manager = processor.standaloneDevices;
        auto* device = manager != nullptr ? manager->getCurrentAudioDevice() : nullptr;
        const double current = device != nullptr ? device->getCurrentSampleRate() : processor.getSampleRate();
        const auto rates = AudioSettings::rates(manager);
        const auto supported = device != nullptr ? device->getAvailableSampleRates() : juce::Array<double>{};
        juce::String signature = juce::String(current) + (manager != nullptr ? "device" : "host");
        for (auto rate : rates) signature += ":" + juce::String(rate) + (supported.contains(rate) ? "+" : "-");
        if (signature == sampleRateSignature) return;
        sampleRateSignature = signature;
        sampleRateList.clear(juce::dontSendNotification);
        for (auto rate : rates) {
            const int id = juce::roundToInt(rate);
            sampleRateList.addItem(juce::String(id) + " Hz", id);
            sampleRateList.setItemEnabled(id, supported.contains(rate));
        }
        sampleRateList.setEnabled(device != nullptr);
        sampleRateList.setText(current > 0 ? juce::String(current, 0) + " Hz" : "Host", juce::dontSendNotification);
        sampleRateList.setTooltip(manager != nullptr ? "Audio device sample rate. Unavailable rates are disabled."
            : "Sample rate is controlled by your DAW / host audio settings.");
    }
    void setupKnob(Knob& control, const juce::String& id, const juce::String& title) {
        addAndMakeVisible(control.slider); addAndMakeVisible(control.label);
        control.slider.setName(processor.state.getParameter(id)->getName(100));
        control.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        control.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
        control.label.setText(title, juce::dontSendNotification); control.label.setFont(juce::Font(juce::FontOptions(12.0f)));
        control.label.setJustificationType(juce::Justification::centred);

        control.attachment = std::make_unique<SliderAttachment>(processor.state, id, control.slider);
        control.slider.textFromValueFunction = [id](double value) {
            for (const auto& d : SourceSettings::definitions) if (id == d.id || id.endsWith("_" + juce::String(d.id))) {
                const juce::String unit(d.unit);
                if (unit == "%") return juce::String(juce::roundToInt(value * 100)) + "%";
                if (unit == "s" && juce::String(d.id) == "glide") return juce::String(juce::roundToInt(value * 1000)) + " ms";
                if (unit == "Hz") return juce::String(value, value < 100 ? 2 : 0) + " Hz";
                return juce::String(value, unit == "st" || unit == "deg" ? 0 : 1) + " " + unit;
            }
            for (const auto& d : EqSettings::definitions) if (id.endsWith(d.suffix))
                return juce::String(value, juce::String(d.unit) == "Hz" ? 0 : 2) + " " + d.unit;
            for (const auto& detail : FxDetails::definitions) if (id.endsWith(detail.suffix)) {
                const juce::String unit(detail.unit);
                if ((id.endsWith("LowCut") && value <= detail.minimum) || (id.endsWith("HighCut") && value >= detail.maximum)) return juce::String("Off");
                if (unit == "dB") return juce::String(value, 1) + " dB";
                if (unit == "ms") return juce::String(value, 1) + " ms";
                if (unit == "Hz") return juce::String(juce::roundToInt(value)) + " Hz";
                if (unit == "degrees") return juce::String(juce::roundToInt(value)) + juce::String::fromUTF8("°");
            }
            if (id == "attack" || id == "decay" || id == "release") return juce::String(value, 3) + " s";
            if (id == "glide") return juce::String(juce::roundToInt(value * 1000)) + " ms";
            if (id == "detune") return juce::String(value, 1) + " ct";
            if (id.endsWith("delayTime")) return juce::String(juce::roundToInt(value)) + " ms";
            if (id.endsWith("Rate")) return juce::String(value, 2) + " Hz";
            if (id.endsWith("Cutoff")) return juce::String(juce::roundToInt(value)) + " Hz";
            return juce::String(juce::roundToInt(value * 100)) + "%";
        };
        control.slider.valueFromTextFunction = [id](const juce::String& text) {
            for (const auto& d : SourceSettings::definitions) if (id == d.id || id.endsWith("_" + juce::String(d.id))) {
                if (juce::String(d.unit) == "%") return text.getDoubleValue() / 100;
                if (juce::String(d.id) == "glide") return text.getDoubleValue() / 1000;
                return text.getDoubleValue();
            }
            for (const auto& d : EqSettings::definitions) if (id.endsWith(d.suffix)) return text.getDoubleValue();
            for (const auto& detail : FxDetails::definitions) if (id.endsWith(detail.suffix)) {
                const juce::String unit(detail.unit);
                if (text.trim().equalsIgnoreCase("Off")) {
                    if (id.endsWith("LowCut")) return static_cast<double>(detail.minimum);
                    if (id.endsWith("HighCut")) return static_cast<double>(detail.maximum);
                }
                if (unit == "dB" || unit == "ms" || unit == "Hz" || unit == "degrees") return text.getDoubleValue();
            }
            if (id == "glide") return text.getDoubleValue() / 1000;
            if (id == "attack" || id == "decay" || id == "release" || id == "detune" || id.endsWith("delayTime") || id.endsWith("Rate") || id.endsWith("Cutoff")) return text.getDoubleValue();
            return text.getDoubleValue() / 100;
        };
        control.slider.updateText();
    }
    void addKnob(const juce::String& id, const juce::String& title) {
        auto control = std::make_unique<Knob>(); setupKnob(*control, id, title);
        control->slider.onDragStart = [this] { processor.materialiseChain(); };
        inspectorKnobs.push_back(std::move(control));
    }
    void addChoice(const juce::String& id, const juce::String& title) {
        auto control = std::make_unique<Choice>();
        addAndMakeVisible(control->box); addAndMakeVisible(control->label);
        control->box.setName(processor.state.getParameter(id)->getName(100));
        control->box.addItemList(dynamic_cast<juce::AudioParameterChoice*>(processor.state.getParameter(id))->choices, 1);
        control->label.setText(title, juce::dontSendNotification); control->label.setFont(juce::Font(juce::FontOptions(11.0f)));

        control->attachment = std::make_unique<ComboAttachment>(processor.state, id, control->box);
        inspectorChoices.push_back(std::move(control));
    }
    void addBeatChoice(const juce::String& divisionId, const juce::String& rhythmId) {
        auto control = std::make_unique<Choice>();
        addAndMakeVisible(control->box); addAndMakeVisible(control->label);
        auto* division = processor.state.getParameter(divisionId);
        auto* rhythm = processor.state.getParameter(rhythmId);
        auto* box = &control->box;
        box->setName(division->getName(100));
        control->label.setText("BEAT", juce::dontSendNotification);
        control->label.setFont(juce::Font(juce::FontOptions(11.0f)));

        const auto notes = juce::StringArray::fromTokens(LfoTiming::divisions, "|", "");
        const juce::StringArray suffixes {"", " Dotted", " Triplet"};
        // IDs 1..7 still refer to the original straight choices; display each
        // note with its dotted/triplet variants without changing host parameters.
        for (int d = 0; d < notes.size(); ++d) {
            if (d > 0) box->addSeparator();
            for (int r = 0; r < 3; ++r) box->addItem(notes[d] + suffixes[r], 1 + d + r * 7);
        }
        auto refresh = [division, rhythm, box](float) {
            const int d = juce::roundToInt(division->convertFrom0to1(division->getValue()));
            const int r = juce::roundToInt(rhythm->convertFrom0to1(rhythm->getValue()));
            box->setSelectedId(1 + d + r * 7, juce::dontSendNotification);
        };
        control->divisionAttachment = std::make_unique<juce::ParameterAttachment>(*division, refresh);
        control->rhythmAttachment = std::make_unique<juce::ParameterAttachment>(*rhythm, refresh);
        control->divisionAttachment->sendInitialUpdate(); control->rhythmAttachment->sendInitialUpdate();
        box->onChange = [this, division, rhythm, box] {
            const int selected = box->getSelectedId() - 1;
            if (selected < 0) return;
            auto set = [](juce::RangedAudioParameter* parameter, int value) {
                const float normalized = parameter->convertTo0to1(static_cast<float>(value));
                if (parameter->getValue() == normalized) return;
                parameter->beginChangeGesture(); parameter->setValueNotifyingHost(normalized); parameter->endChangeGesture();
            };
            set(division, selected % 7); set(rhythm, selected / 7);
            processor.sendChangeMessage();
        };
        inspectorChoices.push_back(std::move(control));
    }
    void setRouteParameter(int source, const juce::String& key, float value) {
        auto* parameter = processor.state.getParameter(processor.sourceParameter(source, key));
        parameter->beginChangeGesture(); parameter->setValueNotifyingHost(parameter->convertTo0to1(value)); parameter->endChangeGesture();
    }
    juce::String lfoRouteSignature() const {
        if (selectedId < 100 || processor.blockKind(selectedId) != SineStack::lfoBlock) return {};
        juce::String signature;
        for (const auto* key : {"lfoTarget", "lfoSync", "lfoDivision", "lfoRhythm"})
            signature += juce::String(processor.state.getRawParameterValue(processor.sourceParameter(selectedId, key))->load()) + "/";
        for (int r = 0; r < SineStack::maxLfoRoutes; ++r)
            for (const auto* key : {"lfoBlock", "lfoParameter", "enabled"}) {
                if (r == 0 && juce::String(key) == "enabled") continue;
                signature += juce::String(processor.state.getRawParameterValue(processor.sourceParameter(selectedId, SineStack::routeSetting(r, key)))->load()) + "/";
            }
        const auto chain = processor.getChain();
        for (int n = 0; n < chain.size; ++n) signature += juce::String(chain.ids[n]) + ":" + processor.blockName(chain.ids[n]) + ";";
        return signature;
    }
    void addLfoDestination(int id) {
        for (int r = 0; r < SineStack::maxLfoRoutes; ++r) {
            auto key = [r](const char* field) { return SineStack::routeSetting(r, field); };
            if (r > 0 && processor.state.getRawParameterValue(processor.sourceParameter(id, key("enabled")))->load() < .5f) continue;
            auto row = std::make_unique<RouteRow>(); row->slot = r;
            auto& target = row->target; auto& setting = row->parameter;
            routeContent.addAndMakeVisible(*row);
            row->number.setText(juce::String(r + 1), juce::dontSendNotification);
            row->number.setFont(juce::Font(juce::FontOptions(11.0f)));
            const int mode = r > 0 ? 4 : static_cast<int>(processor.state.getRawParameterValue(processor.sourceParameter(id, "lfoTarget"))->load());
            const int destination = static_cast<int>(processor.state.getRawParameterValue(processor.sourceParameter(id, key("lfoBlock")))->load());
            const int parameter = static_cast<int>(processor.state.getRawParameterValue(processor.sourceParameter(id, key("lfoParameter")))->load());
            target.setName(r == 0 ? processor.state.getParameter(processor.sourceParameter(id, "lfoTarget"))->getName(100) : "LFO target block " + juce::String(r + 1));
            if (r == 0) target.addItemList({"Off", "Source pitch", "Source volume", "All filters", "Choose a block..."}, 1);
            else target.addItem("Choose a block...", 5);
            target.addSeparator();
            bool found = false;
            auto addTargets = [&](const SineStack::Chain& blocks, bool modulation) {
                for (int n = 0; n < blocks.size; ++n) {
                    const int block = blocks.ids[n]; found = found || block == destination;
                    target.addItem((modulation ? "M" : "") + juce::String(n + 1).paddedLeft('0', 2) + " / " + processor.blockName(block)
                        + (block == id ? " (this LFO)" : ""), 1000 + block);
                    target.setItemEnabled(1000 + block, block != id);
                }
            };
            addTargets(processor.getAudioChain(), false);
            target.addSeparator(); addTargets(processor.getModulators(), true);
            if (mode == 4 && destination >= 0 && !found) target.addItem("Missing block (choose another)", 1000 + destination);
            target.setSelectedId(mode == 4 && destination >= 0 ? 1000 + destination : mode + 1, juce::dontSendNotification);
            target.setTooltip("Choose any block regardless of chain position. Source pitch/volume directly modulate sound sources; All filters controls every filter. Self modulation is unavailable.");
            target.onChange = [this, id, r, key, box = &target] {
                processor.materialiseChain();
                const int selected = box->getSelectedId();
                if (selected >= 1000) {
                    const int block = selected - 1000;
                    int setting = static_cast<int>(processor.state.getRawParameterValue(processor.sourceParameter(id, key("lfoParameter")))->load());
                    if (!processor.canModulate(block, setting)) {
                        setting = 0;
                        for (int candidate = 1; candidate <= ModTargets::count; ++candidate)
                            if (processor.canModulate(block, candidate)) { setting = candidate; break; }
                    }
                    setRouteParameter(id, key("lfoBlock"), static_cast<float>(block));
                    setRouteParameter(id, key("lfoParameter"), static_cast<float>(setting));
                    if (r == 0) setRouteParameter(id, "lfoTarget", 4);
                } else {
                    if (r == 0) setRouteParameter(id, "lfoTarget", static_cast<float>(selected - 1));
                    if (selected == 5) setRouteParameter(id, key("lfoBlock"), -1);
                }
                processor.sendChangeMessage();
            };
            setting.setName(r == 0 ? "LFO destination parameter" : "LFO destination parameter " + juce::String(r + 1));
            setting.addItem("Select a parameter...", 1);
            for (int candidate = 1; candidate <= ModTargets::count; ++candidate)
                if (processor.canModulate(destination, candidate)) setting.addItem(ModTargets::definitions[candidate - 1].name, candidate + 1);
            if (parameter > 0 && !processor.canModulate(destination, parameter)) setting.addItem("Unavailable (choose another)", parameter + 1);
            setting.setSelectedId(parameter + 1, juce::dontSendNotification);
            setting.setEnabled(mode == 4 && found && destination != id);
            setting.setTooltip("Continuous knob parameters only. Type selectors and bypass switches are not modulation targets.");
            setting.onChange = [this, id, key, box = &setting] {
                setRouteParameter(id, key("lfoParameter"), static_cast<float>(box->getSelectedId() - 1)); processor.sendChangeMessage();
            };
            const auto depthId = processor.sourceParameter(id, key("lfoRange"));
            row->depth.setName(processor.state.getParameter(depthId)->getName(100));
            row->depth.setSliderStyle(juce::Slider::LinearHorizontal);
            row->depth.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 24);
            row->attachment = std::make_unique<SliderAttachment>(processor.state, depthId, row->depth);
            row->depth.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; };
            row->depth.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue() / 100; };
            row->depth.updateText(); row->depth.setEnabled(mode == 4);
            row->depth.setTooltip("This connection's depth (-100 to 100%). Negative values reverse direction. Multiplied by the LFO's master Depth. Global targets use the Pitch/Filter Range knobs above.");
            row->remove.setName("Remove LFO target " + juce::String(r + 1));
            row->remove.setTooltip(r == 0 ? "Clear the first target" : "Remove this connection");
            row->remove.onClick = [this, id, r, key] {
                if (r > 0) setRouteParameter(id, key("enabled"), 0);
                else { setRouteParameter(id, "lfoBlock", -1); setRouteParameter(id, "lfoTarget", 4); }
                processor.sendChangeMessage();
            };
            routeRows.push_back(std::move(row));
        }
        addRouteButton.setEnabled(routeRows.size() < SineStack::maxLfoRoutes);
        addRouteButton.setButtonText("+ Target  " + juce::String(routeRows.size()) + "/" + juce::String(SineStack::maxLfoRoutes));
    }
    int selectedType() const {
        return selectedId >= 0 && selectedId < 100 ? static_cast<int>(processor.state.getRawParameterValue("fx" + juce::String(selectedId) + "type")->load()) : processor.blockKind(selectedId);
    }
    void selectEqBand(int band) {
        eqBand = juce::jlimit(0, 2, band);
        eqGraph.setSelectedBand(eqBand);
        if (inspectorType != 8 || inspectorKnobs.size() != 5) return;
        inspectorChoices[1]->box.setSelectedId(eqBand + 1, juce::dontSendNotification);
        const auto prefix = "fx" + juce::String(selectedId);
        for (int i = 0; i < 3; ++i) {
            auto& knob = *inspectorKnobs[1 + i];
            knob.attachment.reset();
            const auto& setting = EqSettings::definitions[eqBand * 3 + i];
            setupKnob(knob, prefix + setting.suffix, setting.label);
        }
        resized();
    }
    void selectBlock(int id) {
        eqGraph.finishDrag();
        envelopeGraph.finishDrag(); selectedId = id; inspectorType = selectedType();
        lfoInspectorSignature = lfoRouteSignature();
        const bool eq = id >= 0 && id < 100 && inspectorType == 8;
        eqGraph.bind(eq ? id : -1, eqBand); eqGraph.setVisible(eq);
        inspectorDescription.setVisible(!eq);
        bypassAttachment.reset(); inspectorKnobs.clear(); inspectorChoices.clear(); routeRows.clear();
        const bool lfo = id >= 100 && processor.blockKind(id) == SineStack::lfoBlock;
        routeViewport.setVisible(lfo); routeHeading.setVisible(lfo); routeParameterHeading.setVisible(lfo); routeDepthHeading.setVisible(lfo); addRouteButton.setVisible(lfo);
        bypassButton.setVisible(id >= 0); duplicateButton.setVisible(id >= 0); removeButton.setVisible(id >= 0);
        updateDuplicateButton();
        if (id < 0) {
            inspectorTitle.setText("No block selected", juce::dontSendNotification);
            inspectorCategory.setText("CHAIN", juce::dontSendNotification);
            inspectorDescription.setText("Use + Add to start with an oscillator, source, modulation, or audio effect.", juce::dontSendNotification);
            resized(); repaint(); return;
        }
        inspectorTitle.setText(processor.blockName(id), juce::dontSendNotification);
        inspectorCategory.setText("", juce::dontSendNotification); // updateRouting supplies role and position.
        removeButton.setEnabled(true);
        bypassButton.setEnabled(true);
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state, processor.blockBypassParameter(id), bypassButton);
        juce::String description;
        if (id < 100) {
            const auto prefix = "fx" + juce::String(id);
            addChoice(prefix + "type", "EFFECT TYPE"); addKnob(prefix + "amount", "AMOUNT");
            description = "Audio effects process left to right in the chain.";
            switch (inspectorType) {
                case 1: addKnob(prefix + "driveStrength", "DRIVE"); addKnob(prefix + "driveTone", "TONE"); break;
                case 2: addKnob(prefix + "chorusRate", "RATE"); addKnob(prefix + "chorusDepth", "DEPTH"); break;
                case 3:
                    addKnob(prefix + "delayTime", "TIME"); addKnob(prefix + "delayFeedback", "FEEDBACK");
                    addChoice(prefix + "delaySync", "CLOCK"); addBeatChoice(prefix + "delayDivision", prefix + "delayRhythm");
                    inspectorKnobs[1]->slider.setTooltip("Free mode uses this saved time, including LFO modulation. Tempo Sync uses Beat instead; the free-time value is preserved.");
                    inspectorChoices[2]->box.setTooltip("Time between echoes at the DAW tempo. Dotted = 1.5x length, Triplet = 2/3 length. Maximum 8 seconds. Without host tempo: 120 BPM, or the last valid tempo.");
                    break;
                case 4: addKnob(prefix + "reverbSize", "ROOM SIZE"); addKnob(prefix + "reverbDamping", "DAMPING"); break;
                case 5: addChoice(prefix + "filterMode", "FILTER MODE"); addKnob(prefix + "filterCutoff", "CUTOFF"); addKnob(prefix + "filterResonance", "RESONANCE"); break;
                case 6: addKnob(prefix + "tremoloRate", "RATE"); addKnob(prefix + "tremoloDepth", "DEPTH"); break;
                case 7: addKnob(prefix + "phaserRate", "RATE"); addKnob(prefix + "phaserDepth", "DEPTH"); break;
                case 8: {
                    description = "Three bell bands. All bands stay active; higher Q makes a narrower band.";
                    auto choice = std::make_unique<Choice>();
                    choice->label.setText("EDIT BAND", juce::dontSendNotification);
                    choice->label.setFont(juce::Font(juce::FontOptions(12.0f)));
                    choice->box.setName("EQ band");
                    choice->box.addItemList({"Low", "Mid", "High"}, 1);
                    choice->box.setSelectedId(eqBand + 1, juce::dontSendNotification);
                    choice->box.onChange = [this, box = &choice->box] {
                        selectEqBand(box->getSelectedId() - 1);
                    };
                    addAndMakeVisible(choice->label); addAndMakeVisible(choice->box);
                    inspectorChoices.push_back(std::move(choice));
                    for (int d = eqBand * 3; d < eqBand * 3 + 3; ++d) {
                        const auto& setting = EqSettings::definitions[d];
                        addKnob(prefix + setting.suffix, setting.label);
                    }
                    break;
                }
                default: break;
            }
            // Keep the core knobs first, then effect-specific details, then output.
            for (const auto& detail : FxDetails::definitions) if (detail.effect == inspectorType && inspectorType > 0) {
                if (*detail.choices != '\0') {
                    addChoice(prefix + detail.suffix, detail.label);
                    inspectorChoices.back()->box.setTooltip(detail.tooltip);
                } else {
                    addKnob(prefix + detail.suffix, detail.label);
                    inspectorKnobs.back()->slider.setTooltip(detail.tooltip);
                }
            }
            if (inspectorType > 0) {
                const auto& output = FxDetails::definitions[FxDetails::outputGain];
                addKnob(prefix + output.suffix, output.label);
                inspectorKnobs.back()->slider.setTooltip(output.tooltip);
            }
        } else {
            const int kind = processor.blockKind(id) - 100;
            const bool parameterMode = kind == 6 && static_cast<int>(processor.state.getRawParameterValue(processor.sourceParameter(id, "lfoTarget"))->load()) == 4;
            for (const auto& d : SourceSettings::definitions) if (d.kind == kind) {
                if (kind == 6 && juce::String(d.id) == "lfoTarget") continue;
                if (parameterMode && (juce::String(d.id) == "lfoPitchRange" || juce::String(d.id) == "lfoFilterRange")) continue;
                const auto parameter = processor.sourceParameter(id, d.id);
                if (*d.choices != '\0') addChoice(parameter, d.label);
                else addKnob(parameter, kind == 6 && juce::String(d.id) == "lfoDepth" ? "MASTER DEPTH" : d.label);
            }
            if (kind == 6) {
                addChoice(processor.sourceParameter(id, "lfoSync"), "CLOCK");
                addBeatChoice(processor.sourceParameter(id, "lfoDivision"), processor.sourceParameter(id, "lfoRhythm"));
                const bool synced = processor.state.getRawParameterValue(processor.sourceParameter(id, "lfoSync"))->load() >= .5f;
                inspectorChoices.back()->box.setEnabled(synced);
                inspectorChoices.back()->box.setTooltip("One LFO cycle per note value. Dotted = 1.5x length; Triplet = 2/3 length. Follows DAW tempo and playback position. Without host tempo: 120 BPM, or the last valid tempo.");
                inspectorKnobs.front()->slider.setEnabled(!synced);
                addLfoDestination(id);

            }
            const juce::StringArray descriptions {
                "Independent oscillator layer. Transpose, fine tuning, level and pan follow the fixed ADSR.",
                "Detuned copies of incoming audio, including earlier FX tails. Stack blocks for successive processing.",
                "Pitch-shifted copies of incoming audio. Interval, tuning, mix and stereo position affect this block only.",
                "Glides the pitch of incoming audio between MIDI notes. Later sources are unaffected; stacked glides add pitch offsets.",
                "Independent sub layer: waveform, octave, tuning and stereo position. Follows the fixed ADSR.",
                "Independent noise layer. Colour: dark / white / bright. Colour Freq sets the tone pivot.",
                "Independent control source. Modulate source pitch / volume, all filters, or a specific block at any chain position."
            };
            description = kind == 6 ? "One LFO, multiple targets. Depth above scales every connection." : descriptions[kind];
            for (auto& knob : inspectorKnobs) knob->slider.setTooltip(description);
            if (kind == 6) inspectorKnobs.front()->slider.setTooltip("Rate in Hz is used in Free mode. Tempo Sync uses the Beat selection instead, including when another LFO targets Rate.");
        }
        inspectorDescription.setText(description, juce::dontSendNotification);
        updateDelayTimingControls();
        updateBypassAppearance(); updateRouting(); resized();
        for (auto* list : {&tiles, &modTiles}) for (auto& tile : *list) { tile->selected = tile->id == selectedId; tile->repaint(); }
        repaint();
    }
    void updateDelayTimingControls() {
        if (selectedId < 0 || selectedId >= SineStack::maxBlocks || inspectorType != 3 || inspectorKnobs.size() < 3 || inspectorChoices.size() < 3) return;
        const auto prefix = "fx" + juce::String(selectedId);
        const bool synced = processor.state.getRawParameterValue(prefix + "delaySync")->load() >= .5f;
        inspectorKnobs[1]->slider.setEnabled(!synced);
        inspectorChoices[2]->box.setEnabled(synced);
        juce::String description = "Free time in milliseconds. Tempo Sync follows the DAW tempo.";
        if (synced) {
            const double bpm = processor.getTempoBpm();
            const int division = static_cast<int>(processor.state.getRawParameterValue(prefix + "delayDivision")->load());
            const int rhythm = static_cast<int>(processor.state.getRawParameterValue(prefix + "delayRhythm")->load());
            const double ms = DelayTiming::milliseconds(bpm, division, rhythm);
            description = "Tempo Sync / " + juce::String(ms, 1) + " ms at " + juce::String(bpm, 1) + " BPM";
            if (ms >= DelayTiming::maxSeconds * 1000) description += " / 8 s limit";
        }
        inspectorDescription.setText(description, juce::dontSendNotification);
    }
    void updateBypassAppearance() {
        bypassButton.setButtonText(bypassButton.getToggleState() ? "BYPASSED" : "ON");
        const float opacity = bypassButton.getToggleState() ? 0.5f : 1.0f;
        for (auto& knob : inspectorKnobs) { knob->slider.setAlpha(opacity); knob->label.setAlpha(opacity); }
        for (auto& choice : inspectorChoices) { choice->box.setAlpha(opacity); choice->label.setAlpha(opacity); }
        routeViewport.setAlpha(opacity);
    }
    ChainTile* findTile(int id) const {
        for (const auto& tile : tiles) if (tile->id == id) return tile.get();
        return nullptr;
    }
    void layoutTiles(bool animate) {
        for (size_t i = 0; i < tiles.size(); ++i) {
            auto* tile = tiles[i].get();
            if (tile->id == dragId) continue;
            int position = static_cast<int>(i);
            if (dragId >= 0) {
                if (dragFrom < dragTarget && position > dragFrom && position <= dragTarget) --position;
                if (dragFrom > dragTarget && position >= dragTarget && position < dragFrom) ++position;
            }
            const juce::Rectangle<int> target(position * chainTilePitch, 0, 148, 72);
            if (animator.getComponentDestination(tile) == target && tile->getAlpha() == 1.0f) continue;
            if (animate && tile->getWidth() > 0) animator.animateComponent(tile, target, 1.0f, 160, false, 1.0, 0.0);
            else { animator.cancelAnimation(tile, false); tile->setBounds(target); tile->setAlpha(1.0f); }
        }
        chainContent.repaint();
    }
    bool dragRemovalPending() const {
        if (dragId < 0) return false;
        const juce::Point<float> point {chainViewport.getX() + dragPointer.x, chainViewport.getY() + dragPointer.y};
        // Leave a margin outside the entire panel, so edge scrolling and small
        // vertical slips do not accidentally remove a block.
        return !juce::Rectangle<float>(24, 436, 968, 166).expanded(12).contains(point);
    }
    void updateDragTarget() {
        if (dragId < 0 || tiles.empty()) return;
        if (dragRemovalPending()) { repaint(); return; }
        const float x = juce::jlimit(0.0f, static_cast<float>(chainViewport.getViewWidth()), dragPointer.x)
            + chainViewport.getViewPositionX() - dragGrab.x + 74;
        const int target = juce::jlimit(0, static_cast<int>(tiles.size()) - 1, static_cast<int>(std::floor(x / chainTilePitch)));
        if (target != dragTarget) { dragTarget = target; layoutTiles(true); }
        repaint();
    }
    void finishDrag(bool commit) {
        if (dragId < 0) return;
        const int from = dragFrom, to = dragTarget, id = dragId;
        commit = commit && chainText(processor.getAudioChain()) == dragSignature;
        const bool remove = commit && dragRemovalPending();
        const bool reorder = commit && !remove && dragPointer.y >= 0 && dragPointer.y < 76;
        if (auto* tile = findTile(dragId)) {
            tile->setBounds(juce::roundToInt(chainViewport.getViewPositionX() + dragPointer.x - dragGrab.x),
                            juce::roundToInt(dragPointer.y - dragGrab.y - 4), 148, 72);
            tile->setAlpha(1.0f);
        }
        dragId = dragFrom = dragTarget = -1; dragImage = {}; dragSignature.clear();
        if (remove) processor.removeBlock(id); // Async UI refresh: the tile is still handling mouseUp.
        else if (reorder && from != to) processor.moveAudioBlock(from, to);
        else layoutTiles(true);
        repaint();
    }
    void dragBlock(int id, juce::Point<float> point, bool finished) {
        auto* tile = findTile(id);
        if (!tile) return;
        if (dragId < 0) {
            if (finished) return;
            animator.cancelAllAnimations(false);
            dragId = id; dragFrom = tile->position; dragTarget = dragFrom;
            dragSignature = chainText(processor.getAudioChain());
            dragGrab = tile->grabPoint;
            dragImage = tile->createComponentSnapshot(tile->getLocalBounds(), true, 2.0f);
            tile->setAlpha(0.0f);
            layoutTiles(true);
        }
        dragPointer = {point.x - chainViewport.getViewPositionX(), point.y};
        updateDragTarget();
        if (finished) finishDrag(true);
    }
    void updateRouting() {
        juce::String flow = "CHAIN:  SILENCE";
        int order = 0;
        for (auto& tile : tiles) {
            const auto number = juce::String(++order).paddedLeft('0', 2);
            const int kind = processor.blockKind(tile->id);
            const bool effect = tile->id < 100 || kind == SineStack::unisonBlock || kind == SineStack::harmonyBlock || kind == SineStack::glideBlock;
            const juce::String category = effect ? "FX" : kind == SineStack::lfoBlock ? "MOD" : "SOURCE";
            tile->category = category;
            flow += "  >  " + number + " " + tile->title + (tile->bypassed ? " (off)" : "");
            if (tile->id == selectedId) inspectorCategory.setText(category + " / " + number, juce::dontSendNotification);
        }
        for (size_t n = 0; n < modTiles.size(); ++n) {
            auto& tile = modTiles[n]; tile->category = "LFO";
            if (tile->id == selectedId) inspectorCategory.setText("MODULATION / " + juce::String(static_cast<int>(n + 1)).paddedLeft('0', 2), juce::dontSendNotification);
        }
        flow += "  >  OUT";
        routingLabel.setText(flow, juce::dontSendNotification);
        routingLabel.setTooltip(flow + "\nAudio flows left to right. The separate MODULATION area can control any block regardless of position.");
    }

    void refreshChain() {
        const auto current = processor.getAudioChain(), mods = processor.getModulators();
        const auto signature = chainText(current), modText = chainText(mods);
        bool selectionPresent = false;
        for (const auto* list : {&current, &mods}) for (int n = 0; n < list->size; ++n) selectionPresent = selectionPresent || list->ids[n] == selectedId;
        if (!selectionPresent) selectedId = current.size > 0 ? current.ids[0] : mods.size > 0 ? mods.ids[0] : -1;
        bool changed = false;
        if (signature != chainSignature) {
            changed = true;
            if (dragId >= 0) finishDrag(false);
            animator.cancelAllAnimations(false);
            auto previous = std::move(tiles);
            tiles.clear(); chainSignature = signature;
            for (int n = 0; n < current.size; ++n) {
                std::unique_ptr<ChainTile> tile;
                for (auto& old : previous) if (old && old->id == current.ids[n]) { tile = std::move(old); break; }
                if (!tile) {
                    tile = std::make_unique<ChainTile>(processor, current.ids[n]);
                    tile->setName("Chain block " + juce::String(tile->id));
                    tile->setMouseCursor(juce::MouseCursor::DraggingHandCursor);
                    const int id = tile->id;
                    tile->onSelect = [this, id] { processor.materialiseChain(); selectBlock(id); };
                    tile->onRemove = [this, id] { processor.removeBlock(id); };
                    tile->onDrag = [this, id](juce::Point<float> point, bool finished) { dragBlock(id, point, finished); };
                    chainContent.addAndMakeVisible(*tile);
                }
                tile->position = n; tiles.push_back(std::move(tile));
            }
        }
        if (modText != modSignature) {
            changed = true; modSignature = modText;
            auto previous = std::move(modTiles); modTiles.clear();
            for (int n = 0; n < mods.size; ++n) {
                std::unique_ptr<ChainTile> tile;
                for (auto& old : previous) if (old && old->id == mods.ids[n]) { tile = std::move(old); break; }
                if (!tile) {
                    tile = std::make_unique<ChainTile>(processor, mods.ids[n]); tile->movable = false;
                    tile->setName("Modulation block " + juce::String(tile->id));
                    tile->setMouseCursor(juce::MouseCursor::PointingHandCursor);
                    const int id = tile->id;
                    tile->onSelect = [this, id] { processor.materialiseChain(); selectBlock(id); };
                    tile->onRemove = [this, id] { processor.removeBlock(id); };
                    modContent.addAndMakeVisible(*tile);
                }
                tile->position = n; modTiles.push_back(std::move(tile));
            }
        }
        if (changed || selectedType() != inspectorType || !selectionPresent || lfoInspectorSignature != lfoRouteSignature()) selectBlock(selectedId);
        updateDelayTimingControls();
        if (changed) { resized(); layoutTiles(true); }
        const bool full = processor.getChain().size >= SineStack::maxBlocks;
        addButton.setEnabled(!full); addLfoButton.setEnabled(!full);
        addLfoButton.setTooltip(full ? "All 32 shared block slots are in use." : "Add an independent LFO, then choose its target block and parameter.");
        updateDuplicateButton();
        for (auto* list : {&tiles, &modTiles}) for (auto& tile : *list) {
            tile->title = processor.blockName(tile->id); tile->selected = tile->id == selectedId; tile->repaint();
        }
        updateRouting(); repaint();
    }
    void updateDuplicateButton() {
        const bool full = processor.getChain().size >= SineStack::maxBlocks;
        duplicateButton.setEnabled(selectedId >= 0 && !full);
        duplicateButton.setTooltip(full ? "All 32 shared audio/modulation slots are in use." : "Copy all settings into an independent block immediately after this one.");
    }
    void revealSelected() {
        const bool mod = processor.blockKind(selectedId) == SineStack::lfoBlock;
        auto& list = mod ? modTiles : tiles; auto& viewport = mod ? modViewport : chainViewport;
        for (const auto& tile : list) if (tile->id == selectedId) {
            const int left = viewport.getViewPositionX(), width = viewport.getViewWidth();
            if (tile->getX() < left) viewport.setViewPosition(tile->getX(), 0);
            else if (tile->getRight() > left + width) viewport.setViewPosition(tile->getRight() - width + 8, 0);
        }
    }
    void showAddMenu() {
        juce::PopupMenu menu; menu.addSectionHeader("Sound sources");
        for (int kind : {SineStack::oscillatorBlock, SineStack::subBlock, SineStack::noiseBlock})
            menu.addItem(kind, sourceNames[kind - 100]);
        menu.addSeparator(); menu.addSectionHeader("Audio effects");
        for (int kind : {SineStack::unisonBlock, SineStack::harmonyBlock, SineStack::glideBlock})
            menu.addItem(kind, sourceNames[kind - 100]);
        for (int kind = 1; kind <= 8; ++kind) menu.addItem(kind, effectNames[kind]);
        const juce::Component::SafePointer<Editor> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addButton), [safe](int kind) {
            if (!safe || kind == 0) return;
            const int id = safe->processor.addBlock(kind);
            if (id < 0) return;
            safe->selectedId = id; safe->refreshChain(); safe->revealSelected();
        });
    }
    void timerCallback() override {
        if (dragId >= 0) {
            const int left = chainViewport.getViewPositionX();
            if (!dragRemovalPending() && dragPointer.y >= 0 && dragPointer.y < 76) {
                if (dragPointer.x < 28) chainViewport.setViewPosition(juce::jmax(0, left - 12), 0);
                else if (dragPointer.x > chainViewport.getViewWidth() - 28) chainViewport.setViewPosition(left + 12, 0);
            }
            if (chainViewport.getViewPositionX() != left) updateDragTarget();
        }
        if (++animationTick % 5 == 0) { refreshSampleRate(); refreshChain(); if (processor.blockKind(selectedId) == SineStack::oscillatorBlock) repaint(); }
    }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refreshPresetList(); refreshChain(); }
    void updateEnvelopeGraph() {
        envelopeGraph.setEnvelope(static_cast<float>(fixedKnobs[0].slider.getValue()), static_cast<float>(fixedKnobs[1].slider.getValue()),
            static_cast<float>(fixedKnobs[2].slider.getValue()), static_cast<float>(fixedKnobs[3].slider.getValue()));
    }
    void setPresetBusy(bool busy) {
        presetBusy = busy;
        savePresetButton.setEnabled(!busy); loadPresetButton.setEnabled(!busy);
        presetBank.setEnabled(!busy); presetCategory.setEnabled(!busy);
        refreshPresetButton.setEnabled(!busy); presetList.setEnabled(!busy && presetCount() > 0);
        const bool canStep = !busy && (presetCount() > 1 || (presetCount() > 0 && presetList.getSelectedId() == 0));
        previousPresetButton.setEnabled(canStep); nextPresetButton.setEnabled(canStep);
    }
    void stepPreset(int direction) {
        if (presetBusy) return;
        refreshPresetList();
        const int count = presetCount();
        if (count == 0) return;
        const int selected = presetList.getSelectedId() - 1;
        int index = selected >= 0 ? selected : (direction > 0 ? count - 1 : 0);
        // Skip unreadable files without reloading the current sound or losing edits.
        const int attempts = selected >= 0 ? count - 1 : count;
        for (int i = 0; i < attempts; ++i) {
            index = (index + direction + count) % count;
            if (loadListedPreset(index).wasOk()) {
                refreshPresetList();
                return;
            }
        }
        if (attempts > 0)
            presetStatus.setText("No other readable presets in this folder", juce::dontSendNotification);
    }
    bool factoryBank() const { return presetBank.getSelectedId() == 1; }
    int presetCount() const { return factoryBank() ? visibleFactoryPresets.size() : presetFiles.size(); }
    juce::Result loadListedPreset(int index) {
        return factoryBank() ? processor.loadFactoryPreset(visibleFactoryPresets[index]) : processor.loadPreset(presetFiles[index]);
    }
    bool matchesCategory(const juce::String& category) const {
        return presetCategory.getSelectedId() <= 1 || category == PresetCategories::names[static_cast<size_t>(presetCategory.getSelectedId() - 2)];
    }
    void refreshPresetList() {
        const auto file = processor.getPresetFile();
        const int factory = processor.getFactoryPresetIndex();
        if (file != lastPresetFile || factory != lastFactoryPreset) {
            presetBank.setSelectedId(file != juce::File{} ? 2 : 1, juce::dontSendNotification);
            // External Load/Save should reveal its result even if the old filter excludes it.
            if (!matchesCategory(processor.getPresetCategory())) presetCategory.setSelectedId(1, juce::dontSendNotification);
        }
        lastPresetFile = file; lastFactoryPreset = factory;
        if (file != juce::File{} && userPresetFolder != file.getParentDirectory()) {
            userPresetFolder = file.getParentDirectory(); categoryCache.clear();
        }
        if (userPresetFolder == juce::File{}) userPresetFolder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        presetList.clear(juce::dontSendNotification);
        int selected = 0;
        if (factoryBank()) {
            visibleFactoryPresets.clear();
            for (int i = 0; i < FactoryPresets::count; ++i) {
                if (!matchesCategory(PresetCategories::canonical(FactoryPresets::presets[i].category))) continue;
                visibleFactoryPresets.add(i);
                presetList.addItem(FactoryPresets::presets[i].name, visibleFactoryPresets.size());
                if (i == factory) selected = visibleFactoryPresets.size();
            }
            presetStatus.setText(selected > 0 ? juce::String(FactoryPresets::presets[factory].category) + " / " + FactoryPresets::presets[factory].description
                                            : "FACTORY / " + juce::String(visibleFactoryPresets.size()) + " matching presets", juce::dontSendNotification);
            presetStatus.setTooltip(factory >= 0 ? FactoryPresets::presets[factory].description : "Built into the plug-in. Select a sound or use the arrows. Save edits as a User preset.");
        } else {
            presetFiles.clear();
            for (const auto& candidate : userPresetFolder.findChildFiles(juce::File::findFiles, false, "*"))
                if (candidate.hasFileExtension("sinestack") && (presetCategory.getSelectedId() <= 1 || matchesCategory(categoryCache.category(candidate)))) presetFiles.add(candidate);
            std::sort(presetFiles.begin(), presetFiles.end(), [](const juce::File& a, const juce::File& b) {
                return a.getFileName().compareNatural(b.getFileName()) < 0;
            });
            for (int i = 0; i < presetFiles.size(); ++i) {
                presetList.addItem(presetFiles[i].getFileNameWithoutExtension(), i + 1);
                if (presetFiles[i] == file) selected = i + 1;
            }
            presetStatus.setText(presetFiles.isEmpty() ? "No matching presets - choose All categories or Save Preset" : "Folder: " + userPresetFolder.getFileName(), juce::dontSendNotification);
            presetStatus.setTooltip(userPresetFolder.getFullPathName());
        }
        presetList.setSelectedId(selected, juce::dontSendNotification);
        setPresetBusy(presetBusy);
    }
    void presetResult(const juce::Result& result, const juce::File& file, bool saving) {
        setPresetBusy(false);
        if (result.wasOk()) {
            categoryCache.clear();
            if (!matchesCategory(processor.getPresetCategory())) presetCategory.setSelectedId(1, juce::dontSendNotification);
            refreshPresetList();
            presetStatus.setText((saving ? "Saved: " : "Loaded: ") + file.getFileNameWithoutExtension(), juce::dontSendNotification);
        } else {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                saving ? "Could not save preset" : "Could not load preset", result.getErrorMessage(), "OK", this);
        }
    }
    void choosePreset(bool saving) {
        if (!saving) { choosePresetFile(false, {}); return; }
        setPresetBusy(true);
        presetCategoryDialog = std::make_unique<juce::AlertWindow>("Save preset", "Choose a category for this sound.", juce::MessageBoxIconType::NoIcon, this);
        presetCategoryDialog->setLookAndFeel(&theme);
        presetCategoryDialog->addComboBox("category", PresetCategories::choices(), "Category");
        auto category = processor.getPresetCategory();
        if (category == PresetCategories::names[0] && presetCategory.getSelectedId() > 1)
            category = PresetCategories::names[static_cast<size_t>(presetCategory.getSelectedId() - 2)];
        presetCategoryDialog->getComboBoxComponent("category")->setSelectedItemIndex(PresetCategories::index(category), juce::dontSendNotification);
        presetCategoryDialog->addButton("Continue", 1, juce::KeyPress(juce::KeyPress::returnKey));
        presetCategoryDialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        const juce::Component::SafePointer<Editor> safe(this);
        presetCategoryDialog->enterModalState(true, juce::ModalCallbackFunction::create([safe](int result) {
            if (!safe || !safe->presetCategoryDialog) return;
            const auto chosen = safe->presetCategoryDialog->getComboBoxComponent("category")->getText();
            safe->presetCategoryDialog.reset();
            if (result == 1) safe->choosePresetFile(true, chosen);
            else safe->setPresetBusy(false);
        }), false);
    }
    void choosePresetFile(bool saving, juce::String category) {
        setPresetBusy(true);
        const auto name = processor.getFactoryPresetIndex() >= 0
            ? juce::String(FactoryPresets::presets[processor.getFactoryPresetIndex()].name) + ".sinestack" : "Sine Stack.sinestack";
        const auto initial = lastPresetFile == juce::File{} ? userPresetFolder.getChildFile(name) : lastPresetFile;
        presetChooser = std::make_unique<juce::FileChooser>(saving ? "Save Sine Stack preset" : "Load Sine Stack preset", initial, "*.sinestack", true, false, this);
        const int flags = juce::FileBrowserComponent::canSelectFiles | (saving
            ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
            : juce::FileBrowserComponent::openMode);
        const juce::Component::SafePointer<Editor> safe(this);
        presetChooser->launchAsync(flags, [safe, saving, category](const juce::FileChooser& chooser) {
            if (!safe) return;
            const auto selected = chooser.getResult();
            if (selected == juce::File{}) {
                safe->setPresetBusy(false);
                return;
            }
            const auto file = saving && selected.getFileExtension().isEmpty() ? selected.withFileExtension("sinestack") : selected;
            if (saving && !file.hasFileExtension("sinestack")) {
                safe->presetResult(juce::Result::fail("Please use the .sinestack file extension."), file, true);
                return;
            }
            if (saving && file != selected && file.existsAsFile()) {
                juce::NativeMessageBox::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon)
                    .withTitle("Replace preset?").withMessage(file.getFileName() + " already exists.")
                    .withButton("Replace").withButton("Cancel").withAssociatedComponent(safe.getComponent()),
                    [safe, file, category](int result) {
                        if (!safe) return;
                        if (result == 0) safe->presetResult(safe->processor.savePreset(file, category), file, true);
                        else { safe->setPresetBusy(false); }
                    });
                return;
            }
            safe->presetResult(saving ? safe->processor.savePreset(file, category) : safe->processor.loadPreset(file), file, saving);
        });
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(uiColour(*this, DesignTheme::background));
        if (theme.index() == 0) {
            g.setGradientFill(juce::ColourGradient(theme.colour(DesignTheme::secondary).withAlpha(.14f), 0, 0,
                theme.colour(DesignTheme::accent).withAlpha(.03f), static_cast<float>(getWidth()), 420, false));
            g.fillRect(getLocalBounds());
        }
        g.setColour(uiColour(*this, DesignTheme::text)); g.setFont(26.0f); g.drawText("SINE STACK", 28, 20, 300, 36, juce::Justification::centredLeft);
        g.setFont(13.0f); g.setColour(uiColour(*this, DesignTheme::muted)); g.drawText("One wave. Build your chain.", 29, 58, 192, 20, juce::Justification::centredLeft);
        g.setColour(uiColour(*this, DesignTheme::panel));
        g.fillRoundedRectangle(24, 104, 324, 316, 12); g.fillRoundedRectangle(361, 104, 631, 316, 12);
        g.fillRoundedRectangle(24, 436, 968, 166, 12);
        g.fillRoundedRectangle(24, 614, 968, 132, 12);
        if (theme.index() == 0) {
            g.setColour(theme.colour(DesignTheme::secondary).withAlpha(.32f));
            for (const auto area : {juce::Rectangle<float>(24, 104, 324, 316), juce::Rectangle<float>(361, 104, 631, 316),
                                   juce::Rectangle<float>(24, 436, 968, 166), juce::Rectangle<float>(24, 614, 968, 132)})
                g.drawRoundedRectangle(area, 12, 1);
        }
        g.setFont(12.0f); g.setColour(uiColour(*this, DesignTheme::accent));
        g.drawText("ADSR", 38, 115, 100, 22, juce::Justification::centredLeft);
        g.drawText("AUDIO CHAIN / " + juce::String(tiles.size()), 38, 445, 150, 22, juce::Justification::centredLeft);
        g.drawText("MODULATION / " + juce::String(modTiles.size()), 38, 622, 180, 22, juce::Justification::centredLeft);
        g.setFont(11.0f); g.setColour(uiColour(*this, DesignTheme::muted));
        g.drawText("Independent LFOs / any target position.  " + juce::String(tiles.size() + modTiles.size()) + " / 32 total blocks", 234, 622, 640, 22, juce::Justification::centredLeft);
        if (modTiles.empty()) g.drawText("Add an LFO, then choose a target block and parameter.", 48, 675, 850, 22, juce::Justification::centredLeft);
        if (dragRemovalPending()) g.setColour(juce::Colour(0xffff998e));
        g.drawText(dragId >= 0 ? (dragRemovalPending() ? "REMOVE " + processor.blockName(dragId) + " / Release to delete"
                              : "MOVE " + juce::String(dragFrom + 1) + "  >  " + juce::String(dragTarget + 1) + " / Release to place")
                              : "Drag to reorder; drag outside or x to remove.",
                   204, 445, 380, 22, juce::Justification::centredLeft);
        g.setColour(uiColour(*this, DesignTheme::muted));
        g.drawText("PITCH LATENCY", 595, 445, 100, 22, juce::Justification::centredRight);
        if (processor.blockKind(selectedId) == SineStack::oscillatorBlock && inspectorKnobs.size() < 3) {
            juce::Path wave; const int type = static_cast<int>(processor.state.getRawParameterValue(processor.sourceParameter(selectedId, "waveform"))->load());
            for (int x = 0; x <= 500; ++x) {
                const double cycle = std::fmod(x / 166.666, 1.0);
                const float value = static_cast<float>(type == 0 ? std::sin(cycle * juce::MathConstants<double>::twoPi) : type == 1 ? (cycle < 0.5 ? 1 : -1) : 2 * cycle - 1);
                const float y = 315 - 32 * value;
                if (x == 0) wave.startNewSubPath(399, y); else wave.lineTo(399.0f + x, y);
            }
            g.setColour(uiColour(*this, DesignTheme::accent)); g.strokePath(wave, juce::PathStrokeType(2));
        }
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey && dragId >= 0) {
            if (auto* tile = findTile(dragId)) { tile->dragCancelled = true; tile->dragging = false; }
            finishDrag(false); return true;
        }
        return false;
    }
    void paintOverChildren(juce::Graphics& g) override {
        if (dragId < 0 || !dragImage.isValid()) return;
        const bool removing = dragRemovalPending();
        if (!removing) {
            const float x = static_cast<float>(chainViewport.getX() + dragTarget * chainTilePitch - chainViewport.getViewPositionX());
            g.saveState(); g.reduceClipRegion(chainViewport.getBounds().withTop(chainViewport.getY() - 6));
            g.setColour(uiColour(*this, DesignTheme::accent).withAlpha(0.12f)); g.fillRoundedRectangle(x, 476, 148, 72, 8);
            g.setColour(uiColour(*this, DesignTheme::accent)); g.drawRoundedRectangle(x, 476, 148, 72, 8, 2);
            g.restoreState();
        }
        const float ghostX = chainViewport.getX() + dragPointer.x - dragGrab.x;
        const float ghostY = chainViewport.getY() + dragPointer.y - dragGrab.y - 4;
        g.setColour(juce::Colours::black.withAlpha(0.4f)); g.fillRoundedRectangle(ghostX + 3, ghostY + 5, 148, 72, 8);
        g.setOpacity(0.95f); g.drawImage(dragImage, juce::Rectangle<float>(ghostX, ghostY, 148, 72));
        if (removing) {
            g.setColour(juce::Colour(0xff8b322c).withAlpha(.88f)); g.fillRoundedRectangle(ghostX, ghostY, 148, 72, 8);
            g.setColour(juce::Colour(0xffffb4a9)); g.drawRoundedRectangle(ghostX, ghostY, 148, 72, 8, 2);
            g.setFont(14); g.drawText("Release to remove", juce::Rectangle<float>(ghostX + 4, ghostY + 12, 140, 24), juce::Justification::centred);
            g.setFont(11); g.drawText("Esc to cancel", juce::Rectangle<float>(ghostX + 4, ghostY + 37, 140, 20), juce::Justification::centred);
        }
    }
    void resized() override {
        scopeButton.setBounds(230, 56, 118, 22);
        designLabel.setBounds(365, 4, 197, 16); designList.setBounds(365, 22, 197, 30);
        sampleRateLabel.setBounds(29, 80, 90, 20); sampleRateList.setBounds(122, 78, 152, 22);
        savePresetButton.setBounds(570, 22, 112, 30); loadPresetButton.setBounds(690, 22, 112, 30);
        refreshPresetButton.setBounds(810, 22, 70, 30); presetBank.setBounds(365, 60, 88, 26); presetCategory.setBounds(459, 60, 122, 26); previousPresetButton.setBounds(587, 60, 26, 26);
        presetList.setBounds(619, 60, 225, 26); nextPresetButton.setBounds(850, 60, 30, 26); presetStatus.setBounds(365, 87, 515, 14);
        fixedKnobs[4].slider.setBounds(889, 10, 100, 66); fixedKnobs[4].label.setBounds(889, 77, 100, 18);
        voiceModeList.setBounds(169, 115, 165, 24);
        envelopeGraph.setBounds(38, 145, 296, 98);
        for (int i = 0; i < 4; ++i) {
            fixedKnobs[i].slider.setBounds(32 + i * 78, 260, 74, 106); fixedKnobs[i].label.setBounds(32 + i * 78, 375, 74, 22);
        }
        inspectorTitle.setBounds(380, 116, 310, 32); inspectorCategory.setBounds(382, 147, 310, 18);
        const bool lfo = selectedId >= 100 && processor.blockKind(selectedId) == SineStack::lfoBlock;
        inspectorDescription.setBounds(382, 168, lfo ? 460 : 592, 21);
        addRouteButton.setBounds(850, 168, 124, 24);
        routeHeading.setBounds(406, 292, 184, 18);
        routeParameterHeading.setBounds(596, 292, 174, 18); routeDepthHeading.setBounds(776, 292, 142, 18);
        routeViewport.setBounds(382, 312, 592, 96);
        routeContent.setSize(580, juce::jmax(96, static_cast<int>(routeRows.size()) * 44));
        for (size_t r = 0; r < routeRows.size(); ++r) routeRows[r]->setBounds(0, static_cast<int>(r) * 44, 580, 44);
        if (revealRoute >= 0) {
            for (const auto& row : routeRows) if (row->slot == revealRoute) {
                const int y = routeViewport.getViewPositionY();
                if (row->getY() < y) routeViewport.setViewPosition(0, row->getY());
                else if (row->getBottom() > y + routeViewport.getHeight()) routeViewport.setViewPosition(0, row->getBottom() - routeViewport.getHeight());
            }
            revealRoute = -1;
        }
        bypassButton.setBounds(704, 121, 86, 28); duplicateButton.setBounds(800, 121, 90, 28); removeButton.setBounds(900, 121, 76, 28);
        const bool eq = inspectorType == 8 && selectedId >= 0 && selectedId < 100;
        eqGraph.setBounds(382, 202, 592, 132);
        for (size_t i = 0; i < inspectorChoices.size(); ++i) {
            inspectorChoices[i]->label.setVisible(!eq);
            const int pitch = inspectorChoices.size() > 2 ? 592 / static_cast<int>(inspectorChoices.size()) : 252;
            const int width = pitch - 14;
            inspectorChoices[i]->label.setBounds(385 + static_cast<int>(i) * pitch, 190, width, 17);
            inspectorChoices[i]->box.setBounds(385 + static_cast<int>(i) * pitch, eq ? 170 : 210, width, 28);
            if (lfo) {
                const int x = 385 + static_cast<int>(i) * 198;
                inspectorChoices[i]->label.setBounds(x, 192, 46, 20); inspectorChoices[i]->box.setBounds(x + 50, 192, 138, 24);
            }
        }
        for (size_t i = 0; i < inspectorKnobs.size(); ++i) {
            if (lfo) {
                const int pitch = 592 / static_cast<int>(inspectorKnobs.size()), x = 382 + static_cast<int>(i) * pitch;
                inspectorKnobs[i]->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
                inspectorKnobs[i]->slider.setBounds(x, 216, pitch - 6, 59);
                inspectorKnobs[i]->label.setBounds(x, 276, pitch - 6, 16);
            } else if (eq) {
                const int x = 382 + static_cast<int>(i) * 118;
                inspectorKnobs[i]->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 18);
                inspectorKnobs[i]->slider.setBounds(x, 335, 110, 64);
                inspectorKnobs[i]->label.setBounds(x, 400, 110, 16);
            } else if (inspectorKnobs.size() > 3) {
                const int columns = inspectorKnobs.size() > 6 ? 4 : 3;
                const int pitch = 592 / columns;
                const int x = 382 + static_cast<int>(i % columns) * pitch;
                const int y = 238 + static_cast<int>(i / columns) * 86;
                inspectorKnobs[i]->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
                inspectorKnobs[i]->slider.setBounds(x, y, pitch - 10, 74);
                inspectorKnobs[i]->label.setBounds(x, y + 74, pitch - 10, 16);
            } else {
                inspectorKnobs[i]->slider.setBounds(392 + static_cast<int>(i) * 174, 256, 130, 112);
                inspectorKnobs[i]->label.setBounds(392 + static_cast<int>(i) * 174, 377, 130, 20);
            }
        }
        pitchLatencyList.setBounds(700, 443, 172, 28);
        addButton.setBounds(888, 443, 88, 28); chainViewport.setBounds(38, 476, 940, 92);
        chainContent.count = static_cast<int>(tiles.size());
        chainContent.setSize(juce::jmax(940, chainContent.count * chainTilePitch), 76);
        for (size_t i = 0; i < tiles.size(); ++i) if (tiles[i]->getWidth() == 0)
            tiles[i]->setBounds(static_cast<int>(i) * chainTilePitch, 0, 148, 72);
        routingLabel.setBounds(38, 576, 940, 20);
        addLfoButton.setBounds(888, 620, 88, 28);
        modViewport.setBounds(38, 652, 940, 86);
        modContent.count = static_cast<int>(modTiles.size());
        modContent.setSize(juce::jmax(940, modContent.count * chainTilePitch), 76);
        for (size_t i = 0; i < modTiles.size(); ++i) modTiles[i]->setBounds(static_cast<int>(i) * chainTilePitch, 0, 148, 72);
        keyboard.setBounds(24, 762, 968, 60);
        buildLabel.setBounds(24, 824, 968, 18);
    }
};
