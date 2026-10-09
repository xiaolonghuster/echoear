#include <cassert>
#include "../countdown.h"
int main() {
    Countdown timer;
    assert(!timer.Running() && !timer.Expire(100));
    const int64_t start = 900000000000LL;
    timer.Start(1, start);
    assert(timer.SecondsLeft(start) == 60);
    assert(timer.SecondsLeft(start + 59999999) == 1);
    assert(!timer.Expire(start + 59999999));
    assert(timer.Expire(start + 60000000));
    assert(!timer.Expire(start + 60000001));
    timer.Start(120, start);
    assert(timer.SecondsLeft(start) == 7200);
    timer.Cancel();
    assert(!timer.Expire(start + 7200000000LL));
    timer.Start(1, start);
    assert(timer.Expire(start + 8000000000LL));
    timer.Start(2, start);
    timer.Start(1, start + 1000000);
    assert(timer.SecondsLeft(start + 1000000) == 60);
}
