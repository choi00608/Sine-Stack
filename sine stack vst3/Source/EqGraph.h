#pragma once
#include "Plugin.h"
#include "EqResponse.h"
#include "DesignTheme.h"

class EqGraph final : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit EqGraph(SineStack& p) : processor(p) {
        setName("EQ frequency response");
        setTooltip("Drag L / M / H: left-right = frequency, up-down = gain. Q changes width. "
                   "Curve shows base settings, including Amount and Output; LFO motion is not shown.");
    }
    ~EqGraph() override { finishDrag(); }
    std::function<void(int)> onBandSelected;
    std::function<void()> onBeforeDrag;
    void bind(int id, int band) {
        finishDrag();
        for (auto& attachment : attachments) attachment.reset();
        block = id; selected = band; hovered = -1;
        if (id < 0) { repaint(); return; }
        const auto prefix = "fx" + juce::String(id);
        for (int i = 0; i < 12; ++i) {
            const juce::String suffix = i < 9 ? EqSettings::definitions[i].suffix : i == 9 ? "amount" : i == 10 ? "outputGain" : "bypass";
            attachments[i] = std::make_unique<juce::ParameterAttachment>(*processor.state.getParameter(prefix + suffix),
                [this, i](float value) { values[i] = value; repaint(); });
            attachments[i]->sendInitialUpdate();
        }
        refreshSampleRate();
    }
    void setSelectedBand(int band) { selected = juce::jlimit(0, 2, band); repaint(); }
    void refreshSampleRate() {
        const double next = processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000;
        if (sampleRate != next) { sampleRate = next; repaint(); }
    }
    void finishDrag() {
        if (dragged < 0) return;
        const int band = dragged; dragged = -1;
        attachments[band * 3]->endGesture(); attachments[band * 3 + 1]->endGesture();
        repaint();
    }
    void visibilityChanged() override { if (!isVisible()) finishDrag(); }
    juce::Rectangle<float> plotBounds() const { return getLocalBounds().toFloat().withTrimmedLeft(33).withTrimmedRight(15).withTrimmedTop(22).withTrimmedBottom(19); }
    juce::Point<float> pointFor(double frequency, double gain) const {
        const auto plot = plotBounds();
        return {plot.getX() + plot.getWidth() * static_cast<float>(std::log10(frequency / 20) / 3),
                plot.getCentreY() - static_cast<float>(gain / 48) * plot.getHeight()};
    }
    juce::Point<float> bandPoint(int band) const { return pointFor(values[band * 3], values[band * 3 + 1]); }
    double responseDb(double frequency) const {
        return EqResponse::decibels(coefficients(), frequency, sampleRate, values[9], values[10], values[11] >= 0.5f);
    }
    void mouseMove(const juce::MouseEvent& e) override {
        const int hit = handleAt(e.position);
        if (hovered != hit) { hovered = hit; repaint(); }
        setMouseCursor(hit >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit(const juce::MouseEvent&) override { hovered = -1; repaint(); }
    void mouseDown(const juce::MouseEvent& e) override {
        if (block < 0 || !e.mods.isLeftButtonDown()) return;
        finishDrag();
        const int hit = handleAt(e.position);
        if (hit < 0) return;
        if (onBeforeDrag) onBeforeDrag();
        selected = hit;
        if (onBandSelected) onBandSelected(hit);
        dragged = hit; dragOffset = bandPoint(hit) - e.position;
        attachments[hit * 3]->beginGesture(); attachments[hit * 3 + 1]->beginGesture();
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragged < 0) return;
        const auto plot = plotBounds(); const auto point = e.position + dragOffset;
        const float frequency = static_cast<float>(20 * std::pow(1000, juce::jlimit(0.0f, 1.0f, (point.x - plot.getX()) / plot.getWidth())));
        const float db = juce::jlimit(-18.0f, 18.0f, (plot.getCentreY() - point.y) * 48 / plot.getHeight());
        attachments[dragged * 3]->setValueAsPartOfGesture(frequency);
        attachments[dragged * 3 + 1]->setValueAsPartOfGesture(db);
    }
    void mouseUp(const juce::MouseEvent&) override { finishDrag(); }
    void paint(juce::Graphics& g) override {
        g.setColour(uiColour(*this, DesignTheme::graph)); g.fillRoundedRectangle(getLocalBounds().toFloat(), 7);
        if (block < 0) return;
        const auto plot = plotBounds();
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        for (int db : {-24, 0, 24}) {
            const float y = pointFor(20, db).y;
            g.setColour(uiColour(*this, DesignTheme::muted).withAlpha(db == 0 ? 0.35f : 0.12f));
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
            g.setColour(uiColour(*this, DesignTheme::muted));
            g.drawText(db > 0 ? "+24" : juce::String(db), 0, juce::roundToInt(y) - 6, 28, 12, juce::Justification::centredRight);
        }
        for (int frequency : {20, 100, 1000, 10000, 20000}) {
            const float x = pointFor(frequency, 0).x;
            g.setColour(uiColour(*this, DesignTheme::muted).withAlpha(0.12f)); g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
            g.setColour(uiColour(*this, DesignTheme::muted));
            const auto label = frequency >= 1000 ? juce::String(frequency / 1000) + "k" : juce::String(frequency);
            g.drawText(label, juce::roundToInt(x) - 17, juce::roundToInt(plot.getBottom()) + 3, 34, 13, juce::Justification::centred);
        }
        const auto bands = coefficients();
        g.saveState(); g.reduceClipRegion(plot.expanded(1).toNearestInt());
        for (int curve = 0; curve < 4; ++curve) {
            juce::Path path;
            for (int x = 0; x <= static_cast<int>(plot.getWidth()); ++x) {
                const double frequency = 20 * std::pow(1000, x / static_cast<double>(plot.getWidth()));
                // Above Nyquist the digital response has no physical meaning.
                if (frequency >= sampleRate * 0.5) break;
                const double db = curve < 3 ? 20 * std::log10(juce::jmax(1.0e-12, std::abs(EqResponse::response(bands[curve], frequency, sampleRate))))
                    : EqResponse::decibels(bands, frequency, sampleRate, values[9], values[10], values[11] >= 0.5f);
                const auto point = pointFor(frequency, juce::jlimit(-100.0, 100.0, db));
                if (x == 0) path.startNewSubPath(point); else path.lineTo(point);
            }
            g.setColour(curve < 3 ? colour(curve).withAlpha(values[11] >= 0.5f ? 0.12f : 0.3f)
                                 : (values[11] >= 0.5f ? uiColour(*this, DesignTheme::muted) : uiColour(*this, DesignTheme::text)));
            g.strokePath(path, juce::PathStrokeType(curve == 3 ? 1.8f : 1.0f));
        }
        g.restoreState();
        // Selected handle goes on top when bands overlap. The band menu also
        // makes every overlapping handle independently selectable.
        for (int n = 0; n < 3; ++n) drawHandle(g, (selected + 1 + n) % 3);
        g.setColour(colour(selected)); g.setFont(juce::Font(juce::FontOptions(10.0f)));
        const auto label = juce::StringArray{"LOW", "MID", "HIGH"}[selected] + "  " + juce::String(values[selected * 3], 0)
            + " Hz  /  " + juce::String(values[selected * 3 + 1], 1) + " dB";
        g.drawText(label, 10, 3, 260, 15, juce::Justification::centredLeft);
        g.setColour(uiColour(*this, DesignTheme::muted));
        g.drawText(values[11] >= 0.5f ? "BYPASSED" : "BASE RESPONSE  /  DRAG POINTS", getWidth() - 240, 3, 230, 15, juce::Justification::centredRight);
    }
private:
    SineStack& processor;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 12> attachments;
    std::array<float, 12> values {};
    double sampleRate = 48000;
    int block = -1, selected = 0, dragged = -1, hovered = -1;
    juce::Point<float> dragOffset;
    juce::Colour colour(int band) const { return uiColour(*this, std::array<DesignTheme::Role, 3>{DesignTheme::accent, DesignTheme::secondary, DesignTheme::tertiary}[band]); }
    std::array<EqResponse::Coefficients, 3> coefficients() const {
        std::array<EqResponse::Coefficients, 3> result;
        for (int band = 0; band < 3; ++band)
            result[band] = EqResponse::coefficients(sampleRate, values[band * 3], values[band * 3 + 1], values[band * 3 + 2]);
        return result;
    }
    int handleAt(juce::Point<float> point) const {
        int closest = -1; float distance = 13;
        for (int n = 0; n < 3; ++n) {
            const int band = (selected + n) % 3;
            const float candidate = bandPoint(band).getDistanceFrom(point);
            if (candidate < distance) { closest = band; distance = candidate; }
        }
        return closest;
    }
    void drawHandle(juce::Graphics& g, int band) {
        const auto point = bandPoint(band);
        if (band == selected || band == hovered) {
            g.setColour(colour(band).withAlpha(0.2f)); g.fillEllipse(point.x - 11, point.y - 11, 22, 22);
        }
        g.setColour(colour(band)); g.fillEllipse(point.x - 7, point.y - 7, 14, 14);
        g.setColour(uiColour(*this, DesignTheme::graph)); g.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
        g.drawText(juce::StringArray{"L", "M", "H"}[band], juce::roundToInt(point.x) - 7, juce::roundToInt(point.y) - 7, 14, 14, juce::Justification::centred);
    }
};
