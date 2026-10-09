#pragma once
#include <functional>
#include <string>
#include "countdown.h"
#include "display/lcd_display.h"

// Shared 466px round UI for EchoEar hardware targets.
class EchoEarExtras;
class OriginalUi;

class EchoEarDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;
    ~EchoEarDisplay() override;
    void SetupUI() override;
    void SetWifiConfigCallback(std::function<void()> callback) {
        wifi_config_callback_ = std::move(callback);
    }
    void SetTimerWakeCallback(std::function<void()> callback) {
        timer_wake_callback_ = std::move(callback);
    }
    void SetTimerSleepCallback(std::function<void()> callback) {
        timer_sleep_callback_ = std::move(callback);
    }
    void MotionReaction(bool shake);
    void OpenLocalApp(int page);
    void SetUpdateFeed(const std::string& url);
    void ShowNotification(const char* text, int duration_ms = 3000) override;
    void ShowNotification(const std::string& text, int duration_ms = 3000) override;
    void SetStatus(const char* status) override;
    void SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;
    void SetTheme(Theme* theme) override;
    void ClearChatMessages() override;
    void SetHideSubtitle(bool hide) override;
    void SetPowerSaveMode(bool on) override;

private:
    friend class EchoEarExtras;
    friend class OriginalUi;
    OriginalUi* original_ui_ = nullptr;
    EchoEarExtras* extras_ = nullptr;
    lv_obj_t* tools_label_ = nullptr;
    std::function<void()> timer_sleep_callback_;
    std::string TranslateUi(const char* text) const;
    Countdown countdown_;
    int timer_minutes_ = 1;
    bool timer_finished_ = false;
    lv_timer_t* countdown_tick_ = nullptr;
    lv_obj_t* timer_panel_ = nullptr;
    lv_obj_t* timer_entry_ = nullptr;
    lv_obj_t* timer_title_ = nullptr;
    lv_obj_t* timer_value_ = nullptr;
    lv_obj_t* timer_action_ = nullptr;
    lv_obj_t* timer_back_ = nullptr;
    std::function<void()> timer_wake_callback_;
    void CreateCountdown();
    void RefreshCountdown();
    void TickCountdown();
    lv_obj_t* cat_ = nullptr;
    lv_obj_t* eyes_[2] = {};
    lv_obj_t* mouth_ = nullptr;
    lv_obj_t* hint_ = nullptr;
    int sound_gaze_ = 0;
    lv_obj_t* bars_[5] = {};
    lv_obj_t* settings_panel_ = nullptr;
    bool english_ = false;
    bool aec_enabled_ = true;
    std::string status_text_;
    lv_obj_t* settings_title_ = nullptr;
    lv_obj_t* more_title_ = nullptr;
    lv_obj_t* more_label_ = nullptr;
    lv_obj_t* language_label_ = nullptr;
    lv_obj_t* back_label_ = nullptr;
    lv_obj_t* info_button_label_ = nullptr;
    lv_obj_t* info_back_label_ = nullptr;
    std::function<void()> wifi_config_callback_;
    lv_obj_t* wifi_label_ = nullptr;
    lv_obj_t* wifi_confirm_ = nullptr;
    lv_obj_t* wifi_question_ = nullptr;
    lv_obj_t* wifi_yes_ = nullptr;
    lv_obj_t* wifi_no_ = nullptr;
    void RefreshNetworkInfo();
    void ConfirmWifiConfig();
    lv_obj_t* info_panel_ = nullptr;
    const char* Text(const char* zh, const char* en) const { return english_ ? en : zh; }
    void RefreshLabels();
    void ToggleLanguage();
    lv_obj_t* more_panel_ = nullptr;
    lv_obj_t* aec_label_ = nullptr;
    lv_obj_t* subtitle_label_ = nullptr;
    lv_obj_t* info_label_ = nullptr;
    void OpenMoreSettings();
    void ToggleAec();
    void ToggleSubtitle();
    lv_obj_t* volume_label_ = nullptr;
    lv_obj_t* brightness_label_ = nullptr;
    lv_obj_t* appearance_label_ = nullptr;
    bool use_theme_expression_ = false;
    bool use_original_eyes_ = false;
    std::unique_ptr<LvglGif> original_gif_;
    std::string original_animation_;
    bool ShowOriginalEyes();
    void ClearOriginalEyes();
    bool sleeping_ = false;
    std::string emotion_ = "neutral";
    void RefreshAppearance();
    void ToggleAppearance();
    void RefreshHintVisibility();
    int volume_ = 85;
    int brightness_ = 75;
    void CreateSettings(lv_obj_t* screen);
    void OpenSettings();
    void CloseSettings();
    void AnimateSettings(bool open);
    void AdjustSetting(bool volume, int delta);
    lv_obj_t* Button(lv_obj_t* parent, int x, int y, int width, const char* text,
                     lv_event_cb_t callback);
    lv_timer_t* timer_ = nullptr;
    uint32_t tick_ = 0;
    enum class CatMood { Neutral, Happy, Sad, Angry, Surprised, Sleepy, Wink, Love, Confused };
    CatMood cat_mood_ = CatMood::Neutral;
    lv_obj_t* brows_[2] = {};
    lv_obj_t* cheeks_[2] = {};
    lv_obj_t* tears_[2] = {};
    lv_obj_t* pupils_[2] = {};
    lv_obj_t* ears_[2] = {};
    int affection_ = 0;
    int pet_action_ = -1;
    int64_t pet_until_ = 0;
    int64_t last_pet_action_ = 0;
    lv_obj_t* pet_panel_ = nullptr;
    lv_obj_t* pet_entry_ = nullptr;
    lv_obj_t* pet_title_ = nullptr;
    lv_obj_t* pet_stats_ = nullptr;
    lv_obj_t* pet_buttons_[3] = {};
    lv_obj_t* pet_back_ = nullptr;
    void CreatePetPanel();
    void RefreshPetPanel();
    void OpenPetPanel();
    void InteractWithPet(int action);
    void AnimateCat(bool listening, bool speaking);
    void ApplyPalette();
    void LayoutSubtitles(bool important = false);
    void ScrollSubtitles(bool speaking);
    bool subtitle_assistant_ = false;
    bool subtitle_was_speaking_ = false;
    int64_t subtitle_elapsed_us_ = 0;
    int64_t subtitle_tick_us_ = 0;
    int64_t subtitle_manual_until_ = 0;
    int64_t subtitle_read_us_ = 0;
    int subtitle_scroll_target_ = 0;
    void ApplyMenuPalette(lv_obj_t* root);
    lv_obj_t* pull_handle_ = nullptr;
    void Animate();
    static lv_obj_t* Shape(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color,
                           int radius);
};
