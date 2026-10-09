# Shared EchoEar firmware

Both boards use this single UI/application implementation. Board folders contain only hardware drivers, pins, build configuration and `echoear_target.h` capabilities. Keep new menu, pet, game and subtitle changes here; do not copy these files into a board directory.

| Target | Display rotation | Audio | Device AEC / direction | SD CLK/CMD/D0 |
|---|---|---|---|---|
| waveshare | 270 degrees | ES7210 + ES8311 | available / experimental | 2/1/3 |
| viewe | native | single PDM + PCM5100 | unavailable | 3/4/2 |

Wi-Fi settings, wake resources, UI language and app logic are shared. Board identity, hardware initialization and OTA image compatibility remain separate. Images are not interchangeable. Original UI icon licensing is in ORIGINAL_UI_LICENSE.md.

## Build and install

From the repository root, use the wrapper (it activates the existing local ESP-IDF installation):

```sh
./scripts/echoear.sh build all
# or: build waveshare / build viewe
./scripts/echoear.sh flash waveshare --port /dev/cu.usbmodem101
./scripts/echoear.sh flash viewe --port /dev/cu.usbmodem101
```

Packages are saved in `dist/echoear/waveshare` and `dist/echoear/viewe`, with independent manifest, hashes, bootloader, partitions, application and resource files. The previous package is retained as `<board>.previous`. Builds are serialized because the canonical build script owns one generated configuration/build directory; packages do not depend on that directory after creation.

The default installation updates only the application and preserves resources/settings. Use `--full` for an initial installation or partition/resource upgrade after backing up the device. Full installation writes the included partition table, bootloader, OTA selection and resources; it does not erase all flash. App-only updates assume this project's existing partition layout.

The two development-device MACs are mapped to their known physical models. A known mismatched board is always rejected. For an additional unit, verify its model and pass `--expected-mac aa:bb:cc:dd:ee:ff`. MAC/chip detection alone cannot identify the wiring of an unknown ESP32 board.

Both configs currently consume the workspace asset file `../assets/custom/assets-with-font.bin` relative to this repository. Prepare this resource in a fresh checkout before building. No Wi-Fi passwords, cloud tokens or private keys belong in packages.

## Validation

Run `python -m unittest discover -s scripts/tests -v`. The board-independent C++ tests are now in `main/echoear/tests`; compile each with C++17 and address/undefined sanitizers. Build both targets for every shared code change. Physical checks still include touch/orientation, capture/playback, wake after leaving apps, keys, and each board's optional peripherals. VIEWE hardware acceptance is pending; compiling its driver does not establish hardware compatibility.

## 中文使用入口

只维护此仓库的 `codex/echoear-unified` 主线，共享界面在 `main/echoear`。从仓库根目录运行 `./scripts/echoear.sh build waveshare` 或 `./scripts/echoear.sh build viewe`；`build all` 生成两份安装包。安装时选择对应板型，例如 `./scripts/echoear.sh flash waveshare --port /dev/cu.usbmodem101`。旧 VIEWE 工作树只保留作历史备份，不再在那里修改 UI。未知新设备须先核对商品型号，再用 `--expected-mac` 指定其地址。首次安装使用 `--full`；已有本项目分区布局的设备默认只更新应用。
