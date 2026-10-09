#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

// Deterministic game core; random tile placement is supplied by the caller.
struct Game2048 {
    std::array<int, 16> cells{}, previous{};
    int score = 0, previous_score = 0;
    bool can_undo = false;
    void Spawn(unsigned random) {
        int empty[16], n = 0;
        for (int i = 0; i < 16; i++)
            if (!cells[i])
                empty[n++] = i;
        if (n)
            cells[empty[random % n]] = (random / 16 % 10 == 0) ? 4 : 2;
    }
    void New(unsigned random) {
        cells.fill(0);
        score = 0;
        can_undo = false;
        Spawn(random);
        Spawn(random / 31 + 19);
    }
    bool Slide(int dir) {
        if (dir < 0 || dir > 3)
            return false;
        auto before = cells;
        int old_score = score;
        for (int line = 0; line < 4; line++) {
            int indexes[4], values[4] = {}, n = 0;
            for (int p = 0; p < 4; p++) {
                int i = dir == 0   ? line * 4 + p
                        : dir == 1 ? line * 4 + 3 - p
                        : dir == 2 ? p * 4 + line
                                   : (3 - p) * 4 + line;
                indexes[p] = i;
                if (cells[i])
                    values[n++] = cells[i];
            }
            int out[4] = {}, k = 0;
            for (int p = 0; p < n; p++) {
                int v = values[p];
                if (p + 1 < n && v == values[p + 1] && v < 1048576) {
                    v *= 2;
                    score += v;
                    p++;
                }
                out[k++] = v;
            }
            for (int p = 0; p < 4; p++)
                cells[indexes[p]] = out[p];
        }
        if (cells == before)
            return false;
        previous = before;
        previous_score = old_score;
        can_undo = true;
        return true;
    }
    void Undo() {
        if (can_undo) {
            cells = previous;
            score = previous_score;
            can_undo = false;
        }
    }
    bool Over() const {
        for (int i = 0; i < 16; i++)
            if (!cells[i] || (i % 4 < 3 && cells[i] == cells[i + 1]) ||
                (i < 12 && cells[i] == cells[i + 4]))
                return false;
        return true;
    }
    std::string Save() const {
        std::string s = std::to_string(score);
        for (int n : cells)
            s += "," + std::to_string(n);
        return s;
    }
    bool Load(const std::string& s) {
        std::array<int, 17> values{};
        size_t pos = 0;
        for (int i = 0; i < 17; i++) {
            if (pos >= s.size() || s[pos] < '0' || s[pos] > '9')
                return false;
            auto end = s.find(',', pos);
            auto token = s.substr(pos, end == std::string::npos ? end : end - pos);
            if (token.size() > 9 || token.find_first_not_of("0123456789") != std::string::npos)
                return false;
            long v = strtol(token.c_str(), nullptr, 10);
            if (v > 100000000)
                return false;
            values[i] = int(v);
            if (i < 16 && end == std::string::npos)
                return false;
            if (i == 16 && end != std::string::npos)
                return false;
            pos = end + 1;
        }
        for (int i = 1; i < 17; i++)
            if (values[i] &&
                (values[i] < 2 || values[i] > 1048576 || (values[i] & (values[i] - 1))))
                return false;
        score = values[0];
        for (int i = 0; i < 16; i++)
            cells[i] = values[i + 1];
        can_undo = false;
        return true;
    }
};

// Pocket calculator, immediate execution with explicit arithmetic error state.
struct PocketCalculator {
    std::string entry = "0";
    double accumulator = 0;
    char operation = 0;
    bool fresh = true, error = false;
    void Clear() {
        entry = "0";
        accumulator = 0;
        operation = 0;
        fresh = true;
        error = false;
    }
    void Result(double v) {
        if (!std::isfinite(v) || std::abs(v) > 1e12) {
            entry = "Error";
            error = true;
            return;
        }
        char b[32];
        snprintf(b, sizeof(b), "%.10g", v);
        entry = b;
        fresh = true;
    }
    void Key(char c) {
        if (c == 'C') {
            Clear();
            return;
        }
        if (error)
            Clear();
        if ((c >= '0' && c <= '9') || c == '.') {
            if (fresh) {
                entry = "0";
                fresh = false;
            }
            if (c == '.') {
                if (entry.find('.') == std::string::npos)
                    entry += '.';
            } else if (entry == "0")
                entry = c;
            else if (entry.size() < 12)
                entry += c;
            return;
        }
        if (c == 'B') {
            if (!fresh) {
                entry.pop_back();
                if (entry.empty() || entry == "-")
                    entry = "0";
            }
            return;
        }
        double v = strtod(entry.c_str(), nullptr);
        if (c == 'N') {
            Result(-v);
            fresh = false;
            return;
        }
        if (c == '%') {
            Result(v / 100);
            return;
        }
        if (c != '+' && c != '-' && c != '*' && c != '/' && c != '=')
            return;
        if (operation && !fresh) {
            if (operation == '+')
                v = accumulator + v;
            if (operation == '-')
                v = accumulator - v;
            if (operation == '*')
                v = accumulator * v;
            if (operation == '/') {
                if (v == 0) {
                    entry = "Error";
                    error = true;
                    operation = 0;
                    return;
                }
                v = accumulator / v;
            }
            Result(v);
            if (error) {
                operation = 0;
                return;
            }
        }
        accumulator = v;
        operation = c == '=' ? 0 : c;
        fresh = true;
    }
};
