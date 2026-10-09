#pragma once
#include <atomic>
#include "sound_direction.h"
struct SenseHub {
    SoundDirection sound;
    std::atomic<bool> motion_enabled{true}, sound_enabled{false}, reverse{false}, imu_ready{false};
    std::atomic<int> ax{0}, ay{0}, az{0}, zero_x{0}, zero_y{0}, gesture{0};
    // Rotating the UI from 90 to 270 degrees reverses the horizontal axis.
    bool ReverseSoundAxis() const { return !reverse.load(); }
    static SenseHub& Get() {
        static SenseHub instance;
        return instance;
    }
};
