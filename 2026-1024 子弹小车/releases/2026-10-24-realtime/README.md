# 语音小车联调发布包：2026-10-24-realtime

包含 ESP32-S3 与 STM32F103 两端固件；源代码在发布仓库同级的 `linx-esp32/` 和 `stm32_motion/`，不含嵌套 Git 仓库、依赖缓存、个人凭据或构建缓存。此发布包是日期标记快照，ESP32 应用内部版本仍沿用 2.1.0。

## 本次行为

ESP32 启用麦克风和 UART1，AI 完整 JSON 严格校验通过后立即发送 AA 55 STATE CRC8，同轮最多一次。自动小车控制且不播放 AI 回复时使用平台 realtime 持续上传音频，JSON 下发后回到聆听，不再等待 tts.stop。旧 stop 按轮次顺序消耗，不清空下一句的半包。STT 仅划分输入轮次，不从识别文字直接执行运动。

平台协议只给出会话 ID；当前关联依赖每轮一个 stop、同连接 stop 按轮次顺序到达、STT 先于对应回复。跨轮重复/乱序结束事件与真实连续识别效果仍需实机核对。

## 当前编译接线

| 连接 | ESP32-S3 | STM32F103 |
| --- | --- | --- |
| 指令发送 | GPIO17 TX | PA10 RX |
| 预留回传 | GPIO21 RX | PA9 TX |
| 共地 | GND | GND |

INMP441：BCLK=GPIO5、WS=GPIO4、SD=GPIO6、L/R 接 GND（左声道），VDD=3V3。业务 LED 尚未配置。

注意：用户曾提到实际使用 GPIO10/12，当前固件配置仍是 GPIO17/21；接线需与此表一致，或确认真实 GPIO 后改配置重新编译。

## ESP32-S3 烧录文件

目标需匹配 16MB Flash / OPI PSRAM 配置。按下表分别写入，不使用全片擦除；固件文件保留自身的 Flash 模式与频率头。

| 地址 | 文件 |
| --- | --- |
| 0x000000 | esp32s3/bootloader.bin |
| 0x008000 | esp32s3/partitions.bin |
| 0x00D000 | esp32s3/ota_data_initial.bin |
| 0x020000 | esp32s3/firmware.bin |
| 0x800000 | esp32s3/generated_assets.bin |

全套文件用于首次安装或恢复；已有同分区布局的设备更新应用时仅更新 0x20000 的 firmware.bin。写入 ota_data_initial.bin 会重新初始化 OTA 选择信息。保留 NVS 区域可保留已有配网信息；平台绑定按项目原流程处理。

也可从源代码构建并上传：进入 linx-esp32，运行 `pio run -e esp32s3 -t upload --upload-port COM5`，COM5 按实机端口替换。

## STM32F103 烧录

firmware.bin 起始地址为 0x08000000；firmware.hex 已包含地址，可用于支持 HEX 的下载工具。沿用 stm32_motion 原板自动串口下载配置，或使用实物支持的调试接口。CH340 TX 与 ESP32 TX 不能同时驱动 STM32 PA10，按原工程说明隔离线路。

STM32 源码沿用已有提交，不新增动作计时、心跳或通信超时停车。运动会持续到下一条有效动作；停止帧为 AA 55 00 82。

## 验证状态

ESP32：19 项业务与 5 项串口输入测试通过，固件构建通过。STM32：构建与 ARM 模拟逻辑测试通过。发布包文件均记录 SHA-256；source-snapshots.json 记录对应源码快照哈希。

新连续监听固件尚未由助手烧录；平台 realtime、连续前进/停止、串口电气收发和机械延迟未完成实机验证。
