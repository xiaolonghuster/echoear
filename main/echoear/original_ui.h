#pragma once
#include <atomic>
#include <functional>
#include <list>
#include <string>
#include <vector>
#include "lvgl.h"
class EchoEarDisplay;
// Speaker-style navigation, adapted to Xiaozhi services and the 466px panel.
class OriginalUi {
public:
    explicit OriginalUi(EchoEarDisplay* display);
    ~OriginalUi();
    void Launcher();
    void Notify(const char* text, int duration_ms);
#if CONFIG_ECHOEAR_UI_REVIEW
    void Review();
#endif
    void Home();
    void Quick();
    void Back();
    void OpenApp(int page);
    bool Active() const { return page_ >= 0; }
    lv_obj_t* AppHost() const { return root_; }

private:
    EchoEarDisplay* d_;
    lv_obj_t *root_, *body_, *quick_, *nav_, *top_, *status_ = nullptr;
    lv_obj_t *password_ = nullptr, *keyboard_ = nullptr;
    lv_timer_t* timer_;
    lv_obj_t* toast_;
    int64_t toast_until_ = 0;
    int page_ = -1, launcher_page_ = 0;
    int64_t activity_ = 0;
    bool wifi_enabled_ = true;
    std::atomic<bool> scanning_{false};
    struct Network {
        std::string ssid;
        bool secured;
        int channel;
    };
    std::vector<Network> networks_;
    std::string selected_ssid_, scan_status_;
    int selected_channel_ = 0;
    struct Action {
        OriginalUi* ui;
        std::function<void()> fn;
    };
    std::list<Action> actions_, quick_actions_;
    const char* T(const char* zh, const char* en) const;
    lv_obj_t* Panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color);
    lv_obj_t* Label(lv_obj_t* parent, const char* text, int x, int y, int width);
    lv_obj_t* Button(lv_obj_t* parent, const char* text, int x, int y, int w, int h,
                     std::function<void()> fn, uint32_t color = 0x24282D);
    void Show(int page);
    void Header(const char* title);
    void Rows(const std::vector<std::pair<std::string, std::function<void()>>>& rows);
    void Slider(bool volume, lv_obj_t* parent, int y);
    void Tick();
    void Scan();
    void Connect();
    void ToggleWifi();
    void Gesture(lv_event_t* e);
};
