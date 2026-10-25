"""补齐原生 CMake 的语言和资源生成目标，供 PlatformIO 编译使用。"""

import os
import re
import subprocess

Import("env")

if not env.GetOption("clean"):
    build_dir = env.subst("$BUILD_DIR")
    if os.path.isfile(os.path.join(build_dir, "build.ninja")):
        platform = env.PioPlatform()
        cmake = os.path.join(platform.get_package_dir("tool-cmake"), "bin", "cmake.exe" if os.name == "nt" else "cmake")
        build_env = dict(env["ENV"])
        # 原工程的 CMake 自定义命令使用 python，确保它指向 IDF 环境。
        python_dir = os.path.dirname(env.subst("$ESPIDF_PYTHONEXE"))
        tool_paths = [python_dir]
        for package, suffix in (("toolchain-xtensa-esp-elf", "bin"),
                                ("toolchain-riscv32-esp", "bin"),
                                ("toolchain-esp32ulp", "bin"),
                                ("tool-cmake", "bin"), ("tool-ninja", "")):
            location = platform.get_package_dir(package)
            if location:
                tool_paths.append(os.path.join(location, suffix))
        build_env["PATH"] = os.pathsep.join(tool_paths + [build_env.get("PATH", os.defpath)])
        build_env["IDF_PATH"] = platform.get_package_dir("framework-espidf")
        targets = ["lang_header"]
        config_path = os.path.join(build_dir, "config", "sdkconfig.json")
        if os.path.isfile(config_path):
            import json

            with open(config_path, encoding="utf-8") as config_file:
                if json.load(config_file).get("FLASH_DEFAULT_ASSETS"):
                    targets.append("generated_default_assets")
        # IDF 的 EMBED_FILES 会先把音效等文件转换成 .S；PlatformIO
        # 不会自动执行这些 CMake 自定义命令，必须在对象编译前补齐。
        ninja = os.path.join(platform.get_package_dir("tool-ninja"), "ninja.exe" if os.name == "nt" else "ninja")
        target_list = subprocess.check_output(
            [ninja, "-C", build_dir, "-t", "targets", "all"],
            env=build_env,
            text=True,
        )
        embed_targets = []
        for line in target_list.splitlines():
            target, _, rule = line.rpartition(": ")
            if rule == "CUSTOM_COMMAND" and target.endswith(".S") and not os.path.isabs(target):
                embed_targets.append(target)
        # Ninja 的嵌入目标还附带整个组件库的顺序依赖。只调用其原生
        # CMake 嵌入命令，避免在 PlatformIO 编译前重复编译所有组件。
        if embed_targets:
            commands = subprocess.check_output(
                [ninja, "-C", build_dir, "-t", "commands", *embed_targets],
                env=build_env,
                text=True,
            )
            embed_script = os.path.join(
                platform.get_package_dir("framework-espidf"),
                "tools", "cmake", "scripts", "data_file_embed_asm.cmake",
            )
            for command in dict.fromkeys(commands.splitlines()):
                if "data_file_embed_asm.cmake" not in command:
                    continue
                defines = [
                    quoted or plain
                    for quoted, plain in re.findall(r'-D\s+(?:"([^"]+)"|(\S+))', command)
                ]
                values = dict(define.split("=", 1) for define in defines)
                source = values["DATA_FILE"]
                output = values["SOURCE_FILE"]
                # 证书 bundle 由 PlatformIO 自身生成，干净构建时其输入
                # 尚不存在；保持原工具链证书处理，不在此重复执行。
                if os.path.basename(source) == "x509_crt_bundle":
                    continue
                if os.path.isfile(output) and os.path.getmtime(output) >= max(
                    os.path.getmtime(source), os.path.getmtime(embed_script)
                ):
                    continue
                subprocess.check_call(
                    [cmake, *["-D" + define for define in defines], "-P", embed_script],
                    cwd=build_dir,
                    env=build_env,
                )
        subprocess.check_call(
            [cmake, "--build", build_dir, "--target", *targets],
            cwd=env.subst("$PROJECT_DIR"),
            env=build_env,
        )
