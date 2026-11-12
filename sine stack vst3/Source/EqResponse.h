#pragma once
#include <juce_dsp/juce_dsp.h>
#include <complex>

namespace EqResponse {
using Coefficients = std::array<double, 6>;
inline Coefficients coefficients(double sampleRate, double frequency, double db, double q) {
    auto result = juce::dsp::IIR::ArrayCoefficients<double>::makePeakFilter(sampleRate,
        juce::jlimit(20.0, sampleRate * 0.45, frequency), q, juce::Decibels::decibelsToGain(db));
    const double a0 = result[3];
    for (auto& value : result) value /= a0;
    return result;
}
inline std::complex<double> response(const Coefficients& a, double frequency, double sampleRate) {
    const auto z = std::polar(1.0, -juce::MathConstants<double>::twoPi * frequency / sampleRate);
    return (a[0] + a[1] * z + a[2] * z * z) / (1.0 + a[4] * z + a[5] * z * z);
}
inline double decibels(const std::array<Coefficients, 3>& bands, double frequency, double sampleRate,
                       double amount, double output, bool bypass) {
    if (bypass) return 0;
    std::complex<double> wet(1, 0);
    for (const auto& band : bands) wet *= response(band, frequency, sampleRate);
    // Mix complex responses, including phase cancellation, just as the DSP mixes audio.
    return 20 * std::log10(juce::jmax(1.0e-12, std::abs(1.0 - amount + amount * wet))) + output;
}
}
