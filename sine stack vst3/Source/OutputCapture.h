#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

// One audio producer and one message-thread consumer. A slow UI drops samples;
// it never blocks the audio thread or modifies the host's output buffer.
class OutputCapture final {
public:
    static constexpr int capacity = 32768;
    struct Frame { float left = 0, right = 0, rate = 0; std::uint64_t sequence = 0; };
    void setEnabled(bool value) noexcept { enabled.store(value, std::memory_order_relaxed); }
    bool isEnabled() const noexcept { return enabled.load(std::memory_order_relaxed); }
    void push(const juce::AudioBuffer<float>& audio, double rate) noexcept {
        const auto first = sequence;
        sequence += static_cast<std::uint64_t>(audio.getNumSamples());
        if (!isEnabled() || audio.getNumChannels() == 0 || rate <= 0) return;
        int start1, size1, start2, size2;
        fifo.prepareToWrite(audio.getNumSamples(), start1, size1, start2, size2);
        const auto* left = audio.getReadPointer(0);
        const auto* right = audio.getReadPointer(juce::jmin(1, audio.getNumChannels() - 1));
        int sample = 0;
        auto copy = [&](int start, int size) {
            for (int i = 0; i < size; ++i, ++sample)
                frames[start + i] = {left[sample], right[sample], static_cast<float>(rate), first + static_cast<std::uint64_t>(sample)};
        };
        copy(start1, size1); copy(start2, size2);
        fifo.finishedWrite(size1 + size2);
    }
    template<class Consumer> int read(Consumer&& consume) noexcept {
        int start1, size1, start2, size2;
        fifo.prepareToRead(fifo.getNumReady(), start1, size1, start2, size2);
        for (int i = 0; i < size1; ++i) consume(frames[start1 + i]);
        for (int i = 0; i < size2; ++i) consume(frames[start2 + i]);
        fifo.finishedRead(size1 + size2);
        return size1 + size2;
    }
    void discard() noexcept { read([](const Frame&) {}); }
private:
    static_assert(std::atomic<bool>::is_always_lock_free);
    std::atomic<bool> enabled {false};
    juce::AbstractFifo fifo {capacity};
    std::unique_ptr<Frame[]> frames {std::make_unique<Frame[]>(capacity)};
    std::uint64_t sequence = 0; // Audio thread only; gaps identify dropped samples.
};
