#pragma once
#include "driver/gpio.h"
#include "lvgl.h"
namespace EchoEarTarget {
inline constexpr const char* kDisplayName = "VIEWE SmartRing 1.75";
inline constexpr auto kRotation = LV_DISPLAY_ROTATION_0;
inline constexpr bool kDeviceAec = false;
inline constexpr bool kSoundDirection = false;
inline constexpr auto kSdClock = GPIO_NUM_3;
inline constexpr auto kSdCommand = GPIO_NUM_4;
inline constexpr auto kSdData = GPIO_NUM_2;
}  // namespace EchoEarTarget
