#include "../../kernel/font866.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 r; __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(p)); return r; }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(p)); }
static inline u16 inw(u16 p) { u16 r; __asm__ volatile("inw %1, %0" : "=a"(r) : "Nd"(p)); return r; }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline u32 inl(u16 p) { u32 r; __asm__ volatile("inl %1, %0" : "=a"(r) : "Nd"(p)); return r; }

static u32 *fb;
static u32 fw, fh, pitch;
static u32 cx, cy;
static u32 fg = 0xE6EDF3, bg = 0x0B0F14;

static void serial_init(void)
{
    outb(0x3F8 + 1, 0x00); outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03); outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03); outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static void sport(char c)
{
    while (!(inb(0x3F8 + 5) & 0x20)) ;
    outb(0x3F8, (u8)c);
}

static void pci_check(void)
{
    u8 dev;
    for (dev = 0; dev < 32; dev++) {
        u32 id;
        outl(0xCF8, 0x80000000u | ((u32)dev << 11) | 0x00);
        id = inl(0xCFC);
        if ((id & 0xFFFF) == 0x1234 && (id >> 16) == 0x1111) {
            u32 cmd;
            outl(0xCF8, 0x80000000u | ((u32)dev << 11) | 0x04);
            cmd = inl(0xCFC) | 0x2 | 0x4;
            outl(0xCFC, cmd);
            outl(0xCF8, 0x80000000u | ((u32)dev << 11) | 0x10);
            fb = (u32 *)(inl(0xCFC) & 0xFFFFFFF0u);
            return;
        }
    }
}

static void vbe_w(u16 idx, u16 val) { outw(0x1CE, idx); outw(0x1CF, val); }

static int fb_init(void)
{
    u16 id;
    fb = 0;
    outw(0x1CE, 0);
    id = inw(0x1CF);
    if ((id & 0xFFF0) != 0xB0C0)
        return 0;
    pci_check();
    if (!fb)
        fb = (u32 *)0xFD000000u;
    vbe_w(4, 0);
    vbe_w(1, 1024);
    vbe_w(2, 768);
    vbe_w(3, 32);
    vbe_w(4, 0x41);
    fw = 1024;
    fh = 768;
    pitch = 1024 * 4;
    return 1;
}

static void px(u32 x, u32 y, u32 c)
{
    if (x < fw && y < fh)
        fb[(y * pitch + x * 4) / 4] = c;
}

static void rect(u32 x, u32 y, u32 w, u32 h, u32 c)
{
    u32 i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            px(x + i, y + j, c);
}

static void glyph(u32 x, u32 y, u8 code)
{
    u32 r, b;
    for (r = 0; r < 16; r++) {
        u8 bits = font866[(u32)code * 16 + r];
        for (b = 0; b < 8; b++)
            px(x + b, y + r, (bits & (0x80 >> b)) ? fg : bg);
    }
}

static u8 cp866(u32 cp)
{
    if (cp >= 0x410 && cp <= 0x42F) return (u8)(0x80 + (cp - 0x410));
    if (cp >= 0x430 && cp <= 0x43F) return (u8)(0xA0 + (cp - 0x430));
    if (cp >= 0x440 && cp <= 0x44F) return (u8)(0xE0 + (cp - 0x440));
    if (cp == 0x401) return 0xF0;
    if (cp == 0x451) return 0xF1;
    return '?';
}

static void putc_c(u8 c)
{
    if (c == '\n') { cx = 0; cy++; return; }
    glyph(cx * 8, cy * 16, c);
    cx++;
    if (cx >= fw / 8) { cx = 0; cy++; }
}

static void puts_c(const char *s)
{
    while (*s) {
        u8 c = (u8)*s++;
        if (c < 0x80) { putc_c(c); sport((char)c); }
        else if ((c == 0xD0 || c == 0xD1) && *s) {
            u8 c2 = (u8)*s++;
            putc_c(cp866(((c & 0x1F) << 6) | (c2 & 0x3F)));
            sport((char)c); sport((char)c2);
        } else sport('?');
    }
}

void kmain64(void)
{
    serial_init();
    if (fb_init()) {
        rect(0, 0, fw, fh, bg);
        cx = 4; cy = 2;
        puts_c("KotOS x64\n");
        puts_c("Framebuffer console works\n");
        puts_c("Консоль x64 работает\n");
        puts_c("C payload, long mode, CP866\n");
    } else {
        puts_c("KotOS x64: VBE unavailable\n");
    }
    for (;;)
        __asm__ volatile("hlt");
}

__attribute__((section(".text.entry"), noreturn))
void start64(void)
{
    kmain64();
    for (;;)
        __asm__ volatile("hlt");
}
