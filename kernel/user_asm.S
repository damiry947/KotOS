.section .text

.global gdt_load
gdt_load:
    mov 4(%esp), %eax
    lgdt (%eax)
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ljmp $0x08, $1f
1:
    ret

.global tss_load
tss_load:
    mov $0x28, %ax
    ltr %ax
    ret

.global idt_load
idt_load:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

.global isr_default
isr_default:
    cli
    call exception_panic
1:
    hlt
    jmp 1b

.global isr80_stub
isr80_stub:
    push %ds
    push %es
    push %fs
    push %gs
    pushal
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    push %esp
    call syscall_handler
    add $4, %esp
    popal
    pop %gs
    pop %fs
    pop %es
    pop %ds
    iret

.global enter_user
enter_user:
    mov 4(%esp), %ecx
    mov 8(%esp), %edx
    mov %esp, saved_kernel_esp
    mov %ebp, saved_kernel_ebp
    mov %ebx, saved_kernel_ebx
    mov %esi, saved_kernel_esi
    mov %edi, saved_kernel_edi
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    push $0x23
    push %edx
    pushfl
    andl $0xFFFFFDFF, (%esp)
    push $0x1B
    push %ecx
    iret

.global user_exit
user_exit:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov saved_kernel_esp, %esp
    mov saved_kernel_ebp, %ebp
    mov saved_kernel_ebx, %ebx
    mov saved_kernel_esi, %esi
    mov saved_kernel_edi, %edi
    ret

.section .data
.global saved_kernel_esp
saved_kernel_esp:
    .long 0
saved_kernel_ebp:
    .long 0
saved_kernel_ebx:
    .long 0
saved_kernel_esi:
    .long 0
saved_kernel_edi:
    .long 0

.section .rodata
.global user_blob_start
.global user_blob_end
user_blob_start:
.incbin "user.bin"
user_blob_end:
