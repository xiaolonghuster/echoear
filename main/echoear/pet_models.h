#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
struct PetVitals {
    int food = 70, energy = 80, mood = 70, xp = 0;
    void Clamp() {
        food = std::clamp(food, 0, 100);
        energy = std::clamp(energy, 0, 100);
        mood = std::clamp(mood, 0, 100);
        xp = std::clamp(xp, 0, 9999);
    }
    void Step(bool sleeping) {
        food -= 1;
        energy += sleeping ? 3 : -1;
        mood += (food < 20 || energy < 20) ? -2 : -1;
        Clamp();
    }
    void Act(int action) {
        if (action == 0) {
            mood += 12;
            xp += 1;
        }
        if (action == 1) {
            food += 20;
            mood += 3;
            xp += 2;
        }
        if (action == 2) {
            energy -= 10;
            mood += 18;
            xp += 3;
        }
        Clamp();
    }
    int Level() const { return 1 + xp / 30; }
};
struct DailyAlarm {
    int minute = 480;
    bool enabled = false;
    int64_t last_day = -1;
    bool Due(int64_t local_seconds) {
        if (!enabled || local_seconds < 1700000000)
            return false;
        auto day = local_seconds / 86400;
        if ((local_seconds % 86400) / 60 != minute || day == last_day)
            return false;
        last_day = day;
        return true;
    }
};
class SudokuGame {
public:
    std::array<int, 81> cells{}, givens{}, solution{};
    struct Move {
        int cell, old;
    };
    std::array<Move, 128> history{};
    int used = 0;
    void New(unsigned seed) {
        const char* puzzle =
            "530070000600195000098000060800060003400803001700020006060000280000419005000080079";
        const char* answer =
            "534678912672195348198342567859761423426853791713924856961537284287419635345286179";
        for (int i = 0; i < 81; ++i) {
            int j = (seed & 1) ? 80 - i : i;
            int v = puzzle[j] - '0';
            givens[i] = cells[i] = v ? 1 + (v - 1 + seed % 9) % 9 : 0;
            solution[i] = 1 + (answer[j] - '1' + seed % 9) % 9;
        }
        used = 0;
    }
    bool Put(int cell, int value) {
        if (cell < 0 || cell >= 81 || givens[cell] || value < 0 || value > 9)
            return false;
        if (cells[cell] == value)
            return false;
        if (used == 128) {
            for (int i = 1; i < 128; ++i)
                history[i - 1] = history[i];
            --used;
        }
        history[used++] = {cell, cells[cell]};
        cells[cell] = value;
        return true;
    }
    void Undo() {
        if (used) {
            auto m = history[--used];
            cells[m.cell] = m.old;
        }
    }
    bool Conflict(int i) const {
        int v = cells[i];
        if (!v)
            return false;
        for (int j = 0; j < 81; ++j)
            if (i != j && cells[j] == v &&
                (i / 9 == j / 9 || i % 9 == j % 9 ||
                 (i / 27 == j / 27 && (i % 9) / 3 == (j % 9) / 3)))
                return true;
        return false;
    }
    bool Solved() const {
        for (int i = 0; i < 81; ++i)
            if (!cells[i] || Conflict(i))
                return false;
        return true;
    }
    std::string Save() const {
        std::string s;
        for (int v : cells)
            s += char('0' + v);
        return s;
    }
    bool Load(const std::string& s) {
        if (s.size() != 81)
            return false;
        for (int i = 0; i < 81; ++i)
            if (s[i] < '0' || s[i] > '9' || (givens[i] && s[i] - '0' != givens[i]))
                return false;
        for (int i = 0; i < 81; ++i)
            cells[i] = s[i] - '0';
        used = 0;
        return true;
    }
};
