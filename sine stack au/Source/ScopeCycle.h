#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Message-thread-only analysis. The audio capture and audio processing are untouched.
// Normalised correlation follows the NSDF principle described by McLeod/Wyvill,
// "A Smarter Way to Find Pitch" (2005); implemented here without external code.
class ScopeCycle final {
public:
    static constexpr int points = 512;
    enum class State { waiting, steady, unstable, locked };
    using Wave = std::array<std::array<float, points + 1>, 2>;
    const Wave& waveform() const { return displayed; }
    State state() const { return status; }
    double frequency() const { return period > 0 ? rate / period : 0; }
    double durationMs() const { return period > 0 ? 1000 * period / rate : 0; }
    float clarity() const { return confidence; }
    void reset() { status = State::waiting; period = rate = 0; reference = -1; confidence = 0; displayed = {}; }

    template<class Sample> void update(int count, double sampleRate, Sample sample) {
        if (sampleRate <= 0 || count < 256) { reset(); return; }
        const int stride = std::max(1, static_cast<int>(sampleRate / 12000));
        const double analysisRate = sampleRate / stride;
        const int size = std::min(2048, count / stride);
        const int begin = count - size * stride;
        std::array<std::array<float, 2048>, 2> signal {};
        std::array<double, 2> mean {}, energy {};
        for (int c = 0; c < 2; ++c) {
            for (int n = 0; n < size; ++n) {
                float value = 0;
                for (int k = 0; k < stride; ++k) value += sample(begin + n * stride + k, c);
                signal[c][n] = value / stride; mean[c] += signal[c][n];
            }
            mean[c] /= size;
            for (int n = 0; n < size; ++n) {
                signal[c][n] -= static_cast<float>(mean[c]); energy[c] += signal[c][n] * signal[c][n];
            }
        }
        // Never sum L/R for detection: antiphase stereo must not cancel the reference.
        const int channel = energy[0] >= energy[1] * .01 ? 0 : 1;
        if (energy[channel] / size < 1.e-10) {
            reset(); status = State::steady;
            for (int c = 0; c < 2; ++c) displayed[c].fill(static_cast<float>(mean[c]));
            return;
        }
        const int maxLag = std::min({1022, size / 3, static_cast<int>(analysisRate / 20)});
        const int minLag = std::max(2, static_cast<int>(analysisRate / 4000));
        if (maxLag <= minLag + 2) { reset(); return; }
        const int window = std::min(1024, size - maxLag);
        const int origin = size - window;
        std::array<double, 1024> score {};
        for (int lag = 0; lag <= maxLag; ++lag) {
            double product = 0, power = 0;
            for (int n = origin; n < size; ++n) {
                const double a = signal[channel][n], b = signal[channel][n - lag];
                product += a * b; power += a * a + b * b;
            }
            score[lag] = power > 1.e-16 ? 2 * product / power : 0;
        }
        bool crossedZero = false;
        double best = 0;
        for (int lag = 1; lag < maxLag; ++lag) {
            crossedZero |= score[lag] <= 0;
            if (crossedZero && lag >= minLag && score[lag] > score[lag - 1] && score[lag] >= score[lag + 1]) best = std::max(best, score[lag]);
        }
        if (best < .86) { reset(); status = State::unstable; return; }
        int chosen = 0; crossedZero = false;
        for (int lag = 1; lag < maxLag; ++lag) {
            crossedZero |= score[lag] <= 0;
            if (crossedZero && lag >= minLag && score[lag] >= std::max(.86, best * .90)
                && score[lag] > score[lag - 1] && score[lag] >= score[lag + 1]) { chosen = lag; break; }
        }
        if (chosen == 0) { reset(); status = State::unstable; return; }
        const double curvature = score[chosen - 1] - 2 * score[chosen] + score[chosen + 1];
        const double adjustment = std::abs(curvature) > 1.e-12
            ? std::clamp(.5 * (score[chosen - 1] - score[chosen + 1]) / curvature, -.5, .5) : 0;
        const double detected = (chosen + adjustment) * stride;
        const bool continuing = status == State::locked && reference == channel && rate == sampleRate && std::abs(detected - period) < period * .08;
        period = continuing ? .8 * period + .2 * detected : detected;
        rate = sampleRate; reference = channel; confidence = static_cast<float>(score[chosen]);

        auto interpolate = [&](double position, int c) {
            const int index = std::clamp(static_cast<int>(position), 0, count - 2);
            const float fraction = static_cast<float>(position - index);
            return sample(index, c) + fraction * (sample(index + 1, c) - sample(index, c));
        };
        const int latest = static_cast<int>(count - period - 2);
        double start = latest, slope = 0;
        for (int n = std::max(1, latest - static_cast<int>(std::ceil(period))); n <= latest; ++n) {
            const double a = sample(n - 1, channel) - mean[channel], b = sample(n, channel) - mean[channel];
            if (a <= 0 && b > 0 && b - a > slope) { slope = b - a; start = n - 1 - a / (b - a); }
        }
        Wave next {};
        for (int c = 0; c < 2; ++c) for (int n = 0; n < points; ++n)
            next[c][n] = interpolate(start + period * n / points, c);
        int shift = 0;
        if (continuing) {
            // One common phase shift preserves the actual L/R phase relationship.
            double bestMatch = -1.e30;
            auto match = [&](int offset) {
                double value = 0;
                for (int n = 0; n < points; n += 2) value += displayed[channel][n] * next[channel][(n + offset + points) % points];
                return value;
            };
            for (int offset = 0; offset < points; offset += 4) {
                const double value = match(offset); if (value > bestMatch) { bestMatch = value; shift = offset; }
            }
            const int coarse = shift;
            for (int offset = coarse - 3; offset <= coarse + 3; ++offset) {
                const double value = match(offset); if (value > bestMatch) { bestMatch = value; shift = (offset + points) % points; }
            }
        }
        // Read both channels from one actual time interval. Do not wrap each
        // channel's samples or force its endpoints to match: modulated stereo
        // channels need not share an exact period.
        double alignedStart = start + period * shift / points;
        while (alignedStart + period > count - 1) alignedStart -= period;
        while (alignedStart < 0) alignedStart += period;
        alignedStart = std::clamp(alignedStart, 0.0, count - 1.0 - period);
        for (int c = 0; c < 2; ++c) {
            for (int n = 0; n <= points; ++n) {
                const float value = interpolate(alignedStart + period * n / points, c);
                displayed[c][n] = continuing ? .75f * displayed[c][n] + .25f * value : value;
            }
        }
        status = State::locked;
    }
private:
    Wave displayed {};
    State status = State::waiting;
    double period = 0, rate = 0;
    float confidence = 0;
    int reference = -1;
};
