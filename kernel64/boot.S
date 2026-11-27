.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00000003
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM

.section .bss
.align 16
idt:
.skip 4096
idt_ptr:
.skip 16
.align 16
tss:
.skip 104
.align 4096
pml4:
.skip 4096
pdpt:
.skip 4096
pd0:
.skip 4096
pd1:
.skip 4096
pd2:
.skip 4096
pd3:
.skip 4096
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .data
.align 16
gdt64:
.quad 0
.quad 0x00AF9A000000FFFF
.quad 0x00CF92000000FFFF
.quad 0x00CFF2000000FFFF
.quad 0x00AFFA000000FFFF
.quad 0
.quad 0
.quad 0x00CFFA000000FFFF
gdt64_end:
gdt64_ptr:
.word gdt64_end - gdt64 - 1
.long gdt64

.section .data
.align 8
saved_rip:
.quad 0
saved_rsp:
.quad 0
.align 4
ticks:
.long 0

.section .rodata
msg:
.asciz "KotOS x64: long mode OK, serial works\r\n"
msg_idt:
.asciz "IDT loaded\r\n"
msg_exc:
.asciz "EXC 0 caught, IDT works\r\n"
dot:
.asciz "."
msg_pit:
.asciz "\r\nPIT OK, interrupts work\r\n"
msg_sys:
.asciz "ring3: int 0x80 works\r\n"
msg_back:
.asciz "back in kernel after exit\r\n"
msg_compat:
.asciz "compat mode: 32-bit works\r\n"
compat_stub:
.code32
    mov $1, %eax
    mov $1, %ebx
    call 1f
1:  pop %ecx
    add $(compat_msg - 1b), %ecx
    mov $28, %edx
    int $0x81
    mov $2, %eax
    int $0x81
2:  jmp 2b
compat_msg:
.ascii "compat mode: int 0x81 works\n"
compat_stub_end:
.code64
.align 8
user_stub:
.code64
    mov $1, %eax
    mov $1, %edi
    lea umsg(%rip), %rsi
    mov $28, %edx
    int $0x80
    mov $2, %eax
    xor %edi, %edi
    int $0x80
1:  jmp 1b
umsg:
.ascii "ring3 ABI: write/exit work\n"
user_stub_end:
.code32

.section .text
.code32
.global _start
_start:
    cli
    mov $stack_top, %esp
    cld

    mov $pml4, %edi
    xor %eax, %eax
    mov $(6 * 1024 / 4), %ecx
    rep stosl

    mov $pdpt, %eax
    or $7, %eax
    mov %eax, pml4

    mov $pd0, %eax
    or $7, %eax
    mov %eax, pdpt
    mov $pd1, %eax
    or $7, %eax
    mov %eax, pdpt + 8
    mov $pd2, %eax
    or $7, %eax
    mov %eax, pdpt + 16
    mov $pd3, %eax
    or $7, %eax
    mov %eax, pdpt + 24

    mov $pd0, %edi
    xor %ebx, %ebx
    mov $2048, %ecx
1:
    mov %ebx, %eax
    or $0x83, %eax
    mov %eax, (%edi)
    add $8, %edi
    add $0x200000, %ebx
    loop 1b

    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4

    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    mov $pml4, %eax
    mov %eax, %cr3

    mov %cr0, %eax
    or $0x80000001, %eax
    mov %eax, %cr0

    lgdt gdt64_ptr
    ljmp $0x08, $long_mode

.code64
long_mode:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov $stack_top, %esp

    movw $0x3F8 + 1, %dx
    xor %al, %al
    out %al, %dx
    movw $0x3F8 + 3, %dx
    movb $0x80, %al
    out %al, %dx
    movw $0x3F8, %dx
    movb $0x03, %al
    out %al, %dx
    movw $0x3F8 + 1, %dx
    xor %al, %al
    out %al, %dx
    movw $0x3F8 + 3, %dx
    movb $0x03, %al
    out %al, %dx
    movw $0x3F8 + 2, %dx
    movb $0xC7, %al
    out %al, %dx
    movw $0x3F8 + 4, %dx
    movb $0x0B, %al
    out %al, %dx

    lea idt(%rip), %rdi
    lea isr_default(%rip), %rax
    mov $256, %ecx
5:
    mov %eax, %edx
    mov %dx, 0(%rdi)
    movw $0x08, 2(%rdi)
    movb $0, 4(%rdi)
    movb $0x8E, 5(%rdi)
    shr $16, %edx
    mov %dx, 6(%rdi)
    movl $0, 8(%rdi)
    movl $0, 12(%rdi)
    add $16, %rdi
    loop 5b

    lea idt_ptr(%rip), %rdi
    movw $4095, (%rdi)
    lea idt(%rip), %rax
    mov %rax, 2(%rdi)
    lidt (%rdi)

    lea msg(%rip), %rsi
    call puts64
    lea msg_idt(%rip), %rsi
    call puts64

    call irq_setup
    sti
wait_ticks:
    cmpl $5, ticks(%rip)
    jae go_ring3
    hlt
    jmp wait_ticks
go_ring3:
    cli
    call ring3_setup
    hlt
    jmp .

irq_setup:
    lea idt + 0x200(%rip), %rdi
    lea irq0(%rip), %rax
    mov %eax, %edx
    mov %dx, 0(%rdi)
    movw $0x08, 2(%rdi)
    movb $0, 4(%rdi)
    movb $0x8E, 5(%rdi)
    shr $16, %edx
    mov %dx, 6(%rdi)
    movl $0, 8(%rdi)
    movl $0, 12(%rdi)

    movw $0x20, %dx
    movb $0x11, %al
    out %al, %dx
    movw $0xA0, %dx
    movb $0x11, %al
    out %al, %dx
    movw $0x21, %dx
    movb $0x20, %al
    out %al, %dx
    movw $0xA1, %dx
    movb $0x28, %al
    out %al, %dx
    movw $0x21, %dx
    movb $0x04, %al
    out %al, %dx
    movw $0xA1, %dx
    movb $0x02, %al
    out %al, %dx
    movw $0x21, %dx
    movb $0x01, %al
    out %al, %dx
    movw $0xA1, %dx
    movb $0x01, %al
    out %al, %dx
    movw $0x21, %dx
    movb $0xFE, %al
    out %al, %dx
    movw $0xA1, %dx
    movb $0xFF, %al
    out %al, %dx

    movw $0x43, %dx
    movb $0x36, %al
    out %al, %dx
    movw $0x40, %dx
    movb $0x9C, %al
    out %al, %dx
    movb $0x2E, %al
    out %al, %dx
    ret

irq0:
    push %rax
    push %rdx
    push %rsi
    lea dot(%rip), %rsi
    call puts64
    movl ticks(%rip), %eax
    inc %eax
    movl %eax, ticks(%rip)
    cmp $5, %eax
    jne 6f
    lea msg_pit(%rip), %rsi
    call puts64
6:
    movb $0x20, %al
    movw $0x20, %dx
    out %al, %dx
    pop %rsi
    pop %rdx
    pop %rax
    iretq

putc64:
    push %rdx
    push %rax
5:
    movw $0x3FD, %dx
    in %dx, %al
    test $0x20, %al
    jz 5b
    pop %rax
    movw $0x3F8, %dx
    out %al, %dx
    pop %rdx
    ret

puts64:
    push %rsi
1:
    movb (%rsi), %al
    testb %al, %al
    jz 3f
    call putc64
    inc %rsi
    jmp 1b
3:
    pop %rsi
    ret

isr_default:
    lea msg_exc(%rip), %rsi
    call puts64
    hlt
    jmp isr_default

ring3_setup:
    lea gdt64(%rip), %rdi
    add $0x28, %rdi
    lea tss(%rip), %rax
    movw $103, 0(%rdi)
    mov %ax, %dx
    mov %dx, 2(%rdi)
    shr $16, %eax
    movb %al, 4(%rdi)
    movb $0x89, 5(%rdi)
    movb $0, 6(%rdi)
    shr $8, %eax
    movb %al, 7(%rdi)
    movl $0, 8(%rdi)
    movl $0, 12(%rdi)

    lea tss(%rip), %rdi
    movq $0, 0(%rdi)
    lea stack_top(%rip), %rax
    mov %rax, 4(%rdi)
    movw $104, 102(%rdi)
    movw $0x28, %ax
    ltr %ax

    lea idt + 0x800(%rip), %rdi
    lea int80(%rip), %rax
    mov %eax, %edx
    mov %dx, 0(%rdi)
    movw $0x08, 2(%rdi)
    movb $0, 4(%rdi)
    movb $0xEE, 5(%rdi)
    shr $16, %edx
    mov %dx, 6(%rdi)
    movl $0, 8(%rdi)
    movl $0, 12(%rdi)

    lea pd0(%rip), %rdi
    movl $0x400000, %eax
    or $0x87, %eax
    mov %eax, 16(%rdi)
    movl $0, 20(%rdi)

    lea user_stub(%rip), %rsi
    movl $0x400000, %edi
    movl $(user_stub_end - user_stub), %ecx
    rep movsb

    lea after_user(%rip), %rax
    mov %rax, saved_rip(%rip)
    mov %rsp, saved_rsp(%rip)

    movl $0x5FF000, %eax
    pushq $0x1B
    pushq %rax
    pushq $0x002
    pushq $0x23
    pushq $0x400000
    iretq

int80:
    cmp $1, %rax
    je sys_write
    cmp $2, %rax
    je sys_exit
    mov $-1, %rax
    iretq

sys_write:
    push %rcx
    push %r8
    push %r9
    mov %rsi, %r8
    mov %rdx, %r9
    xor %ecx, %ecx
1:
    cmp %r9, %rcx
    jae 2f
    movb (%r8,%rcx), %al
    call putc64
    inc %rcx
    jmp 1b
2:
    mov %r9, %rax
    pop %r9
    pop %r8
    pop %rcx
    iretq

sys_exit:
    mov saved_rsp(%rip), %rsp
    mov saved_rip(%rip), %rax
    jmp *%rax

after_user:
    lea msg_back(%rip), %rsi
    call puts64
    call compat_setup
9:
    hlt
    jmp 9b

compat_setup:
    lea idt + 0x810(%rip), %rdi
    lea compat_int(%rip), %rax
    mov %eax, %edx
    mov %dx, 0(%rdi)
    movw $0x08, 2(%rdi)
    movb $0, 4(%rdi)
    movb $0xEE, 5(%rdi)
    shr $16, %edx
    mov %dx, 6(%rdi)
    movl $0, 8(%rdi)
    movl $0, 12(%rdi)

    lea compat_stub(%rip), %rsi
    movl $0x400000, %edi
    movl $(compat_stub_end - compat_stub), %ecx
    rep movsb

    lea after_compat(%rip), %rax
    mov %rax, saved_rip(%rip)
    mov %rsp, saved_rsp(%rip)

    movl $0x5FF000, %eax
    pushq $0x1B
    pushq %rax
    pushq $0x002
    pushq $0x3B
    pushq $0x400000
    iretq

compat_int:
    cmp $1, %eax
    je compat_write
    cmp $2, %eax
    je sys_exit
    mov $-1, %eax
    iretq

compat_write:
    movl %ecx, %r8d
    movl %edx, %r9d
    xor %ecx, %ecx
1:
    cmp %r9, %rcx
    jae 2f
    movb (%r8,%rcx), %al
    call putc64
    inc %rcx
    jmp 1b
2:
    mov %r9, %rax
    iretq

after_compat:
    lea msg_compat(%rip), %rsi
    call puts64
    hlt
    jmp after_compat
