#include <cassert>
#include "../pet_models.h"
int CountSolutions(SudokuGame& g) {
    int cell = -1;
    for (int i = 0; i < 81; ++i)
        if (!g.cells[i]) {
            cell = i;
            break;
        }
    if (cell < 0)
        return g.Solved() ? 1 : 0;
    int n = 0;
    for (int v = 1; v <= 9 && n < 2; ++v) {
        g.cells[cell] = v;
        if (!g.Conflict(cell))
            n += CountSolutions(g);
    }
    g.cells[cell] = 0;
    return n;
}
int main() {
    PetVitals pet;
    for (int i = 0; i < 1000; ++i)
        pet.Step(false);
    assert(pet.food == 0 && pet.energy == 0 && pet.mood == 0);
    for (int i = 0; i < 1000; ++i)
        pet.Act(1);
    assert(pet.food == 100 && pet.mood == 100 && pet.xp == 2000);
    for (int i = 0; i < 100; ++i)
        pet.Step(true);
    assert(pet.energy == 100);
    DailyAlarm a;
    a.enabled = true;
    a.minute = 480;
    const int64_t day = 20000LL * 86400;
    assert(!a.Due(day + 479 * 60));
    assert(a.Due(day + 480 * 60));
    assert(!a.Due(day + 480 * 60 + 1));
    assert(a.Due(day + 86400 + 480 * 60));
    a.enabled = false;
    assert(!a.Due(day + 2 * 86400 + 480 * 60));
    a.enabled = true;
    assert(!a.Due(0));
    for (unsigned seed = 0; seed < 18; ++seed) {
        SudokuGame g;
        g.New(seed);
        assert(CountSolutions(g) == 1);
        auto saved = g.Save();
        assert(!g.Load("bad"));
        assert(g.Save() == saved);
        int cell = 0;
        while (g.givens[cell])
            ++cell;
        assert(g.Put(cell, g.solution[cell]));
        g.Undo();
        assert(g.Save() == saved);
        assert(g.Load(saved));
        for (int i = 0; i < 81; ++i)
            g.Put(i, g.solution[i]);
        assert(g.Solved());
        assert(!g.Put(-1, 1));
        assert(!g.Put(81, 1));
        assert(!g.Put(cell, 10));
    }
}
