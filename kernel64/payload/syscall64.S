.code64

.set MSR_EFER,   0xC0000080
.set MSR_STAR,   0xC0000081
.set MSR_LSTAR,  0xC0000082
.set MSR_FMASK,  0xC0000084

.section .text

.global syscall_msr_setup
syscall_msr_setup:
    movq %rsi, syscall_kstack_top(%rip)

    movl $MSR_EFER, %ecx
    rdmsr
    orl $0x1, %eax
    wrmsr

    movl $MSR_STAR, %ecx
    movl $0x00080000, %eax
    movl $0x0023, %edx
    wrmsr

    movl $MSR_LSTAR, %ecx
    movq %rdi, %rax
    movq %rdi, %rdx
    shrq $32, %rdx
    wrmsr

    movl $MSR_FMASK, %ecx
    movl $0x600, %eax
    xorl %edx, %edx
    wrmsr
    ret

.global syscall_entry
syscall_entry:
    movq %rsp, syscall_user_rsp(%rip)
    movq syscall_kstack_top(%rip), %rsp

    pushq %rax
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %rdx
    pushq %r10
    pushq %r8
    pushq %r9
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rcx, syscall_user_rip(%rip)
    movq %r11, syscall_user_flags(%rip)

    movq %rsp, %rdi
    call syscall_dispatch

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r9
    popq %r8
    popq %r10
    popq %rdx
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rax

    pushq $0x43
    pushq syscall_user_rsp(%rip)
    pushq syscall_user_flags(%rip)
    pushq $0x23
    pushq syscall_user_rip(%rip)
    iretq

.global enter_user64
enter_user64:
    cli
    movq %rsp, syscall_kernel_rsp(%rip)
    leaq 1f(%rip), %rax
    movq %rax, syscall_kernel_rip(%rip)
    pushq $0x43
    pushq %rsi
    pushq $0x202
    pushq $0x23
    pushq %rdi
    xorl %edx, %edx
    iretq
1:
    ret

.global user_exit64
user_exit64:
    movq syscall_kernel_rsp(%rip), %rsp
    jmp *syscall_kernel_rip(%rip)


.macro ISR0 n
.global isr\n
isr\n:
    pushq $0
    pushq $\n
    jmp isr_common
.endm

.macro ISR1 n
.global isr\n
isr\n:
    pushq $\n
    jmp isr_common
.endm

ISR0 0
ISR0 1
ISR0 2
ISR0 3
ISR0 4
ISR0 5
ISR0 6
ISR0 7
ISR1 8
ISR0 9
ISR1 10
ISR1 11
ISR1 12
ISR1 13
ISR1 14
ISR0 15
ISR0 16
ISR1 17
ISR0 18
ISR0 19
ISR0 20
ISR1 21
ISR0 22
ISR0 23
ISR0 24
ISR0 25
ISR0 26
ISR0 27
ISR0 28
ISR1 29
ISR1 30
ISR0 31

isr_common:
    movq %rsp, %rdi
    andq $-16, %rsp
    call exception_panic
1:
    hlt
    jmp 1b

.section .rodata
.align 8
.global isr_table
isr_table:
.quad isr0
.quad isr1
.quad isr2
.quad isr3
.quad isr4
.quad isr5
.quad isr6
.quad isr7
.quad isr8
.quad isr9
.quad isr10
.quad isr11
.quad isr12
.quad isr13
.quad isr14
.quad isr15
.quad isr16
.quad isr17
.quad isr18
.quad isr19
.quad isr20
.quad isr21
.quad isr22
.quad isr23
.quad isr24
.quad isr25
.quad isr26
.quad isr27
.quad isr28
.quad isr29
.quad isr30
.quad isr31

.section .bss
.align 16
syscall_stack_area:
.skip 32768
.global syscall_kstack_top
syscall_kstack_top:
.quad 0

.global syscall_user_rsp
syscall_user_rsp:
.quad 0
.global syscall_user_rip
syscall_user_rip:
.quad 0
.global syscall_user_flags
syscall_user_flags:
.quad 0
.global syscall_kernel_rsp
syscall_kernel_rsp:
.quad 0
.global syscall_kernel_rip
syscall_kernel_rip:
.quad 0
