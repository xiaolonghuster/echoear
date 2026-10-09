#include "original_ui.h"
#include <esp_app_desc.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <algorithm>
#include <ctime>
#include "application.h"
#include "audio/audio_codec.h"
#include "backlight.h"
#include "board.h"
#include "echoear_display.h"
#include "echoear_extras.h"
#include "echoear_target.h"
#include "original_icons.h"
#include "original_navigation.h"
#include "ssid_manager.h"
#include "wifi_manager.h"

const char* OriginalUi::T(const char* zh, const char* en) const { return d_->Text(zh, en); }
lv_obj_t* OriginalUi::Panel(lv_obj_t* p, int x, int y, int w, int h, uint32_t c) {
    return d_->Shape(p, x, y, w, h, c, 0);
}
lv_obj_t* OriginalUi::Label(lv_obj_t* p, const char* text, int x, int y, int w) {
    auto l = lv_label_create(p);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    return l;
}
lv_obj_t* OriginalUi::Button(lv_obj_t* p, const char* text, int x, int y, int w, int h,
                             std::function<void()> fn, uint32_t c) {
    auto b = Panel(p, x, y, w, h, c);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
    auto l = Label(b, text, 8, 0, w - 16);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    auto& list = p == quick_ ? quick_actions_ : actions_;
    list.push_back({this, std::move(fn)});
    lv_obj_add_event_cb(
        b,
        [](lv_event_t* e) {
            auto a = static_cast<Action*>(lv_event_get_user_data(e));
            a->ui->activity_ = esp_timer_get_time();
            auto fn = a->fn;
            fn();
        },
        LV_EVENT_SHORT_CLICKED, &list.back());
    return b;
}
OriginalUi::OriginalUi(EchoEarDisplay* d) : d_(d) {
    auto screen = lv_display_get_screen_active(d_->display_);
    root_ = Panel(screen, 0, 0, 466, 466, 0);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    body_ = Panel(root_, 0, 0, 466, 466, 0);
    lv_obj_add_flag(body_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(body_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    quick_ = Panel(screen, 0, 0, 466, 466, 0);
    lv_obj_add_flag(quick_, LV_OBJ_FLAG_CLICKABLE);
    nav_ = Panel(root_, 90, 420, 286, 46, 0);
    lv_obj_add_flag(nav_, LV_OBJ_FLAG_CLICKABLE);
    auto bar = Panel(nav_, 105, 8, 76, 5, 0xB9C0C5);
    lv_obj_set_style_radius(bar, 3, 0);
    top_ = Panel(root_, 80, 0, 306, 48, 0);
    lv_obj_add_flag(top_, LV_OBJ_FLAG_CLICKABLE);
    for (auto obj : {root_, quick_, nav_, top_}) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(
            obj,
            [](lv_event_t* e) { static_cast<OriginalUi*>(lv_event_get_user_data(e))->Gesture(e); },
            LV_EVENT_GESTURE, this);
    }
    lv_obj_add_event_cb(
        root_,
        [](lv_event_t* e) {
            static_cast<OriginalUi*>(lv_event_get_user_data(e))->activity_ = esp_timer_get_time();
        },
        LV_EVENT_PRESSED, this);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(quick_, LV_OBJ_FLAG_HIDDEN);
    toast_ = Panel(screen, 63, 305, 340, 94, 0x343940);
    lv_obj_set_style_radius(toast_, 18, 0);
    Label(toast_, "", 12, 12, 316);
    lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    timer_ = lv_timer_create(
        [](lv_timer_t* t) { static_cast<OriginalUi*>(lv_timer_get_user_data(t))->Tick(); }, 250,
        this);
}
OriginalUi::~OriginalUi() { lv_timer_delete(timer_); }
void OriginalUi::Gesture(lv_event_t* e) {
    auto in = lv_indev_active();
    if (!in)
        return;
    auto dir = lv_indev_get_gesture_dir(in);
    auto target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    activity_ = esp_timer_get_time();
    if (target == quick_) {
        if (dir == LV_DIR_TOP)
            lv_obj_add_flag(quick_, LV_OBJ_FLAG_HIDDEN);
    } else if (target == nav_ && dir == LV_DIR_TOP)
        Back();
    else if (target == top_ && dir == LV_DIR_BOTTOM)
        Quick();
    else if (page_ == 0 && (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT)) {
        launcher_page_ = OriginalNavigation::SwipePage(launcher_page_, dir == LV_DIR_LEFT);
        Show(0);
    }
    lv_indev_wait_release(in);
}
void OriginalUi::Home() {
    page_ = -1;
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(quick_, LV_OBJ_FLAG_HIDDEN);
    d_->extras_->Hide();
    Application::GetInstance().SetVoiceInteractionEnabled(true);
}
void OriginalUi::Launcher() { Show(0); }
void OriginalUi::Back() {
    if (!lv_obj_has_flag(quick_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(quick_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (page_ == 0)
        Home();
    else
        Launcher();
}
void OriginalUi::Header(const char* title) {
    Button(
        body_, "<", 85, 48, 48, 42, [this] { Show(OriginalNavigation::HeaderParent(page_)); }, 0);
    auto label = Label(body_, title, 138, 54, 225);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}
void OriginalUi::Rows(const std::vector<std::pair<std::string, std::function<void()>>>& rows) {
    auto list = Panel(body_, 66, 103, 334, 307, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    int y = 0;
    for (auto& row : rows) {
        if (row.second) {
            auto button = Button(list, row.first.c_str(), 0, y, 334, 55, row.second);
            lv_label_set_long_mode(lv_obj_get_child(button, 0), LV_LABEL_LONG_DOT);
            y += 61;
        } else {
            auto heading = Label(list, row.first.c_str(), 8, y, 318);
            lv_obj_set_style_text_color(heading, lv_color_hex(0x9A9FA8), 0);
            y += 43;
        }
    }
}
void OriginalUi::Slider(bool volume, lv_obj_t* p, int y) {
    int value = volume ? d_->volume_ : d_->brightness_;
    auto label = Label(p, volume ? T("音量", "Sound") : T("亮度", "Display"), 93, y, 280);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    auto slider = lv_slider_create(p);
    lv_obj_set_pos(slider, 103, y + 68);
    lv_obj_set_size(slider, 260, 24);
    lv_slider_set_range(slider, volume ? 0 : 10, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x30363E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x21B9D6), LV_PART_INDICATOR);
    auto& list = p == quick_ ? quick_actions_ : actions_;
    list.push_back({this, [this, slider, volume] {
                        int v = lv_slider_get_value(slider);
                        d_->AdjustSetting(volume, v - (volume ? d_->volume_ : d_->brightness_));
                    }});
    lv_obj_add_event_cb(
        slider,
        [](lv_event_t* e) {
            auto a = static_cast<Action*>(lv_event_get_user_data(e));
            auto fn = a->fn;
            fn();
        },
        LV_EVENT_RELEASED, &list.back());
}
void OriginalUi::Show(int page) {
    if (page_ < 0)
        Application::GetInstance().SetVoiceInteractionEnabled(false);
    if (auto codec = Board::GetInstance().GetAudioCodec())
        d_->volume_ = codec->output_volume();
    if (auto backlight = Board::GetInstance().GetBacklight())
        d_->brightness_ = backlight->brightness();
    page_ = page;
    activity_ = esp_timer_get_time();
    d_->extras_->Hide();
    lv_obj_add_flag(d_->timer_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(d_->pet_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(quick_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
    lv_obj_clean(body_);
    actions_.clear();
    status_ = nullptr;
    password_ = nullptr;
    keyboard_ = nullptr;
    lv_obj_move_foreground(top_);
    lv_obj_move_foreground(nav_);
    if (page == 0) {
        struct Item {
            const char* zh;
            const char* en;
            const char* symbol;
            uint32_t color;
            int action;
            const lv_image_dsc_t* image;
        };
        static const Item items[] = {
            {"设置", "Settings", "", 0x21B5CF, 1, &original_settings_icon},
            {"AI 配置", "AI Profile", "", 0x399563, 7, &original_profile_icon},
            {"2048", "2048", "", 0xD6A955, 180, &original_game_icon},
            {"计算器", "Calculator", "+", 0xEC943A, 181, &original_calculator_icon},
            {"时钟", "Timer", "12:00", 0x389AA0, 8, &original_clock_icon},
            {"宠物", "Pet", "~", 0xB777C7, 105, nullptr},
            {"天气", "Weather", "O", 0x4688C7, 102, nullptr},
            {"数独", "Sudoku", "9", 0x6D9B65, 103, nullptr},
            {"闹钟", "Alarm", "!", 0xC97965, 101, nullptr},
            {"倒计时", "Countdown", "1:00", 0x667FCC, 11, nullptr},
            {"秒表", "Stopwatch", "0:00", 0xA881C4, 192, nullptr},
            {"工具", "Tools", "...", 0x637787, 104, nullptr}};
        for (int i = 0; i < 2; i++) {
            auto item = items[launcher_page_ * 2 + i];
            int x = 88 + i * 172;
            auto b = Button(
                body_, item.symbol, x, 164, 118, 118,
                [this, item] {
                    if (item.action >= 100)
                        OpenApp(item.action - 100);
                    else
                        Show(item.action);
                },
                item.color);
            lv_obj_set_style_radius(b, 26, 0);
            if (item.image) {
                auto image = lv_image_create(b);
                lv_image_set_src(image, item.image);
                lv_image_set_scale(image, 256 * 112 / item.image->header.w);
                lv_obj_center(image);
            }
            auto label = Label(body_, T(item.zh, item.en), x - 24, 297, 166);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        }
        for (int i = 0; i < 6; i++) {
            auto dot = Panel(body_, 185 + i * 17, 376, i == launcher_page_ ? 15 : 6, 6,
                             i == launcher_page_ ? 0xFFFFFF : 0x50555C);
            lv_obj_set_style_radius(dot, 3, 0);
        }
    } else if (page == 1) {
        Header(T("设置", "Settings"));
        Rows({{T("无线连接", "Wireless"), {}},
              {T("无线网络  >", "WLAN  >"),
               [this] {
                   Show(2);
                   Scan();
               }},
              {T("蓝牙：不可用", "Bluetooth: unavailable"),
               [this] {
                   d_->ShowNotification(T("当前固件未提供蓝牙音频", "Bluetooth audio unavailable"));
               }},
              {T("媒体", "Media"), {}},
              {T("声音  >", "Sound  >"), [this] { Show(3); }},
              {T("显示  >", "Display  >"), [this] { Show(4); }},
              {T("输入", "Input"), {}},
              {T("输入与感知  >", "Input & motion  >"), [this] { OpenApp(90); }},
              {T("更多", "More"), {}},
              {T("关于设备  >", "About  >"), [this] { Show(6); }},
              {T("开发者模式  >", "Developer mode  >"), [this] { Show(9); }},
              {T("恢复出厂设置", "Restore factory"), [this] { OpenApp(72); }},
              {T("个性化与更新  >", "Preferences & updates  >"), [this] { Show(5); }}});
    } else if (page == 2) {
        Header("WLAN");
        std::vector<std::pair<std::string, std::function<void()>>> rows;
        rows.push_back(
            {wifi_enabled_ ? T("WLAN：开", "WLAN: On") : T("WLAN：关", "WLAN: Off"), [this] {
                 ToggleWifi();
                 Show(2);
             }});
        auto& wifi = WifiManager::GetInstance();
        if (wifi.IsConnected())
            rows.push_back({wifi.GetSsid() + T(" · 已连接", " · Connected"), [this] {
                                d_->ShowNotification(WifiManager::GetInstance().GetIpAddress());
                            }});
        rows.push_back({scanning_ ? T("扫描中…", "Scanning...") : T("刷新网络", "Refresh networks"),
                        [this] { Scan(); }});
        for (auto n : networks_)
            rows.push_back({n.ssid + (n.secured ? " *" : ""), [this, n] {
                                selected_ssid_ = n.ssid;
                                selected_channel_ = n.channel;
                                Show(12);
                            }});
        rows.push_back({"SoftAP Mode", [this] { Show(13); }});
        Rows(rows);
    } else if (page == 3) {
        Header(T("声音", "Sound"));
        Slider(true, body_, 140);
        Button(body_,
               EchoEarTarget::kDeviceAec ? T("回声消除", "Echo cancellation")
                                         : T("回声消除：不支持", "AEC unavailable"),
               88, 295, 290, 55, [this] { d_->ToggleAec(); });
    } else if (page == 4) {
        Header(T("显示", "Display"));
        Slider(false, body_, 110);
        Button(body_, T("切换外观", "Change appearance"), 88, 260, 290, 55,
               [this] { d_->ToggleAppearance(); });
        Button(body_, T("聊天字幕 开/关", "Toggle subtitles"), 88, 324, 290, 55,
               [this] { d_->ToggleSubtitle(); });
    } else if (page == 5) {
        Header(T("更多", "More"));
        Rows({{T("语言：中文 / English", "Language: English / 中文"),
               [this] {
                   d_->ToggleLanguage();
                   Show(5);
               }},
              {T("关于设备  >", "About  >"), [this] { Show(6); }},
              {T("开发者模式  >", "Developer mode  >"), [this] { Show(9); }},
              {T("在线更新  >", "Update  >"), [this] { OpenApp(7); }},
              {T("重新启动", "Restart"), [this] { OpenApp(73); }},
              {T("恢复出厂设置", "Restore factory"), [this] { OpenApp(72); }}});
    } else if (page == 6) {
        Header(T("关于", "About"));
        status_ = Label(body_, "", 82, 110, 302);
    } else if (page == 7) {
        Header("AI Profile");
        Label(body_,
              T("小智 · 当前服务\n\n角色和音色在小智平台管理。\n唤醒词：嗨，喵喵",
                "Xiaozhi · Active service\n\nManage role and voice on the Xiaozhi platform.\nWake: "
                "Hi, Miaomiao"),
              83, 130, 300);
    } else if (page == 8) {
        Header(T("时钟", "Timer"));
        status_ = Label(body_, "", 88, 175, 290);
        lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
    } else if (page == 9) {
        Header(T("开发者模式", "Developer mode"));
        Label(body_,
              T("USB 串口维护\n\n当前使用内部闪存资源，暂不提供 U 盘模式。",
                "USB serial maintenance\n\nAssets use internal flash. USB disk mode unavailable."),
              83, 115, 300);
        Button(body_, T("存储管理", "Storage"), 93, 310, 280, 55, [this] { OpenApp(91); });
    } else if (page == 11) {
        lv_obj_set_parent(d_->timer_panel_, root_);
        lv_obj_remove_flag(d_->timer_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(d_->timer_panel_);
        d_->RefreshCountdown();
        lv_obj_move_foreground(nav_);
    } else if (page == 12) {
        Header(T("连接网络", "Join network"));
        Label(body_, selected_ssid_.c_str(), 88, 95, 290);
        password_ = lv_textarea_create(body_);
        lv_obj_set_pos(password_, 73, 141);
        lv_obj_set_size(password_, 320, 49);
        lv_textarea_set_one_line(password_, true);
        lv_textarea_set_password_mode(password_, true);
        lv_textarea_set_max_length(password_, 63);
        lv_obj_set_style_text_font(password_, &lv_font_montserrat_14, 0);
        keyboard_ = lv_keyboard_create(body_);
        lv_obj_set_align(keyboard_, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(keyboard_, 78, 240);
        lv_obj_set_size(keyboard_, 310, 163);
        lv_obj_set_style_text_font(keyboard_, &lv_font_montserrat_14, LV_PART_ITEMS);
        lv_keyboard_set_textarea(keyboard_, password_);
        Button(body_, T("连接", "Connect"), 148, 196, 170, 43, [this] { Connect(); });
        lv_obj_add_event_cb(
            keyboard_,
            [](lv_event_t* e) { static_cast<OriginalUi*>(lv_event_get_user_data(e))->Connect(); },
            LV_EVENT_READY, this);
    } else if (page == 13) {
        Header("SoftAP Mode");
        Label(
            body_,
            T("启动配网热点后，用手机连接热点并打开配网页面。",
              "Start the setup hotspot, then connect your phone and open its configuration page."),
            83, 120, 300);
        Button(body_, T("开始配网", "Start setup"), 93, 290, 280, 55, [this] {
            Home();
            if (d_->wifi_config_callback_)
                d_->wifi_config_callback_();
        });
    }
}
void OriginalUi::OpenApp(int page) {
    Show(10);
    d_->extras_->Open(page);
    lv_obj_move_foreground(nav_);
    lv_obj_move_foreground(top_);
}
void OriginalUi::Quick() {
    if (auto c = Board::GetInstance().GetAudioCodec())
        d_->volume_ = c->output_volume();
    if (auto b = Board::GetInstance().GetBacklight())
        d_->brightness_ = b->brightness();
    lv_obj_clean(quick_);
    quick_actions_.clear();
    lv_obj_remove_flag(quick_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(quick_);
    const lv_image_dsc_t* icons[] = {&original_wifi_icon, &original_sound_icon,
                                     &original_light_icon};
    auto& wifi = WifiManager::GetInstance();
    const std::string network_name = wifi_enabled_ && wifi.IsConnected() ? wifi.GetSsid() : "Wi-Fi";
    const char* labels[] = {network_name.c_str(), T("声音", "Sound"), T("亮度", "Light")};
    for (int i = 0; i < 3; ++i) {
        auto button = Button(
            quick_, "", 84 + i * 103, 113, 92, 92,
            [this, i] {
                if (i == 0)
                    ToggleWifi();
                else {
                    bool volume = i == 1;
                    int old = volume ? d_->volume_ : d_->brightness_;
                    int next =
                        old >= 100 ? (volume ? 0 : 25) : std::min(100, ((old / 25) + 1) * 25);
                    d_->AdjustSetting(volume, next - old);
                }
                Quick();
            },
            i == 0 && wifi_enabled_ ? 0x17A5CA : 0x333940);
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
        auto image = lv_image_create(button);
        lv_image_set_src(image, icons[i]);
        lv_obj_center(image);
        auto label = Label(quick_, labels[i], 79 + i * 103, 213, 102);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        quick_actions_.push_back({this, [this, i] {
                                      Show(i == 0 ? 2 : i == 1 ? 3 : 4);
                                      if (i == 0)
                                          Scan();
                                  }});
        lv_obj_add_event_cb(
            button,
            [](lv_event_t* e) {
                auto a = static_cast<Action*>(lv_event_get_user_data(e));
                auto fn = a->fn;
                fn();
                if (auto input = lv_indev_active())
                    lv_indev_wait_release(input);
            },
            LV_EVENT_LONG_PRESSED, &quick_actions_.back());
    }
    char info[100];
    int level;
    bool charging, discharging;
    if (Board::GetInstance().GetBatteryLevel(level, charging, discharging) &&
        (level > 0 || charging || discharging))
        snprintf(info, sizeof(info), "%s %d%%",
                 charging ? T("充电中", "Charging") : T("电量", "Battery"), level);
    else
        snprintf(info, sizeof(info), "%s", T("USB 供电", "USB power"));
    auto power = Label(quick_, info, 98, 297, 270);
    lv_obj_set_style_text_align(power, LV_TEXT_ALIGN_CENTER, 0);
    Button(quick_, T("设置", "Settings"), 143, 350, 180, 50, [this] { Show(1); }, 0x333940);
    auto handle = Panel(quick_, 195, 428, 76, 5, 0xB9C0C5);
    lv_obj_set_style_radius(handle, 3, 0);
}
void OriginalUi::ToggleWifi() {
    wifi_enabled_ = !wifi_enabled_;
    networks_.clear();
    bool enabled = wifi_enabled_;
    Application::GetInstance().Schedule([enabled] {
        auto& w = WifiManager::GetInstance();
        if (enabled)
            w.StartStation();
        else
            w.StopStation();
    });
}
void OriginalUi::Scan() {
    if (!wifi_enabled_ || scanning_.exchange(true))
        return;
    if (xTaskCreate(
            [](void* arg) {
                auto self = static_cast<OriginalUi*>(arg);
                wifi_scan_config_t cfg{};
                cfg.show_hidden = false;
                std::vector<Network> result;
                auto error = esp_wifi_scan_start(&cfg, true);
                if (error == ESP_OK) {
                    uint16_t count = 24;
                    wifi_ap_record_t records[24]{};
                    if (esp_wifi_scan_get_ap_records(&count, records) == ESP_OK)
                        for (int i = 0; i < count; i++) {
                            std::string ssid(reinterpret_cast<char*>(records[i].ssid));
                            if (ssid.empty())
                                continue;
                            if (std::none_of(result.begin(), result.end(),
                                             [&](auto& n) { return n.ssid == ssid; }))
                                result.push_back({ssid, records[i].authmode != WIFI_AUTH_OPEN,
                                                  records[i].primary});
                        }
                }
                {
                    DisplayLockGuard lock(self->d_);
                    self->networks_ = std::move(result);
                    self->scanning_ = false;
                    if (self->page_ == 2)
                        self->Show(2);
                    if (error != ESP_OK)
                        self->d_->ShowNotification(
                            self->T("扫描忙，请稍后刷新", "Scan busy; retry shortly"));
                }
                vTaskDelete(nullptr);
            },
            "ui_wifi_scan", 6144, this, 2, nullptr) != pdPASS) {
        scanning_ = false;
        d_->ShowNotification(T("无法开始扫描", "Unable to scan"));
    }
}
void OriginalUi::Connect() {
    if (!password_)
        return;
    std::string password = lv_textarea_get_text(password_);
    if (!password.empty() && password.size() < 8) {
        d_->ShowNotification(T("密码至少 8 位", "Password: at least 8 characters"));
        return;
    }
    auto ssid = selected_ssid_;
    int channel = selected_channel_;
    Application::GetInstance().Schedule([ssid, password, channel] {
        auto& w = WifiManager::GetInstance();
        w.StopStation();
        SsidManager::GetInstance().AddSsid(ssid, password, channel);
        w.StartStation();
    });
    Show(2);
    d_->ShowNotification(T("正在连接所选网络", "Connecting to selected network"));
}
void OriginalUi::Tick() {
    if (auto input = lv_indev_active();
        input && lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED)
        activity_ = esp_timer_get_time();
    bool pressed = false;
    for (auto input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
        pressed |= lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED;
    if (OriginalNavigation::ReturnToFace(page_, !lv_obj_has_flag(quick_, LV_OBJ_FLAG_HIDDEN),
                                         pressed, esp_timer_get_time() - activity_)) {
        Home();
        return;
    }
    if (toast_until_ && esp_timer_get_time() >= toast_until_) {
        lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
        toast_until_ = 0;
    }
    if (!status_)
        return;
    if (page_ == 8) {
        time_t now = time(nullptr);
        tm local{};
        localtime_r(&now, &local);
        char text[80];
        strftime(text, sizeof(text), "%H:%M:%S\n\n%Y-%m-%d", &local);
        lv_label_set_text(status_, text);
    }
    if (page_ == 6) {
        auto& w = WifiManager::GetInstance();
        std::string info = std::string(EchoEarTarget::kDisplayName) +
                           "\nv0.19.0\nESP32-S3 · 8 MB RAM\n16 MB Flash\n\n" + w.GetSsid() + "\n" +
                           w.GetIpAddress();
        lv_label_set_text(status_, info.c_str());
    }
}

#if CONFIG_ECHOEAR_UI_REVIEW
void OriginalUi::Review() {
    xTaskCreate(
        [](void* arg) {
            auto self = static_cast<OriginalUi*>(arg);
            vTaskDelay(pdMS_TO_TICKS(15000));
            const int pages[] = {0, 1, 3, 4, 7, 12, 8, 14};
            for (int page : pages) {
                {
                    DisplayLockGuard lock(self->d_);
                    lv_timer_pause(self->timer_);
                    self->Show(page);
                    if (page == 14)
                        self->Quick();
                }
                vTaskDelay(pdMS_TO_TICKS(700));
                std::string jpeg;
                if (self->d_->SnapshotToJpeg(jpeg, 85)) {
                    for (size_t offset = 0; offset < jpeg.size(); offset += 96) {
                        std::string line =
                            "UI_SHOT " + std::to_string(page) + " " + std::to_string(offset) + " ";
                        static const char* digits = "0123456789abcdef";
                        for (size_t i = offset; i < std::min(offset + 96, jpeg.size()); i++) {
                            auto c = (unsigned char)jpeg[i];
                            line += digits[c >> 4];
                            line += digits[c & 15];
                        }
                        printf("%s\n", line.c_str());
                        vTaskDelay(pdMS_TO_TICKS(8));
                    }
                }
            }
            {
                DisplayLockGuard lock(self->d_);
                self->Home();
                lv_timer_resume(self->timer_);
            }
            printf("UI_REVIEW_DONE\n");
            vTaskDelete(nullptr);
        },
        "ui_review", 8192, this, 1, nullptr);
}
#endif

void OriginalUi::Notify(const char* text, int duration_ms) {
    lv_label_set_text(lv_obj_get_child(toast_, 0), text);
    lv_obj_remove_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(toast_);
    toast_until_ = esp_timer_get_time() + int64_t(duration_ms) * 1000;
}
