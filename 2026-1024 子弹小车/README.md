# AI-powered_voice-controlled_car

智能语音小车项目，包含 ESP32-S3 与 STM32F103 两端源码和可烧录固件。

| 目录 | 用途 |
| --- | --- |
| [linx-esp32](linx-esp32/) | 麦克风上传、平台激活、AI JSON 严格校验、连续监听及 UART 动作下发 |
| [stm32_motion](stm32_motion/) | USART1 接收 AA55 状态帧、TIM4 PWM、差速运动与单次蓄力重炮灯效 |
| [发布包](releases/2026-10-25-fire/README.md) | 固件、烧录地址、接线、验证状态及 SHA-256 校验清单 |

## 当前发布：2026-10-25-fire

网页新增“发射一次 · 蓄力重炮”按钮，在输出锁内保留当前运动位并置 bit4，经原 AA55 状态帧转发 STM32；STM32 播放单次灯效，忙时忽略重复发射。麦克风授权改为取得遥控权后点击“开始收听”即向浏览器申请，平台未就绪时仍可触发授权并显示原因。完整 AI JSON 仍校验后下发，协议帧与运动位含义不变。

当前编译配置 UART1 TX=GPIO17、RX=GPIO21，分别连接 STM32 PA10、PA9，共地。麦克风 BCLK=GPIO5、WS=GPIO4、SD=GPIO6，左声道，蜂鸣器 PB0。实际接线必须与配置一致，具体见发布说明。

ESP32 固件包含本次网页改动，STM32 固件重新构建（Flash 4148、RAM 1728 字节）。相关业务、网页与流程测试记录见发布说明。历史发布包 [2026-10-24-web-remote](releases/2026-10-24-web-remote/README.md) 保留，请按同一发布包内的固件、页面与证书配套烧录。灯效亮度与节奏、蜂鸣器发声、真实手机授权和实车运动已由用户上机验证通过（2026-10-25）。

## 编译与使用

分别在对应子目录使用 PlatformIO 构建；固件与下载地址见发布包说明。

- [小车控制与连续监听](linx-esp32/docs/car-integration.md)
- [配网与激活码](linx-esp32/docs/wifi-activation.md)
- [STM32 接线与运动协议](stm32_motion/README.md)
- [文件校验清单](releases/2026-10-25-fire/SHA256SUMS.txt)

## 来源与许可

源码来自本地两个独立工程。每个源文件快照的 SHA-256、源仓库基线提交和发布元数据见发布目录 manifest.json 与 source-snapshots.json。原有版权与许可保留，ESP32 原工程见 [LICENSE](linx-esp32/LICENSE)，STM32 库版权见对应源码。
