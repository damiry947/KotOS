typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;
typedef long i64;

#define USER_MIN   0x400000ULL
#define USER_MAX   0x600000ULL
#define MMAP_MIN   0x800000ULL
#define MMAP_MAX   0xA00000ULL
#define STACK_TOP  0x7FF000ULL

#define EPERM  1
#define ENOENT 2
#define EBADF  9
#define ENOMEM 12
#define EFAULT 14
#define ENODEV 19
#define EINVAL 22
#define ENOTTY 25
#define ESPIPE 29
#define ENOSYS 38
#define ERANGE 34

extern void kputc(u8 c);
extern void kputs(const char *s);
extern void kserial_char(char c);

extern void syscall_entry(void);
extern void syscall_msr_setup(void *entry, void *kstack_top);
extern void enter_user64(u64 entry, u64 stack);
extern void user_exit64(u64 code);
extern u64 syscall_kstack_top;
extern u64 isr_table[32];

struct regs64 {
    u64 r15, r14, r13, r12;
    u64 r9, r8, r10, rdx;
    u64 rdi, rsi, rbp, rbx;
    u64 rax;
};

typedef struct {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} __attribute__((packed)) elf64_ehdr;

typedef struct {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} __attribute__((packed)) elf64_phdr;

static u64 g_mb_info;
static u8 *mod_base;
static u64 mod_size;
static u64 brk_cur;
static u64 mmap_cur;

typedef struct {
    u16 limit;
    u64 base;
} __attribute__((packed)) idtr64;

static void khex(u64 v, char *out)
{
    int i;
    out[0] = '0';
    out[1] = 'x';
    for (i = 0; i < 16; i++)
        out[2 + i] = "0123456789ABCDEF"[(v >> (60 - i * 4)) & 15];
    out[18] = 0;
}

static void kserial_dec(u64 v)
{
    char b[24];
    int n = 0, i;
    if (!v)
        b[n++] = '0';
    while (v) {
        b[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    }
    for (i = n - 1; i >= 0; i--)
        kserial_char(b[i]);
}

static void kserial_str(const char *s)
{
    while (*s)
        kserial_char(*s++);
}

static void linux_err(const char *s)
{
    kputs("linux: ");
    kputs(s);
    kputs("\n");
}

static void zcopy(void *dst, const void *src, u64 n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    u64 i;
    if (d < s || d >= s + n) {
        for (i = 0; i < n; i++)
            d[i] = s[i];
    } else {
        for (i = n; i > 0; i--)
            d[i - 1] = s[i - 1];
    }
}

static void zmem(u8 *p, u64 n)
{
    u64 i;
    for (i = 0; i < n; i++)
        p[i] = 0;
}

static void putstr(char *dst, const char *src, u64 max)
{
    u64 i = 0;
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void wrmsr(u32 msr, u64 v)
{
    u32 lo = (u32)v, hi = (u32)(v >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

void exception_panic(u64 *f)
{
    char buf[19];
    u64 vec = f[0], err = f[1], rip = f[2];
    u64 cr2 = 0;

    kputs("\nEXC ");
    kserial_dec(vec);
    kputs(" err=");
    khex(err, buf);
    kputs(buf);
    kputs(" rip=");
    khex(rip, buf);
    kputs(buf);
    if (vec == 14) {
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        kputs(" cr2=");
        khex(cr2, buf);
        kputs(buf);
    }
    kputs(" cs=");
    khex(f[3], buf);
    kputs(buf);
    kputs("\n");
    for (;;)
        __asm__ volatile("hlt");
}

static void install_exceptions(void)
{
    idtr64 idt;
    u8 *p;
    int i, k;
    __asm__ volatile("sidt %0" : "=m"(idt));
    p = (u8 *)(u64)idt.base;
    for (i = 0; i < 32; i++) {
        u64 off = isr_table[i];
        for (k = 0; k < 16; k++)
            p[i * 16 + k] = 0;
        p[i * 16 + 0] = (u8)(off & 0xFF);
        p[i * 16 + 1] = (u8)((off >> 8) & 0xFF);
        p[i * 16 + 2] = 0x08;
        p[i * 16 + 5] = 0x8E;
        p[i * 16 + 6] = (u8)((off >> 16) & 0xFF);
        p[i * 16 + 7] = (u8)((off >> 24) & 0xFF);
        p[i * 16 + 8] = (u8)((off >> 32) & 0xFF);
        p[i * 16 + 9] = (u8)((off >> 40) & 0xFF);
        p[i * 16 + 10] = (u8)((off >> 48) & 0xFF);
        p[i * 16 + 11] = (u8)((off >> 56) & 0xFF);
    }
}

static int load_module(void)
{
    u32 *mbi = (u32 *)(u64)g_mb_info;
    u32 count, addr;
    u32 *m;
    if (!mbi)
        return 0;
    count = mbi[5];
    addr = mbi[6];
    if (!count || !addr)
        return 0;
    m = (u32 *)(u64)addr;
    mod_base = (u8 *)(u64)m[0];
    mod_size = (u64)m[1] - (u64)m[0];
    if (!mod_base || mod_size < 64 || mod_size > 0x800000ULL)
        return 0;
    return 1;
}

static int elf_load(u64 *entry, u64 *phdr_addr, u16 *phnum)
{
    elf64_ehdr *eh = (elf64_ehdr *)mod_base;
    u64 hi = 0;
    u16 i;

    if (mod_size < sizeof(elf64_ehdr))
        return -1;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
        return -1;
    if (eh->e_ident[4] != 2 || eh->e_ident[5] != 1)
        return -1;
    if (eh->e_machine != 62 || eh->e_type != 2)
        return -1;
    if (!eh->e_phnum || eh->e_phoff + (u64)eh->e_phnum * sizeof(elf64_phdr) > mod_size)
        return -1;

    for (i = 0; i < eh->e_phnum; i++) {
        elf64_phdr *ph = (elf64_phdr *)(mod_base + eh->e_phoff + (u64)i * sizeof(elf64_phdr));
        if (ph->p_type != 1)
            continue;
        if (ph->p_offset + ph->p_filesz > mod_size)
            return -1;
        if (ph->p_vaddr < USER_MIN || ph->p_vaddr + ph->p_memsz > USER_MAX)
            return -1;
        if (ph->p_filesz > ph->p_memsz)
            return -1;
        zcopy((void *)(u64)ph->p_vaddr, mod_base + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz)
            zmem((u8 *)(u64)(ph->p_vaddr + ph->p_filesz), ph->p_memsz - ph->p_filesz);
        if (ph->p_vaddr + ph->p_memsz > hi)
            hi = ph->p_vaddr + ph->p_memsz;
    }
    if (!hi)
        return -1;
    if (eh->e_entry < USER_MIN || eh->e_entry >= USER_MAX)
        return -1;
    brk_cur = (hi + 0xF) & ~0xFULL;
    *entry = eh->e_entry;
    *phdr_addr = USER_MIN + eh->e_phoff;
    *phnum = eh->e_phnum;
    return 0;
}

static void enable_sse(void)
{
    u64 cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(u64)(1 << 2);
    cr0 |= (u64)(1 << 1);
    cr0 &= ~(u64)(1 << 3);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x600ULL;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
    __asm__ volatile("fninit");
}

static void map_user_pages(void)
{
    u64 cr3;
    u64 *pml4, *pdpt, *pd;
    int i;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    pml4 = (u64 *)(cr3 & ~0xFFFULL);
    pdpt = (u64 *)(pml4[0] & ~0xFFFULL);
    pd = (u64 *)(pdpt[0] & ~0xFFFULL);
    for (i = 2; i <= 4; i++)
        pd[i] |= 0x4ULL;
    pd[1] &= ~0x4ULL;
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

static int user_ok(u64 p, u64 n)
{
    if (p < USER_MIN || p >= MMAP_MAX)
        return 0;
    if (n > MMAP_MAX - p)
        return 0;
    return 1;
}

static void build_stack(u64 entry, u64 phdr, u64 phnum)
{
    char *name = (char *)0x7FFF00;
    u8 *rnd = (u8 *)0x7FFF80;
    u64 *sp = (u64 *)0x7FFEE0;
    int i;

    putstr(name, "linux_app", 64);
    for (i = 0; i < 16; i++)
        rnd[i] = (u8)(0x11 * (i + 1));

#define PUSH(v) do { *--sp = (u64)(v); } while (0)
    PUSH(0); PUSH(0);
    PUSH(0x7FFF00); PUSH(31);
    PUSH(0x7FFF80); PUSH(25);
    PUSH(0); PUSH(23);
    PUSH(0); PUSH(14);
    PUSH(0); PUSH(13);
    PUSH(0); PUSH(12);
    PUSH(0); PUSH(11);
    PUSH(entry); PUSH(9);
    PUSH(0); PUSH(8);
    PUSH(0); PUSH(7);
    PUSH(4096); PUSH(6);
    PUSH(phnum); PUSH(5);
    PUSH(56); PUSH(4);
    PUSH(phdr); PUSH(3);
    PUSH(0);
    PUSH(0);
    PUSH(0x7FFF00);
    PUSH(1);
#undef PUSH

    enter_user64(entry, (u64)sp);
}

typedef struct {
    u64 dev, ino, nlink;
    u32 mode, uid, gid, pad0;
    u64 rdev;
    i64 size, blksize, blocks;
    i64 atime;
    u64 atime_ns;
    i64 mtime;
    u64 mtime_ns;
    i64 ctime;
    u64 ctime_ns;
    i64 unused[3];
} linux_stat;

static long sys_write(u32 fd, u64 buf, u64 count)
{
    u64 i;
    if (fd != 1 && fd != 2)
        return -EBADF;
    if (count && !user_ok(buf, count))
        return -EFAULT;
    for (i = 0; i < count; i++) {
        u8 c = *(u8 *)(buf + i);
        kputc(c);
        kserial_char((char)c);
    }
    return (long)count;
}

static long sys_fstat(u32 fd, u64 st)
{
    linux_stat *s;
    if (fd > 2)
        return -EBADF;
    if (!user_ok(st, sizeof(linux_stat)))
        return -EFAULT;
    s = (linux_stat *)st;
    zmem((u8 *)s, sizeof(linux_stat));
    s->nlink = 1;
    s->mode = 0x2000 | 0600;
    s->blksize = 4096;
    return 0;
}

long syscall_dispatch(struct regs64 *r)
{
    u64 nr = r->rax;
    long ret;

    switch (nr) {
    case 0:
        ret = 0;
        break;
    case 1:
        ret = sys_write((u32)r->rdi, r->rsi, r->rdx);
        break;
    case 2:
    case 257:
        ret = -ENOENT;
        break;
    case 3:
        ret = 0;
        break;
    case 5:
        ret = sys_fstat((u32)r->rdi, r->rsi);
        break;
    case 8:
        ret = -ESPIPE;
        break;
    case 9: {
        u64 addr = r->rdi, len = r->rsi, flags = r->r10;
        len = (len + 0xFFF) & ~0xFFFULL;
        if (!(flags & 0x20)) {
            ret = -ENODEV;
            break;
        }
        if ((flags & 0x10) && user_ok(addr, len)) {
            zmem((u8 *)addr, len);
            ret = (long)addr;
            break;
        }
        if (mmap_cur + len > MMAP_MAX) {
            ret = -ENOMEM;
            break;
        }
        ret = (long)mmap_cur;
        zmem((u8 *)mmap_cur, len);
        mmap_cur += len;
        break;
    }
    case 10:
        ret = 0;
        break;
    case 11:
        ret = 0;
        break;
    case 12: {
        u64 addr = r->rdi;
        if (!addr) {
            ret = (long)brk_cur;
            break;
        }
        if (addr < USER_MIN || addr > USER_MAX) {
            ret = (long)brk_cur;
            break;
        }
        if (addr > brk_cur)
            zmem((u8 *)brk_cur, addr - brk_cur);
        brk_cur = addr;
        ret = (long)brk_cur;
        break;
    }
    case 13:
    case 14:
        ret = 0;
        break;
    case 16:
        ret = -ENOTTY;
        break;
    case 20: {
        u64 vec = r->rsi;
        u32 cnt = (u32)r->rdx, i;
        long total = 0;
        if (!user_ok(vec, (u64)cnt * 16))
            return -EFAULT;
        for (i = 0; i < cnt; i++) {
            u64 base = *(u64 *)(vec + (u64)i * 16);
            u64 len = *(u64 *)(vec + (u64)i * 16 + 8);
            long n = sys_write((u32)r->rdi, base, len);
            if (n < 0)
                return n;
            total += n;
        }
        ret = total;
        break;
    }
    case 21:
    case 89:
    case 262:
    case 267:
        ret = -ENOENT;
        break;
    case 24:
        ret = 0;
        break;
    case 28:
        ret = 0;
        break;
    case 39:
    case 186:
        ret = 1;
        break;
    case 60:
    case 231:
        user_exit64(r->rdi & 0xFF);
        ret = 0;
        break;
    case 63: {
        char *u = (char *)r->rdi;
        if (!user_ok(r->rdi, 390))
            return -EFAULT;
        zmem((u8 *)u, 390);
        putstr(u, "Linux", 65);
        putstr(u + 65, "kotos", 65);
        putstr(u + 130, "1.0", 65);
        putstr(u + 195, "KotOS x64 Linux ABI", 65);
        putstr(u + 260, "x86_64", 65);
        putstr(u + 325, "local", 65);
        ret = 0;
        break;
    }
    case 79: {
        char *u = (char *)r->rdi;
        u64 n = r->rsi;
        if (!user_ok(r->rdi, 2) || n < 2)
            return -ERANGE;
        u[0] = '/';
        u[1] = 0;
        ret = 2;
        break;
    }
    case 96:
    case 228: {
        u64 p = r->rdi;
        if (!user_ok(p, 16))
            return -EFAULT;
        zmem((u8 *)p, 16);
        ret = 0;
        break;
    }
    case 102:
    case 104:
    case 107:
    case 108:
        ret = 0;
        break;
    case 131:
        ret = 0;
        break;
    case 158:
        if (r->rdi == 0x1002 || r->rdi == 0x1001) {
            wrmsr(r->rdi == 0x1002 ? 0xC0000100 : 0xC0000101, r->rsi);
            ret = 0;
        } else if (r->rdi == 0x1003 || r->rdi == 0x1004) {
            u32 lo, hi;
            u32 msr = r->rdi == 0x1003 ? 0xC0000100 : 0xC0000101;
            __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
            if (user_ok(r->rsi, 8))
                *(u64 *)r->rsi = ((u64)hi << 32) | lo;
            ret = 0;
        } else {
            ret = -EINVAL;
        }
        break;
    case 202:
        ret = -ENOSYS;
        break;
    case 204:
        if (r->rsi >= 8 && user_ok(r->rdi, 8)) {
            *(u64 *)r->rdi = 1;
            ret = 8;
        } else {
            ret = -EINVAL;
        }
        break;
    case 218:
        ret = 1;
        break;
    case 273:
        ret = 0;
        break;
    case 302:
        if (r->rsi && user_ok(r->rsi, 16)) {
            *(u64 *)r->rsi = ~0ULL;
            *(u64 *)(r->rsi + 8) = ~0ULL;
        }
        ret = 0;
        break;
    case 318: {
        u64 n = r->rsi;
        if (n > 256)
            n = 256;
        if (n && !user_ok(r->rdi, n))
            return -EFAULT;
        if (n)
            zmem((u8 *)r->rdi, n);
        ret = (long)r->rdx;
        break;
    }
    case 334:
        ret = -ENOSYS;
        break;
    default:
        kserial_str("linux: syscall ");
        kserial_dec(nr);
        kserial_str(" -> ENOSYS\n");
        ret = -ENOSYS;
        break;
    }

    r->rax = (u64)ret;
    return ret;
}

void linuxabi_init(u64 mb_info)
{
    g_mb_info = mb_info;
}

int linuxabi_exec(void)
{
    u64 entry = 0, phdr = 0;
    u16 phnum = 0;

    if (!load_module()) {
        linux_err("нет ELF-модуля: добавьте -initrd linux_app.elf или module в GRUB");
        return 0;
    }
    if (elf_load(&entry, &phdr, &phnum) != 0) {
        linux_err("неподдерживаемый ELF (нужен статический ET_EXEC x86_64)");
        return 0;
    }

    mmap_cur = MMAP_MIN;
    map_user_pages();
    enable_sse();
    install_exceptions();
    syscall_msr_setup((void *)syscall_entry, (void *)&syscall_kstack_top);

    kputs("linux: ELF64 загружен, entry=");
    {
        char hx[19];
        int i;
        hx[0] = '0'; hx[1] = 'x';
        for (i = 0; i < 16; i++)
            hx[2 + i] = "0123456789ABCDEF"[(entry >> (60 - i * 4)) & 15];
        hx[18] = 0;
        kputs(hx);
    }
    kputs("\n");

    build_stack(entry, phdr, phnum);
    linux_err("программа завершилась");
    return 0;
}
