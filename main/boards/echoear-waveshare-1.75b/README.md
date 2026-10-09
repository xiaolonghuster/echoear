# 喵伴 · Waveshare ESP32-S3-Touch-AMOLED-1.75-B

独立板型 `echoear-waveshare-1.75b`，基于 Waveshare 1.75 实现，接入小智服务。
请勿使用 1.75C 的固件或引脚。Flash 16 MB、八线 PSRAM 8 MB。

- CO5300 466×466 QSPI，CST9217 触摸；v0.4 使用板型独立的猫咪圆屏界面。
- ES8311 扬声器、ES7210 麦克风，24 kHz 双工，麦克风 30 dB，扬声器默认 85%。
- 四槽 TDM 实际顺序 MIC1 / REF(MIC3) / MIC2 / NC；上送 MIC1/MIC2 的平均信号和 REF。
- 参考通道物理 MIC3 增益 0 dB；AFE 回声消除已配置，效果尚待实机验证。
- 仅选择 MIC1/2/3；PA 供电模型为 3.3 V。
- 不写 AXP2101 充电及电源轨寄存器，不自动关机；电池状态只读。
- 点击屏幕表情区域切换聊天；BOOT 单击切换聊天；启动期间单击进入配网；空闲时双击切换 AEC。
- 内部 Flash 存资源，不要求 microSD。无需扣子 Bot ID/OAuth 私钥。

从工作空间运行 `scripts/build-xiaozhi.sh`。需 ESP-IDF >=6.0.1。
本板型独立报告 OTA 身份，不能用通用板型的在线固件覆盖。

v0.2 已烧录并由串口确认进入配网模式；触摸空闲读不再触发重启。
需实测：显示方向、触摸坐标、联网和账号绑定、
唤醒、录音/播报音量、打断和 AEC、断网重连、电池状态。
猫咪由 LVGL 几何图形绘制，支持眨眼及状态动画，不是喵伴原版动画资源。
回复卡片可滚动，配网和激活消息上移到圆屏中央。语音无回应仍待排查。

UI v0.5：底部齿轮打开设置，音量 0–100%、亮度 10–100%，每次调节 5%。
设置调用现有持久化接口；硬件写入由应用任务执行。点击完成返回首页。
新增启动、连接、激活、配网、更新和异常提示；状态条不是实测音量。
验收：打开/关闭设置、连点加减、重启保留设置、圆屏裁切和字幕滚动需实机确认。

自定义资源：板型构建使用工作空间 `assets/custom/assets-with-font.bin`。当前包含唤醒模型、30px 中文字体、21 张表情与基础配色；板型仍使用原生猫脸，资源表情尚未接入显示。更新此文件后须重新构建并核对 flash_args，不要使用旧的默认资源烧录包。

请从工作空间 `scripts/build-xiaozhi.sh` 构建；该入口从原始 `assets/custom/assets.bin` 自动补齐缺失字体。用户原始文件保持不变。

v0.6 收音调理试验版：针对实测说话电平偏低，混合麦克风通道在送入 AFE 前经过 80Hz 高通、16 倍数字增益和 ±28000 限幅。参考通道不变。诊断日志增加调理后峰值与限幅计数；识别、噪声、打断及 AEC 效果需要实机复测。

v0.7 下拉设置：移除首页底部设置按钮；从顶部状态区（上方 112px）向下滑打开音量/亮度面板，上滑或点击“收起”关闭，200ms 滑入/滑出动画。字幕区下滑不触发设置。手势后等待手指释放，避免触发底层猫咪/按钮点击。实机手势待验证。

v0.8 外观与休眠：下拉设置点击“外观”切换原生猫咪/主题资源表情，选择保存到 `echoear_ui/theme_face`。未知表情回退 neutral；资源缺失回退猫咪。主题模式读取主题配色及可选背景图，图片按圆屏区域缩放。空闲 60 秒沿用现有省电计时器，暂停眨眼，触摸会在应用任务唤醒并恢复亮度。修复清空/隐藏字幕后的首页提示恢复。


### v0.9 更多设置

下拉面板底部新增“更多”，二级面板提供 AEC、字幕和版本/IP 信息，可返回或上滑收起。AEC 操作通过 Application::Schedule 执行，并关闭当前对话通道；不持久化 AEC，重启仍为默认开启。字幕开关保存到 echoear_ui/hide_subtitle，加载资源包时以用户选择为准。设备信息仅显示局域网地址，不显示 Wi-Fi 密码、令牌或身份凭据。

实际按钮交互、字幕选择重启保留和 AEC 听感仍需设备人工验证。


### v0.10 菜单与界面语言

主下拉面板保留音量、亮度和更多，移除收起按钮，统一上滑关闭。外观移入更多，与 AEC、字幕和语言同页；设备信息单独打开，返回不关闭整个抽屉。

点击“语言：中文 / Language: English”即时切换菜单、首页提示与常用状态，NVS `echoear_ui/english` 保存选择。该选项是本地界面语言，不改变云端角色、识别及回复语言；底层固件通知、配网网页和内置语音提示仍使用构建语言。自定义资源无需重新下载。

验证：ESP-IDF 6.0.1 构建与 C++ 格式检查通过；设备交互、中英文排版和断电保存需实际确认。


### v0.11 猫咪表情

原生猫咪新增日常、开心、委屈、生气、惊讶、困倦、眨眼、喜爱、疑惑九类神态，支持云端情绪别名映射。眉毛、眼睛高光、腮红、胡须、哭泣落泪、嘴形及耳朵动作组合变化；使用现有 120ms 动画定时器和预先创建的对象，不加载额外资源。下载的主题表情模式不受影响。待机时长按猫咪可轮流预览，短按仍开启对话；长按在对话中不预览。

编译后仍需实机确认长按、各表情视觉效果与语音回归。


### v0.12 息屏与唤醒

短按 PWR（EXIO4，高电平有效，90ms 消抖，按下不足1.2秒后释放）切换显示；只屏蔽 AMOLED 亮度输出，不关闭音频输入、WakeNet 或电源。待机空闲60秒自动息屏，同样适用于 USB 无电池供电。触摸第一下只亮屏，BOOT 在息屏时只亮屏；唤醒词建立对话进入 PERFORMANCE 时恢复屏幕。亮度屏蔽不写 NVS，保留原亮度与运行期间亮度修改。长按不定义软件关机行为。

验证：IDF 6.0.1 构建和格式检查通过。PWR 实物消抖、息屏唤醒词、触摸恢复和60秒超时仍需实机验收。


### v0.13 网络管理

更多 → 设备信息显示固件版本、Wi-Fi 信号强度和局域网 IP；打开时刷新，可见时每3秒更新，断开时显示离线提示。新增重新配网入口及继续/取消确认，确认后结束当前对话并调用板级既有配网流程；不主动清除已保存凭据。配网入口通过板级回调连接，无公共核心改动。

已有 v0.12 PWR 息屏与语音唤醒获得用户实测确认。本版配网整流程需用户在手机上验收，自动验证不主动断开当前网络。


### v0.14 本地倒计时

下拉设置新增倒计时，1–120分钟，支持开始/取消、后台倒计时与到期亮屏。使用单调时钟，独立于猫咪动画计时器，息屏仍运行。到期一次性提醒；待机播放短提示音，对话中仅视觉提醒。返回或上滑不取消，重启/断电取消计时。中英文界面均支持。

主机边界测试覆盖到期前一微秒、到期时刻、只触发一次、取消、120分钟、延迟轮询与重设。实际屏幕、提示音和息屏到期行为仍需实机确认。


### v0.15 宠物互动

下拉设置 → 宠物互动，或长按首页猫咪/主题表情打开互动面板；短按首页仍启动聊天。摸摸、喂食、逗猫分别触发亲昵、咀嚼、张望跳动反馈，8秒后恢复；主题资源模式使用对应已有表情。提示文字双语，亲密度每次增加1/2/3，最高100，保存至 echoear_ui/affection。互动间隔至少3秒；对话中提示先结束对话，避免覆盖语音情绪。本版替代旧长按轮播表情行为。

这是一轮轻量本地互动，没有饥饿衰减、物品库存、声音反馈或复杂成长系统。实机互动显示、长按及亲密度重启保留仍需验收。


### v0.18.10 saved baseline (2026-10-09)

The earlier sections describe historical milestones. Current firmware uses Xiaozhi conversation, original double-eye animations by default, a settings drawer, Chinese/English menus, pet interactions, timers, alarms, Shenzhen weather, Sudoku, 2048, calculator and stopwatch. Apps, motion interaction, and screen-off voice wake were confirmed by the user. Sound direction remains experimental: physical left/right tests have not passed.

Display: 270-degree software rotation, CO5300 native RAM offset (6, 0). The user confirmed the bottom white artifact is gone. Eyes are raised 20px; subtitles fit two font-dependent lines with sentence-paced scrolling. Temporary on-screen direction diagnostics are removed; serial counters remain.

Validation: IDF 6.0.1 build, 98 host tests, successful flash and startup. Board model tests are in tests/; compile each using c++ -std=c++17 -fsanitize=address,undefined and run.

Public OTA awaits ICP filing for vocat.xiaolong.xyz. Physical SD tests require a card; motor/base features require hardware; Coze is excluded. Never flash this board binary onto another board without checking peripheral types and pin assignments.


### v0.19.0 original UI navigation (2026-10-09)

Reference: https://espressif.craft.me/BBkCPR3ZaoLCV8 and ESP-Brookesia release/v0.6 at b22c488f50bafe53342c8e171081bd736396ef58. Adapted to the 466px circular Waveshare display, retaining Xiaozhi and validated hardware drivers. Original icon attribution: ORIGINAL_UI_LICENSE.md.

- Face: tap to chat, hold to open the two-icon paged launcher.
- Launcher: swipe horizontally; five seconds idle returns to the face.
- Bottom gesture strip: swipe up from apps to launcher, then up to face.
- Top control center: Wi-Fi, sound and brightness icons. Tap toggles/cycles; hold opens the corresponding settings page. Swipe up dismisses.
- Settings: Wireless, Media, Input and More grouped lists, nested navigation.
- WLAN: enable, scan, select SSID, screen keyboard/password and SoftAP setup.
- Sound/display: sliders, AEC, appearance and subtitle controls.
- AI Profile: current Xiaozhi service and platform configuration guidance; no Coze role selector.
- Voice: suspended in system apps, restored on return to face or screen-off.
- Existing pet, weather, alarm, Sudoku, 2048, calculator, countdown and stopwatch remain available through the launcher.

This adapts UI/navigation without replacing the runtime with Brookesia. Existing extension apps retain their internal layouts. POS and USB networking examples are not included; Bluetooth audio and USB disk mode are explicitly unavailable. SD, battery and motor-base functions still need their hardware.

CONFIG_ECHOEAR_UI_REVIEW is explicitly disabled in release config. The opt-in development mode captures actual rendered pages as serial JPEG records and must never be enabled in normal releases.

Validation: hardware framebuffer captures of launcher, settings, sound, display, AI profile, password keyboard, clock and quick controls. Physical gestures, changing Wi-Fi credentials, and wake/conversation after leaving apps require user acceptance; screenshots do not validate those interactions. Baseline v0.18.10 remains on its original branch.


## Unified source tree

UI, apps and tests now live in `main/echoear/`; this folder owns only this board's hardware adaptation. See [shared build and installation guide](../../echoear/README.md). Use `python scripts/echoear.py build all` from the repository root to produce separate packages from one source tree. The old VIEWE worktree is a historical reference, not the development entry point.
