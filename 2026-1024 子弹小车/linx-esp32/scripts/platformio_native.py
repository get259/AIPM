"""为 Windows 电脑端业务测试提供独立编译器，不改变系统 PATH。"""

import os

Import("env")

compiler = os.path.abspath(os.path.join(
    env.subst("$PROJECT_DIR"), "..", ".cache", "car-test-tools", "ziglang",
    "zig.exe" if os.name == "nt" else "zig",
))
if not os.path.isfile(compiler):
    raise RuntimeError("Install test-native/requirements.txt into .cache/car-test-tools first")
# native 平台会重新探测 gcc/g++，通过仅供当前子进程使用的包装目录
# 提供它们，避免后置修改编译器时漏掉测试框架已经创建的环境副本。
wrapper_dir = os.path.join(os.path.dirname(os.path.dirname(compiler)), "bin")
os.makedirs(wrapper_dir, exist_ok=True)
for name, command in {"gcc": "cc", "g++": "c++", "ar": "ar", "ranlib": "ranlib"}.items():
    path = os.path.join(wrapper_dir, name + (".cmd" if os.name == "nt" else ""))
    if os.name == "nt":
        contents = '@echo off\n"%s" %s %%*\n' % (compiler, command)
    else:
        contents = '#!/bin/sh\nexec "%s" %s "$@"\n' % (compiler, command)
    with open(path, "w", encoding="utf-8") as wrapper:
        wrapper.write(contents)
    if os.name != "nt":
        os.chmod(path, 0o755)
env.PrependENVPath("PATH", wrapper_dir)
