static long sys_call3(long nr, long a, long b, long c)
{
    long r;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(nr), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return r;
}

static void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60L), "D"((long)code) : "rcx", "r11", "memory");
    for (;;)
        ;
}

void _start(void)
{
    static const char msg[] = "hello from ELF64 Linux binary\n";
    sys_call3(1, 1, (long)msg, (long)(sizeof(msg) - 1));
    sys_exit(0);
}
