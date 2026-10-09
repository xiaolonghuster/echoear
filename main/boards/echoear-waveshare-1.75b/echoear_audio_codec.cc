#include "echoear_audio_codec.h"
#include "sense_hub.h"

#include <driver/i2c_master.h>
#include <driver/i2s_tdm.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <algorithm>
#include <array>
#include <cstdlib>

#define TAG "EchoEarAudioCodec"

EchoEarAudioCodec::EchoEarAudioCodec(void* i2c_master_handle, int input_sample_rate,
                                     int output_sample_rate, gpio_num_t mclk, gpio_num_t bclk,
                                     gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
                                     gpio_num_t pa_pin, uint8_t es8311_addr, uint8_t es7210_addr,
                                     bool input_reference, float input_gain,
                                     int reference_gain_channel, float reference_gain)
    : microphone_conditioner_(input_sample_rate) {
    output_volume_ = 85;
    duplex_ = true;                              // 是否双工
    input_reference_ = input_reference;          // 是否使用参考输入，实现回声消除
    input_channels_ = input_reference_ ? 2 : 1;  // 输入通道数
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    input_gain_ = input_gain;
    reference_gain_channel_ = reference_gain_channel;
    reference_gain_ = reference_gain;

    CreateDuplexChannels(mclk, bclk, ws, dout, din);

    // Do initialize of related interface: data_if, ctrl_if and gpio_if
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = rx_handle_,
        .tx_handle = tx_handle_,
    };
    data_if_ = audio_codec_new_i2s_data(&i2s_cfg);
    assert(data_if_ != NULL);

    // Output
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = (i2c_port_t)1,
        .addr = es8311_addr,
        .bus_handle = i2c_master_handle,
    };
    out_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(out_ctrl_if_ != NULL);

    gpio_if_ = audio_codec_new_gpio();
    assert(gpio_if_ != NULL);

    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = out_ctrl_if_;
    es8311_cfg.gpio_if = gpio_if_;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    es8311_cfg.pa_pin = pa_pin;
    es8311_cfg.use_mclk = true;
    es8311_cfg.hw_gain.pa_voltage = 3.3;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
    out_codec_if_ = es8311_codec_new(&es8311_cfg);
    assert(out_codec_if_ != NULL);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = out_codec_if_,
        .data_if = data_if_,
    };
    output_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(output_dev_ != NULL);

    // Input
    i2c_cfg.addr = es7210_addr;
    in_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(in_ctrl_if_ != NULL);

    es7210_codec_cfg_t es7210_cfg = {};
    es7210_cfg.ctrl_if = in_ctrl_if_;
    es7210_cfg.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3;
    in_codec_if_ = es7210_codec_new(&es7210_cfg);
    assert(in_codec_if_ != NULL);

    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    dev_cfg.codec_if = in_codec_if_;
    input_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(input_dev_ != NULL);

    ESP_LOGI(TAG, "BoxAudioDevice initialized");
}

EchoEarAudioCodec::~EchoEarAudioCodec() {
    ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    esp_codec_dev_delete(output_dev_);
    ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    esp_codec_dev_delete(input_dev_);

    audio_codec_delete_codec_if(in_codec_if_);
    audio_codec_delete_ctrl_if(in_ctrl_if_);
    audio_codec_delete_codec_if(out_codec_if_);
    audio_codec_delete_ctrl_if(out_ctrl_if_);
    audio_codec_delete_gpio_if(gpio_if_);
    audio_codec_delete_data_if(data_if_);
}

void EchoEarAudioCodec::CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws,
                                             gpio_num_t dout, gpio_num_t din) {
    assert(input_sample_rate_ == output_sample_rate_);

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {.sample_rate_hz = (uint32_t)output_sample_rate_,
                    .clk_src = I2S_CLK_SRC_DEFAULT,
                    .ext_clk_freq_hz = 0,
                    .mclk_multiple = I2S_MCLK_MULTIPLE_256},
        .slot_cfg = {.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                     .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                     .slot_mode = I2S_SLOT_MODE_STEREO,
                     .slot_mask = I2S_STD_SLOT_BOTH,
                     .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
                     .ws_pol = false,
                     .bit_shift = true,
                     .left_align = true,
                     .big_endian = false,
                     .bit_order_lsb = false},
        .gpio_cfg = {.mclk = mclk,
                     .bclk = bclk,
                     .ws = ws,
                     .dout = dout,
                     .din = I2S_GPIO_UNUSED,
                     .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}}};

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg =
            {
                .sample_rate_hz = (uint32_t)input_sample_rate_,
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .ext_clk_freq_hz = 0,
                .mclk_multiple = I2S_MCLK_MULTIPLE_256,
                .bclk_div = 8,
            },
        .slot_cfg = {.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                     .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                     .slot_mode = I2S_SLOT_MODE_STEREO,
                     .slot_mask = i2s_tdm_slot_mask_t(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
                                                      I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
                     .ws_width = I2S_TDM_AUTO_WS_WIDTH,
                     .ws_pol = false,
                     .bit_shift = true,
                     .left_align = false,
                     .big_endian = false,
                     .bit_order_lsb = false,
                     .skip_mask = false,
                     .total_slot = I2S_TDM_AUTO_SLOT_NUM},
        .gpio_cfg = {.mclk = mclk,
                     .bclk = bclk,
                     .ws = ws,
                     .dout = I2S_GPIO_UNUSED,
                     .din = din,
                     .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}}};

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(rx_handle_, &tdm_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    ESP_LOGI(TAG, "Duplex channels created");
}

void EchoEarAudioCodec::SetOutputVolume(int volume) {
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, volume));
    AudioCodec::SetOutputVolume(volume);
}

void EchoEarAudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (enable == input_enabled_) {
        return;
    }
    if (enable) {
        microphone_conditioner_.Reset();
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 4,
            .channel_mask = 0x0f,
            .sample_rate = (uint32_t)output_sample_rate_,
            .mclk_multiple = 0,
        };
        if (input_reference_) {
            fs.channel_mask |= ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
        }
        ESP_ERROR_CHECK(esp_codec_dev_open(input_dev_, &fs));
        ESP_ERROR_CHECK(esp_codec_dev_set_in_channel_gain(
            input_dev_, (ES7210_SEL_MIC1 | ES7210_SEL_MIC2), input_gain_));
        if (input_reference_ && reference_gain_channel_ >= 0) {
            // ES7210 gain masks use physical MIC numbering, which differs
            // from the TDM slot order (MIC1, MIC3, MIC2, MIC4).
            ESP_ERROR_CHECK(esp_codec_dev_set_in_channel_gain(
                input_dev_, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(reference_gain_channel_),
                reference_gain_));
        }
    } else {
        ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    }
    AudioCodec::EnableInput(enable);
}

void EchoEarAudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (enable == output_enabled_) {
        return;
    }
    if (enable) {
        // Play 16bit 1 channel
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = 0,
            .sample_rate = (uint32_t)output_sample_rate_,
            .mclk_multiple = 0,
        };
        ESP_ERROR_CHECK(esp_codec_dev_open(output_dev_, &fs));
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, output_volume_));
    } else {
        ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    }
    AudioCodec::EnableOutput(enable);
}

int EchoEarAudioCodec::Read(int16_t* dest, int samples) {
    if (!input_enabled_ || samples <= 0 || samples % input_channels_ != 0)
        return 0;
    // Four physical TDM slots: MIC1, REF(MIC3), MIC2, unused.
    // Feed the AFE a dual-microphone average followed by the unfiltered reference.
    std::array<int16_t, 256 * 4> tdm;
    static int peaks[4] = {};
    static unsigned frame_count = 0;
    static int conditioned_peak = 0;
    static unsigned clipped = 0;
    static int64_t last_report = 0;
    const int frames = samples / input_channels_;
    for (int offset = 0; offset < frames; offset += 256) {
        const int count = std::min(256, frames - offset);
        if (esp_codec_dev_read(input_dev_, tdm.data(), count * 4 * sizeof(int16_t)) != ESP_OK) {
            return 0;
        }
        SenseHub::Get().sound.Feed(tdm.data(), count);
        for (int i = 0; i < count; ++i) {
            for (int ch = 0; ch < 4; ++ch)
                peaks[ch] = std::max(peaks[ch], std::abs(static_cast<int>(tdm[i * 4 + ch])));
            const int16_t microphone = (static_cast<int32_t>(tdm[i * 4]) + tdm[i * 4 + 2]) / 2;
            const int16_t conditioned = microphone_conditioner_.Process(microphone);
            dest[(offset + i) * input_channels_] = conditioned;
            conditioned_peak = std::max(conditioned_peak, std::abs(static_cast<int>(conditioned)));
            if (std::abs(static_cast<int>(conditioned)) >= 28000)
                ++clipped;
            if (input_reference_)
                dest[(offset + i) * input_channels_ + 1] = tdm[i * 4 + 1];
        }
    }
    frame_count += frames;
    const int64_t now = esp_timer_get_time();
    if (now - last_report >= 2000000) {
        ESP_LOGI(TAG,
                 "Capture frames=%u peaks MIC1=%d REF=%d MIC2=%d NC=%d conditioned=%d limited=%u",
                 frame_count, peaks[0], peaks[1], peaks[2], peaks[3], conditioned_peak, clipped);
        auto& hub = SenseHub::Get();
        auto& direction = hub.sound;
        const bool reverse = hub.ReverseSoundAxis();
        ESP_LOGI(TAG,
                 "SoundDir enabled=%d active=%d reverse=%d L=%u C=%u R=%u low=%u weak=%u edge=%u "
                 "rms_max=%d/%d lag=%d corr_raw=%d",
                 int(hub.sound_enabled.load()), int(direction.enabled.load()), int(reverse),
                 direction.windows[reverse ? 5 : 3], direction.windows[4],
                 direction.windows[reverse ? 3 : 5], direction.windows[0], direction.windows[1],
                 direction.windows[2], direction.max_rms_a, direction.max_rms_b,
                 direction.lag.load(), direction.raw_correlation);
        direction.windows.fill(0);
        direction.max_rms_a = direction.max_rms_b = 0;
        for (auto& peak : peaks)
            peak = 0;
        frame_count = 0;
        conditioned_peak = 0;
        clipped = 0;
        last_report = now;
    }
    return samples;
}

int EchoEarAudioCodec::Write(const int16_t* data, int samples) {
    if (!output_enabled_)
        return 0;
    if (output_enabled_) {
        if (esp_codec_dev_write(output_dev_, (void*)data, samples * sizeof(int16_t)) != ESP_OK)
            return 0;
    }
    return samples;
}
