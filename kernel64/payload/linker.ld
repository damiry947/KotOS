ENTRY(kmain64)
OUTPUT_FORMAT(elf64-x86-64)
SECTIONS
{
    . = 0x300000;
    .text : { *(.text.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
}
