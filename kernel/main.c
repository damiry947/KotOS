typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define VGA_W 80
#define VGA_H 25
#define COM1 0x3F8

static volatile u16 *const vga = (volatile u16 *)0xB8000;
static u32 vrow = 0, vcol = 0;
static u8 vattr = 0x07;

static inline void outb(u16 port, u8 val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static void serial_init(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void serial_putc(char c)
{
    while (!(inb(COM1 + 5) & 0x20))
        ;
    outb(COM1, (u8)c);
}

static void vga_scroll(void)
{
    u32 i;
    for (i = 0; i < (VGA_W * (VGA_H - 1)); i++)
        vga[i] = vga[i + VGA_W];
    for (i = VGA_W * (VGA_H - 1); i < VGA_W * VGA_H; i++)
        vga[i] = (u16)(' ' | (vattr << 8));
}

static void vga_putc(char c)
{
    if (c == '\n') {
        vcol = 0;
        vrow++;
    } else if (c == '\r') {
        vcol = 0;
    } else if (c == '\b') {
        if (vcol) {
            vcol--;
            vga[vrow * VGA_W + vcol] = (u16)(' ' | (vattr << 8));
        }
    } else {
        vga[vrow * VGA_W + vcol] = (u16)((u8)c | (vattr << 8));
        vcol++;
        if (vcol >= VGA_W) {
            vcol = 0;
            vrow++;
        }
    }
    if (vrow >= VGA_H) {
        vga_scroll();
        vrow = VGA_H - 1;
    }
}

static void vga_clear(void)
{
    u32 i;
    for (i = 0; i < VGA_W * VGA_H; i++)
        vga[i] = (u16)(' ' | (vattr << 8));
    vrow = 0;
    vcol = 0;
}

static void vga_set_attr(u8 a)
{
    vattr = a;
}

static void putc(char c)
{
    vga_putc(c);
    if (c == '\n')
        serial_putc('\r');
    serial_putc(c);
}

static void puts(const char *s)
{
    while (*s)
        putc(*s++);
}

static void putu(u32 v)
{
    char b[11];
    int i = 10;
    b[10] = 0;
    if (!v) {
        putc('0');
        return;
    }
    while (v && i > 0) {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    }
    puts(&b[i]);
}

static void splash(void);
static int gfx_init(void);
static void gui_run(void);

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static const char kbd_map[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static const char kbd_map_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

static int shift_down = 0;

static char kbd_getc(void)
{
    for (;;) {
        if (!(inb(0x64) & 1))
            continue;
        u8 sc = inb(0x60);
        if (sc == 0x2A || sc == 0x36) {
            shift_down = 1;
            continue;
        }
        if (sc == 0xAA || sc == 0xB6) {
            shift_down = 0;
            continue;
        }
        if (sc & 0x80)
            continue;
        char c = shift_down ? kbd_map_shift[sc] : kbd_map[sc];
        if (c)
            return c;
    }
}


static void cputs(u8 attr, const char *s)
{
    vga_set_attr(attr);
    puts(s);
}

static void splash(void)
{
    putc('\n');
    cputs(0x0E, "   /\\_/\\     ");
    cputs(0x0B, "damir@kotos\n");
    cputs(0x0E, "  ( o.o )    ");
    cputs(0x08, "-----------------\n");
    cputs(0x0E, "   > ^ <     ");
    cputs(0x0A, "OS     ");
    cputs(0x07, "KotOS 1.0\n");
    cputs(0x0E, "             ");
    cputs(0x0A, "Kernel ");
    cputs(0x07, "native x86 (Multiboot)\n");
    cputs(0x0E, "             ");
    cputs(0x0A, "Shell  ");
    cputs(0x07, "kotos-sh\n");
    vga_set_attr(0x07);
    putc('\n');
}

static char line[128];
static int line_len = 0;

static void prompt(void)
{
    vga_set_attr(0x0A);
    puts("kotos");
    vga_set_attr(0x07);
    puts("> ");
}

static void reboot(void)
{
    while (inb(0x64) & 0x02)
        ;
    outb(0x64, 0xFE);
}


#define FS_MAX 64
#define FS_NAME 24
#define FS_DATA 512

typedef struct {
    char name[FS_NAME];
    char data[FS_DATA];
    u32 size;
    int used;
    int is_dir;
    int parent;
} fsnode;

static fsnode fs[FS_MAX];
static int cwd = 0;

extern u32 mb_info;

static void str_copy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void fs_init(void)
{
    int i;
    for (i = 0; i < FS_MAX; i++)
        fs[i].used = 0;
    fs[0].used = 1;
    fs[0].is_dir = 1;
    fs[0].parent = -1;
    fs[0].size = 0;
    str_copy(fs[0].name, "/", FS_NAME);
    cwd = 0;
}

static int fs_find(int dir, const char *name)
{
    int i;
    if (!name || !*name)
        return -1;
    if (streq(name, "/"))
        return 0;
    for (i = 1; i < FS_MAX; i++)
        if (fs[i].used && fs[i].parent == dir && streq(fs[i].name, name))
            return i;
    return -1;
}

static int fs_create(int dir, const char *name, int is_dir)
{
    int i;
    if (!name || !*name || streq(name, ".") || streq(name, ".."))
        return -1;
    if (fs_find(dir, name) >= 0)
        return -1;
    for (i = 1; i < FS_MAX; i++) {
        if (!fs[i].used) {
            fs[i].used = 1;
            fs[i].is_dir = is_dir;
            fs[i].parent = dir;
            fs[i].size = 0;
            fs[i].data[0] = 0;
            str_copy(fs[i].name, name, FS_NAME);
            return i;
        }
    }
    return -1;
}

static int fs_has_children(int dir)
{
    int i;
    for (i = 1; i < FS_MAX; i++)
        if (fs[i].used && fs[i].parent == dir)
            return 1;
    return 0;
}

static void fs_remove(int dir, const char *name)
{
    int idx = fs_find(dir, name);
    if (idx <= 0) {
        puts("rm: no such file\n");
        return;
    }
    if (fs[idx].is_dir && fs_has_children(idx)) {
        puts("rm: directory not empty\n");
        return;
    }
    fs[idx].used = 0;
}

static void fs_ls(void)
{
    int i;
    for (i = 1; i < FS_MAX; i++) {
        if (fs[i].used && fs[i].parent == cwd) {
            puts(fs[i].name);
            if (fs[i].is_dir)
                putc('/');
            puts("  ");
        }
    }
    putc('\n');
}

static void fs_pwd(void)
{
    int chain[16];
    int n = 0, i;
    char buf[160];
    int pos = 0;
    int cur = cwd;

    while (cur > 0 && n < 16) {
        chain[n++] = cur;
        cur = fs[cur].parent;
    }
    buf[pos++] = '/';
    for (i = n - 1; i >= 0; i--) {
        const char *nm = fs[chain[i]].name;
        while (*nm && pos < (int)sizeof(buf) - 2)
            buf[pos++] = *nm++;
        if (i > 0 && pos < (int)sizeof(buf) - 2)
            buf[pos++] = '/';
    }
    buf[pos] = 0;
    puts(buf);
    putc('\n');
}

static int fs_cd(const char *name)
{
    int idx;
    if (!*name || streq(name, "/")) {
        cwd = 0;
        return 1;
    }
    if (streq(name, "..")) {
        if (fs[cwd].parent >= 0)
            cwd = fs[cwd].parent;
        return 1;
    }
    idx = fs_find(cwd, name);
    if (idx < 0 || !fs[idx].is_dir) {
        puts("cd: no such directory\n");
        return 0;
    }
    cwd = idx;
    return 1;
}

static void fs_cat(const char *name)
{
    int idx = fs_find(cwd, name);
    if (idx < 0 || fs[idx].is_dir) {
        puts("cat: no such file\n");
        return;
    }
    if (fs[idx].size)
        puts(fs[idx].data);
}

static void fs_write(const char *name, const char *text)
{
    int idx = fs_find(cwd, name);
    if (idx < 0)
        idx = fs_create(cwd, name, 0);
    if (idx < 0) {
        puts("echo: cannot create file\n");
        return;
    }
    if (fs[idx].is_dir) {
        puts("echo: is a directory\n");
        return;
    }
    str_copy(fs[idx].data, text, FS_DATA);
    fs[idx].size = 0;
    while (fs[idx].data[fs[idx].size])
        fs[idx].size++;
}

static void run_command(const char *cmd)
{
    char verb[24];
    const char *arg;
    int i = 0, n = 0;

    while (cmd[i] == ' ')
        i++;
    while (cmd[i] && cmd[i] != ' ' && n < 23)
        verb[n++] = cmd[i++];
    verb[n] = 0;
    while (cmd[i] == ' ')
        i++;
    arg = cmd + i;
    if (!*verb)
        return;

    if (streq(verb, "help")) {
        puts("Commands: help, about, os, install gui, version, clear, reboot, halt\n");
        puts("Files:    ls, dir, cd, pwd, mkdir, touch, cat, echo, rm, free, uname, whoami\n");
    } else if (streq(verb, "about")) {
        puts("KotOS is a real operating system with its own kernel, no Linux.\n");
    } else if (streq(verb, "os")) {
        splash();
    } else if (streq(verb, "gui") || (streq(verb, "install") && streq(arg, "gui"))) {
        if (gfx_init()) {
            gui_run();
            vga_clear();
            splash();
        } else {
            puts("VBE not available, GUI cannot be installed\n");
        }
    } else if (streq(verb, "version")) {
        puts("KotOS native kernel 1.0 (x86, Multiboot)\n");
    } else if (streq(verb, "clear")) {
        vga_clear();
    } else if (streq(verb, "reboot")) {
        puts("Rebooting...\n");
        reboot();
    } else if (streq(verb, "halt")) {
        puts("Halted.\n");
        for (;;)
            __asm__ volatile("cli; hlt");
    } else if (streq(verb, "ls") || streq(verb, "dir")) {
        fs_ls();
    } else if (streq(verb, "cd")) {
        fs_cd(arg);
    } else if (streq(verb, "pwd")) {
        fs_pwd();
    } else if (streq(verb, "mkdir")) {
        if (!*arg || fs_create(cwd, arg, 1) < 0)
            puts("mkdir: cannot create directory\n");
    } else if (streq(verb, "touch")) {
        if (!*arg)
            puts("touch: missing name\n");
        else if (fs_find(cwd, arg) < 0 && fs_create(cwd, arg, 0) < 0)
            puts("touch: cannot create file\n");
    } else if (streq(verb, "cat")) {
        if (!*arg)
            puts("cat: missing name\n");
        else
            fs_cat(arg);
    } else if (streq(verb, "rm")) {
        if (!*arg)
            puts("rm: missing name\n");
        else
            fs_remove(cwd, arg);
    } else if (streq(verb, "echo")) {
        const char *gt = arg;
        while (*gt && *gt != '>')
            gt++;
        if (*gt == '>') {
            char text[FS_DATA];
            char fname[FS_NAME];
            int t = 0, f = 0;
            const char *p = arg;
            while (p < gt) {
                if (!(*p == ' ' && t == 0) && t < FS_DATA - 1)
                    text[t++] = *p;
                p++;
            }
            while (t > 0 && text[t - 1] == ' ')
                t--;
            text[t] = 0;
            p = gt + 1;
            while (*p == ' ')
                p++;
            while (*p && *p != ' ' && f < FS_NAME - 1)
                fname[f++] = *p++;
            fname[f] = 0;
            if (!f)
                puts("echo: missing file name\n");
            else
                fs_write(fname, text);
        } else {
            puts(arg);
            putc('\n');
        }
    } else if (streq(verb, "uname")) {
        puts("KotOS 1.0 i386 Multiboot\n");
    } else if (streq(verb, "whoami")) {
        puts("root\n");
    } else if (streq(verb, "free")) {
        u32 up = 0;
        if (mb_info)
            up = *(volatile u32 *)(mb_info + 8);
        puts("Memory: ");
        putu(1024 + up);
        puts(" KB total (from Multiboot)\n");
    } else {
        puts("Unknown command: ");
        puts(verb);
        putc('\n');
    }
}
static void shell(void)
{
    for (;;) {
        char c = kbd_getc();
        if (c == '\n') {
            putc('\n');
            line[line_len] = 0;
            run_command(line);
            line_len = 0;
            prompt();
        } else if (c == '\b') {
            if (line_len) {
                line_len--;
                putc('\b');
            }
        } else if (c >= ' ' && c < 127 && line_len < 127) {
            line[line_len++] = c;
            putc(c);
        }
    }
}


static void puthex(u32 v);

static inline void outw(u16 port, u16 val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outl(u16 port, u32 val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline u32 inl(u16 port)
{
    u32 r;
    __asm__ volatile("inl %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static inline u16 inw(u16 port)
{
    u16 r;
    __asm__ volatile("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

#define VBE_IDX 0x1CE
#define VBE_DAT 0x1CF

static void vbe_w(u16 idx, u16 val)
{
    outw(VBE_IDX, idx);
    outw(VBE_DAT, val);
}

static u16 vbe_r(u16 idx)
{
    outw(VBE_IDX, idx);
    return inw(VBE_DAT);
}

static u32 *fb;
static u32 fb_w, fb_h, fb_pitch;
static u32 fb_addr;

static u32 pci_read32(u8 bus, u8 dev, u8 fn, u8 off)
{
    outl(0xCF8, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
                     ((u32)fn << 8) | (off & 0xFC));
    return inl(0xCFC);
}

static void pci_write32(u8 bus, u8 dev, u8 fn, u8 off, u32 val)
{
    outl(0xCF8, 0x80000000u | ((u32)bus << 16) | ((u32)dev << 11) |
                     ((u32)fn << 8) | (off & 0xFC));
    outl(0xCFC, val);
}

static u32 pci_find_vga_lfb(void)
{
    u8 dev;
    for (dev = 0; dev < 32; dev++) {
        u32 id = pci_read32(0, dev, 0, 0x00);
        u16 vendor = (u16)(id & 0xFFFF);
        u16 device = (u16)(id >> 16);
        if (vendor == 0x1234 && (device == 0x1111 || device == 0x1112)) {
            u32 cmd = pci_read32(0, dev, 0, 0x04);
            pci_write32(0, dev, 0, 0x04, cmd | 0x2 | 0x4);
            return pci_read32(0, dev, 0, 0x10) & 0xFFFFFFF0u;
        }
    }
    return 0;
}

static int gfx_init(void)
{
    u32 bar;
    u16 id = vbe_r(0);
    if ((id & 0xFFF0) != 0xB0C0)
        return 0;
    bar = pci_find_vga_lfb();
    puts("VGA BAR0: ");
    puthex(bar);
    putc('\n');
    if (!bar)
        bar = 0xE0000000u;
    vbe_w(4, 0);
    vbe_w(1, 1024);
    vbe_w(2, 768);
    vbe_w(3, 32);
    vbe_w(4, 0x41);
    fb = (u32 *)bar;
    fb_addr = bar;
    fb_w = 1024;
    fb_h = 768;
    fb_pitch = 1024 * 4;
    return 1;
}

static void px(u32 x, u32 y, u32 c)
{
    if (x < fb_w && y < fb_h)
        fb[(y * fb_pitch + x * 4) / 4] = c;
}

static void rect(u32 x, u32 y, u32 w, u32 h, u32 c)
{
    u32 i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            px(x + i, y + j, c);
}

static void frame(u32 x, u32 y, u32 w, u32 h, u32 c, u32 t)
{
    rect(x, y, w, t, c);
    rect(x, y + h - t, w, t, c);
    rect(x, y, t, h, c);
    rect(x + w - t, y, t, h, c);
}

static void desktop(void)
{
    u32 x, y;
    for (y = 0; y < fb_h; y++) {
        u32 k = y * 255 / fb_h;
        u32 c = 0x000000 | ((8 + k / 6) << 16) | ((12 + k / 4) << 8) | (24 + k / 3);
        for (x = 0; x < fb_w; x++)
            px(x, y, c);
    }
    rect(0, 0, fb_w, 36, 0x141C26);
    rect(0, 36, fb_w, 1, 0x2A3A4A);
    rect(16, 8, 20, 20, 0x22D3EE);
    rect(46, 8, 20, 20, 0x4ADE80);
    rect(fb_w - 150, 8, 130, 20, 0x1E2A36);

    rect(160, 130, 560, 380, 0x0E141B);
    frame(160, 130, 560, 380, 0x2A3A4A, 2);
    rect(160, 130, 560, 34, 0x2563EB);
    rect(672, 138, 12, 12, 0xF87171);
    rect(690, 138, 12, 12, 0xFBBF24);
    rect(708, 138, 12, 12, 0x4ADE80);

    rect(180, 190, 520, 14, 0x1F2A37);
    rect(180, 216, 300, 10, 0x18222E);
    rect(180, 240, 420, 10, 0x18222E);
    rect(180, 264, 360, 10, 0x18222E);
    rect(180, 300, 240, 28, 0x2563EB);
    rect(180, 350, 200, 120, 0x101820);
    frame(180, 350, 200, 120, 0x2A3A4A, 1);
    rect(400, 350, 300, 120, 0x101820);
    frame(400, 350, 300, 120, 0x2A3A4A, 1);
}

static const unsigned char cursor_bmp[19][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,0,1,0,0,0,0,0,0,0,0,0},
    {1,0,0,1,0,0,0,0,0,0,0,0},
    {1,0,0,0,1,0,0,0,0,0,0,0},
    {1,0,0,0,0,1,0,0,0,0,0,0},
    {1,0,0,0,0,0,1,0,0,0,0,0},
    {1,0,0,0,0,0,0,1,0,0,0,0},
    {1,0,0,0,0,0,0,0,1,0,0,0},
    {1,0,0,0,0,0,0,0,0,1,0,0},
    {1,0,0,0,0,0,0,0,0,0,1,0},
    {1,0,0,0,0,0,1,1,1,1,1,0},
    {1,0,0,0,1,0,1,0,0,0,0,0},
    {1,0,0,1,0,1,0,1,0,0,0,0},
    {1,0,1,0,0,1,0,1,0,0,0,0},
    {1,1,0,0,0,0,1,0,1,0,0,0},
    {1,0,0,0,0,0,1,0,1,0,0,0},
    {0,0,0,0,0,0,0,1,0,1,0,0},
    {0,0,0,0,0,0,0,1,0,1,0,0}
};

static u32 cur_x = 500, cur_y = 400;

static void draw_cursor(void)
{
    u32 j, i;
    for (j = 0; j < 19; j++) {
        for (i = 0; i < 12; i++) {
            if (!cursor_bmp[j][i])
                continue;
            px(cur_x + i - 1, cur_y + j - 1, 0x000000);
            px(cur_x + i, cur_y + j, 0xFFFFFF);
        }
    }
}


static void vga_text_restore(void)
{
    static const u8 seq[5] = {0x03, 0x00, 0x03, 0x00, 0x02};
    static const u8 crtc[25] = {
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
        0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF
    };
    static const u8 gfx[9] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF};
    u8 i;

    vbe_w(4, 0);
    outb(0x3C2, 0x67);
    for (i = 0; i < 5; i++) {
        outb(0x3C4, i);
        outb(0x3C5, seq[i]);
    }
    outb(0x3D4, 0x11);
    outb(0x3D5, 0x0E);
    for (i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc[i]);
    }
    for (i = 0; i < 9; i++) {
        outb(0x3CE, i);
        outb(0x3CF, gfx[i]);
    }
    (void)inb(0x3DA);
    for (i = 0; i < 0x14; i++) {
        u8 v = i < 0x10 ? i : (i == 0x10 ? 0x0C : (i == 0x11 ? 0x00 : (i == 0x12 ? 0x0F : 0x08)));
        outb(0x3C0, i);
        outb(0x3C0, v);
    }
    outb(0x3C0, 0x20);
    vga_clear();
}

static u8 kbd_scancode(void)
{
    while (!(inb(0x64) & 1))
        ;
    return inb(0x60);
}

static void gui_run(void)
{
    int e0 = 0;
    puts("GUI: Bochs VBE 1024x768x32, LFB ");
    puthex(fb_addr);
    putc('\n');
    desktop();
    draw_cursor();
    for (;;) {
        u8 sc = kbd_scancode();
        if (sc == 0xE0) {
            e0 = 1;
            continue;
        }
        if (sc & 0x80) {
            e0 = 0;
            continue;
        }
        u32 nx = cur_x, ny = cur_y;
        if (e0 && sc == 0x4B) nx -= 8;
        else if (e0 && sc == 0x4D) nx += 8;
        else if (e0 && sc == 0x48) ny -= 8;
        else if (e0 && sc == 0x50) ny += 8;
        else if (!e0 && sc == 0x01) {
            vga_text_restore();
            puts("GUI: closed, back to terminal\n");
            return;
        }
        e0 = 0;
        if (nx != cur_x || ny != cur_y) {
            rect(cur_x, cur_y, 12, 19, 0);
            cur_x = nx;
            cur_y = ny;
            desktop();
            draw_cursor();
        }
    }
}

extern u32 mb_magic;
extern u32 mb_info;

static void puthex(u32 v)
{
    static const char h[] = "0123456789ABCDEF";
    int i;
    puts("0x");
    for (i = 28; i >= 0; i -= 4)
        putc(h[(v >> i) & 15]);
}

void kmain(void)
{
    serial_init();
    puts("MB magic: ");
    puthex(mb_magic);
    putc('\n');
    if (mb_magic == 0x2BADB002 && mb_info) {
        volatile u32 *mbi = (volatile u32 *)mb_info;
        u32 flags = mbi[0];
        puts("MB flags: ");
        puthex(flags);
        putc('\n');
        if (flags & 0x1000) {
            u32 pitch = *(volatile u32 *)(mb_info + 96);
            u32 w = *(volatile u32 *)(mb_info + 100);
            u32 h = *(volatile u32 *)(mb_info + 104);
            u32 bpp = *(volatile u32 *)(mb_info + 108);
            u32 addr_lo = *(volatile u32 *)(mb_info + 88);
            puts("FB addr: ");
            puthex(addr_lo);
            puts(" pitch: ");
            putu(pitch);
            puts(" size: ");
            putu(w);
            putc('x');
            putu(h);
            puts(" bpp: ");
            putu(bpp);
            putc('\n');
        } else {
            puts("FB: no video mode\n");
        }
    }

    serial_init();
    vga_clear();

    fs_init();
    splash();
    puts("Type help for a list of commands.\n\n");
    prompt();

    shell();
}
