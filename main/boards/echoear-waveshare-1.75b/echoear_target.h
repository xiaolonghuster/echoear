#pragma once
#include "driver/gpio.h"
#include "lvgl.h"
namespace EchoEarTarget {
inline constexpr const char* kDisplayName = "Waveshare 1.75-B";
inline constexpr auto kRotation = LV_DISPLAY_ROTATION_270;
inline constexpr bool kDeviceAec = true;
inline constexpr bool kSoundDirection = true;
inline constexpr auto kSdClock = GPIO_NUM_2;
inline constexpr auto kSdCommand = GPIO_NUM_1;
inline constexpr auto kSdData = GPIO_NUM_3;
}  // namespace EchoEarTarget
