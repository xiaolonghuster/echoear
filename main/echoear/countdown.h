#pragma once
#include <cstdint>
// Monotonic, non-persistent countdown; expiry is delivered exactly once.
class Countdown {
public:
    void Start(int minutes, int64_t now) { deadline_ = now + int64_t(minutes) * 60000000; }
    void Cancel() { deadline_ = 0; }
    bool Running() const { return deadline_ != 0; }
    int SecondsLeft(int64_t now) const {
        return deadline_ > now ? int((deadline_ - now + 999999) / 1000000) : 0;
    }
    bool Expire(int64_t now) {
        if (!Running() || now < deadline_)
            return false;
        Cancel();
        return true;
    }

private:
    int64_t deadline_ = 0;
};
