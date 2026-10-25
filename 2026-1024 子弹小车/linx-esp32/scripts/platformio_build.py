"""只适配 PlatformIO 构建环境，不改变固件功能。"""

import os

Import("env")

# ESP-IDF 原生组件清单由 IDF Component Manager 处理，而非 Arduino lib_deps。
os.environ["IDF_COMPONENT_MANAGER"] = "1"
env["ENV"]["IDF_COMPONENT_MANAGER"] = "1"
