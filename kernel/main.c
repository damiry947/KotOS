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
        puts("Commands: help, about, version, clear, reboot, halt\n");
    } else if (streq(cmd, "about")) {
        puts("KotOS is a hobby OS with its own kernel, no Linux.\n");
    } else if (streq(cmd, "version")) {
        puts("KotOS native kernel 0.3 (x86, Multiboot)\n");
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

void kmain(void)
{
    serial_init();
    vga_clear();

    vga_set_attr(0x0B);
    puts("  KotOS v0.3 - native kernel\n");
    vga_set_attr(0x07);
    puts("  x86, Multiboot, no Linux\n\n");
    puts("Type help for a list of commands.\n\n");
    prompt();

    shell();
}
