# EchoEar VIEWE SmartRing 1.75 port

Based on verified Waveshare source commit 419bff6, with an independent board identity and hardware driver. Target identified by factory firmware SKU viewe-uedx46460-1.75; ESP32-S3 R8, 16MB Flash.

Manufacturer source: https://github.com/VIEWESMART/VIEWE-SMARTRING (main schematic/ and examples/esp-idf/lvgl_squareline_port_15_175). CO5300 QSPI, CST816S touch, QMI8658, single PDM microphone (48 clock/47 data), PCM5100 output (18 BCLK/16 WS/17 data/15 unmute). LCD supply enable 40. SD 1-bit pins: CLK3/CMD4/D0=2.

Single microphone: direction tracking disabled; no physical AEC reference input. No Waveshare PMIC or expander writes. BOOT click chats, long press toggles screen; touch/wake restores it. Battery state is unavailable until a verified measurement interface is implemented.

Uses the v0.18.10 UI and apps with independent assets/build directory. Initial adaptation requires compile and hardware acceptance; not a drop-in Waveshare binary. Factory flash is backed up outside Git.


## Unified source tree

UI, apps and tests now live in `external/echoear-core/`; this folder owns only this board's hardware adaptation. See [shared build and installation guide](../../../docs/echoear.md). Use `python scripts/echoear.py build all` from the repository root to produce separate packages from one source tree. The old VIEWE worktree is a historical reference, not the development entry point.
