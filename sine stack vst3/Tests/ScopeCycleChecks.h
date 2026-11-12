#pragma once
#include "../Source/ScopeCycle.h"
#include <vector>
#include <random>

inline void checkScopeCycle() {
    constexpr double tau = 6.283185307179586;
    std::vector<std::array<float, 2>> samples(32768);
    ScopeCycle cycle;
    auto analyse = [&](double sr) { cycle.update(static_cast<int>(samples.size()), sr, [&](int n, int c) { return samples[n][c]; }); };
    for (double sr : {44100., 48000., 96000., 192000.}) for (double hz : {27.5, 55., 110., 440., 1000., 3000.})
        for (int kind = 0; kind < 3; ++kind) {
            cycle.reset();
            for (int frame = 0; frame < 4; ++frame) {
                for (int n = 0; n < static_cast<int>(samples.size()); ++n) {
                    double phase = (n + frame * static_cast<int>(sr / 30)) * hz / sr; phase -= std::floor(phase);
                    const float value = kind == 0 ? static_cast<float>(std::sin(tau * phase)) : kind == 1 ? (phase < .5 ? 1.f : -1.f) : static_cast<float>(2 * phase - 1);
                    samples[n] = {.6f * value, -.3f * value};
                }
                analyse(sr);
                require(cycle.state() == ScopeCycle::State::locked && std::abs(cycle.frequency() / hz - 1) < .025,
                        "Cycle did not find the sine/square/saw period across sample rates");
                for (int n = 0; n <= ScopeCycle::points; ++n)
                    require(std::abs(cycle.waveform()[0][n] + 2 * cycle.waveform()[1][n]) < 1.e-6,
                            "Cycle alignment changed stereo phase or amplitude ratio");
            }
        }
    // Advancing block boundaries must not make the normalized cycle scroll.
    cycle.reset(); ScopeCycle::Wave previous {};
    for (int frame = 0; frame < 20; ++frame) {
        for (int n = 0; n < static_cast<int>(samples.size()); ++n) {
            const double phase = (n + frame * 1597) * 441.37 / 48000;
            samples[n] = {static_cast<float>(.6 * std::sin(tau * phase)), static_cast<float>(.3 * std::cos(tau * phase))};
        }
        analyse(48000);
        if (frame > 0) {
            double error = 0, cross = 0, leftPower = 0, rightPower = 0;
            for (int n = 0; n < ScopeCycle::points; ++n) {
                const auto l = cycle.waveform()[0][n], r = cycle.waveform()[1][n];
                error += std::pow(l - previous[0][n], 2); cross += l * r; leftPower += l * l; rightPower += r * r;
            }
            require(std::sqrt(error / ScopeCycle::points) < .015, "Stable cycle drifts with the capture boundary");
            require(std::abs(cross / std::sqrt(leftPower * rightPower)) < .025, "Cycle removed the stereo phase offset");
        }
        previous = cycle.waveform();
    }
    for (int frame = 0; frame < 20; ++frame) {
        for (int n = 0; n < static_cast<int>(samples.size()); ++n) samples[n] = {static_cast<float>(.2 * std::sin(tau * (n + frame * 1600) * 220 / 48000)), 0};
        analyse(48000);
    }
    float peak = 0; for (float v : cycle.waveform()[0]) peak = std::max(peak, std::abs(v));
    require(std::abs(cycle.frequency() - 220) < 1 && std::abs(peak - .2f) < .01f, "Cycle did not follow note/level changes");
    for (int n = 0; n < static_cast<int>(samples.size()); ++n) samples[n] = {0, static_cast<float>(.4 * std::sin(tau * n * 330 / 48000))};
    analyse(48000);
    require(cycle.state() == ScopeCycle::State::locked && std::abs(cycle.frequency() - 330) < 1, "Right-only output cannot lock");
    cycle.reset();
    for (int n = 0; n < static_cast<int>(samples.size()); ++n)
        samples[n] = {static_cast<float>(.6 * std::sin(tau * n * 440 / 48000)), static_cast<float>(.3 * std::sin(tau * n * 550 / 48000 + .3))};
    analyse(48000);
    require(cycle.state() == ScopeCycle::State::locked, "Different stereo frequencies did not produce a reference cycle");
    const auto& right = cycle.waveform()[1];
    const double step = tau * 550 / cycle.frequency() / ScopeCycle::points;
    for (int n = 1; n < ScopeCycle::points; ++n)
        require(std::abs(right[n + 1] + right[n - 1] - 2 * std::cos(step) * right[n]) < .002,
                "Secondary channel was wrapped or given a false matching endpoint");
    for (auto& s : samples) s = {0, 0}; analyse(48000);
    require(cycle.state() == ScopeCycle::State::steady && cycle.frequency() == 0, "Silence retained a stale cycle");
    for (auto& s : samples) s = {.2f, -.1f}; analyse(48000);
    require(cycle.state() == ScopeCycle::State::steady && std::abs(cycle.waveform()[0][0] - .2f) < 1.e-6, "DC was mistaken for a repeating cycle");
    std::mt19937 random(1789); std::uniform_real_distribution<float> noise(-.5f, .5f);
    for (auto& s : samples) s = {noise(random), noise(random)}; analyse(48000);
    require(cycle.state() == ScopeCycle::State::unstable, "Noise was displayed as a definite period");
    cycle.update(0, 48000, [](int, int) { return 0.f; });
    require(cycle.state() == ScopeCycle::State::waiting, "A reset retained the previous waveform");
}
