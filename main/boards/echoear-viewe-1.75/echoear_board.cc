#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <esp_lcd_co5300.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_touch_cst816s.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include "application.h"
#include "audio/codecs/no_audio_codec.h"
#include "backlight.h"
#include "button.h"
#include "config.h"
#include "echoear_display.h"
#include "mcp_server.h"
#include "sense_hub.h"
#include "settings.h"
#include "wifi_board.h"
#define TAG "EchoEarViewe175"
#define LCD_OPCODE_WRITE_CMD 0x02
static const co5300_lcd_init_cmd_t vendor_specific_init[] = {
    // set display to qspi mode
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},

    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

class CustomBacklight : public Backlight {
public:
    CustomBacklight(esp_lcd_panel_io_handle_t panel_io) : Backlight(), panel_io_(panel_io) {}

    void SetScreenOff(bool off) {
        DisplayLockGuard lock(Board::GetInstance().GetDisplay());
        screen_off_ = off;
        SetBrightnessImpl(brightness());
    }

protected:
    bool screen_off_ = false;
    esp_lcd_panel_io_handle_t panel_io_;

    virtual void SetBrightnessImpl(uint8_t brightness) override {
        auto display = Board::GetInstance().GetDisplay();
        DisplayLockGuard lock(display);
        uint8_t data[1] = {screen_off_ ? uint8_t(0) : uint8_t((255 * brightness) / 100)};
        int lcd_cmd = 0x51;
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
        esp_lcd_panel_io_tx_param(panel_io_, lcd_cmd, &data, sizeof(data));
    }
};

class EchoEarViewe175 : public WifiBoard {
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    i2c_master_dev_handle_t imu_ = nullptr;
    Button boot_button_{GPIO_NUM_0};
    EchoEarDisplay* display_ = nullptr;
    CustomBacklight* backlight_ = nullptr;
    esp_lcd_touch_handle_t touch_panel_ = nullptr;
    std::atomic<bool> screen_off_{false};
    std::atomic<int64_t> last_activity_{0};
    int64_t last_touch_wake_ = 0;
    bool suppress_wake_touch_ = false;
    void SetScreenOff(bool off) {
        screen_off_ = off;
        backlight_->SetScreenOff(off);
        display_->SetPowerSaveMode(off);
    }
    void WakeScreen() {
        last_activity_ = esp_timer_get_time();
        if (screen_off_)
            SetScreenOff(false);
    }
    void InitializeMotion() {
        auto& hub = SenseHub::Get();
        Settings prefs("echoear_sense");
        hub.motion_enabled = prefs.GetBool("motion", true);
        hub.sound_enabled = false;
        hub.reverse = prefs.GetBool("reverse", false);
        hub.zero_x = prefs.GetInt("zero_x", 0);
        hub.zero_y = prefs.GetInt("zero_y", 0);
        i2c_device_config_t cfg{};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = 0x6b;
        cfg.scl_speed_hz = 400000;
        if (i2c_master_bus_add_device(i2c_bus_, &cfg, &imu_) != ESP_OK)
            return;
        uint8_t reg = 0, id = 0;
        auto write = [this](uint8_t r, uint8_t value) {
            uint8_t b[] = {r, value};
            return i2c_master_transmit(imu_, b, 2, 30) == ESP_OK;
        };
        // QMI8658: identity 0x05, auto-increment, little endian, +/-4g at 62.5Hz.
        bool ready = i2c_master_transmit_receive(imu_, &reg, 1, &id, 1, 30) == ESP_OK && id == 5;
        ready = ready && write(0x08, 0) && write(0x02, 0x40) && write(0x03, 0x17) && write(0x08, 1);
        hub.imu_ready = ready;
        ESP_LOGI(TAG, "QMI8658 ready=%d id=0x%02x", ready, id);
        xTaskCreate(
            [](void* arg) {
                auto self = static_cast<EchoEarViewe175*>(arg);
                auto& h = SenseHub::Get();
                float px = 0, py = 0, pz = 0;
                int samples = 0, moving = 0;
                int64_t last_event = 0;
                while (true) {
                    auto state = Application::GetInstance().GetDeviceState();
                    if (esp_timer_get_time() < 15000000 ||
                        (state != kDeviceStateIdle && state != kDeviceStateListening &&
                         state != kDeviceStateSpeaking)) {
                        h.sound.enabled = false;
                        vTaskDelay(pdMS_TO_TICKS(200));
                        continue;
                    }
                    h.sound.enabled = false;
                    uint8_t r = 0x35, b[6]{};
                    if (h.imu_ready &&
                        i2c_master_transmit_receive(self->imu_, &r, 1, b, 6, 30) == ESP_OK) {
                        auto value = [&](int i) {
                            return int16_t(uint16_t(b[i]) | (uint16_t(b[i + 1]) << 8)) / 8192.0f;
                        };
                        float x = value(0), y = value(2), z = value(4);
                        h.ax = int(x * 1000);
                        h.ay = int(y * 1000);
                        h.az = int(z * 1000);
                        if (samples++ < 20) {
                            px = x;
                            py = y;
                            pz = z;
                        }
                        float change = std::sqrt((x - px) * (x - px) + (y - py) * (y - py) +
                                                 (z - pz) * (z - pz));
                        px = px * 0.85f + x * 0.15f;
                        py = py * 0.85f + y * 0.15f;
                        pz = pz * 0.85f + z * 0.15f;
                        moving = change > 0.4f ? moving + 1 : 0;
                        int64_t now = esp_timer_get_time();
                        if (h.motion_enabled && samples > 20 && moving >= 3 &&
                            now - last_event > 4000000 && state == kDeviceStateIdle) {
                            last_event = now;
                            moving = 0;
                            bool shake = change > 1.0f;
                            h.gesture = shake ? 2 : 1;
                            Application::GetInstance().Schedule([self, shake] {
                                if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle)
                                    return;
                                self->WakeScreen();
                                self->display_->MotionReaction(shake);
                            });
                        }
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            },
            "echoear_sense", 4096, this, 1, nullptr);
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.data4_io_num = GPIO_NUM_NC;
        buscfg.data5_io_num = GPIO_NUM_NC;
        buscfg.data6_io_num = GPIO_NUM_NC;
        buscfg.data7_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = EXAMPLE_PIN_NUM_LCD_PCLK;
        buscfg.data0_io_num = EXAMPLE_PIN_NUM_LCD_DATA0;
        buscfg.data1_io_num = EXAMPLE_PIN_NUM_LCD_DATA1;
        buscfg.data2_io_num = EXAMPLE_PIN_NUM_LCD_DATA2;
        buscfg.data3_io_num = EXAMPLE_PIN_NUM_LCD_DATA3;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        buscfg.flags = SPICOMMON_BUSFLAG_QUAD;
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            Application::GetInstance().Schedule([this]() {
                if (screen_off_.load()) {
                    WakeScreen();
                    return;
                }
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting) {
                    EnterWifiConfigMode();
                    return;
                }
                app.ToggleChatState();
            });
        });
#if CONFIG_USE_DEVICE_AEC
        boot_button_.OnDoubleClick([]() {
            Application::GetInstance().Schedule([]() {
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateIdle) {
                    app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
                }
            });
        });
#endif
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS;
        io_config.dc_gpio_num = GPIO_NUM_NC;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 32;
        io_config.lcd_param_bits = 8;
        io_config.flags.quad_mode = true;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        const co5300_vendor_config_t vendor_config = {
            .init_cmds = &vendor_specific_init[0],
            .init_cmds_size = sizeof(vendor_specific_init) / sizeof(co5300_lcd_init_cmd_t),
            .flags = {
                .use_qspi_interface = 1,
            }};

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.vendor_config = (void*)&vendor_config;
        ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(panel_io, &panel_config, &panel));
        // Software rotation keeps the CO5300 native RAM window: columns 6..471.
        esp_lcd_panel_set_gap(panel, 0x06, 0);
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, false);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);
        display_ = new EchoEarDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                      DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                      DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        backlight_ = new CustomBacklight(panel_io);
        backlight_->RestoreBrightness();
    }

    void InitializeTouch() {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH,
            .y_max = DISPLAY_HEIGHT,
            .rst_gpio_num = PIN_NUM_TOUCH_RST,
            .int_gpio_num = PIN_NUM_TOUCH_INT,
            .levels =
                {
                    .reset = 0,
                    .interrupt = 0,
                },
            .flags =
                {
                    .swap_xy = 0,
                    .mirror_x = 0,
                    .mirror_y = 0,
                },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = {};
        tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
        tp_io_config.control_phase_bytes = 1;
        tp_io_config.lcd_cmd_bits = 8;
        tp_io_config.flags.disable_control_phase = 1;
        tp_io_config.scl_speed_hz = 400 * 1000;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle));
        ESP_LOGI(TAG, "Initialize touch controller");
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &tp));
        // Idle CST816S frames may have no ACK. Treat them as release, not a fatal error.
        lvgl_port_lock(0);
        lv_indev_t* indev = lv_indev_create();
        assert(indev);
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(indev, lv_display_get_default());
        touch_panel_ = tp;
        lv_indev_set_user_data(indev, this);
        lv_indev_set_read_cb(indev, [](lv_indev_t* input, lv_indev_data_t* data) {
            auto self = static_cast<EchoEarViewe175*>(lv_indev_get_user_data(input));
            auto touch = self->touch_panel_;
            data->state = LV_INDEV_STATE_RELEASED;
            const esp_err_t result = esp_lcd_touch_read_data(touch);
            if (result != ESP_OK) {
                self->suppress_wake_touch_ = false;
                return;
            }
            esp_lcd_touch_point_data_t point = {};
            uint8_t count = 0;
            if (esp_lcd_touch_get_data(touch, &point, &count, 1) == ESP_OK && count) {
                if (self->screen_off_.load()) {
                    if (!self->suppress_wake_touch_)
                        Application::GetInstance().Schedule([self]() { self->WakeScreen(); });
                    self->suppress_wake_touch_ = true;
                }
                if (self->suppress_wake_touch_)
                    return;
                data->point.x = point.x;
                data->point.y = point.y;
                data->state = LV_INDEV_STATE_PRESSED;
                const int64_t now = esp_timer_get_time();
                if (now - self->last_touch_wake_ >= 500000) {
                    self->last_touch_wake_ = now;
                    Application::GetInstance().Schedule([self]() { self->WakeScreen(); });
                }
            } else {
                self->suppress_wake_touch_ = false;
            }
        });
        lvgl_port_unlock();
        ESP_LOGI(TAG, "Touch panel initialized successfully");
    }

    // 初始化工具
    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool(
            "self.apps.open",
            "Open a local screen app. app: calculator, 2048, sudoku, stopwatch, alarm, weather, "
            "senses, storage, apps. Does not modify saved games or settings.",
            PropertyList({Property("app", kPropertyTypeString)}),
            [this](const PropertyList& properties) {
                auto name = properties["app"].value<std::string>();
                int page = -1;
                const std::pair<const char*, int> pages[] = {
                    {"calculator", 81}, {"2048", 80},    {"sudoku", 3},
                    {"stopwatch", 92},  {"alarm", 1},    {"weather", 2},
                    {"senses", 90},     {"storage", 91}, {"apps", 8}};
                for (const auto& entry : pages)
                    if (name == entry.first)
                        page = entry.second;
                if (page < 0)
                    return false;
                Application::GetInstance().Schedule([this, page] {
                    WakeScreen();
                    display_->OpenLocalApp(page);
                });
                return true;
            });
        mcp_server.AddTool("self.system.set_update_feed",
                           "Set the HTTPS manifest URL for this custom EchoEar firmware. Does not "
                           "install an update.",
                           PropertyList({Property("url", kPropertyTypeString)}),
                           [this](const PropertyList& properties) {
                               auto url = properties["url"].value<std::string>();
                               if (url.rfind("https://", 0) != 0 || url.size() > 512)
                                   return false;
                               display_->SetUpdateFeed(url);
                               return true;
                           });
        mcp_server.AddTool("self.system.reconfigure_wifi",
                           "End this conversation and enter WiFi configuration mode.\n"
                           "**CAUTION** You must ask the user to confirm this action.",
                           PropertyList(), [this](const PropertyList& properties) {
                               EnterWifiConfigMode();
                               return true;
                           });
    }

public:
    EchoEarViewe175() {
        // Manufacturer schematic: LCD supply enable 40, DAC mute release 15.
        for (auto pin : {GPIO_NUM_40, GPIO_NUM_15}) {
            gpio_config_t cfg{};
            cfg.pin_bit_mask = 1ULL << pin;
            cfg.mode = GPIO_MODE_OUTPUT;
            ESP_ERROR_CHECK(gpio_config(&cfg));
            gpio_set_level(pin, 1);
        }
        InitializeCodecI2c();
        InitializeSpi();
        InitializeDisplay();
        display_->SetWifiConfigCallback([this] {
            WakeScreen();
            EnterWifiConfigMode();
        });
        display_->SetTimerWakeCallback([this] { WakeScreen(); });
        display_->SetTimerSleepCallback(
            [this] { Application::GetInstance().Schedule([this] { SetScreenOff(true); }); });
        InitializeTouch();
        InitializeButtons();
        InitializeMotion();
        InitializeTools();
        boot_button_.OnLongPress([this] {
            Application::GetInstance().Schedule([this] { SetScreenOff(!screen_off_); });
        });
        last_activity_ = esp_timer_get_time();
        xTaskCreate(
            [](void* arg) {
                auto self = static_cast<EchoEarViewe175*>(arg);
                while (true) {
                    const auto now = esp_timer_get_time();
                    if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle)
                        self->last_activity_ = now;
                    if (!self->screen_off_ && now - self->last_activity_.load() >= 60000000) {
                        self->last_activity_ = now;
                        Application::GetInstance().Schedule([self, now] {
                            if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle &&
                                self->last_activity_ == now)
                                self->SetScreenOff(true);
                        });
                    }
                    vTaskDelay(pdMS_TO_TICKS(250));
                }
            },
            "viewe_idle", 3072, this, 1, nullptr);
    }
    AudioCodec* GetAudioCodec() override {
        static NoAudioCodecSimplexPdm codec(16000, 24000, GPIO_NUM_18, GPIO_NUM_16, GPIO_NUM_17,
                                            GPIO_NUM_48, GPIO_NUM_47);
        return &codec;
    }
    Display* GetDisplay() override { return display_; }
    Backlight* GetBacklight() override { return backlight_; }
    void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER)
            WakeScreen();
        WifiBoard::SetPowerSaveLevel(level);
    }
};
DECLARE_BOARD(EchoEarViewe175);
