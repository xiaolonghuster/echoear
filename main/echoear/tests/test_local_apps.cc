#include <cassert>
#include <iostream>
#include "../local_apps.h"
#include "../sound_direction.h"
int main() {
    Game2048 g;
    g.cells = {2, 2, 2, 2, 4, 0, 4, 0};
    assert(g.Slide(0));
    assert(g.cells[0] == 4 && g.cells[1] == 4 && g.cells[2] == 0 && g.cells[4] == 8 &&
           g.score == 16);
    g.Undo();
    assert(g.cells[0] == 2 && g.cells[3] == 2 && g.score == 0);
    g.cells = {2, 2, 4, 0};
    g.score = 0;
    assert(g.Slide(1));
    assert(g.cells[3] == 4 && g.cells[2] == 4 && g.score == 4);
    g.cells = {2, 0, 0, 0, 2};
    assert(g.Slide(2));
    assert(g.cells[0] == 4 && g.cells[4] == 0);
    auto saved = g.Save();
    Game2048 h;
    assert(h.Load(saved));
    assert(h.cells == g.cells);
    assert(!h.Load("1,3"));
    assert(!h.Load(saved + ",0"));
    g.cells = {2, 4, 2, 4, 4, 2, 4, 2, 2, 4, 2, 4, 4, 2, 4, 2};
    assert(g.Over());
    assert(!g.Slide(0));
    g.cells[0] = 4;
    assert(!g.Over());
    PocketCalculator c;
    for (char v : std::string("12+3="))
        c.Key(v);
    assert(c.entry == "15");
    c.Key('C');
    for (char v : std::string("1/0="))
        c.Key(v);
    assert(c.error);
    c.Key('2');
    assert(c.entry == "2" && !c.error);
    c.Clear();
    for (char v : std::string("0.5*8="))
        c.Key(v);
    assert(c.entry == "4");
    c.Clear();
    for (char v : std::string("2+*3="))
        c.Key(v);
    assert(c.entry == "6");
    SoundDirection d;
    d.enabled = true;
    std::array<int16_t, 512 * 4> frames{};
    std::array<int16_t, 512> noise{};
    unsigned seed = 123;
    for (int i = 0; i < 512; i++) {
        seed = seed * 1664525 + 1013904223;
        noise[i] = int(seed >> 20) - 2048;
        frames[i * 4] = noise[i];
        if (i >= 3)
            frames[i * 4 + 2] = noise[i - 3];
    }
    d.Feed(frames.data(), 512);
    assert(d.confidence.load() > 90 && d.direction.load() == 75);
    assert(d.rms_a.load() > 25 && d.rms_b.load() > 25 && d.lag.load() == 3);
    assert(d.windows[5] == 1);
    SoundDirection left;
    left.enabled = true;
    for (int i = 0; i < 512; i++) {
        frames[i * 4 + 2] = noise[i];
        frames[i * 4] = i >= 3 ? noise[i - 3] : 0;
    }
    left.Feed(frames.data(), 512);
    assert(left.confidence.load() > 90 && left.direction.load() == -75);
    assert(left.windows[3] == 1);
    left.enabled = false;
    left.Feed(frames.data(), 512);
    assert(left.confidence.load() == 0 && left.rms_a.load() == 0);
    SoundDirection silent;
    silent.enabled = true;
    frames.fill(0);
    silent.Feed(frames.data(), 512);
    assert(silent.confidence.load() == 0 && silent.windows[0] == 1);
    std::cout << "2048, calculator, delayed stereo and silence checks passed\n";
}
