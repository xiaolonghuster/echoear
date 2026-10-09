#include "echoear_extras.h"
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <nvs_flash.h>
#include <psa/crypto.h>
#include <cstdio>
#include <ctime>
#include <memory>
#include "application.h"
#include "assets/lang_config.h"
#include "board.h"
#include "echoear_display.h"
#include "echoear_target.h"
#include "original_ui.h"
#include "sense_hub.h"
#include "settings.h"
#include "storage_support.h"

namespace {
constexpr const char* kBoard = BOARD_TYPE;
constexpr const char* kVersion = "0.19.0";
std::string Hex(const unsigned char* bytes, int count) {
    const char* h = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < count; ++i) {
        s += h[bytes[i] >> 4];
        s += h[bytes[i] & 15];
    }
    return s;
}
std::string Download(const std::string& url, size_t maximum) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
    http->SetTimeout(12000);
    if (!http->Open("GET", url))
        return {};
    auto status = http->GetStatusCode();
    if (!status || *status != 200) {
        http->Close();
        return {};
    }
    std::string body;
    char buf[512];
    while (true) {
        auto n = http->Read(buf, sizeof(buf));
        if (!n || *n < 0) {
            body.clear();
            break;
        }
        if (!*n)
            break;
        if (body.size() + *n > maximum) {
            body.clear();
            break;
        }
        body.append(buf, *n);
    }
    http->Close();
    return body;
}
std::string JsonString(cJSON* root, const char* key) {
    auto v = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
}  // namespace
EchoEarExtras::EchoEarExtras(EchoEarDisplay* display) : d_(display) {
    Settings p("echoear_pet");
    pet_.food = p.GetInt("food", 70);
    pet_.energy = p.GetInt("energy", 80);
    pet_.mood = p.GetInt("mood", 70);
    pet_.xp = p.GetInt("xp", 0);
    pet_.Clamp();
    Settings u("echoear_tools");
    alarm_.minute = std::clamp<int>(u.GetInt("alarm", 480), 0, 1439);
    alarm_.enabled = u.GetBool("alarm_on", false);
    alarm_.last_day = u.GetInt("alarm_day", -1);
    background_ = std::clamp<int>(u.GetInt("background", 0), 0, 3);
    sudoku_seed_ = u.GetInt("sudoku_seed", 0);
    game_.New(unsigned(sudoku_seed_));
    game_.Load(u.GetString("sudoku"));
    tiles_.New(esp_random());
    tiles_.Load(u.GetString("tiles"));
    root_ = d_->Shape(d_->settings_panel_, 0, 0, 466, 466, 0x101D24, 0);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    last_pet_tick_ = esp_timer_get_time();
    timer_ = lv_timer_create(
        [](lv_timer_t* t) { static_cast<EchoEarExtras*>(lv_timer_get_user_data(t))->Tick(); }, 1000,
        this);
}
EchoEarExtras::~EchoEarExtras() {
    if (timer_)
        lv_timer_delete(timer_);
}
const char* EchoEarExtras::T(const char* zh, const char* en) const { return d_->Text(zh, en); }
uint32_t EchoEarExtras::Background(uint32_t fallback) const {
    static const uint32_t colors[] = {0, 0x172D3D, 0x322138, 0x2E291D};
    return background_ ? colors[background_] : fallback;
}
void EchoEarExtras::Label(const std::string& value, int y, int height) {
    text_ = lv_label_create(root_);
    lv_obj_set_pos(text_, 63, y);
    lv_obj_set_size(text_, 340, height);
    lv_obj_set_style_text_align(text_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(text_, lv_color_hex(0xE9F3E9), 0);
    lv_label_set_long_mode(text_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(text_, value.c_str());
}
void EchoEarExtras::Button(const char* value, int id, int x, int y, int width, int height,
                           bool small) {
    auto b = d_->Shape(root_, x, y, width, height, 0x20343C, 12);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
    auto l = lv_label_create(b);
    if (small)
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xE9F3E9), 0);
    lv_label_set_text(l, value);
    lv_obj_center(l);
    actions_.push_back({this, id});
    lv_obj_add_event_cb(
        b,
        [](lv_event_t* e) {
            auto a = *static_cast<Action*>(lv_event_get_user_data(e));
            a.self->ActionClick(a.id);
        },
        LV_EVENT_CLICKED, &actions_.back());
}
void EchoEarExtras::Open(int page) {
    DisplayLockGuard lock(d_);
    page_ = page;
    lv_obj_clean(root_);
    actions_.clear();
    text_ = nullptr;
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root_);
    if (page == 0) {
        Label(T("工具", "Tools"), 45, 55);
        Button(T("闹钟", "Alarm"), 1, 93, 108);
        Button(T("深圳天气", "Shenzhen weather"), 2, 93, 170);
        Button(T("数独", "Sudoku"), 3, 93, 232);
        Button(T("更多工具", "More tools"), 4, 93, 294);
    }
    if (page == 4) {
        Label(T("更多工具", "More tools"), 45, 55);
        Button(T("养成", "Pet"), 5, 83, 112, 140);
        Button(T("背景", "Backdrop"), 6, 243, 112, 140);
        Button(T("系统", "System"), 7, 83, 185, 140);
        Button(T("应用", "Apps"), 8, 243, 185, 140);
        Button(T("感知", "Senses"), 90, 83, 258, 140);
        Button(T("存储", "Storage"), 91, 243, 258, 140);
    }
    if (page == 8) {
        Label(T("本地应用", "Local apps"), 45, 55);
        Button("2048", 80, 83, 115, 140);
        Button(T("计算器", "Calculator"), 81, 243, 115, 140);
        Button(T("秒表", "Stopwatch"), 92, 83, 190, 140);
        Button(T("数独", "Sudoku"), 3, 243, 190, 140);
        Button(T("闹钟", "Alarm"), 1, 83, 265, 140);
        Button(T("天气", "Weather"), 2, 243, 265, 140);
    }
    if (page == 80) {
        char title[80];
        snprintf(title, sizeof(title), "2048 · %d%s", tiles_.score,
                 tiles_.Over() ? T(" 结束", " Over") : "");
        Label(title, 30, 42);
        for (int i = 0; i < 16; i++) {
            char b[16];
            snprintf(b, sizeof(b), "%d", tiles_.cells[i]);
            auto tile = d_->Shape(root_, 101 + i % 4 * 66, 84 + i / 4 * 62, 62, 58,
                                  tiles_.cells[i] ? 0x36585E : 0x20343C, 8);
            auto label = lv_label_create(tile);
            lv_obj_set_style_text_color(label, lv_color_white(), 0);
            if (tiles_.cells[i] >= 1024)
                lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
            lv_label_set_text(label, tiles_.cells[i] ? b : "");
            lv_obj_center(label);
        }
        const char* arrows[] = {"<", ">", "^", "v"};
        for (int i = 0; i < 4; i++)
            Button(arrows[i], 2100 + i, 108 + i * 64, 337, 58, 40, true);
        Button(T("返回", "Back"), 8, 107, 388, 80, 36, true);
        Button(T("撤销", "Undo"), 2104, 193, 388, 80, 36, true);
        Button(T("新局", "New"), 2105, 279, 388, 80, 36, true);
        d_->ApplyMenuPalette(root_);
        return;
    }
    if (page == 82) {
        Label(T("重新开始2048？", "Restart 2048?"), 100, 130);
        Button(T("确认", "Confirm"), 2106, 93, 270);
    }
    if (page == 81) {
        Label(calculator_.entry, 48, 58);
        const char* labels[] = {"C", "DEL", "%", "/", "7", "8", "9",   "*", "4", "5",
                                "6", "-",   "1", "2", "3", "+", "+/-", "0", ".", "="};
        for (int i = 0; i < 20; i++)
            Button(labels[i], 2000 + i, 103 + i % 4 * 66, 119 + i / 4 * 47, 60, 42, true);
    }
    if (page == 92) {
        Label(T("秒表", "Stopwatch"), 45, 55);
        auto elapsed =
            stopwatch_elapsed_ + (stopwatch_running_ ? esp_timer_get_time() - stopwatch_start_ : 0);
        char b[60];
        snprintf(b, sizeof(b), "%02lld:%02lld:%02lld", elapsed / 3600000000LL,
                 elapsed / 60000000 % 60, elapsed / 1000000 % 60);
        Label(b, 140, 80);
        Button(stopwatch_running_ ? T("暂停", "Pause") : T("开始", "Start"), 2200, 83, 265, 140);
        Button(T("清零", "Reset"), 2201, 243, 265, 140);
    }
    if (page == 90) {
        Label(T("感知互动", "Senses"), 32, 50);
        auto& h = SenseHub::Get();
        Button(h.motion_enabled ? T("体感：开", "Motion: On") : T("体感：关", "Motion: Off"), 2300,
               93, 93);
        Button(!EchoEarTarget::kSoundDirection ? T("声音跟随：不支持", "Sound gaze: unavailable")
               : h.sound_enabled               ? T("声音跟随：开", "Sound gaze: On")
                                               : T("声音跟随：关", "Sound gaze: Off"),
               2301, 93, 148);
        if (EchoEarTarget::kSoundDirection) {
            Button(T("反转方向", "Reverse"), 2302, 83, 207, 140);
            Button(T("校准姿态", "Calibrate"), 2303, 243, 207, 140);
        } else {
            Button(T("校准姿态", "Calibrate"), 2303, 93, 207);
        }
        Label("", 267, 89);
        RefreshSense();
    }
    if (page == 91) {
        Label(storage_status_.empty()
                  ? T("存储管理\n插卡后点击读取", "Storage\nInsert card, then read")
                  : storage_status_,
              65, 205);
        Button(T("读取SD卡", "Read SD"), 2400, 83, 284, 140);
        Button(T("备份资源", "Backup"), 2401, 243, 284, 140);
    }
    if (page == 1) {
        char b[120];
        auto now = time(nullptr);
        tm local{};
        localtime_r(&now, &local);
        snprintf(b, sizeof(b), "%s\n%02d:%02d  %s\n%s", T("每日闹钟", "Daily alarm"),
                 alarm_.minute / 60, alarm_.minute % 60,
                 alarm_.enabled ? T("开", "On") : T("关", "Off"),
                 now > 1700000000 ? T("中国标准时间", "China time")
                                  : T("等待联网校时", "Waiting for clock"));
        Label(b, 60, 170);
        Button("-1h", 10, 73, 238, 100);
        Button("+1h", 11, 183, 238, 100);
        Button("+1m", 12, 293, 238, 100);
        Button(alarm_.enabled ? T("关闭闹钟", "Disable") : T("开启闹钟", "Enable"), 13, 93, 300);
    }
    if (page == 2) {
        Label(weather_.empty() ? T("深圳天气\n点击刷新", "Shenzhen\nTap refresh") : weather_, 65,
              230);
        Button(busy_ ? T("正在读取", "Loading") : T("刷新天气", "Refresh"), 20, 93, 303);
        auto l = lv_label_create(root_);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(0xA7DCC4), 0);
        lv_label_set_text(l, "Weather: Open-Meteo.com");
        lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -18);
    }
    if (page == 5) {
        char b[180];
        snprintf(b, sizeof(b),
                 T("等级 %d · 亲密 %d\n饱食 %d  精力 %d\n心情 %d",
                   "Level %d · Bond %d\nFood %d  Energy %d\nMood %d"),
                 pet_.Level(), d_->affection_, pet_.food, pet_.energy, pet_.mood);
        Label(b, 90, 190);
        Button(T("休息一会儿", "Rest"), 50, 93, 285);
    }
    if (page == 6) {
        Label(T("选择背景", "Background"), 50, 60);
        const char* zh[] = {"跟随主题", "深海蓝", "星夜紫", "暖棕色"};
        const char* en[] = {"Follow theme", "Ocean blue", "Night purple", "Warm brown"};
        for (int i = 0; i < 4; ++i)
            Button(T(zh[i], en[i]), 60 + i, 93, 115 + i * 56);
    }
    if (page == 7) {
        Label("EchoEar v0.19.0", 45, 55);
        Button(T("检查更新", "Check update"), 70, 93, 110);
        Button(T("恢复上一版", "Previous version"), 71, 93, 174);
        Button(T("恢复出厂设置", "Factory reset"), 72, 93, 238);
        Button(T("重启设备", "Restart"), 73, 93, 302);
    }
    if (page == 70) {
        Label(update_version_.empty() ? T("尚未检查更新", "No update checked")
                                      : std::string(T("可用版本 ", "Version ")) + update_version_,
              70, 150);
        if (!update_url_.empty())
            Button(T("确认安装", "Install"), 75, 93, 270);
    }
    if (page == 71) {
        Label(T("恢复上一版固件？\n将重新启动", "Restore previous\nfirmware and restart?"), 95,
              170);
        Button(T("确认恢复", "Restore"), 76, 93, 290);
    }
    if (page == 72) {
        Label(
            T("清除配网和设置？\n宠物进度也会清除", "Erase Wi-Fi/settings?\nPet progress is lost."),
            95, 170);
        Button(T("确认清除", "Erase & restart"), 77, 93, 290);
    }
    if (page == 73) {
        Label(T("重新启动设备？", "Restart device?"), 110, 100);
        Button(T("确认重启", "Restart"), 78, 93, 270);
    }
    if (page == 3) {
        Button(T("返回", "Back"), 0, 123, 22, 100, 38);
        Button(T("撤销", "Undo"), 31, 243, 22, 100, 38);
        for (int i = 0; i < 81; ++i) {
            char b[2] = {game_.cells[i] ? char('0' + game_.cells[i]) : ' ', 0};
            int x = 80 + (i % 9) * 34, y = 67 + (i / 9) * 34;
            Button(b, 1000 + i, x, y, 32, 32, true);
            auto obj = lv_obj_get_child(root_, -1);
            lv_obj_set_style_bg_color(
                obj,
                lv_color_hex(game_.Conflict(i) ? 0x7D3333
                             : game_.givens[i]
                                 ? 0x324E57
                                 : ((i / 27 + (i % 9) / 3) % 2 ? 0x263D42 : 0x1A2C34)),
                0);
        }
        Button(T("提示", "Hint"), 32, 103, 383, 120, 40);
        Button(game_.Solved() ? T("完成！新局", "Won! New") : T("新一局", "New game"), 33, 243, 383,
               120, 40);
        d_->ApplyMenuPalette(root_);
        return;
    }
    if (page == 30) {
        Label(T("选择数字", "Choose a digit"), 42, 55);
        for (int i = 1; i <= 9; ++i) {
            char b[2] = {char('0' + i), 0};
            Button(b, 1100 + i, 98 + ((i - 1) % 3) * 94, 108 + ((i - 1) / 3) * 62, 82, 52);
        }
        Button(T("擦除", "Erase"), 1100, 98, 305, 130);
        Button(T("返回", "Back"), 3, 248, 305, 120);
        d_->ApplyMenuPalette(root_);
        return;
    }
    if (page == 33) {
        Label(T("开始新的数独？\n当前进度会清除", "Start a new puzzle?\nCurrent game is lost."),
              100, 170);
        Button(T("开始新局", "New game"), 34, 93, 290);
    }
    Button(T("返回", "Back"), page == 0 ? -1 : ((page == 81 || page == 92 || page == 82) ? 8 : 0),
           163, 363, 140);
    d_->ApplyMenuPalette(root_);
}
void EchoEarExtras::ActionClick(int id) {
    if ((id == -1 || id == 0 || id == 8) && d_->original_ui_) {
        d_->original_ui_->Launcher();
        return;
    }
    if (id >= 2000 && id < 2020) {
        static const char keys[] = "CB%/789*456-123+N0.=";
        calculator_.Key(keys[id - 2000]);
        Open(81);
        return;
    }
    if (id >= 2100 && id <= 2103) {
        if (tiles_.Slide(id - 2100)) {
            tiles_.Spawn(esp_random());
            SaveTiles();
        }
        Open(80);
        return;
    }
    if (id == 2104) {
        tiles_.Undo();
        SaveTiles();
        Open(80);
        return;
    }
    if (id == 2105) {
        Open(82);
        return;
    }
    if (id == 2106) {
        tiles_.New(esp_random());
        SaveTiles();
        Open(80);
        return;
    }
    if (id == 2200) {
        auto now = esp_timer_get_time();
        if (stopwatch_running_)
            stopwatch_elapsed_ += now - stopwatch_start_;
        else
            stopwatch_start_ = now;
        stopwatch_running_ = !stopwatch_running_;
        Open(92);
        return;
    }
    if (id == 2201) {
        stopwatch_elapsed_ = 0;
        stopwatch_start_ = esp_timer_get_time();
        Open(92);
        return;
    }
    if ((id == 2301 || id == 2302) && !EchoEarTarget::kSoundDirection) {
        d_->ShowNotification(
            T("单麦设备不支持方向跟随", "Single microphone: no direction tracking"));
        return;
    }
    if (id >= 2300 && id <= 2303) {
        auto& h = SenseHub::Get();
        if (id == 2300)
            h.motion_enabled = !h.motion_enabled;
        if (id == 2301)
            h.sound_enabled = !h.sound_enabled;
        if (id == 2302)
            h.reverse = !h.reverse;
        if (id == 2303 && h.imu_ready) {
            h.zero_x = h.ax.load();
            h.zero_y = h.ay.load();
        }
        Application::GetInstance().Schedule([] {
            auto& h = SenseHub::Get();
            Settings p("echoear_sense", true);
            p.SetBool("motion", h.motion_enabled);
            p.SetBool("sound", h.sound_enabled);
            p.SetBool("reverse", h.reverse);
            p.SetInt("zero_x", h.zero_x);
            p.SetInt("zero_y", h.zero_y);
        });
        Open(90);
        return;
    }
    if (id == 2400 || id == 2401) {
        Storage(id == 2401);
        return;
    }

    if (busy_.load() && id >= 70 && id <= 78)
        return;
    if (id == -1) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (id >= 1000 && id < 1081) {
        selected_ = id - 1000;
        if (!game_.givens[selected_])
            Open(30);
        return;
    }
    if (id >= 1100 && id <= 1109) {
        game_.Put(selected_, id - 1100);
        SaveGame();
        Open(3);
        return;
    }
    switch (id) {
        case 10:
            alarm_.minute = (alarm_.minute + 1380) % 1440;
            SaveAlarm();
            Open(1);
            return;
        case 11:
            alarm_.minute = (alarm_.minute + 60) % 1440;
            SaveAlarm();
            Open(1);
            return;
        case 12:
            alarm_.minute = (alarm_.minute + 1) % 1440;
            SaveAlarm();
            Open(1);
            return;
        case 13:
            alarm_.enabled = !alarm_.enabled;
            SaveAlarm();
            Open(1);
            return;
        case 20:
            Weather();
            return;
        case 31:
            game_.Undo();
            SaveGame();
            Open(3);
            return;
        case 32:
            for (int i = 0; i < 81; ++i)
                if (game_.cells[i] != game_.solution[i]) {
                    game_.Put(i, game_.solution[i]);
                    break;
                }
            SaveGame();
            Open(3);
            return;
        case 34:
            sudoku_seed_ = int(esp_random() % 18);
            game_.New(unsigned(sudoku_seed_));
            SaveGame();
            Open(3);
            return;
        case 50:
            d_->CloseSettings();
            if (d_->timer_sleep_callback_)
                d_->timer_sleep_callback_();
            return;
        case 60:
        case 61:
        case 62:
        case 63:
            background_ = id - 60;
            Application::GetInstance().Schedule([value = background_] {
                Settings p("echoear_tools", true);
                p.SetInt("background", value);
            });
            d_->ApplyPalette();
            Open(6);
            return;
        case 70:
            CheckUpdate();
            return;
        case 75:
            InstallUpdate();
            return;
        case 76:
            Rollback();
            return;
        case 77:
            Reset();
            return;
        case 78:
            Application::GetInstance().Schedule([] { Application::GetInstance().Reboot(); });
            return;
        default:
            Open(id);
            return;
    }
}
void EchoEarExtras::SavePet() {
    auto v = pet_;
    Application::GetInstance().Schedule([v] {
        Settings p("echoear_pet", true);
        p.SetInt("food", v.food);
        p.SetInt("energy", v.energy);
        p.SetInt("mood", v.mood);
        p.SetInt("xp", v.xp);
    });
}
void EchoEarExtras::SaveAlarm() {
    auto a = alarm_;
    Application::GetInstance().Schedule([a] {
        Settings p("echoear_tools", true);
        p.SetInt("alarm", a.minute);
        p.SetBool("alarm_on", a.enabled);
        p.SetInt("alarm_day", int(a.last_day));
    });
}
void EchoEarExtras::SaveGame() {
    auto value = game_.Save();
    auto seed = sudoku_seed_;
    Application::GetInstance().Schedule([value, seed] {
        Settings p("echoear_tools", true);
        p.SetInt("sudoku_seed", seed);
        p.SetString("sudoku", value);
    });
}
void EchoEarExtras::Pet(int action) {
    pet_.Act(action);
    SavePet();
    Application::GetInstance().Schedule([] {
        if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle)
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_POPUP);
    });
}
void EchoEarExtras::SaveTiles() {
    auto saved = tiles_.Save();
    Application::GetInstance().Schedule([saved] {
        Settings p("echoear_tools", true);
        p.SetString("tiles", saved);
    });
}
void EchoEarExtras::RefreshSense() {
    if (!text_)
        return;
    auto& h = SenseHub::Get();
    char b[180];
    snprintf(b, sizeof(b),
             T("体感 %s · 方向 %d\n可信度 %d%% · %s", "IMU %s · Axis %d\nConfidence %d%% · %s"),
             h.imu_ready ? "OK" : "--", h.sound.direction.load() * (h.ReverseSoundAxis() ? -1 : 1),
             h.sound.confidence.load(), h.reverse ? T("反向", "Reversed") : T("正向", "Normal"));
    lv_label_set_text(text_, b);
}
void EchoEarExtras::Storage(bool backup) {
    if (busy_)
        return;
    storage_status_ = T("正在读取…", "Reading…");
    Open(91);
    Job([this, backup] {
        auto status = BoardStorage::Get().Inspect(backup);
        Application::GetInstance().Schedule([this, status] {
            DisplayLockGuard lock(d_);
            busy_ = false;
            storage_status_ = status;
            if (page_ == 91)
                Open(91);
        });
    });
}
void EchoEarExtras::Tick() {
    auto now = esp_timer_get_time();
    if (!lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN) &&
        !lv_obj_has_flag(d_->settings_panel_, LV_OBJ_FLAG_HIDDEN)) {
        if (page_ == 90)
            RefreshSense();
        if (page_ == 92 && text_) {
            auto elapsed = stopwatch_elapsed_ + (stopwatch_running_ ? now - stopwatch_start_ : 0);
            char b[60];
            snprintf(b, sizeof(b), "%02lld:%02lld:%02lld", elapsed / 3600000000LL,
                     elapsed / 60000000 % 60, elapsed / 1000000 % 60);
            lv_label_set_text(text_, b);
        }
    }
    if (now - last_pet_tick_ >= 300000000) {
        pet_.Step(d_->sleeping_);
        last_pet_tick_ = now;
        SavePet();
    }
    // Xiaozhi's time synchronizer already stores timezone-adjusted local time.
    if (alarm_.Due(time(nullptr))) {
        SaveAlarm();
        Reminder("闹钟时间到了", "Alarm time");
    }
}
void EchoEarExtras::Reminder(const char* zh, const char* en) {
    std::string message = T(zh, en);
    Application::GetInstance().Schedule([this, message] {
        if (d_->timer_wake_callback_)
            d_->timer_wake_callback_();
        {
            DisplayLockGuard lock(d_);
            d_->OpenSettings();
            Open(99);
            Label(message, 100, 170);
        }
        if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle)
            Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS);
    });
}
void EchoEarExtras::Message(const std::string& message) {
    DisplayLockGuard lock(d_);
    Open(99);
    Label(message, 100, 220);
}
void EchoEarExtras::Job(std::function<void()> fn, bool reserved) {
    if (!reserved && busy_.exchange(true))
        return;
    auto job = new std::function<void()>(std::move(fn));
    auto ok = xTaskCreate(
        [](void* p) {
            std::unique_ptr<std::function<void()>> work(static_cast<std::function<void()>*>(p));
            (*work)();
            work.reset();
            vTaskDelete(nullptr);
        },
        "echoear_job", 8192, job, 3, nullptr);
    if (ok != pdPASS) {
        delete job;
        busy_ = false;
        Application::GetInstance().Schedule([] {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateUpgrading) {
                app.GetAudioService().Start();
                app.SetDeviceState(kDeviceStateIdle);
            }
        });
        Message(T("任务启动失败", "Could not start task"));
    }
}
void EchoEarExtras::Weather() {
    if (busy_)
        return;
    weather_ = T("深圳\n正在读取天气", "Shenzhen\nLoading weather");
    Open(2);
    Job([this] {
        auto body = Download(
            "https://api.open-meteo.com/v1/"
            "forecast?latitude=22.5431&longitude=114.0579&current=temperature_2m,weather_code&"
            "timezone=Asia%2FShanghai",
            8192);
        auto json = cJSON_Parse(body.c_str());
        auto current = cJSON_GetObjectItemCaseSensitive(json, "current");
        auto temp = cJSON_GetObjectItemCaseSensitive(current, "temperature_2m");
        auto code = cJSON_GetObjectItemCaseSensitive(current, "weather_code");
        bool valid = cJSON_IsNumber(temp) && cJSON_IsNumber(code);
        double value = valid ? temp->valuedouble : 0;
        int weather = valid ? code->valueint : -1;
        std::string stamp = JsonString(current, "time");
        cJSON_Delete(json);
        Application::GetInstance().Schedule([this, valid, value, weather, stamp] {
            DisplayLockGuard lock(d_);
            busy_ = false;
            if (valid) {
                const char* desc = weather == 0    ? T("晴", "Clear")
                                   : weather <= 3  ? T("多云", "Cloudy")
                                   : weather <= 48 ? T("雾", "Fog")
                                   : weather <= 67 ? T("雨", "Rain")
                                   : weather <= 77 ? T("雪", "Snow")
                                   : weather <= 82 ? T("阵雨", "Showers")
                                   : weather <= 86 ? T("阵雪", "Snow showers")
                                                   : T("雷雨", "Thunderstorm");
                char b[180];
                snprintf(b, sizeof(b), "%s %.1f C\n%s\n%s", T("深圳", "Shenzhen"), value, desc,
                         stamp.size() >= 16 ? stamp.substr(11, 5).c_str() : "");
                weather_ = b;
            } else
                weather_ = T("天气获取失败\n请检查网络后重试",
                             "Weather unavailable\nCheck Wi-Fi and retry");
            if (page_ == 2)
                Open(2);
        });
    });
}
void EchoEarExtras::SetUpdateFeed(const std::string& url) {
    Settings s("echoear_update", true);
    s.SetString("feed", url);
}
void EchoEarExtras::CheckUpdate() {
    if (busy_)
        return;
    Settings s("echoear_update");
    auto feed = s.GetString("feed", "https://vocat.xiaolong.xyz/firmware/stable.json");
    if (feed.rfind("https://", 0) != 0) {
        Message(T("尚未配置更新源\n可继续使用本地烧录", "No update source\nUse local flashing"));
        return;
    }
    Message(T("正在检查更新", "Checking update"));
    Job([this, feed] {
        auto body = Download(feed, 4096);
        auto root = cJSON_Parse(body.c_str());
        auto url = JsonString(root, "url"), sha = JsonString(root, "sha256"),
             version = JsonString(root, "version");
        auto size = cJSON_GetObjectItemCaseSensitive(root, "size");
        int bytes = cJSON_IsNumber(size) ? size->valueint : 0;
        auto partition = esp_ota_get_next_update_partition(nullptr);
        bool valid = JsonString(root, "board") == kBoard && url.rfind("https://", 0) == 0 &&
                     sha.size() == 64 &&
                     sha.find_first_not_of("0123456789abcdef") == std::string::npos && partition &&
                     bytes > 0 && bytes <= int(partition->size) && !version.empty();
        cJSON_Delete(root);
        Application::GetInstance().Schedule([this, url, sha, version, bytes, valid] {
            DisplayLockGuard lock(d_);
            busy_ = false;
            if (!valid) {
                Message(T("更新信息无效", "Invalid update manifest"));
                return;
            }
            if (version == kVersion) {
                Message(T("已是当前版本", "Already up to date"));
                return;
            }
            update_url_ = url;
            update_sha_ = sha;
            update_version_ = version;
            update_size_ = bytes;
            Open(70);
        });
    });
}
void EchoEarExtras::InstallUpdate() {
    if (busy_ || update_url_.empty())
        return;
    if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
        Message(T("请先结束对话", "Finish chat first"));
        return;
    }
    if (busy_.exchange(true))
        return;
    auto url = update_url_, hash = update_sha_;
    int bytes = update_size_;
    Message(T("正在安装\n请勿断电", "Installing\nKeep power connected"));
    Application::GetInstance().Schedule([this, url, hash, bytes] {
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() != kDeviceStateIdle) {
            busy_ = false;
            Message(T("请先结束对话", "Finish chat first"));
            return;
        }
        app.SetDeviceState(kDeviceStateUpgrading);
        app.GetAudioService().Stop();
        if (d_->timer_wake_callback_)
            d_->timer_wake_callback_();
        Job(
            [this, url, hash, bytes] {
                const auto target = esp_ota_get_next_update_partition(nullptr);
                esp_ota_handle_t handle = 0;
                bool begun = false, ok = false;
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
                http->SetTimeout(15000);
                psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;

                bool hash_ok = psa_crypto_init() == PSA_SUCCESS &&
                               psa_hash_setup(&sha, PSA_ALG_SHA_256) == PSA_SUCCESS;
                size_t total = 0;
                if (hash_ok && target && http->Open("GET", url)) {
                    auto status = http->GetStatusCode();
                    if (status && *status == 200 &&
                        esp_ota_begin(target, bytes, &handle) == ESP_OK) {
                        begun = true;
                        char buffer[2048];
                        int last = -1;
                        const auto started = esp_timer_get_time();
                        while (total < size_t(bytes) &&
                               esp_timer_get_time() - started < 180000000) {
                            auto n =
                                http->Read(buffer, std::min(sizeof(buffer), size_t(bytes) - total));
                            if (!n || *n <= 0)
                                break;
                            if (esp_ota_write(handle, buffer, *n) != ESP_OK)
                                break;
                            if (psa_hash_update(&sha, reinterpret_cast<unsigned char*>(buffer),
                                                *n) != PSA_SUCCESS)
                                break;
                            total += *n;
                            int progress = int(total * 100 / bytes);
                            if (progress / 10 != last) {
                                last = progress / 10;
                                Application::GetInstance().Schedule([this, progress] {
                                    char b[100];
                                    snprintf(b, sizeof(b),
                                             T("正在安装 %d%%\n请勿断电",
                                               "Installing %d%%\nKeep power connected"),
                                             progress);
                                    Message(b);
                                });
                            }
                        }
                        unsigned char sum[32] = {};
                        size_t hash_length = 0;
                        auto hash_status = psa_hash_finish(&sha, sum, sizeof(sum), &hash_length);
                        ok = hash_status == PSA_SUCCESS && hash_length == 32 &&
                             total == size_t(bytes) && Hex(sum, 32) == hash;
                    }
                }
                http->Close();
                psa_hash_abort(&sha);
                if (begun) {
                    if (ok)
                        ok = esp_ota_end(handle) == ESP_OK;
                    else
                        esp_ota_abort(handle);
                }
                if (ok) {
                    auto previous = esp_ota_get_running_partition();
                    unsigned char old_sha[32];
                    ok = esp_partition_get_sha256(previous, old_sha) == ESP_OK;
                    if (ok) {
                        Settings p("echoear_update", true);
                        p.SetString("previous", previous->label);
                        p.SetString("previous_sha", Hex(old_sha, 32));
                    }
                    if (ok)
                        ok = esp_ota_set_boot_partition(target) == ESP_OK;
                }
                Application::GetInstance().Schedule([this, ok] {
                    busy_ = false;
                    if (ok) {
                        Application::GetInstance().Reboot();
                        return;
                    }
                    Application::GetInstance().GetAudioService().Start();
                    Application::GetInstance().SetDeviceState(kDeviceStateIdle);
                    Message(T("升级失败\n仍运行原版本", "Update failed\nOriginal version kept"));
                });
            },
            true);
    });
}

void EchoEarExtras::Rollback() {
    Application::GetInstance().Schedule([this] {
        Settings p("echoear_update");
        auto label = p.GetString("previous"), expected = p.GetString("previous_sha");
        auto old = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY,
                                            label.c_str());
        unsigned char hash[32];
        esp_app_desc_t desc{};
        if (label.empty() || !old || old == esp_ota_get_running_partition() ||
            esp_partition_get_sha256(old, hash) != ESP_OK || Hex(hash, 32) != expected ||
            esp_ota_get_partition_description(old, &desc) != ESP_OK) {
            Message(T("没有可验证的上一版", "No verified previous version"));
            return;
        }
        if (esp_ota_set_boot_partition(old) == ESP_OK)
            Application::GetInstance().Reboot();
        else
            Message(T("恢复失败", "Restore failed"));
    });
}
void EchoEarExtras::Reset() {
    Application::GetInstance().Schedule([this] {
        Application::GetInstance().GetAudioService().Stop();
        if (nvs_flash_erase() == ESP_OK)
            esp_restart();
        else {
            Application::GetInstance().GetAudioService().Start();
            Message(T("清除失败", "Reset failed"));
        }
    });
}
