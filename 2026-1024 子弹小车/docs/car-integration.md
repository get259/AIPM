# 小车业务融合

本工程保留 linx 的联网、OTA、WSS/令牌、音频采集/Opus 与异常恢复实现，只接入小车业务。PlatformIO 的 `esp32s3` 环境启用 `CAR_CONTROL`；其他构建默认关闭。

## 数据路径

麦克风 → 原音频/协议链路 → tts.start → 累积 sentence_start.text → 正常 tts.stop → 严格 JSON 校验 → 状态字节 → 低有效 LED、STM32 UART。

仅正常且完整的 AI 回复参与解析；`is_aborted` 必须明确为 false。缺少该标记、空回复、溢出或中断回复不执行动作。完整回复格式错误时应用全零状态，合法全 off 也发送零状态。最大回复 8192 字节；断线时清理未完成文本，不新增异常停止或重试策略。

平台需要返回以下结构的纯 JSON，不附加说明或 Markdown 围栏：

```json
{"forward":"on","backward":"off","turn_left":"on","turn_right":"off","fire":"on"}
```

五个字段必须齐全，值为字符串 on/off。允许乱序和空白，不允许未知键、重复键或转义键；各动作允许组合。用户识别文字与表情事件不参与控制。

固件同时兼容 `turn_left`/`turnleft` 和 `turn_right`/`turnright`，左右字段可混用两种写法。别名属于同一个字段；同一对象同时出现 `turn_left` 与 `turnleft`（或右转的两种写法），即使值相同也会拒绝，不能借助别名重复或缺少其他动作字段。

当前串口文字测试模式只汇总、校验并打印状态，不执行动作，详见 [serial-text-chat.md](serial-text-chat.md)。

## 状态与通信

| bit | 含义 |
| --- | --- |
| 0 | 前进 |
| 1 | 后退 |
| 2 | 左转 |
| 3 | 右转 |
| 4 | 本地 LED，on 为低电平 |
| 5～7 | 保留为 0 |

UART 使用 115200、8N1，发送完整四字节 `AA 55 STATE CRC8`。CRC 参数为 poly=07、init=00、refin/refout=false、xorout=00，覆盖前三字节。

- 前进、左转、LED：`AA 55 15 E9`。
- 全部关闭：`AA 55 00 82`。
- 全部开启：`AA 55 1F DF`。

STM32 只使用运动位并执行运动；本工程不实现运动时长、动作互斥、状态周期发送或 STM32 接收看门狗。

## 启用硬件

当前 UART TX/RX 和业务 LED 均为 -1，硬件输出关闭。软件仍解析、编码并输出日志，不能把这理解为已经向 STM32 发送或实际点亮 LED。

使用 `pio run -e esp32s3 -t menuconfig`，进入 Xiaozhi Assistant → Enable car JSON control，配置：

- CAR_UART_PORT：1 或 2，默认 1；若已被其他驱动占用，小车 UART 不启用。
- CAR_UART_TX_GPIO、CAR_UART_RX_GPIO：两者均配置且有效时启用 UART。
- CAR_LED_GPIO：配置为有效输出 GPIO 时启用低有效 LED。

不得直接沿用未经确认的示例引脚。必须核对与所选板型的 I²S、OLED、按键、状态灯、示例 LAMP_GPIO 等不冲突。模块会拒绝自己的 UART TX/RX 重复、UART 与业务 LED 重复配置；不会自动接管其他板级外设。

当前仍使用已编译通过的 bread-compact-wifi 128x32 板型，因此麦克风、屏幕、按键的实际硬件适配仍待接线确认，不表示固件已经适合直接烧入任意 ESP32-S3 板。

## 监听和声音

CAR_AUTO_LISTEN 默认启用：激活完成后调用原会话入口，默认无 AEC 时采用 AutoStop；正常回复结束后复用原状态机继续监听。回复期间不持续上传，不开启新的 Realtime/AEC 模式。断开会话后仍沿用 linx 原恢复方式，不额外加入自动重连。

CAR_REPLY_AUDIO 默认关闭，只忽略服务端下行语音，避免播读动作 JSON；原有系统提示音保持不变。INMP441 仍走 linx 现有 I²S 音频驱动，MAX98357 的最终用途未进一步指定。

## 验证方式

```powershell
python -m pip install --target .cache/car-test-tools -r test-native/requirements.txt
pio test -d test-native -e car_tests
pio run -e esp32s3
```

电脑端测试覆盖 32 种动作组合、非法/截断 JSON、分段汇总、重复事件、中断/断线清理、缓冲上限与 CRC 固定向量。固件编译验证只证明软件可构建；平台返回事件、连续多轮录音、UART 电气输出及 LED 电平仍需要实机联调。

2026-10-24 验证结果：电脑端 8 组测试全部通过；ESP32-S3 干净目录重新构建成功。静态 RAM 69,084 字节（21.1%），程序占应用分区 2,617,291 字节（63.4%），固件为 `.pio/build/esp32s3/firmware.bin`。未烧录或实机测试。
