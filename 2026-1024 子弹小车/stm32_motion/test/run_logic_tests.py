"""在 ARM 指令模拟器中执行实际 C 逻辑，不连接电机或串口。"""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / ".pio" / "test-tools"))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC

home = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
tools = home / "packages" / "toolchain-gccarmnoneeabi" / "bin"
suffix = ".exe" if os.name == "nt" else ""
def tool(name):
    return str(tools / ("arm-none-eabi-" + name + suffix))

out = root / ".pio" / "logic-tests"
out.mkdir(parents=True, exist_ok=True)
elf, binary = out / "test.elf", out / "test.bin"
subprocess.run([
    tool("gcc"), "-mcpu=cortex-m3", "-mthumb", "-std=c99", "-O2",
    "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-nostdlib",
    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
    "-DSTM32F10X_MD", "-DUSE_STDPERIPH_DRIVER",
    "-I", str(root / "lib" / "STM32F1" / "include"),
    "-I", str(root / "include"), "-T", str(root / "test" / "logic.ld"),
    str(root / "src" / "car_protocol.c"), str(root / "src" / "car_motion.c"),
    str(root / "src" / "car_uart.c"),
    str(root / "src" / "car_fire.c"),
    str(root / "src" / "fire_led.c"),
    str(root / "lib" / "STM32F1" / "src" / "stm32f10x_tim.c"),
    str(root / "test" / "test_logic.c"), "-o", str(elf),
], check=True)
subprocess.run([tool("objcopy"), "-O", "binary", str(elf), str(binary)], check=True)
symbols = subprocess.check_output([tool("nm"), str(elf)], text=True)
entry = next(int(line.split()[0], 16) for line in symbols.splitlines()
             if line.split()[-1] == "Test_Run")
cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
cpu.mem_map(0x10000, 0x20000)
cpu.mem_map(0x20000000, 0x10000)
cpu.mem_map(0x40000000, 0x20000)
cpu.mem_write(0x10000, binary.read_bytes())
cpu.reg_write(UC_ARM_REG_SP, 0x20005000)
cpu.reg_write(UC_ARM_REG_LR, 0x2FFF1)
# 包含完整蓄力期间的真实 TIM2 中断，核对 PB0 双脉冲及引脚隔离。
cpu.emu_start(entry | 1, 0x2FFF0, count=100000000)
if cpu.reg_read(UC_ARM_REG_PC) != 0x2FFF0:
    raise SystemExit("FAIL: tests did not return before instruction limit")
result = cpu.reg_read(UC_ARM_REG_R0)
if result:
    raise SystemExit(f"FAIL: test_logic.c:{result}")
print("PASS: CRC, all states, stream recovery, motion, UART, three-round charge/reverse shot/full-bright lamps/retrigger/wraparound, two active-high PB0 pulses, active-low PWM and pin masks")
