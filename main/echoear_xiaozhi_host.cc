#include <algorithm>
#include "application.h"
#include "audio/audio_codec.h"
#include "backlight.h"
#include "board.h"
#include "echoear_platform.h"
#include "echoear_target.h"

namespace echoear {
class XiaozhiHost final : public Host {
public:
    const BoardProfile& Profile() const override {
        static const BoardProfile profile{
            EchoEarTarget::kDisplayName, static_cast<int>(EchoEarTarget::kRotation),
            EchoEarTarget::kDeviceAec,   EchoEarTarget::kSoundDirection,
            EchoEarTarget::kSdClock,     EchoEarTarget::kSdCommand,
            EchoEarTarget::kSdData};
        return profile;
    }
    void Schedule(std::function<void()> callback) override {
        Application::GetInstance().Schedule(std::move(callback));
    }
    void EnableVoice(bool enabled) override {
        Application::GetInstance().SetVoiceInteractionEnabled(enabled);
    }
    int Volume() const override {
        auto codec = Board::GetInstance().GetAudioCodec();
        return codec ? codec->output_volume() : 0;
    }
    int Brightness() const override {
        auto light = Board::GetInstance().GetBacklight();
        return light ? light->brightness() : 0;
    }
    void SetVolume(int value) override {
        if (auto codec = Board::GetInstance().GetAudioCodec())
            codec->SetOutputVolume(std::clamp(value, 0, 100));
    }
    void SetBrightness(int value) override {
        if (auto light = Board::GetInstance().GetBacklight())
            light->SetBrightness(std::clamp(value, 10, 100), true);
    }
};
Host& GetHost() {
    static XiaozhiHost host;
    return host;
}
}  // namespace echoear
