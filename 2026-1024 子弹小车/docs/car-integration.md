# 小车业务融合

本工程保留 linx 的联网、OTA、WSS/令牌、音频采集/Opus 与异常恢复实现，只接入小车业务。PlatformIO 的 `esp32s3` 环境启用 `CAR_CONTROL`；其他构建默认关闭。

## 数据路径

麦克风 → 原音频/协议链路 → tts.start → 累积 sentence_start.text → 完整 JSON 严格校验通过 → 立即发送状态帧（每轮一次）；JSON 下发后直接回到聆听状态；tts.stop 仅做对应轮次的收尾。

语音控制在每次片段追加后，对当前全部文本执行严格校验；完整五字段 JSON 通过后立即下发，不再等待 `tts.stop` 或其 `is_aborted=false` 标记。半包、非法字段、重复字段和同片段内的尾随说明均不能提前触发。每轮最多下发一次，合法全 off 也立即发送零状态。已经下发后，后续片段、重复开始/结束及之后到达的中断不会重复执行或撤销动作；未来追加的内容无法在提前执行时预知。若尚未下发，正常完整结束的非法回复仍按原规则清零，中断、空回复、溢出则丢弃。最大回复 8192 字节；本地取消后不再处理后续片段，断线清理未完成文本。

平台需要返回以下结构的纯 JSON，不附加说明或 Markdown 围栏：

```json
{"forward":"on","backward":"off","turn_left":"on","turn_right":"off","fire":"on"}
```

五个字段必须齐全，值为字符串 on/off。允许乱序和空白，不允许未知键、重复键或转义键；各动作允许组合。用户识别文字与表情事件不参与控制。

固件同时兼容 `turn_left`/`turnleft` 和 `turn_right`/`turnright`，左右字段可混用两种写法。别名属于同一个字段；同一对象同时出现 `turn_left` 与 `turnleft`（或右转的两种写法），即使值相同也会拒绝，不能借助别名重复或缺少其他动作字段。

当前已关闭串口文字测试模式，恢复麦克风上传、自动监听和 UART 动作发送。文字测试模式的历史操作见 [serial-text-chat.md](serial-text-chat.md)。

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

当前 UART1 已启用：TX=GPIO17 → STM32 PA10（USART1 RX），RX=GPIO21 ← STM32 PA9（USART1 TX），双方共地；115200、8N1。业务 LED 仍为 -1，未启用。有效完整 AI 回复产生的状态帧会实际写入 UART，日志不代表对端已接收。

使用 `pio run -e esp32s3 -t menuconfig`，进入 Xiaozhi Assistant → Enable car JSON control，配置：

- CAR_UART_PORT：1 或 2，默认 1；若已被其他驱动占用，小车 UART 不启用。
- CAR_UART_TX_GPIO、CAR_UART_RX_GPIO：两者均配置且有效时启用 UART。
- CAR_LED_GPIO：配置为有效输出 GPIO 时启用低有效 LED。

不得直接沿用未经确认的示例引脚。必须核对与所选板型的 I²S、OLED、按键、状态灯、示例 LAMP_GPIO 等不冲突。模块会拒绝自己的 UART TX/RX 重复、UART 与业务 LED 重复配置；不会自动接管其他板级外设。

当前使用 bread-compact-wifi 128x32 板型。用户已确认 INMP441：BCLK=GPIO5、WS=GPIO4、SD=GPIO6、L/R=GND（左声道），与既有配置一致。UART17/21 与该板型的 I²S、显示、按键、状态灯及示例灯不冲突。屏幕、按键及真实采音效果仍待实机验证。

## 监听和声音

CAR_AUTO_LISTEN 默认启用。小车自动监听且不播放 AI 回复语音时，使用原协议的 realtime 模式；回复期间麦克风继续处理并上传音频，完整 JSON 校验通过立即执行并回到聆听状态，不等待 tts.stop。无需为切换监听模式启用设备端或服务端 AEC；没有新增主动 abort 流程。手动录音、纯文字测试和播放 AI 回复的配置保持原监听选择。断开会话后仍沿用 linx 原恢复方式。

CAR_REPLY_AUDIO 默认关闭，只忽略服务端下行语音，避免播读动作 JSON；原有系统提示音保持不变。INMP441 仍走 linx 现有 I²S 音频驱动，MAX98357 的最终用途未进一步指定。

## 验证方式

```powershell
python -m pip install --target .cache/car-test-tools -r test-native/requirements.txt
pio test -d test-native -e car_tests
pio run -e esp32s3
```

电脑端测试覆盖 32 种动作组合、非法/截断 JSON、分段汇总、重复事件、中断/断线清理、缓冲上限与 CRC 固定向量。固件编译验证只证明软件可构建；平台返回事件、连续多轮录音、UART 电气输出及 LED 电平仍需要实机联调。

2026-10-24 验证结果：电脑端 8 组测试全部通过；ESP32-S3 干净目录重新构建成功。静态 RAM 69,084 字节（21.1%），程序占应用分区 2,617,291 字节（63.4%），固件为 `.pio/build/esp32s3/firmware.bin`。未烧录或实机测试。

## 2026-10-24 麦克风与 STM32 联调

- 用户明确主工程为 linx-esp32，继续使用原 ESP-IDF 音频、Opus 及平台协议（自动小车控制现改用 realtime）；不采用旁边 ESP32-S3 工程的 Arduino 录音方式。
- 关闭 `CONFIG_SERIAL_TEXT_CHAT`，保持 `CONFIG_CAR_CONTROL=y`、`CONFIG_CAR_AUTO_LISTEN=y`、`CONFIG_CAR_REPLY_AUDIO=n`，配置 UART1 TX=17、RX=21。默认配置与现有生成配置同步，防止旧配置继续禁用硬件。
- 激活后进入既有自动监听；完整 JSON 校验通过立即发送状态帧，JSON 下发后不等待结束事件即回到聆听。无需输入旧工程的 start/stop 命令。
- 启动时查看 `Car: Ready` 和 `STM32 UART1 TX=GPIO17 RX=GPIO21, 115200 8N1`，动作日志中应无 `(UART disabled)`。
- GPIO21 已配置为接收脚，当前只实现动作下发，没有约定 STM32 回传格式或 ACK，不把无回传视为接收失败。
- STM32 端需使用匹配的 stm32_motion 固件；全部关闭帧为 `AA 55 00 82`。不新增心跳、运动计时、断网停机或超时重试。

- 本轮语音/UART 配置固件构建成功：静态 RAM 69,100 字节（21.1%），应用 2,638,759 / 4,128,768 字节（63.9%）。生成 `.pio/build/esp32s3/firmware.bin`；生成配置已核对文字模式关闭、自动监听开启、UART1 TX=17/RX=21。编译有依赖宏重定义/弃用及环境变量提示，无构建错误。串口枚举无可用设备，未烧录或进行实机联调。

## 完整 JSON 到达即执行（2026-10-24）

用户确认将执行时机提前，不等待平台整轮结束。`CarReply::TakeAction` 复用同一严格解析器并记录本轮已取出动作；`CarController::AppendReply` 立即下发，之后 `FinishReply` 仅清理已处理轮次。串口纯文字测试不调用这个提前执行入口，仍保持整轮结束后校验且不操作硬件。

升级后可观察到 `Complete action JSON received; sending before tts.stop`，紧接着为 `Car: state=... frame=...`；恢复聆听现已随 JSON 下发前移。此次缩短的是 JSON 完整到动作发送之间的等待，不代表已测量网络、任务调度、UART 或机械响应延迟。

- 即时下发版本固件编译、链接成功：静态 RAM 69,100 字节（21.1%），应用 2,639,147 / 4,128,768 字节（63.9%），已更新 .pio/build/esp32s3/firmware.bin；尚未烧录，实际延迟待实测。构建仅有原依赖的宏重定义警告，无错误。

## 连续接收下一句（2026-10-24）

- 用户确认完整 JSON 校验通过后立即执行，同时继续接收下一句，恢复监听不依赖 tts.stop。
- 自动小车控制且 CAR_REPLY_AUDIO=n 时，AutoStop 入口切换为原协议 realtime；麦克风在 speaking 状态继续运行。JSON 下发后转为 listening，正在运行的音频处理器不重复发送 listen.start，不清空下一句音频。
- STT 事件只标记新输入边界并清理旧 JSON 缓冲，不从识别文字直接执行动作；收到新的 tts.start 后照常拼接该轮 JSON。已执行/已被下一句替换的旧轮次以固定大小计数保存待收 stop，按顺序消耗，不清空新轮次半包，不再次下发或等待播放。
- 协议事件只有会话 ID，没有逐轮 ID；上述关联要求服务端同一连接按轮次顺序发送每轮一个 stop，并先发送该句 STT 再开始该句回复。无法凭当前协议区分跨轮重复或乱序 stop，实际平台行为须联调验证。
- 联网、令牌、GPIO、UART 协议不变；不新增本地关键词动作、主动打断、状态心跳、运动时长或超时策略。
- 本轮 ESP32 构建成功：静态 RAM 69,100 字节，应用 2,639,647 字节；电脑端 19 项小车业务与 5 项串口输入测试全部通过。新固件未烧录，持续收音、平台实时模式、连续前进/停止及实际延迟尚未实机验证。
