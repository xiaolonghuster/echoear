#include "echoear_display.h"
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <algorithm>
#include <cstring>
#include "application.h"
#include "assets.h"
#include "assets/lang_config.h"
#include "audio/audio_codec.h"
#include "backlight.h"
#include "board.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "echoear_extras.h"
#include "echoear_target.h"
#include "original_ui.h"
#include "sense_hub.h"
#include "settings.h"
#include "ui_translations.h"

namespace {
constexpr uint32_t kBackground = 0x101D24;
constexpr uint32_t kCard = 0x20343C;
constexpr uint32_t kText = 0xE9F3E9;
constexpr uint32_t kMint = 0xA7DCC4;
constexpr uint32_t kInk = 0x163731;
constexpr int kOriginalEyesTop = 103;  // 20px above the previous eye position.
}  // namespace

lv_obj_t* EchoEarDisplay::Shape(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color,
                                int radius) {
    auto obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

EchoEarDisplay::~EchoEarDisplay() {
    DisplayLockGuard lock(this);
    ClearOriginalEyes();
    delete original_ui_;
    delete extras_;
    if (timer_)
        lv_timer_delete(timer_);
    if (countdown_tick_)
        lv_timer_delete(countdown_tick_);
    if (settings_panel_)
        lv_anim_delete(settings_panel_, nullptr);
}

void EchoEarDisplay::SetupUI() {
    if (cat_)
        return;
    SpiLcdDisplay::SetupUI();
    DisplayLockGuard lock(this);
    // Put the microphone baseline on the UI horizontal axis. LVGL also rotates
    // raw pointer coordinates, so the touch driver must not rotate them twice.
    lv_display_set_rotation(display_, EchoEarTarget::kRotation);
    auto screen = lv_display_get_screen_active(display_);
    // Only the subtitle viewport should scroll. Root scrolling can expose a
    // scrollbar at the round panel edge when the drawer moves off screen.
    for (auto fixed : {screen, container_, emoji_box_, top_bar_, status_bar_}) {
        lv_obj_set_scrollbar_mode(fixed, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(fixed, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_scroll_to(fixed, 0, 0, LV_ANIM_OFF);
    }
    lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    Settings preferences("echoear_ui");
    affection_ = std::clamp<int>(preferences.GetInt("affection", 0), 0, 100);
    english_ = preferences.GetBool("english", false);
    // New devices and factory resets start with the original double-eye theme.
    // An explicit appearance choice still survives reboot and firmware updates.
    const int saved_appearance = preferences.GetInt("appearance", 2);
    const int appearance = saved_appearance >= 0 && saved_appearance <= 2 ? saved_appearance : 2;
    use_theme_expression_ = appearance == 1;
    use_original_eyes_ = appearance == 2;
    ESP_LOGI("EchoEarUI", "Restored appearance: %s",
             use_original_eyes_ ? "original eyes" : (use_theme_expression_ ? "theme" : "cat"));
    lv_obj_set_size(emoji_box_, 340, 183);
    lv_obj_align(emoji_box_, LV_ALIGN_TOP_MID, 0, use_original_eyes_ ? kOriginalEyesTop : 123);
    lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(emoji_label_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        emoji_box_,
        [](lv_event_t*) {
            Application::GetInstance().Schedule(
                []() { Application::GetInstance().ToggleChatState(); });
        },
        LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    // Keep status icons and labels inside the circular aperture.
    lv_obj_set_width(top_bar_, 230);
    lv_obj_align(top_bar_, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_size(status_bar_, 330, 56);
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 68);
    lv_obj_set_width(status_label_, 320);
    lv_obj_set_width(notification_label_, 320);

    cat_ = Shape(screen, 83, 123, 300, 183, kBackground, 0);
    lv_obj_add_flag(cat_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(cat_, 12);
    // Diamond ears, their lower halves hidden by the rounded face.
    int ear_index = 0;
    for (int x : {42, 196}) {
        auto ear = ears_[ear_index++] = Shape(cat_, x, 9, 62, 62, kMint, 10);
        lv_obj_set_style_transform_pivot_x(ear, 31, 0);
        lv_obj_set_style_transform_pivot_y(ear, 31, 0);
        lv_obj_set_style_transform_rotation(ear, 450, 0);
        auto inner = Shape(cat_, x + 16, 22, 28, 28, 0xEAA99B, 6);
        lv_obj_set_style_transform_pivot_x(inner, 14, 0);
        lv_obj_set_style_transform_pivot_y(inner, 14, 0);
        lv_obj_set_style_transform_rotation(inner, 450, 0);
    }
    Shape(cat_, 25, 43, 250, 131, kMint, 62);
    eyes_[0] = Shape(cat_, 83, 81, 18, 36, kInk, 9);
    eyes_[1] = Shape(cat_, 199, 81, 18, 36, kInk, 9);
    cheeks_[0] = Shape(cat_, 60, 119, 26, 10, 0xEAA99B, 5);
    cheeks_[1] = Shape(cat_, 217, 119, 26, 10, 0xEAA99B, 5);
    for (int i = 0; i < 2; ++i) {
        brows_[i] = Shape(cat_, 77 + i * 116, 69, 30, 5, kInk, 3);
        pupils_[i] = Shape(eyes_[i], 4, 4, 6, 8, 0xE9FFF6, 3);
        tears_[i] = Shape(cat_, 87 + i * 116, 118, 9, 16, 0x75CDEE, 5);
        lv_obj_add_flag(tears_[i], LV_OBJ_FLAG_HIDDEN);
        // Short whiskers stay within the cheeks and circular display aperture.
        for (int j = 0; j < 2; ++j) {
            auto whisker = Shape(cat_, i ? 252 : 29, 108 + j * 11, 19, 3, kInk, 2);
            lv_obj_set_style_transform_rotation(whisker, (i ? 1 : -1) * (j ? 80 : -80), 0);
        }
    }
    Shape(cat_, 144, 113, 12, 8, kInk, 4);
    mouth_ = Shape(cat_, 141, 130, 18, 7, kInk, 4);
    lv_obj_add_event_cb(
        cat_,
        [](lv_event_t*) {
            Application::GetInstance().Schedule(
                []() { Application::GetInstance().ToggleChatState(); });
        },
        LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(
        cat_,
        [](lv_event_t* e) {
            static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->original_ui_->Launcher();
        },
        LV_EVENT_LONG_PRESSED, this);
    for (int i = 0; i < 5; ++i) {
        bars_[i] = Shape(screen, 201 + i * 14, 310, 8, 5, kMint, 4);
    }
    hint_ = lv_label_create(screen);
    lv_obj_set_width(hint_, 330);
    lv_obj_set_style_text_align(hint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint_, LV_ALIGN_TOP_MID, 0, 346);
    lv_label_set_text(hint_, "");
    // Portrait text was clipped at the bottom of the original rectangular layout.
    lv_obj_set_size(bottom_bar_, 300, 84);
    lv_obj_set_style_radius(bottom_bar_, 20, 0);
    lv_obj_set_style_pad_all(bottom_bar_, 10, 0);
    lv_obj_align(bottom_bar_, LV_ALIGN_TOP_MID, 0, 322);
    lv_obj_set_scroll_dir(bottom_bar_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_width(chat_message_label_, 274);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(chat_message_label_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_move_foreground(bottom_bar_);
    LayoutSubtitles();
    lv_obj_add_event_cb(
        bottom_bar_,
        [](lv_event_t* e) {
            auto self = static_cast<EchoEarDisplay*>(lv_event_get_user_data(e));
            self->subtitle_manual_until_ = esp_timer_get_time() + 5000000;
        },
        LV_EVENT_PRESSED, this);
    // Only swipes starting in the top status area open settings. This avoids
    // stealing vertical scrolling from conversation subtitles or cat taps.
    auto pull_area = Shape(screen, 0, 0, 466, 112, kBackground, 0);
    lv_obj_set_style_bg_opa(pull_area, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(pull_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(pull_area, LV_OBJ_FLAG_GESTURE_BUBBLE);
    pull_handle_ = Shape(pull_area, 211, 18, 44, 5, 0x65867B, 3);
    lv_obj_add_event_cb(
        pull_area,
        [](lv_event_t* e) {
            auto indev = lv_indev_active();
            if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) {
                static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->OpenSettings();
                lv_indev_wait_release(indev);
            }
        },
        LV_EVENT_GESTURE, this);
    CreateSettings(screen);
    CreateCountdown();
    CreatePetPanel();
    extras_ = new EchoEarExtras(this);
    original_ui_ = new OriginalUi(this);
    extras_->Attach(original_ui_->AppHost());
#if CONFIG_ECHOEAR_UI_REVIEW
    original_ui_->Review();
#endif
    // CO5300 requires even DMA rectangles.
    lv_display_add_event_cb(
        display_,
        [](lv_event_t* e) {
            auto area = static_cast<lv_area_t*>(lv_event_get_param(e));
            area->x1 &= ~1;
            area->y1 &= ~1;
            area->x2 |= 1;
            area->y2 |= 1;
        },
        LV_EVENT_INVALIDATE_AREA, nullptr);
    ApplyPalette();
    RefreshAppearance();
    timer_ = lv_timer_create(
        [](lv_timer_t* t) { static_cast<EchoEarDisplay*>(lv_timer_get_user_data(t))->Animate(); },
        120, this);
}

// Restrict recoloring to menu widgets; cat artwork and semantic warning colors stay intact.
void EchoEarDisplay::ApplyMenuPalette(lv_obj_t* root) {
    if (!root)
        return;
    const uint32_t colors[][2] = {
        {kBackground, 0x000000}, {kCard, 0x24282D},    {kText, 0xFFFFFF},    {kMint, 0xD6EFF2},
        {0x65867B, 0x858B93},    {0x36584F, 0x414952}, {0x36585E, 0x414B58}, {0x324E57, 0x3B4550},
        {0x263D42, 0x30343A},    {0x1A2C34, 0x1A1D22}};
    auto map = [&](lv_color_t color) {
        const uint32_t rgb = lv_color_to_u32(color) & 0xFFFFFF;
        for (const auto& pair : colors)
            if (rgb == pair[0])
                return lv_color_hex(pair[1]);
        return color;
    };
    lv_obj_set_style_bg_color(root, map(lv_obj_get_style_bg_color(root, LV_PART_MAIN)), 0);
    lv_obj_set_style_text_color(root, map(lv_obj_get_style_text_color(root, LV_PART_MAIN)), 0);
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_CLICKABLE) &&
        lv_obj_get_style_height(root, LV_PART_MAIN) <= 90)
        lv_obj_set_style_bg_color(root, lv_color_hex(0x414952), LV_STATE_PRESSED);
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        ApplyMenuPalette(lv_obj_get_child(root, i));
}

void EchoEarDisplay::ApplyPalette() {
    if (!cat_)
        return;
    LayoutSubtitles();
    ApplyMenuPalette(settings_panel_);
    ApplyMenuPalette(pull_handle_);
    auto screen = lv_display_get_screen_active(display_);
    auto theme = static_cast<LvglTheme*>(current_theme_);
    auto background = use_theme_expression_ ? theme->background_color() : lv_color_hex(kBackground);
    if (extras_)
        background = lv_color_hex(extras_->Background(lv_color_to_u32(background) & 0xffffff));
    if (use_original_eyes_)
        background = lv_color_black();
    lv_obj_set_style_bg_color(cat_, background, 0);
    lv_obj_set_style_bg_color(screen, background, 0);
    lv_obj_set_style_bg_color(container_, background, 0);
    auto image = use_theme_expression_ ? theme->background_image() : nullptr;
    if (extras_ && extras_->Background(0))
        image = nullptr;
    lv_obj_set_style_bg_image_src(container_, image ? image->image_dsc() : nullptr, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(bottom_bar_,
                              use_original_eyes_ ? lv_color_black() : lv_color_hex(kCard), 0);
    lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_COVER, 0);
    for (auto obj : {status_label_, notification_label_, network_label_, battery_label_,
                     mute_label_, chat_message_label_, hint_}) {
        if (obj)
            lv_obj_set_style_text_color(
                obj, use_original_eyes_ ? lv_color_white() : lv_color_hex(kText), 0);
    }
    auto foreground = use_original_eyes_
                          ? lv_color_white()
                          : (use_theme_expression_ ? theme->text_color() : lv_color_hex(kText));
    for (auto obj :
         {status_label_, notification_label_, network_label_, battery_label_, mute_label_})
        lv_obj_set_style_text_color(obj, foreground, 0);
    lv_obj_set_style_text_color(hint_,
                                use_original_eyes_
                                    ? lv_color_hex(0xAAAAAA)
                                    : (use_theme_expression_ ? foreground : lv_color_hex(0x8AA79E)),
                                0);
}

void EchoEarDisplay::SetTheme(Theme* theme) {
    SpiLcdDisplay::SetTheme(theme);
    DisplayLockGuard lock(this);
    ApplyPalette();
    RefreshAppearance();
}

void EchoEarDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    pet_until_ = 0;
    pet_action_ = -1;
    emotion_ = emotion && *emotion ? emotion : "neutral";
    cat_mood_ = CatMood::Neutral;
    auto is = [this](const char* name) { return emotion_ == name; };
    if (is("happy") || is("laughing") || is("funny") || is("confident"))
        cat_mood_ = CatMood::Happy;
    else if (is("sad") || is("crying"))
        cat_mood_ = CatMood::Sad;
    else if (is("angry"))
        cat_mood_ = CatMood::Angry;
    else if (is("surprised") || is("shocked") || is("scared"))
        cat_mood_ = CatMood::Surprised;
    else if (is("sleepy") || is("relaxed"))
        cat_mood_ = CatMood::Sleepy;
    else if (is("winking") || is("cool"))
        cat_mood_ = CatMood::Wink;
    else if (is("loving") || is("kissy") || is("delicious"))
        cat_mood_ = CatMood::Love;
    else if (is("confused") || is("thinking") || is("embarrassed"))
        cat_mood_ = CatMood::Confused;
    RefreshAppearance();
}

void EchoEarDisplay::SetChatMessage(const char* role, const char* content) {
    // Cloud weather tool progress arrives as an assistant subtitle, e.g.
    // "% get_weather...". Suppress only that exact marker, not weather answers.
    if (role && strcmp(role, "assistant") == 0 && content) {
        std::string marker(content);
        const auto first = marker.find_first_not_of(" \t\r\n");
        const auto last = marker.find_last_not_of(" \t\r\n");
        if (first != std::string::npos)
            marker = marker.substr(first, last - first + 1);
        if (marker == "% get_weather..." || marker == "% get_weather…") {
            ESP_LOGI("EchoEarUI", "Hidden weather tool progress subtitle");
            return;
        }
    }
    auto translated = role && strcmp(role, "system") == 0 ? TranslateUi(content)
                                                          : std::string(content ? content : "");
    SpiLcdDisplay::SetChatMessage(role, translated.c_str());
    DisplayLockGuard lock(this);
    if (!cat_)
        return;
    bool has_text = content && *content;
    if (has_text && !hide_subtitle_)
        lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(hint_, LV_OBJ_FLAG_HIDDEN);
    auto state = Application::GetInstance().GetDeviceState();
    bool important = state == kDeviceStateActivating || state == kDeviceStateWifiConfiguring;
    LayoutSubtitles(important);
    lv_obj_scroll_to_y(bottom_bar_, 0, LV_ANIM_OFF);
    subtitle_assistant_ = role && strcmp(role, "assistant") == 0 && has_text && !important;
    subtitle_was_speaking_ = false;
    subtitle_elapsed_us_ = 0;
    subtitle_tick_us_ = esp_timer_get_time();
    subtitle_manual_until_ = 0;
    subtitle_scroll_target_ = 0;
    // The server sends sentence starts, not word timestamps. Estimate reading time
    // within each sentence; the next sentence always resets to its first line.
    subtitle_read_us_ = 0;
    for (const unsigned char c : translated) {
        if (c >= 0xC0)
            subtitle_read_us_ += 220000;
        else if (c > ' ' && c < 0x80)
            subtitle_read_us_ += 70000;
    }
    subtitle_read_us_ = std::max<int64_t>(1800000, subtitle_read_us_);
    if (important && has_text)
        lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
}

void EchoEarDisplay::LayoutSubtitles(bool important) {
    if (!bottom_bar_ || !chat_message_label_)
        return;
    const auto font = lv_obj_get_style_text_font(chat_message_label_, LV_PART_MAIN);
    const int line = font->line_height;
    // Size from the actual resource font, rather than a fixed 64px text viewport.
    const int height = important ? 170 : 2 * line + 4 + 16;
    lv_obj_set_size(bottom_bar_, important ? 344 : 300, height);
    lv_obj_set_style_pad_all(bottom_bar_, 8, 0);
    lv_obj_set_style_border_width(bottom_bar_, 0, 0);
    lv_obj_set_style_text_line_space(chat_message_label_, 4, 0);
    lv_obj_set_size(chat_message_label_, important ? 318 : 274, LV_SIZE_CONTENT);
    lv_obj_align(chat_message_label_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_align(bottom_bar_, LV_ALIGN_TOP_MID, 0, important ? 224 : 410 - height);
    lv_obj_set_scrollbar_mode(bottom_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_update_layout(bottom_bar_);
}

void EchoEarDisplay::ScrollSubtitles(bool speaking) {
    const int64_t now = esp_timer_get_time();
    const int64_t delta =
        subtitle_tick_us_ ? std::clamp<int64_t>(now - subtitle_tick_us_, 0, 250000) : 0;
    subtitle_tick_us_ = now;
    if (!subtitle_assistant_ || !bottom_bar_ || hide_subtitle_ || sleeping_ ||
        lv_obj_has_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN))
        return;
    if (now < subtitle_manual_until_)
        return;
    const int max_scroll =
        std::max(0, int(lv_obj_get_scroll_y(bottom_bar_) + lv_obj_get_scroll_bottom(bottom_bar_)));
    if (max_scroll <= 0)
        return;
    const int line = lv_obj_get_style_text_font(chat_message_label_, LV_PART_MAIN)->line_height + 4;
    const int lines = std::max(2, (int(lv_obj_get_height(chat_message_label_)) + 4) / line);
    if (speaking && lv_obj_has_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN))
        subtitle_elapsed_us_ += delta;
    const int64_t per_line = std::max<int64_t>(800000, subtitle_read_us_ / lines);
    int target = std::min<int64_t>(
        max_scroll, std::max<int64_t>(0, subtitle_elapsed_us_ - 500000) / per_line * line);
    if (!speaking && subtitle_was_speaking_)
        target = max_scroll;
    if (speaking || subtitle_was_speaking_) {
        if (target != subtitle_scroll_target_) {
            lv_obj_scroll_to_y(bottom_bar_, target, LV_ANIM_ON);
            subtitle_scroll_target_ = target;
        }
    }
    subtitle_was_speaking_ = speaking;
}

void EchoEarDisplay::OpenLocalApp(int page) {
    DisplayLockGuard lock(this);
    if (original_ui_)
        original_ui_->OpenApp(page);
}

void EchoEarDisplay::MotionReaction(bool shake) {
    DisplayLockGuard lock(this);
    SetEmotion(shake ? "confused" : "happy");
    pet_until_ = esp_timer_get_time() + 4000000;
    pet_action_ = -1;
}

void EchoEarDisplay::Animate() {
    ++tick_;
    if (pet_until_ && esp_timer_get_time() >= pet_until_)
        SetEmotion("neutral");
    if (tick_ % 25 == 0 && info_panel_ && !lv_obj_has_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN) &&
        !lv_obj_has_flag(more_panel_, LV_OBJ_FLAG_HIDDEN) &&
        !lv_obj_has_flag(info_panel_, LV_OBJ_FLAG_HIDDEN))
        RefreshNetworkInfo();
    auto state = Application::GetInstance().GetDeviceState();
    bool listening = state == kDeviceStateListening;
    bool speaking = state == kDeviceStateSpeaking;
    ScrollSubtitles(speaking);
    AnimateCat(listening, speaking);
    auto& senses = SenseHub::Get();
    int gaze = 0;
    if (senses.sound_enabled) {
        const bool active = senses.sound.enabled && !speaking;
        const bool valid = active && senses.sound.confidence.load() >= 65;
        const int direction = senses.sound.direction.load() * (senses.ReverseSoundAxis() ? -1 : 1);
        gaze = valid ? direction / 4 : 0;
    } else if (senses.motion_enabled && senses.imu_ready) {
        gaze = std::clamp((senses.ay.load() - senses.zero_y.load()) / 50, -12, 12);
    }
    // Bounded movement avoids single-window jumps without inventing a valid direction.
    sound_gaze_ += std::clamp(gaze - sound_gaze_, -5, 5);
    if (use_original_eyes_)
        lv_obj_align(emoji_image_, LV_ALIGN_CENTER, sound_gaze_, 0);
    else if (!use_theme_expression_) {
        for (auto pupil : pupils_)
            if (pupil)
                lv_obj_set_style_translate_x(pupil, std::clamp(sound_gaze_ / 4, -3, 3), 0);
    }
    for (int i = 0; i < 5; ++i) {
        int h = listening || speaking ? 5 + ((tick_ + i * 2) % 5) * 3 : 5;
        lv_obj_set_height(bars_[i], h);
        lv_obj_set_y(bars_[i], 313 - h / 2);
    }
    if (use_original_eyes_) {
        for (auto bar : bars_)
            lv_obj_set_style_bg_color(bar, lv_color_hex(0xD6EFF2), 0);
    } else if (state == kDeviceStateConnecting || state == kDeviceStateActivating) {
        for (auto bar : bars_)
            lv_obj_set_style_bg_color(bar, lv_color_hex(0xE6C681), 0);
    } else {
        for (auto bar : bars_)
            lv_obj_set_style_bg_color(bar, lv_color_hex(kMint), 0);
    }
    // Activity animation represents device state, not measured microphone level.
    const char* hint = listening  ? Text("正在聆听 · 轻触结束", "Listening · Tap to stop")
                       : speaking ? Text("正在说话 · 轻触打断", "Speaking · Tap to stop")
                                  : "";
    switch (state) {
        case kDeviceStateStarting:
            hint = Text("正在启动", "Starting");
            break;
        case kDeviceStateConnecting:
            hint = Text("正在连接", "Connecting");
            break;
        case kDeviceStateWifiConfiguring:
            hint = Text("请先连接无线网络", "Connect to Wi-Fi");
            break;
        case kDeviceStateActivating:
            hint = Text("正在激活设备", "Activating");
            break;
        case kDeviceStateUpgrading:
            hint = Text("正在更新 · 请勿断电", "Updating · Keep power on");
            break;
        case kDeviceStateFatalError:
            hint = Text("连接异常 · 请稍后重试", "Connection error · Retry");
            break;
        default:
            break;
    }
    if (pet_until_ && pet_action_ >= 0 && pet_action_ < 3 && state == kDeviceStateIdle) {
        static const char* zh[] = {"呼噜呼噜，喜欢你", "好香呀，谢谢你", "再陪我玩一会儿"};
        static const char* en[] = {"Purr... I like you!", "Yummy! Thank you!", "Let's play again!"};
        hint = Text(zh[pet_action_], en[pet_action_]);
    }
    if (sleeping_)
        hint = Text("轻触唤醒 · 下拉设置", "Tap to wake · Swipe down");
    if (strcmp(lv_label_get_text(hint_), hint) != 0)
        lv_label_set_text(hint_, hint);
}

lv_obj_t* EchoEarDisplay::Button(lv_obj_t* parent, int x, int y, int width, const char* text,
                                 lv_event_cb_t callback) {
    auto button = Shape(parent, x, y, width, 48, kCard, 24);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x36584F), LV_STATE_PRESSED);
    auto label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
    return button;
}

void EchoEarDisplay::CreateSettings(lv_obj_t* screen) {
    settings_panel_ = Shape(screen, 0, 0, 466, 466, kBackground, 0);
    // An opaque, clickable panel also prevents taps reaching the cat underneath.
    lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(settings_panel_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(
        settings_panel_,
        [](lv_event_t* e) {
            auto indev = lv_indev_active();
            if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) {
                static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->CloseSettings();
                lv_indev_wait_release(indev);
            }
        },
        LV_EVENT_GESTURE, this);
    Shape(settings_panel_, 211, 404, 44, 5, 0x65867B, 3);
    auto title = settings_title_ = lv_label_create(settings_panel_);
    lv_label_set_text(title, "设备设置");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_text_color(title, lv_color_hex(kMint), 0);
    volume_label_ = lv_label_create(settings_panel_);
    brightness_label_ = lv_label_create(settings_panel_);
    for (auto label : {volume_label_, brightness_label_}) {
        lv_obj_set_width(label, 180);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
    }
    lv_obj_set_pos(volume_label_, 143, 125);
    lv_obj_set_pos(brightness_label_, 143, 205);
    Button(settings_panel_, 77, 116, 56, "-", [](lv_event_t* e) {
        static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->AdjustSetting(true, -5);
    });
    Button(settings_panel_, 333, 116, 56, "+", [](lv_event_t* e) {
        static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->AdjustSetting(true, 5);
    });
    Button(settings_panel_, 77, 196, 56, "-", [](lv_event_t* e) {
        static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->AdjustSetting(false, -5);
    });
    Button(settings_panel_, 333, 196, 56, "+", [](lv_event_t* e) {
        static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->AdjustSetting(false, 5);
    });
    auto more = Button(settings_panel_, 83, 290, 140, "更多", [](lv_event_t* e) {
        static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->OpenMoreSettings();
    });
    more_label_ = lv_obj_get_child(more, 0);
    more_panel_ = Shape(settings_panel_, 0, 0, 466, 466, kBackground, 0);
    lv_obj_add_flag(more_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(more_panel_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    auto more_title = more_title_ = lv_label_create(more_panel_);
    lv_label_set_text(more_title, "更多设置");
    lv_obj_align(more_title, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_text_color(more_title, lv_color_hex(kMint), 0);
    auto aec = Button(more_panel_, 83, 98, 300, "回声消除：开", [](lv_event_t* e) {
        static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->ToggleAec();
    });
    aec_label_ = lv_obj_get_child(aec, 0);
    auto subtitle = Button(more_panel_, 83, 160, 300, "聊天字幕：开", [](lv_event_t* e) {
        static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->ToggleSubtitle();
    });
    subtitle_label_ = lv_obj_get_child(subtitle, 0);
    auto appearance = Button(more_panel_, 83, 222, 300, "", [](lv_event_t* e) {
        static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->ToggleAppearance();
    });
    appearance_label_ = lv_obj_get_child(appearance, 0);
    auto language = Button(more_panel_, 83, 284, 300, "", [](lv_event_t* e) {
        static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->ToggleLanguage();
    });
    language_label_ = lv_obj_get_child(language, 0);
    auto info = Button(more_panel_, 88, 352, 170, "", [](lv_event_t* e) {
        auto self = static_cast<EchoEarDisplay*>(lv_event_get_user_data(e));
        lv_obj_add_flag(self->wifi_confirm_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(self->info_panel_, LV_OBJ_FLAG_HIDDEN);
        self->RefreshNetworkInfo();
    });
    info_button_label_ = lv_obj_get_child(info, 0);
    auto back = Button(more_panel_, 268, 352, 110, "", [](lv_event_t* e) {
        auto self = static_cast<EchoEarDisplay*>(lv_event_get_user_data(e));
        lv_obj_add_flag(self->more_panel_, LV_OBJ_FLAG_HIDDEN);
    });
    back_label_ = lv_obj_get_child(back, 0);
    info_panel_ = Shape(more_panel_, 0, 0, 466, 466, kBackground, 0);
    lv_obj_add_flag(info_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(info_panel_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    info_label_ = lv_label_create(info_panel_);
    lv_obj_set_size(info_label_, 330, 160);
    lv_obj_set_pos(info_label_, 68, 105);
    lv_obj_set_style_text_color(info_label_, lv_color_hex(kText), 0);
    lv_obj_set_style_text_align(info_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(info_label_, LV_LABEL_LONG_WRAP);
    auto wifi = Button(info_panel_, 83, 280, 300, "", [](lv_event_t* e) {
        auto self = static_cast<EchoEarDisplay*>(lv_event_get_user_data(e));
        lv_obj_remove_flag(self->wifi_confirm_, LV_OBJ_FLAG_HIDDEN);
    });
    wifi_label_ = lv_obj_get_child(wifi, 0);
    auto info_back = Button(info_panel_, 163, 350, 140, "", [](lv_event_t* e) {
        auto self = static_cast<EchoEarDisplay*>(lv_event_get_user_data(e));
        lv_obj_add_flag(self->info_panel_, LV_OBJ_FLAG_HIDDEN);
    });
    info_back_label_ = lv_obj_get_child(info_back, 0);
    wifi_confirm_ = Shape(info_panel_, 0, 0, 466, 466, kBackground, 0);
    lv_obj_add_flag(wifi_confirm_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(wifi_confirm_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    wifi_question_ = lv_label_create(wifi_confirm_);
    lv_obj_set_size(wifi_question_, 320, 180);
    lv_obj_set_pos(wifi_question_, 73, 100);
    lv_obj_set_style_text_align(wifi_question_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(wifi_question_, lv_color_hex(kText), 0);
    auto no = Button(wifi_confirm_, 83, 300, 140, "", [](lv_event_t* e) {
        auto self = static_cast<EchoEarDisplay*>(lv_event_get_user_data(e));
        lv_obj_add_flag(self->wifi_confirm_, LV_OBJ_FLAG_HIDDEN);
    });
    wifi_no_ = lv_obj_get_child(no, 0);
    auto yes = Button(wifi_confirm_, 243, 300, 140, "", [](lv_event_t* e) {
        static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->ConfirmWifiConfig();
    });
    wifi_yes_ = lv_obj_get_child(yes, 0);
    lv_obj_add_flag(wifi_confirm_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(info_panel_, LV_OBJ_FLAG_HIDDEN);
    RefreshLabels();
    lv_obj_add_flag(more_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
}

void EchoEarDisplay::OpenMoreSettings() {
    // Read application state on its owning task, then update LVGL under its lock.
    Application::GetInstance().Schedule([this]() {
        auto enabled = Application::GetInstance().GetAecMode() != kAecOff;
        DisplayLockGuard lock(this);
        aec_enabled_ = enabled;
        RefreshLabels();

        lv_obj_add_flag(info_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(more_panel_);
        lv_obj_remove_flag(more_panel_, LV_OBJ_FLAG_HIDDEN);
    });
}

void EchoEarDisplay::ToggleAec() {
    if (!EchoEarTarget::kDeviceAec) {
        ShowNotification(Text("此设备不支持本机回声消除", "Device AEC unavailable"));
        return;
    }
    Application::GetInstance().Schedule([this]() {
        auto& app = Application::GetInstance();
        bool enabled = app.GetAecMode() == kAecOff;
        app.SetAecMode(enabled ? kAecOnDeviceSide : kAecOff);
        DisplayLockGuard lock(this);
        aec_enabled_ = enabled;
        RefreshLabels();
        ShowNotification(enabled ? Text("回声消除已开启", "Echo cancellation on")
                                 : Text("回声消除已关闭", "Echo cancellation off"));
        ESP_LOGI("EchoEarUI", "AEC requested: %s", enabled ? "on" : "off");
    });
}

void EchoEarDisplay::ToggleSubtitle() {
    Application::GetInstance().Schedule([this]() {
        DisplayLockGuard lock(this);
        bool hide = !hide_subtitle_;
        {
            Settings preferences("echoear_ui", true);
            preferences.SetBool("hide_subtitle", hide);
        }
        SetHideSubtitle(hide);
        RefreshLabels();
        ShowNotification(hide ? Text("聊天字幕已关闭", "Subtitles off")
                              : Text("聊天字幕已开启", "Subtitles on"));
        ESP_LOGI("EchoEarUI", "Subtitles: %s", hide ? "off" : "on");
    });
}

void EchoEarDisplay::OpenSettings() {
    if (original_ui_)
        original_ui_->Quick();
}

void EchoEarDisplay::AdjustSetting(bool volume, int delta) {
    int& value = volume ? volume_ : brightness_;
    value = std::clamp(value + delta, volume ? 0 : 10, 100);
    lv_label_set_text_fmt(volume ? volume_label_ : brightness_label_,
                          volume ? Text("音量 %d%%", "Vol %d%%") : Text("亮度 %d%%", "Light %d%%"),
                          value);
    // Hardware and NVS operations run on the application task, outside the LVGL lock.
    Application::GetInstance().Schedule([volume, value]() {
        auto& board = Board::GetInstance();
        if (volume) {
            if (auto codec = board.GetAudioCodec())
                codec->SetOutputVolume(value);
        } else if (auto backlight = board.GetBacklight()) {
            backlight->SetBrightness(value, true);
        }
    });
}

void EchoEarDisplay::CloseSettings() {
    if (original_ui_)
        original_ui_->Home();
}

void EchoEarDisplay::AnimateSettings(bool open) {
    lv_anim_delete(settings_panel_, nullptr);
    int start =
        lv_obj_has_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN) ? -466 : lv_obj_get_y(settings_panel_);
    lv_obj_remove_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(settings_panel_, start);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, settings_panel_);
    lv_anim_set_values(&animation, start, open ? 0 : -466);
    lv_anim_set_duration(&animation, 200);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation,
                        [](void* obj, int32_t y) { lv_obj_set_y(static_cast<lv_obj_t*>(obj), y); });
    if (!open) {
        lv_anim_set_completed_cb(&animation, [](lv_anim_t* a) {
            lv_obj_add_flag(static_cast<lv_obj_t*>(a->var), LV_OBJ_FLAG_HIDDEN);
        });
    }
    lv_anim_start(&animation);
}

void EchoEarDisplay::ClearOriginalEyes() {
    if (original_gif_) {
        lv_image_set_src(emoji_image_, nullptr);
        original_gif_.reset();
    }
    original_animation_.clear();
}

bool EchoEarDisplay::ShowOriginalEyes() {
    const char* file = "brk_blink1.gif";
    switch (sleeping_ ? CatMood::Sleepy : cat_mood_) {
        case CatMood::Happy:
        case CatMood::Love:
            file = "brk_happy.gif";
            break;
        case CatMood::Sad:
            file = "brk_sad.gif";
            break;
        case CatMood::Angry:
            file = "brk_angry.gif";
            break;
        case CatMood::Sleepy:
            file = "brk_sleep.gif";
            break;
        case CatMood::Confused:
            file = "brk_dizzy.gif";
            break;
        case CatMood::Surprised:
            file = "brk_blink_fast.gif";
            break;
        case CatMood::Wink:
            file = "brk_blink_slow.gif";
            break;
        default:
            break;
    }
    if (original_animation_ != file || !original_gif_) {
        void* data = nullptr;
        size_t size = 0;
        if (!Assets::GetInstance().GetAssetData(file, data, size)) {
            ESP_LOGW("EchoEarUI", "Missing original animation: %s", file);
            ClearOriginalEyes();
            return false;
        }
        ClearOriginalEyes();
        if (gif_controller_) {
            lv_image_set_src(emoji_image_, nullptr);
            gif_controller_.reset();
        }
        LvglRawImage image(data, size);
        original_gif_ = std::make_unique<LvglGif>(image.image_dsc());
        if (!original_gif_->IsLoaded()) {
            ClearOriginalEyes();
            return false;
        }
        original_animation_ = file;
        original_gif_->SetLoopCount(0);
        original_gif_->SetFrameCallback(
            [this]() { lv_image_set_src(emoji_image_, original_gif_->image_dsc()); });
        original_gif_->Start();
        ESP_LOGI("EchoEarUI", "Original eyes loaded: %s (%u bytes)", file, unsigned(size));
    }
    lv_image_set_src(emoji_image_, original_gif_->image_dsc());
    lv_image_set_scale(emoji_image_, 256);
    lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cat_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    if (sleeping_)
        original_gif_->Pause();
    else
        original_gif_->Resume();
    return true;
}

void EchoEarDisplay::RefreshAppearance() {
    if (!cat_ || !current_theme_)
        return;
    lv_obj_align(emoji_box_, LV_ALIGN_TOP_MID, 0, use_original_eyes_ ? kOriginalEyesTop : 123);
    if (use_original_eyes_ && ShowOriginalEyes())
        return;
    ClearOriginalEyes();
    lv_obj_align(emoji_image_, LV_ALIGN_CENTER, 0, 0);
    auto collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();
    const char* name = sleeping_ ? "sleepy" : emotion_.c_str();
    auto image = collection ? collection->GetEmojiImage(name) : nullptr;
    if (!image && collection) {
        name = "neutral";
        image = collection->GetEmojiImage(name);
    }
    if (use_theme_expression_ && image) {
        SpiLcdDisplay::SetEmotion(name);
        lv_image_header_t header = {};
        if (lv_image_decoder_get_info(image->image_dsc(), &header) == LV_RESULT_OK &&
            header.w > 0 && header.h > 0) {
            const int scale = std::min(256 * 250 / int(header.w), 256 * 174 / int(header.h));
            lv_image_set_scale(emoji_image_, std::clamp(scale, 32, 1024));
        }
        lv_obj_add_flag(cat_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (gif_controller_) {
            gif_controller_->Stop();
            gif_controller_.reset();
        }
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cat_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EchoEarDisplay::ToggleAppearance() {
    auto collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();
    const int current = use_original_eyes_ ? 2 : (use_theme_expression_ ? 1 : 0);
    int next = (current + 1) % 3;
    if (next == 1 && (!collection || !collection->GetEmojiImage("neutral")))
        next = 2;
    if (next == 2) {
        void* data = nullptr;
        size_t size = 0;
        if (!Assets::GetInstance().GetAssetData("brk_blink1.gif", data, size)) {
            ShowNotification(Text("请更新本地资源", "Update local assets"));
            return;
        }
    }
    use_theme_expression_ = next == 1;
    use_original_eyes_ = next == 2;
    ESP_LOGI("EchoEarUI", "Appearance: %d", next);
    RefreshLabels();
    ApplyPalette();
    RefreshAppearance();
    Application::GetInstance().Schedule([enabled = use_theme_expression_, next]() {
        Settings preferences("echoear_ui", true);
        preferences.SetBool("theme_face", enabled);
        preferences.SetInt("appearance", next);
    });
}

void EchoEarDisplay::RefreshHintVisibility() {
    if (!hint_ || !bottom_bar_)
        return;
    if (lv_obj_has_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN))
        lv_obj_remove_flag(hint_, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN);
}

void EchoEarDisplay::ClearChatMessages() {
    SpiLcdDisplay::ClearChatMessages();
    DisplayLockGuard lock(this);
    subtitle_assistant_ = false;
    RefreshHintVisibility();
}

void EchoEarDisplay::SetHideSubtitle(bool hide) {
    Settings preferences("echoear_ui");
    SpiLcdDisplay::SetHideSubtitle(preferences.GetBool("hide_subtitle", hide));
    DisplayLockGuard lock(this);
    RefreshHintVisibility();
}

void EchoEarDisplay::SetPowerSaveMode(bool on) {
    if (on && original_ui_ && original_ui_->Active()) {
        DisplayLockGuard lock(this);
        original_ui_->Home();
    }
    DisplayLockGuard lock(this);
    sleeping_ = on;
    ESP_LOGI("EchoEarUI", "Display sleep: %s", on ? "on" : "off");
    SpiLcdDisplay::SetPowerSaveMode(on);
    RefreshHintVisibility();
    RefreshAppearance();
    Animate();
    if (timer_) {
        if (on)
            lv_timer_pause(timer_);
        else
            lv_timer_resume(timer_);
    }
}

void EchoEarDisplay::RefreshLabels() {
    if (tools_label_)
        lv_label_set_text(tools_label_, Text("工具", "Tools"));
    RefreshCountdown();
    RefreshPetPanel();
    if (!language_label_)
        return;
    lv_label_set_text(wifi_label_, Text("重新配网", "Change Wi-Fi"));
    lv_label_set_text(wifi_question_, Text("重新连接无线网络？\n将结束当前对话",
                                           "Change Wi-Fi?\nThis ends the\ncurrent chat."));
    lv_label_set_text(wifi_yes_, Text("继续", "Continue"));
    lv_label_set_text(wifi_no_, Text("取消", "Cancel"));
    lv_label_set_text(settings_title_, Text("设备设置", "Settings"));
    lv_label_set_text(more_title_, Text("更多设置", "More settings"));
    lv_label_set_text(more_label_, Text("更多", "More"));
    lv_label_set_text(back_label_, Text("返回", "Back"));
    lv_label_set_text(info_back_label_, Text("返回", "Back"));
    lv_label_set_text(info_button_label_, Text("设备信息", "Device info"));
    lv_label_set_text(language_label_, Text("语言：中文", "Language: English"));
    lv_label_set_text_fmt(volume_label_, Text("音量 %d%%", "Vol %d%%"), volume_);
    lv_label_set_text_fmt(brightness_label_, Text("亮度 %d%%", "Light %d%%"), brightness_);
    lv_label_set_text(aec_label_, aec_enabled_ ? Text("回声消除：开", "Echo cancel: On")
                                               : Text("回声消除：关", "Echo cancel: Off"));
    lv_label_set_text(subtitle_label_, hide_subtitle_ ? Text("聊天字幕：关", "Subtitles: Off")
                                                      : Text("聊天字幕：开", "Subtitles: On"));
    lv_label_set_text(appearance_label_,
                      use_original_eyes_      ? Text("外观：原版双眼", "Look: Original eyes")
                      : use_theme_expression_ ? Text("外观：主题表情", "Look: Theme")
                                              : Text("外观：猫咪", "Look: Cat"));
}

void EchoEarDisplay::ToggleLanguage() {
    english_ = !english_;
    RefreshLabels();
    SetStatus(status_text_.c_str());
    Animate();
    Application::GetInstance().Schedule([english = english_]() {
        Settings preferences("echoear_ui", true);
        preferences.SetBool("english", english);
        ESP_LOGI("EchoEarUI", "UI language: %s", english ? "en" : "zh");
    });
}

void EchoEarDisplay::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    status_text_ = status ? status : "";
    const char* text = status_text_.c_str();
    if (english_) {
        struct Translation {
            const char* source;
            const char* target;
        };
        const Translation translations[] = {
            {Lang::Strings::STANDBY, "Standby"},
            {Lang::Strings::CONNECTING, "Connecting"},
            {Lang::Strings::LISTENING, "Listening"},
            {Lang::Strings::SPEAKING, "Speaking"},
            {Lang::Strings::ACTIVATION, "Activating"},
            {Lang::Strings::CHECKING_NEW_VERSION, "Checking update"},
            {Lang::Strings::LOADING_PROTOCOL, "Loading"},
            {Lang::Strings::REGISTERING_NETWORK, "Joining network"},
            {Lang::Strings::DETECTING_MODULE, "Detecting module"},
        };
        for (const auto& entry : translations) {
            if (status_text_ == entry.source) {
                text = entry.target;
                break;
            }
        }
    }
    auto translated = TranslateUi(text);
    SpiLcdDisplay::SetStatus(translated.c_str());
}

void EchoEarDisplay::AnimateCat(bool listening, bool speaking) {
    if (!cat_ || lv_obj_has_flag(cat_, LV_OBJ_FLAG_HIDDEN))
        return;
    auto mood = sleeping_ ? CatMood::Sleepy : cat_mood_;
    bool happy = mood == CatMood::Happy || mood == CatMood::Love;
    bool sad = mood == CatMood::Sad;
    bool angry = mood == CatMood::Angry;
    bool surprise = mood == CatMood::Surprised;
    bool sleepy = mood == CatMood::Sleepy;
    bool confused = mood == CatMood::Confused;
    bool blink = !speaking && tick_ % 41 >= 39;
    int gaze = pet_until_ && pet_action_ == 2 ? (tick_ % 10 < 5 ? -5 : 5)
               : listening                    ? 0
                           : (tick_ % 100 < 15 ? -3 : (tick_ % 100 > 84 ? 3 : 0));
    // All objects are retained; animation only changes geometry and styles.
    for (int i = 0; i < 2; ++i) {
        bool closed = blink || sleepy || (mood == CatMood::Wink && i == 0);
        int width = happy ? 26 : (surprise ? 26 : 18);
        int height = closed ? 4 : happy ? 10 : sad ? 22 : surprise ? 42 : angry ? 18 : 36;
        lv_obj_set_size(eyes_[i], width, height);
        lv_obj_set_pos(eyes_[i], 92 + i * 116 - width / 2 + gaze, 99 - height / 2);
        if (closed || happy || angry)
            lv_obj_add_flag(pupils_[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_remove_flag(pupils_[i], LV_OBJ_FLAG_HIDDEN);
        int angle = angry      ? (i ? -230 : 230)
                    : sad      ? (i ? 180 : -180)
                    : confused ? (i ? 0 : -200)
                               : 0;
        lv_obj_set_style_transform_rotation(brows_[i], angle, 0);
        lv_obj_set_y(brows_[i], surprise ? 59 : confused && i == 0 ? 62 : 70);
        lv_obj_set_style_bg_opa(cheeks_[i],
                                mood == CatMood::Love ? LV_OPA_COVER
                                : happy               ? LV_OPA_80
                                                      : LV_OPA_50,
                                0);
        if (sad && emotion_ == "crying") {
            lv_obj_remove_flag(tears_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(tears_[i], 116 + (tick_ % 6) * 3);
        } else
            lv_obj_add_flag(tears_[i], LV_OBJ_FLAG_HIDDEN);
        int twitch = listening && tick_ % 24 < 3 ? (i ? -50 : 50) : 0;
        lv_obj_set_style_transform_rotation(ears_[i], 450 + twitch, 0);
    }
    int mouth_width = happy ? 30 : surprise ? 20 : sleepy ? 12 : 18;
    int mouth_height = speaking ? 10 + int(tick_ % 4) * 4 : surprise ? 26 : happy ? 15 : 7;
    if (pet_until_ && pet_action_ == 1)
        mouth_height = 5 + int(tick_ % 3) * 5;
    lv_obj_set_size(mouth_, mouth_width, mouth_height);
    lv_obj_set_pos(mouth_, 150 - mouth_width / 2, 129);
    lv_obj_set_style_radius(mouth_, happy ? 12 : 8, 0);
    // Outline smile/frown; filled oval for surprise and speaking.
    bool outline = !speaking && !(pet_until_ && pet_action_ == 1) && (happy || sad);
    lv_obj_set_style_bg_opa(mouth_, outline ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(mouth_, lv_color_hex(kInk), 0);
    lv_obj_set_style_border_width(mouth_, outline ? 3 : 0, 0);
    lv_obj_set_style_border_side(mouth_, sad ? LV_BORDER_SIDE_TOP : LV_BORDER_SIDE_BOTTOM, 0);
    int bob = happy && !sleeping_ ? (tick_ % 12 < 6 ? -2 : 1) : 0;
    lv_obj_set_y(cat_, 123 + bob);
}

void EchoEarDisplay::RefreshNetworkInfo() {
    Application::GetInstance().Schedule([this]() {
        esp_netif_ip_info_t ip = {};
        auto netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        wifi_ap_record_t ap = {};
        bool connected = netif && esp_netif_is_netif_up(netif) &&
                         esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr &&
                         esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
        DisplayLockGuard lock(this);
        if (connected) {
            lv_label_set_text_fmt(info_label_, "EchoEar v0.16\nWi-Fi %d dBm\n" IPSTR, ap.rssi,
                                  IP2STR(&ip.ip));
        } else {
            lv_label_set_text(info_label_, Text("喵伴 v0.16\n网络未连接\n可重新配网",
                                                "EchoEar v0.16\nWi-Fi offline\nReconnect below"));
        }
    });
}

void EchoEarDisplay::ConfirmWifiConfig() {
    // Dismiss before scheduling to prevent repeated taps spawning config tasks.
    lv_obj_add_flag(wifi_confirm_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(info_panel_, LV_OBJ_FLAG_HIDDEN);
    CloseSettings();
    Application::GetInstance().Schedule([this]() {
        auto state = Application::GetInstance().GetDeviceState();
        if (state != kDeviceStateIdle && state != kDeviceStateListening &&
            state != kDeviceStateSpeaking && state != kDeviceStateNotifying) {
            ShowNotification(Text("请稍后再试", "Please try again later"));
            return;
        }
        if (wifi_config_callback_)
            wifi_config_callback_();
    });
}

void EchoEarDisplay::CreateCountdown() {
    auto entry = Button(settings_panel_, 243, 290, 140, "", [](lv_event_t* e) {
        auto self = static_cast<EchoEarDisplay*>(lv_event_get_user_data(e));
        self->RefreshCountdown();
        lv_obj_remove_flag(self->timer_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(self->timer_panel_);
    });
    timer_entry_ = lv_obj_get_child(entry, 0);
    timer_panel_ = Shape(settings_panel_, 0, 0, 466, 466, kBackground, 0);
    lv_obj_add_flag(timer_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(timer_panel_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    timer_title_ = lv_label_create(timer_panel_);
    lv_obj_align(timer_title_, LV_ALIGN_TOP_MID, 0, 65);
    lv_obj_set_style_text_color(timer_title_, lv_color_hex(kMint), 0);
    timer_value_ = lv_label_create(timer_panel_);
    lv_obj_set_size(timer_value_, 200, 60);
    lv_obj_set_pos(timer_value_, 133, 153);
    lv_obj_set_style_text_align(timer_value_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(timer_value_, lv_color_hex(kText), 0);
    Button(timer_panel_, 65, 153, 56, "-", [](lv_event_t* e) {
        auto self = static_cast<EchoEarDisplay*>(lv_event_get_user_data(e));
        if (!self->countdown_.Running()) {
            self->timer_minutes_ = std::max(1, self->timer_minutes_ - 1);
            self->timer_finished_ = false;
            self->RefreshCountdown();
        }
    });
    Button(timer_panel_, 345, 153, 56, "+", [](lv_event_t* e) {
        auto self = static_cast<EchoEarDisplay*>(lv_event_get_user_data(e));
        if (!self->countdown_.Running()) {
            self->timer_minutes_ = std::min(120, self->timer_minutes_ + 1);
            self->timer_finished_ = false;
            self->RefreshCountdown();
        }
    });
    auto action = Button(timer_panel_, 103, 255, 260, "", [](lv_event_t* e) {
        auto self = static_cast<EchoEarDisplay*>(lv_event_get_user_data(e));
        if (self->countdown_.Running())
            self->countdown_.Cancel();
        else
            self->countdown_.Start(self->timer_minutes_, esp_timer_get_time());
        self->timer_finished_ = false;
        self->RefreshCountdown();
        ESP_LOGI("EchoEarUI", "Timer %s: %d min",
                 self->countdown_.Running() ? "started" : "cancelled", self->timer_minutes_);
    });
    timer_action_ = lv_obj_get_child(action, 0);
    auto back = Button(timer_panel_, 163, 340, 140, "", [](lv_event_t* e) {
        auto self = static_cast<EchoEarDisplay*>(lv_event_get_user_data(e));
        lv_obj_add_flag(self->timer_panel_, LV_OBJ_FLAG_HIDDEN);
        if (self->original_ui_)
            self->original_ui_->Launcher();
    });
    timer_back_ = lv_obj_get_child(back, 0);
    lv_obj_add_flag(timer_panel_, LV_OBJ_FLAG_HIDDEN);
    countdown_tick_ = lv_timer_create(
        [](lv_timer_t* timer) {
            static_cast<EchoEarDisplay*>(lv_timer_get_user_data(timer))->TickCountdown();
        },
        1000, this);
    RefreshCountdown();
}

void EchoEarDisplay::RefreshCountdown() {
    if (!timer_back_)
        return;
    lv_label_set_text(timer_entry_, Text("倒计时", "Timer"));
    lv_label_set_text(timer_title_,
                      timer_finished_ ? Text("时间到了", "Time is up") : Text("倒计时", "Timer"));
    lv_label_set_text(timer_back_, Text("返回", "Back"));
    lv_label_set_text(timer_action_, countdown_.Running() ? Text("取消计时", "Cancel timer")
                                                          : Text("开始", "Start"));
    if (countdown_.Running()) {
        int seconds = countdown_.SecondsLeft(esp_timer_get_time());
        lv_label_set_text_fmt(timer_value_, "%02d:%02d", seconds / 60, seconds % 60);
    } else if (timer_finished_)
        lv_label_set_text(timer_value_, "00:00");
    else
        lv_label_set_text_fmt(timer_value_, Text("%d 分钟", "%d min"), timer_minutes_);
}

void EchoEarDisplay::TickCountdown() {
    if (!countdown_.Running())
        return;
    if (countdown_.Expire(esp_timer_get_time())) {
        timer_finished_ = true;
        Application::GetInstance().Schedule([this]() {
            if (timer_wake_callback_)
                timer_wake_callback_();
            {
                DisplayLockGuard lock(this);
                OpenSettings();
                lv_obj_remove_flag(timer_panel_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(timer_panel_);
                RefreshCountdown();
            }
            // Avoid mixing the reminder into an active conversation.
            if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle)
                Application::GetInstance().PlaySound(Lang::Sounds::OGG_SUCCESS);
            ESP_LOGI("EchoEarUI", "Timer expired");
        });
    }
    RefreshCountdown();
}

void EchoEarDisplay::CreatePetPanel() {
    auto entry = Button(settings_panel_, 63, 356, 200, "", [](lv_event_t* e) {
        static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->original_ui_->Launcher();
    });
    pet_entry_ = lv_obj_get_child(entry, 0);
    pet_panel_ = Shape(settings_panel_, 0, 0, 466, 466, kBackground, 0);
    lv_obj_add_flag(pet_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(pet_panel_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    pet_title_ = lv_label_create(pet_panel_);
    lv_obj_align(pet_title_, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_text_color(pet_title_, lv_color_hex(kMint), 0);
    pet_stats_ = lv_label_create(pet_panel_);
    lv_obj_set_size(pet_stats_, 320, 55);
    lv_obj_set_pos(pet_stats_, 73, 100);
    lv_obj_set_style_text_align(pet_stats_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(pet_stats_, lv_color_hex(kText), 0);
    for (int i = 0; i < 3; ++i) {
        auto button = Button(pet_panel_, 103, 163 + i * 59, 260, "", [](lv_event_t* e) {
            auto self = static_cast<EchoEarDisplay*>(lv_event_get_user_data(e));
            for (int action = 0; action < 3; ++action) {
                if (lv_obj_get_child(static_cast<lv_obj_t*>(lv_event_get_target(e)), 0) ==
                    self->pet_buttons_[action]) {
                    self->InteractWithPet(action);
                    break;
                }
            }
        });
        pet_buttons_[i] = lv_obj_get_child(button, 0);
    }
    auto back = Button(pet_panel_, 163, 350, 140, "", [](lv_event_t* e) {
        auto self = static_cast<EchoEarDisplay*>(lv_event_get_user_data(e));
        lv_obj_add_flag(self->pet_panel_, LV_OBJ_FLAG_HIDDEN);
    });
    pet_back_ = lv_obj_get_child(back, 0);
    lv_obj_add_flag(pet_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        emoji_box_,
        [](lv_event_t* e) {
            static_cast<EchoEarDisplay*>(lv_event_get_user_data(e))->original_ui_->Launcher();
        },
        LV_EVENT_LONG_PRESSED, this);
    RefreshPetPanel();
}

void EchoEarDisplay::RefreshPetPanel() {
    if (!pet_back_)
        return;
    lv_label_set_text(pet_entry_, Text("宠物互动", "Play with cat"));
    lv_label_set_text(pet_title_, Text("陪陪猫咪", "Cat time"));
    lv_label_set_text_fmt(pet_stats_, Text("亲密度 %d / 100", "Bond %d / 100"), affection_);
    lv_label_set_text(pet_buttons_[0], Text("摸摸", "Pet"));
    lv_label_set_text(pet_buttons_[1], Text("喂食", "Feed"));
    lv_label_set_text(pet_buttons_[2], Text("逗猫", "Play"));
    lv_label_set_text(pet_back_, Text("返回", "Back"));
}

void EchoEarDisplay::OpenPetPanel() {
    OpenSettings();
    RefreshPetPanel();
    lv_obj_remove_flag(pet_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(pet_panel_);
}

void EchoEarDisplay::InteractWithPet(int action) {
    if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
        ShowNotification(Text("请先结束对话", "Finish chat first"));
        return;
    }
    const auto now = esp_timer_get_time();
    if (last_pet_action_ && now - last_pet_action_ < 3000000) {
        ShowNotification(Text("让猫咪缓一缓", "Give kitty a moment"));
        return;
    }
    last_pet_action_ = now;
    affection_ = std::min(100, affection_ + action + 1);
    if (extras_)
        extras_->Pet(action);
    static const char* moods[] = {"loving", "delicious", "happy"};
    SetEmotion(moods[action]);
    pet_action_ = action;
    pet_until_ = now + 8000000;
    ClearChatMessages();
    CloseSettings();
    Animate();
    Application::GetInstance().Schedule([bond = affection_]() {
        Settings preferences("echoear_ui", true);
        preferences.SetInt("affection", bond);
    });
    ESP_LOGI("EchoEarUI", "Pet interaction=%d bond=%d", action, affection_);
}

std::string EchoEarDisplay::TranslateUi(const char* text) const {
    std::string result = text ? text : "";
    if (!english_)
        return result;
    for (const auto& entry : kUiTranslations) {
        size_t pos = 0;
        while ((pos = result.find(entry.zh, pos)) != std::string::npos) {
            result.replace(pos, strlen(entry.zh), entry.en);
            pos += strlen(entry.en);
        }
    }
    return result;
}
void EchoEarDisplay::ShowNotification(const char* text, int duration_ms) {
    DisplayLockGuard lock(this);
    auto translated = TranslateUi(text);
    SpiLcdDisplay::ShowNotification(translated.c_str(), duration_ms);
    if (original_ui_ && original_ui_->Active())
        original_ui_->Notify(translated.c_str(), duration_ms);
}
void EchoEarDisplay::ShowNotification(const std::string& text, int duration_ms) {
    ShowNotification(text.c_str(), duration_ms);
}
void EchoEarDisplay::SetUpdateFeed(const std::string& url) {
    if (extras_)
        extras_->SetUpdateFeed(url);
}
