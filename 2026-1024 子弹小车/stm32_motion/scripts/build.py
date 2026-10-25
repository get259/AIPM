"""Link the existing SPL application with its GCC startup and small C runtime."""

Import("env")

env.Append(
    LINKFLAGS=[
        "-nostartfiles",
        "--specs=nano.specs",
        "--specs=nosys.specs",
        "-Wl,-Map,$BUILD_DIR/firmware.map",
    ]
)
# Include vectors and reserved heap/stack in PlatformIO's memory checks.
env.Replace(
    SIZEPROGREGEXP=r"^(?:\.isr_vector|\.text|\.data|\.rodata|\.ARM.exidx|\.ARM.extab|\.preinit_array|\.init_array|\.fini_array)\s+(\d+).*",
    SIZEDATAREGEXP=r"^(?:\.data|\.bss|\.noinit|\.heap_stack)\s+(\d+).*",
)
