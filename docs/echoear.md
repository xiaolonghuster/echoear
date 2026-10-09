# EchoEar：固件与公共模块

## 两个仓库

- [echoear](https://github.com/xiaolonghuster/echoear)：小智基础框架、硬件驱动、板型配置、主机接口实现、编译及发布。
- [echoear-core](https://github.com/xiaolonghuster/echoear-core)：宠物/游戏/计时/导航模型、LVGL 资源与完整小智界面适配层。

固件通过 `external/echoear-core` Git 子模块固定提交。公共库包含独立核心组件和显式的小智适配层；后者仍使用小智显示、网络、状态和存储 API，不代表所有界面已与语音平台完全解耦。适配源码在固件 main 中编译，独立组件不依赖 main，避免循环依赖。板型能力、音量、亮度、任务调度和语音开关通过 `echoear::Host` 接口连接，实现在 `main/echoear_xiaozhi_host.cc`。

## 克隆和编译

```sh
git clone --recurse-submodules https://github.com/xiaolonghuster/echoear.git
cd echoear
# 已经克隆过则先执行：
git submodule update --init --recursive
./scripts/echoear.sh build all
# 也可只编译 build waveshare 或 build viewe
```

需要 ESP-IDF >=6.0.1。工具可使用工作空间已有工具链，或设置 `XIAOZHI_IDF_DIR`。资源默认放在仓库旁的 `../assets/custom/assets-with-font.bin`；新检出可从现有 Release 安装包提取该资源文件，也可修改板型配置。资源中不应包含账号令牌、私钥或 Wi-Fi 密码。

输出在 `dist/echoear/waveshare` 和 `dist/echoear/viewe`。manifest 同时记录固件提交、公共库提交、构建时是否有未提交改动和各文件 SHA-256。两块板的 OTA 身份和固件不可混用。

```sh
./scripts/echoear.sh flash waveshare --port /dev/cu.usbmodem101
./scripts/echoear.sh flash viewe --port /dev/cu.usbmodem101
```

默认仅更新应用，要求设备已使用本项目分区布局。首次安装或更新分区/资源可加 `--full`；先备份原固件。它会写入指定引导、分区、OTA 选择、应用与资源，不全片擦除。已登记的两台开发设备按 MAC 校验板型；其他设备须人工确认型号后用 `--expected-mac` 指定地址，MAC 本身不能识别硬件接线。

## 公共库升级

先在公共库完成修改、运行测试并推送，再在固件中选择该提交：

```sh
git -C external/echoear-core fetch origin
git -C external/echoear-core checkout <已验证提交或版本标签>
cmake -S external/echoear-core -B /tmp/echoear-core-tests
cmake --build /tmp/echoear-core-tests
ctest --test-dir /tmp/echoear-core-tests --output-on-failure
./scripts/echoear.sh build all
git add external/echoear-core
git commit -m "Update EchoEar core"
```

不在构建时自动拉取 main。固件 pin 更新和公共库提交是两次明确提交，因此历史版本可复现。

## 新板型，包括原版 ESP-VoCat

1. 在 `main/boards/<独立板型名>` 增加驱动和 `config.h`，不修改已有板的引脚。
2. 增加 `config.json`、Kconfig 板型及 CMake `BOARD_DIR` 映射，保持独立 OTA 身份。
3. 在该板配置中启用 `CONFIG_ECHOEAR_UI=y`，提供 `echoear_target.h`：显示名、旋转、可用能力、SD 引脚。参考已有两个板型。
4. 板级驱动创建共享的 `EchoEarDisplay`，接入触摸、音频和按键；可选能力未验证时禁用。
5. 在 `scripts/echoear.py` 增加板型别名，验证芯片、Flash、分区及资源容量后再制作安装包；新设备先核对型号和引脚，不能仅凭 ESP32-S3 判断可刷。
6. 验证显示裁切、触摸坐标、音频录放、唤醒、应用切换、按键与休眠，再公开发布。

当前界面布局针对 466×466；其他尺寸需要调整布局，不能仅更换驱动。原版 ESP-VoCat 未在本次拆分中完成驱动接入或实机验收。

## 当前板型

| 板型 | 显示旋转 | 音频 | 本机 AEC / 声音跟随 | SD CLK/CMD/D0 |
|---|---|---|---|---|
| waveshare | 270° | ES7210 + ES8311 | 已配置 / 实验功能 | 2/1/3 |
| viewe | 原生方向 | 单 PDM + PCM5100 | 不支持 | 3/4/2 |

主机工具测试：`python -m unittest discover -s scripts/tests -v`。公共模型测试位于子模块 tests，可独立运行。每次升级公共库都需构建两个板型；构建成功不能替代触摸、录音播放、唤醒和外设的实机验收。旧 VIEWE 工作树只保留作历史参考。
