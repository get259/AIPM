# PlatformIO 开发

在 VS Code 的 PlatformIO 中打开 `linx-esp32` 目录，使用 `esp32s3` 环境。

当前打包工具还需要 `intelhex`。首次构建前，将 `requirements-platformio.txt` 安装到 PlatformIO Core 的 Python 环境。在本机可运行：

```powershell
& "$env:USERPROFILE/.platformio/penv/Scripts/python.exe" -m pip install -r requirements-platformio.txt
```

```powershell
pio run -e esp32s3
pio run -e esp32s3 -t menuconfig
pio device monitor -b 115200
```

本配置保留 ESP-IDF、CMake 和 `main/idf_component.yml` 原有依赖管理，使用官方 `espressif32@6.13.0`（ESP-IDF 5.5.3），不使用 Arduino。

构建参数沿用工程的 ESP32-S3 默认值：16 MB QIO Flash、OPI PSRAM、`partitions/v2/16m.csv`。板型选择工程现有的 `bread-compact-wifi`、128x32 OLED 变体，只用于编译适配，不表示实际硬件已按此接线；不要直接据此烧录未知硬件。

`sdkconfig.defaults.platformio` 只补充板型选择；通用和芯片默认值继续来自现有 `sdkconfig.defaults` 与 `sdkconfig.defaults.esp32s3`。PlatformIO 生成的配置为 `sdkconfig.esp32s3`。修改 defaults 后，若已有该生成文件，需同步使用 menuconfig 调整或在保留个人配置后重新生成。

首次构建需要下载工具链、ESP-IDF 和组件依赖，耗时及网络需求明显高于后续增量构建。构建目录 `.pio/` 和生成配置已加入忽略规则。

此配置不修改原有联网、异常恢复、WSS、令牌、音频或设备控制功能。

构建脚本额外执行原工程的语言头文件、默认资源和嵌入音效生成步骤，补齐 PlatformIO 未自动执行的 CMake 自定义命令；无需手写或提交生成的 `main/assets/lang_config.h`。

## 融合前基线验证结果

2026-10-24 已通过 `pio run -e esp32s3`，生成 `.pio/build/esp32s3/firmware.bin`、`bootloader.bin`、分区表和 `generated_assets.bin`。链接结果为静态 RAM 60,876 字节，程序占应用分区 2,614,327 / 4,128,768 字节（63.3%）。此结果只证明该构建配置可编译，未烧录或验证实际硬件。

如果首次初始化报 `No pyvenv.cfg file` 或无法删除 IDF 环境目录，应避免 IDE 初始化和终端构建同时运行。先结束本项目的重复构建/初始化，再检查环境是否已自动完成安装，不要直接删除整个 `.platformio` 目录。

当前 `esp32s3` 配置已启用小车软件融合。业务行为、硬件引脚配置及最新验证结果见 [car-integration.md](car-integration.md) 和工程根目录 `AGENTS.md`。

配网成功页现可显示灵矽激活码，操作与实现边界见 [wifi-activation.md](wifi-activation.md)。配网组件使用项目内维护的版本，依赖缓存重新下载不会覆盖本地改动。

当前 PlatformIO 配置已关闭串口文字测试模式，启用麦克风上传及 STM32 UART1（TX=17、RX=21），自动小车控制且不播放 AI 回复时，使用原协议 realtime 持续监听，JSON 下发后不等待 tts.stop 即回到聆听。文字测试操作见 [serial-text-chat.md](serial-text-chat.md)。
