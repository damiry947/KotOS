typedef unsigned int u32;

static inline int sys(u32 n, u32 a, u32 b, u32 c)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

static u32 slen(const char *s)
{
    u32 n = 0;
    while (s[n])
        n++;
    return n;
}

static void swrite(const char *s)
{
    sys(1, 1, (u32)s, slen(s));
}

void _start(void)
{
    swrite("KotOS userland: hello from ring 3!\n");
    swrite("Type text, Enter to exit.\n");
    for (;;) {
        int ch = sys(3, 0, 0, 0);
        char b[2];
        if (ch == '\n')
            break;
        b[0] = (char)ch;
        b[1] = 0;
        swrite(b);
    }
    swrite("\nBye from userspace.\n");
    sys(2, 0, 0, 0);
    for (;;)
        ;
}
