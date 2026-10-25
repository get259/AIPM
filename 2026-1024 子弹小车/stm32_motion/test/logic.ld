ENTRY(Test_Run)
SECTIONS
{
    . = 0x10000;
    .text : { *(.text*) *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
}
