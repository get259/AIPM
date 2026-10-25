# 网页遥控发布包：2026-10-24-web-remote

源码已同步到发布仓库的 linx-esp32/。旧发布包保留，STM32 固件沿用上一版。

## 本次更新

- 独立轻量遥控页，左右按钮映射调整；配网仍使用独立 HTTP 页面。
- 手机点击开始/关闭持续收听，关闭收听保持网页独占，关闭遥控先停止再恢复板载麦克风。
- 手机 PCM 处理、Opus 编码、云端发送分任务；有界流水上传，校验连接代际，结束时等待在途帧。
- 热点常开，取消遥控模式下的全域名 DNS 重定向；控制 WSS 与页面 HTTPS 分开。

## 使用

手机连接小车热点，在完整浏览器打开 https://192.168.4.1/ 。首次使用安装并信任 certificates/car-root-ca.cer；该公开证书与本包固件对应。iPhone 还需开启根证书完全信任。重新生成证书的固件需要匹配的新根证书。

配网地址为 http://192.168.4.1/ 。热点不提供互联网转发，ESP32 需通过已配置的路由器访问灵矽。更新后断开再连接手机热点，清除此前 DNS 重定向影响。

## 烧录

ESP32-S3，16 MB Flash；保留镜像自身的 flash mode/frequency（keep）。

| 地址 | 文件 |
| --- | --- |
| 0x0 | esp32s3/bootloader.bin |
| 0x8000 | esp32s3/partitions.bin |
| 0xd000 | esp32s3/ota_data_initial.bin |
| 0x20000 | esp32s3/firmware.bin |
| 0x800000 | esp32s3/generated_assets.bin |

STM32F103：stm32f103/firmware.bin 烧录到 0x08000000，或使用同目录 HEX。
接线沿用上一版：ESP32 UART1 TX17→STM32 PA10、RX21←PA9、共地；INMP441 BCLK5、WS4、SD6，左声道。

## 验证状态

最新 ESP32 固件构建成功，固件与当前网页/证书一致性检查通过。此前 31 项本机业务测试及并行发送相关 9 组流程测试通过。最新并行版本尚未由助手烧录，持续收听、真实手机权限、TLS 稳定性和实车延迟待联调。

manifest.json 记录源代码基线、快照与提交状态；source-snapshots.json 记录源文件哈希；SHA256SUMS.txt 用于核对发布文件。不包含原始私钥文件、Wi-Fi 密码、平台 token、依赖或构建缓存；服务器私钥按 HTTPS 固件正常实现嵌入设备镜像。
