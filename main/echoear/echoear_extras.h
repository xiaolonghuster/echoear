#pragma once
#include <atomic>
#include <functional>
#include <list>
#include <string>
#include "local_apps.h"
#include "lvgl.h"
#include "pet_models.h"
class EchoEarDisplay;
class EchoEarExtras {
public:
    explicit EchoEarExtras(EchoEarDisplay* display);
    ~EchoEarExtras();
    void Open(int page = 0);
    void Hide() { lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN); }
    void Attach(lv_obj_t* parent) { lv_obj_set_parent(root_, parent); }
    void Tick();
    void Pet(int action);
    uint32_t Background(uint32_t fallback) const;
    void SetUpdateFeed(const std::string& url);

private:
    EchoEarDisplay* d_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* text_ = nullptr;
    struct Action {
        EchoEarExtras* self;
        int id;
    };
    std::list<Action> actions_;
    lv_timer_t* timer_ = nullptr;
    PetVitals pet_;
    DailyAlarm alarm_;
    SudokuGame game_;
    Game2048 tiles_;
    PocketCalculator calculator_;
    int64_t stopwatch_start_ = 0, stopwatch_elapsed_ = 0;
    bool stopwatch_running_ = false;
    std::string storage_status_;
    void Storage(bool backup);
    void SaveTiles();
    void RefreshSense();
    int page_ = 0, selected_ = 0, background_ = 0, sudoku_seed_ = 0;
    int64_t last_pet_tick_ = 0;
    std::atomic<bool> busy_{false};
    std::string weather_, update_url_, update_sha_, update_version_;
    int update_size_ = 0;
    int background_color_ = 0;
    const char* T(const char* zh, const char* en) const;
    void ActionClick(int id);
    void Label(const std::string& text, int y, int height = 160);
    void Button(const char* text, int id, int x, int y, int width = 280, int height = 48,
                bool small = false);
    void SavePet();
    void SaveAlarm();
    void SaveGame();
    void Reminder(const char* zh, const char* en);
    void Weather();
    void CheckUpdate();
    void InstallUpdate();
    void Rollback();
    void Reset();
    void Job(std::function<void()> fn, bool reserved = false);
    void Message(const std::string& message);
};
