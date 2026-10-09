#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Board microphones produce low-level PCM. Remove DC before applying gain;
// leave the independent playback reference channel unchanged.
class MicrophoneConditioner {
public:
    explicit MicrophoneConditioner(int sample_rate)
        : coefficient_(std::exp(-2.0f * 3.14159265f * 80.0f / sample_rate)) {}

    void Reset() { previous_ = highpass_ = 0.0f; }

    int16_t Process(int16_t input) {
        highpass_ = coefficient_ * (highpass_ + input - previous_);
        previous_ = input;
        return static_cast<int16_t>(std::clamp(highpass_ * 16.0f, -28000.0f, 28000.0f));
    }

private:
    float coefficient_;
    float previous_ = 0.0f;
    float highpass_ = 0.0f;
};
