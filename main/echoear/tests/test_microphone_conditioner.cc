#include <cassert>
#include <cmath>
#include "../microphone_conditioner.h"

int main() {
    MicrophoneConditioner filter(24000);
    int output = 0;
    for (int i = 0; i < 24000; ++i)
        output = filter.Process(180);
    assert(std::abs(output) <= 1);  // DC offsets must not be amplified indefinitely.
    filter.Reset();
    double power = 0;
    for (int i = 0; i < 24000; ++i) {
        int16_t input = static_cast<int16_t>(100 * std::sin(2 * 3.14159265 * 1000 * i / 24000));
        int value = filter.Process(input);
        if (i >= 12000)
            power += value * value;
    }
    double rms = std::sqrt(power / 12000);
    assert(rms > 1000 && rms < 1200);  // Quiet speech-band signal receives the intended gain.
    filter.Reset();
    for (int i = 0; i < 10000; ++i) {
        int value = filter.Process(i % 2 ? 32767 : -32768);
        assert(value >= -28000 && value <= 28000);
    }
    filter.Reset();
    assert(filter.Process(0) == 0);
}
