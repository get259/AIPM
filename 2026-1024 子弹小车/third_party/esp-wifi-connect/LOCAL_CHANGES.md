# 本地维护说明

来源：78/esp-wifi-connect 3.0.2，原仓库 https://github.com/78/esp-wifi-connect ，上游清单声明许可证为 MIT。

本目录保留原组件源码及清单，通过 main/idf_component.yml 的 override_path 引用，不修改 managed_components 缓存。保留上游的作者、仓库与许可证声明；未复制下载校验元数据或预览图片。

本地变更仅用于配网页显示平台激活码：

- 可选 activation_portal 配置，默认关闭，关闭时保留原配网退出流程。
- 成功页通过状态接口显示激活码；先保留热点，再由原 WifiStation 连接路由器并启动原平台激活。
- 等待激活期间暂停 AP 扫描，避免两套扫描器竞争。
- 激活成功后页面请求退出，并有 5 秒延迟关闭兜底；通过代际编号避免旧关闭任务影响后来重开的热点。
- AP 关闭时保留 STA，避免重新配网或重复连接路由器。未增加平台重试、令牌或 WSS 策略。
- 激活状态只存内存，接口不返回 Wi-Fi 密码、平台 token 或完整 OTA 响应。
