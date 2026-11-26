.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00000007
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM
.long 0
.long 1024
.long 768
.long 32

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .bss
.global mb_magic
.global mb_info
mb_magic:
.long 0
mb_info:
.long 0

.section .text
.global _start
.type _start, @function
_start:
    mov %eax, mb_magic
    mov %ebx, mb_info
    cli
    mov $stack_top, %esp
    xor %ebp, %ebp
    call kmain

1:  cli
    hlt
    jmp 1b

.size _start, . - _start
