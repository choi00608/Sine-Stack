#pragma once
#include "OutputCapture.h"
#include "DesignTheme.h"
#include "ScopeCycle.h"

class OutputScope final : public juce::Component, private juce::Timer {
public:
    explicit OutputScope(OutputCapture& source) : capture(source) {
        setName("Output waveform");
        addAndMakeVisible(view); addAndMakeVisible(timeRange); addAndMakeVisible(freeze); addAndMakeVisible(trigger);
        view.setName("Scope view");
        view.addItem("Cycle / L + R", 1); view.addItem("Real-time", 2);
        view.setSelectedId(1, juce::dontSendNotification);
        view.onChange = [this] {
            updateView();
            if (view.getSelectedId() == 1) { cycle.reset(); analyseCycle(); }
            repaint();
        };
        timeRange.setName("Scope time range");
        timeRange.addItem("5 ms", 5); timeRange.addItem("20 ms", 20);
        timeRange.addItem("50 ms", 50); timeRange.addItem("100 ms", 100);
        timeRange.setSelectedId(20, juce::dontSendNotification);
        timeRange.onChange = [this] { repaint(); };
        freeze.setName("Freeze output waveform"); freeze.setClickingTogglesState(true);
        freeze.onClick = [this] { repaint(); };
        freeze.setTooltip("Hold the display. Audio continues unchanged.");
        trigger.setName("Scope trigger"); trigger.setClickingTogglesState(true);
        trigger.setToggleState(true, juce::dontSendNotification);
        trigger.onClick = [this] { repaint(); };
        trigger.setTooltip("Align both channels to a rising zero crossing on the left channel.");
        updateView(); setSize(720, 380);
    }
    ~OutputScope() override { setActive(false); }
    void setActive(bool active) {
        stopTimer(); capture.setEnabled(false);
        if (!active) return;
        capture.discard(); count = write = 0; sampleRate = 0; cycle.reset();
        freeze.setToggleState(false, juce::dontSendNotification);
        lastData = juce::Time::getMillisecondCounterHiRes();
        capture.setEnabled(true); startTimerHz(30); repaint();
    }
    void refresh() {
        const bool frozen = freeze.getToggleState();
        const int received = capture.read([&](const OutputCapture::Frame& frame) {
            if (frozen) return;
            if (sampleRate != frame.rate || (count > 0 && frame.sequence != lastSequence + 1)) { count = write = 0; cycle.reset(); }
            sampleRate = frame.rate; lastSequence = frame.sequence;
            history[write] = {frame.left, frame.right};
            write = (write + 1) % OutputCapture::capacity;
            count = juce::jmin(count + 1, OutputCapture::capacity);
        });
        if (received > 0) {
            lastData = juce::Time::getMillisecondCounterHiRes();
            if (!frozen && view.getSelectedId() == 1) analyseCycle();
        }
        repaint();
    }
    const ScopeCycle& cycleAnalysis() const { return cycle; }
    void resized() override {
        view.setBounds(getWidth() - 190, 14, 172, 26);
        freeze.setBounds(getWidth() - 286, 14, 88, 26);
        timeRange.setBounds(getWidth() - 206, 48, 96, 26);
        trigger.setBounds(getWidth() - 102, 48, 84, 26);
    }
    void paint(juce::Graphics& g) override {
        const bool cycleView = view.getSelectedId() == 1;
        g.fillAll(uiColour(*this, DesignTheme::background));
        g.setColour(uiColour(*this, DesignTheme::text)); g.setFont(17);
        g.drawText("OUTPUT SCOPE", 18, 14, 220, 26, juce::Justification::centredLeft);
        g.setColour(uiColour(*this, DesignTheme::muted)); g.setFont(11);
        g.drawText(cycleView ? "One stabilized cycle / final stereo output" : "Final output / L above, R below",
                   18, 48, getWidth() - (cycleView ? 36 : 236), 22, juce::Justification::centredLeft);
        if (cycleView) paintCycle(g); else paintRealtime(g);
        juce::String status;
        if (freeze.getToggleState()) status = "FROZEN / audio continues";
        else if (count == 0) status = "Waiting for audio";
        else if (juce::Time::getMillisecondCounterHiRes() - lastData > 250) status = "No incoming audio / last waveform";
        else if (!cycleView) status = "LIVE";
        else switch (cycle.state()) {
            case ScopeCycle::State::locked: status = "STABILIZED / representative cycle"; break;
            case ScopeCycle::State::steady: status = "Steady signal / no repeating cycle"; break;
            case ScopeCycle::State::unstable: status = "No clear period / try Real-time"; break;
            default: status = "Finding a cycle..."; break;
        }
        if (sampleRate > 0) status += "  /  " + juce::String(sampleRate / 1000, 1) + " kHz";
        g.setColour(uiColour(*this, DesignTheme::muted)); g.setFont(11);
        g.drawText(status, 18, getHeight() - 27, getWidth() - 236, 20, juce::Justification::centredLeft);
        const auto scale = cycleView ? (cycle.state() == ScopeCycle::State::locked
            ? "~" + juce::String(cycle.frequency(), 1) + " Hz  /  " + juce::String(cycle.durationMs(), 2) + " ms" : juce::String())
            : "0 - " + juce::String(timeRange.getSelectedId()) + " ms";
        g.drawText(scale, getWidth() - 220, getHeight() - 27, 202, 20, juce::Justification::centredRight);
    }
private:
    void updateView() {
        const bool realtime = view.getSelectedId() == 2;
        timeRange.setVisible(realtime); trigger.setVisible(realtime);
    }
    void analyseCycle() { cycle.update(count, sampleRate, [this](int n, int c) { return at(n, c); }); }
    void grid(juce::Graphics& g, juce::Rectangle<float> area) {
        g.setColour(uiColour(*this, DesignTheme::graph)); g.fillRoundedRectangle(area, 6);
        g.setColour(uiColour(*this, DesignTheme::track));
        for (int i = 0; i <= 4; ++i) {
            const float y = area.getY() + area.getHeight() * i / 4;
            g.drawHorizontalLine(static_cast<int>(y), area.getX(), area.getRight());
            const float x = area.getX() + area.getWidth() * i / 4;
            g.drawVerticalLine(static_cast<int>(x), area.getY(), area.getBottom());
        }
        g.setColour(uiColour(*this, DesignTheme::muted)); g.setFont(10);
        g.drawText("+1", 24, static_cast<int>(area.getY()), 25, 14, juce::Justification::centredRight);
        g.drawText("0", 24, static_cast<int>(area.getCentreY()) - 7, 25, 14, juce::Justification::centredRight);
        g.drawText("-1", 24, static_cast<int>(area.getBottom()) - 14, 25, 14, juce::Justification::centredRight);
    }
    static float yFor(float value, juce::Rectangle<float> area) {
        return area.getCentreY() - juce::jlimit(-1.0f, 1.0f, value) * (area.getHeight() * .5f - 2);
    }
    juce::Colour channelColour(int c) const { return uiColour(*this, c == 0 ? DesignTheme::accent : DesignTheme::secondary); }
    static juce::String peakText(float peak) {
        return peak > .000001f ? juce::String(juce::Decibels::gainToDecibels(peak), 1) + " dBFS" : juce::String("-inf dBFS");
    }
    void paintCycle(juce::Graphics& g) {
        const juce::Rectangle<float> area(54, 88, getWidth() - 72.0f, getHeight() - 146.0f);
        grid(g, area);
        const auto& wave = cycle.waveform();
        const bool drawable = cycle.state() == ScopeCycle::State::locked || cycle.state() == ScopeCycle::State::steady;
        for (int c = 0; c < 2; ++c) {
            float peak = 0;
            if (drawable) {
                juce::Path path;
                for (int n = 0; n <= ScopeCycle::points; ++n) {
                    const float x = area.getX() + area.getWidth() * n / ScopeCycle::points;
                    const float y = yFor(wave[c][n], area);
                    if (n == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
                    peak = juce::jmax(peak, std::abs(wave[c][n]));
                }
                // Different weights keep both colours visible for identical L/R signals.
                g.setColour(channelColour(c).withAlpha(c == 0 ? .85f : 1.0f));
                g.strokePath(path, juce::PathStrokeType(c == 0 ? 3.6f : 1.5f));
            }
            g.setColour(channelColour(c)); g.setFont(11);
            const auto label = juce::String(c == 0 ? "L" : "R") + "  Shape peak " + (drawable ? peakText(peak) : "--");
            g.drawText(label, area.toNearestInt().reduced(10, 3).removeFromTop(18).translated(0, c * 18), juce::Justification::centredRight);
        }
        if (!drawable) {
            g.setColour(uiColour(*this, DesignTheme::muted)); g.setFont(13);
            g.drawText(cycle.state() == ScopeCycle::State::unstable ? "No clear repeating cycle. Select Real-time to inspect this sound."
                : "Play a note to see one cycle.", area.reduced(12).toNearestInt(), juce::Justification::centred, true);
        }
        g.setColour(uiColour(*this, DesignTheme::muted)); g.setFont(10);
        for (int i = 0; i <= 4; ++i) {
            const int x = static_cast<int>(area.getX() + area.getWidth() * i / 4);
            const juce::String labels[] {"0", "1/4", "1/2", "3/4", "1 cycle"};
            g.drawText(labels[i], x - (i == 4 ? 44 : 16), static_cast<int>(area.getBottom()) + 4, 48, 16, juce::Justification::centred);
        }
    }
    void paintRealtime(juce::Graphics& g) {
        const int desired = juce::jmax(2, static_cast<int>(sampleRate * timeRange.getSelectedId() / 1000.0f));
        const int length = juce::jmin(count, desired);
        int start = juce::jmax(0, count - length);
        if (trigger.getToggleState() && length == desired) {
            const int earliest = juce::jmax(1, start - desired);
            for (int i = start; i >= earliest; --i)
                if (at(i - 1, 0) <= 0 && at(i, 0) > 0) { start = i; break; }
        }
        const float laneHeight = (getHeight() - 120.0f) * .5f;
        for (int c = 0; c < 2; ++c) {
            const juce::Rectangle<float> area(54, 88 + c * laneHeight, getWidth() - 72.0f, laneHeight - 14);
            grid(g, area);
            g.setColour(channelColour(c)); g.setFont(13);
            g.drawText(c == 0 ? "L" : "R", 4, static_cast<int>(area.getCentreY()) - 10, 24, 20, juce::Justification::centred);
            juce::Path wave;
            const int pixels = static_cast<int>(area.getWidth());
            float peak = 0;
            for (int i = 0; i < length; ++i) peak = juce::jmax(peak, std::abs(at(start + i, c)));
            // Min/max per pixel preserves narrow peaks for long real-time windows.
            for (int x = 0; x < pixels; ++x) {
                const int first = x * desired / pixels;
                if (first >= length) break;
                const int end = juce::jmin(length, juce::jmax(first + 1, (x + 1) * desired / pixels));
                float lo = at(start + first, c), hi = lo;
                for (int i = first + 1; i < end; ++i) { lo = juce::jmin(lo, at(start + i, c)); hi = juce::jmax(hi, at(start + i, c)); }
                const float px = area.getX() + x;
                if (x == 0) wave.startNewSubPath(px, yFor(lo, area)); else wave.lineTo(px, yFor(lo, area));
                wave.lineTo(px, yFor(hi, area));
            }
            g.setColour(channelColour(c)); g.strokePath(wave, juce::PathStrokeType(1.3f));
            g.setFont(11); g.drawText("Peak " + peakText(peak), area.toNearestInt().reduced(8, 2).removeFromTop(18), juce::Justification::centredRight);
        }
    }
    float at(int chronological, int channel) const { return history[(write - count + chronological + OutputCapture::capacity) % OutputCapture::capacity][channel]; }
    void timerCallback() override { refresh(); }
    OutputCapture& capture;
    juce::ComboBox view, timeRange;
    ScopeCycle cycle;
    juce::TextButton freeze {"Freeze"}, trigger {"Trigger"};
    std::array<std::array<float, 2>, OutputCapture::capacity> history {};
    int write = 0, count = 0;
    float sampleRate = 0;
    std::uint64_t lastSequence = 0;
    double lastData = 0;
};

class OutputScopeWindow final : public juce::DocumentWindow {
public:
    OutputScopeWindow(OutputCapture& capture, juce::LookAndFeel& theme)
        : DocumentWindow("Sine Stack / Output Scope", juce::Colours::black, closeButton), scope(capture) {
        setLookAndFeel(&theme); setUsingNativeTitleBar(true);
        setContentNonOwned(&scope, true); setResizable(true, false); setResizeLimits(600, 320, 1400, 800);
        centreWithSize(720, 408);
    }
    ~OutputScopeWindow() override { scope.setActive(false); clearContentComponent(); setLookAndFeel(nullptr); }
    void show() { if (!isVisible()) { scope.setActive(true); setVisible(true); } toFront(false); }
    void closeButtonPressed() override { scope.setActive(false); setVisible(false); }
private:
    OutputScope scope;
};
