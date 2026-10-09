#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
// A bounded time-delay estimate before mono mixing. No allocation or locks in capture.
// Positive means MIC2 lags MIC1; this is an array-axis estimate, not a 360-degree bearing.
class SoundDirection {
    std::array<float, 512> a_{}, b_{};
    int used_ = 0, skip_ = 0;
    float last_a_ = 0, last_b_ = 0, hp_a_ = 0, hp_b_ = 0;

public:
    std::atomic<int> direction{0}, confidence{0}, rms_a{0}, rms_b{0}, lag{0};
    std::atomic<bool> enabled{false};
    // Capture-task-only counters: low level, weak match, edge lag, negative, zero, positive.
    std::array<unsigned, 6> windows{};
    int raw_correlation = 0;
    int max_rms_a = 0, max_rms_b = 0;
    void Feed(const int16_t* tdm, int count) {
        if (!enabled.load()) {
            used_ = skip_ = 0;
            last_a_ = last_b_ = hp_a_ = hp_b_ = 0;
            rms_a = rms_b = lag = 0;
            confidence = 0;
            direction = 0;
            return;
        }
        for (int i = 0; i < count; i++) {
            float a = tdm[i * 4], b = tdm[i * 4 + 2];
            hp_a_ = a - last_a_ + 0.98f * hp_a_;
            last_a_ = a;
            hp_b_ = b - last_b_ + 0.98f * hp_b_;
            last_b_ = b;
            if (skip_ > 0) {
                --skip_;
                continue;
            }
            a_[used_] = hp_a_;
            b_[used_] = hp_b_;
            if (++used_ == 512) {
                Estimate();
                used_ = 0;
                skip_ = 1888;
            }
        }
    }
    void Estimate() {
        float best = -1, ea = 0, eb = 0;
        int lag_best = 0;
        for (int i = 0; i < 512; i++) {
            ea += a_[i] * a_[i];
            eb += b_[i] * b_[i];
        }
        rms_a = int(std::sqrt(ea / 512));
        rms_b = int(std::sqrt(eb / 512));
        max_rms_a = std::max(max_rms_a, rms_a.load());
        max_rms_b = std::max(max_rms_b, rms_b.load());
        raw_correlation = 0;
        lag = 0;
        if (ea < 512 * 25.0f * 25 || eb < 512 * 25.0f * 25) {
            ++windows[0];
            confidence = 0;
            direction = 0;
            return;
        }
        for (int lag = -8; lag <= 8; lag++) {
            float cross = 0, x = 0, y = 0;
            for (int i = 8; i < 504; i++) {
                float va = a_[i], vb = b_[i + lag];
                cross += va * vb;
                x += va * va;
                y += vb * vb;
            }
            float c = cross / std::sqrt(std::max(1.0f, x * y));
            if (c > best) {
                best = c;
                lag_best = lag;
            }
        }
        lag = lag_best;
        raw_correlation = int(best * 100);
        ++windows[best <= 0.65f             ? 1
                  : std::abs(lag_best) >= 8 ? 2
                  : lag_best < 0            ? 3
                  : lag_best == 0           ? 4
                                            : 5];
        const bool valid = best > 0.65f && std::abs(lag_best) < 8;
        confidence = valid ? int(best * 100) : 0;
        direction = valid ? std::clamp(lag_best * 25, -100, 100) : 0;
    }
};
