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

static void run_command(const char *cmd)
{
    if (!*cmd)
        return;
    if (streq(cmd, "help")) {
        puts("Commands: help, about, os, version, clear, reboot, halt\n");
    } else if (streq(cmd, "about")) {
        puts("KotOS is a hobby OS with its own kernel, no Linux.\n");
    } else if (streq(cmd, "os")) {
        splash();
    } else if (streq(cmd, "version")) {
        puts("KotOS native kernel 1.0 (x86, Multiboot)\n");
    } else if (streq(cmd, "clear")) {
        vga_clear();
    } else if (streq(cmd, "reboot")) {
        puts("Rebooting...\n");
        reboot();
    } else if (streq(cmd, "halt")) {
        puts("Halted.\n");
        for (;;)
            __asm__ volatile("cli; hlt");
    } else {
        puts("Unknown command: ");
        puts(cmd);
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
            puts("GUI: Esc, halted\n");
            for (;;) __asm__ volatile("cli; hlt");
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

    splash();
    puts("Type help for a list of commands.\n\n");
    prompt();

    if (gfx_init()) {
        gui_run();
    }

    shell();
}
