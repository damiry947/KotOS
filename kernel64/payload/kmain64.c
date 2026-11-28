#include "../../kernel/font866.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 r; __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(p)); return r; }
static inline void outw(u16 p, u16 v) { __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(p)); }
static inline u16 inw(u16 p) { u16 r; __asm__ volatile("inw %1, %0" : "=a"(r) : "Nd"(p)); return r; }
static inline void outl(u16 p, u32 v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline u32 inl(u16 p) { u32 r; __asm__ volatile("inl %1, %0" : "=a"(r) : "Nd"(p)); return r; }

static u32 *fb;
static u32 fw, fh, pitch, cx, cy;
static u32 fg = 0xE6EDF3, bg = 0x0B0F14;

#define CON_COLS 78
#define CON_ROWS 26
#define PANEL_H 40

static u8 conbuf[CON_ROWS][CON_COLS];
static u32 con_ox = 52, con_oy = 106;

typedef struct { int x, y, w, h, vis; const char *title; int term; } win_t;

static win_t wins[2] = {
    { 120, 72, 640, 464, 1, "Терминал", 1 },
    { 750, 150, 262, 220, 1, "О системе", 0 }
};
static int order[2] = { 0, 1 };
static int drag_win = -1, drag_dx, drag_dy, mouse_prev, drag_tick;

static void draw_clock(void);
static void draw_scene(void);
static void draw_terminal_cells(void);
static void cursor_draw(void);
static void cursor_erase(void);
static u8 cp866(u32 cp);

extern void linuxabi_init(u64 mb_info);
extern int linuxabi_exec(void);

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
        outl(0xCF8, 0x80000000u | ((u32)dev << 11));
        id = inl(0xCFC);
        if ((id & 0xFFFF) == 0x1234 && (id >> 16) == 0x1111) {
            outl(0xCF8, 0x80000000u | ((u32)dev << 11) | 0x04);
            outl(0xCFC, inl(0xCFC) | 0x2 | 0x4);
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
    fw = 1024; fh = 768; pitch = 4096;
    return 1;
}

static u32 px_get(u32 x, u32 y)
{
    if (x < fw && y < fh)
        return fb[(y * pitch + x * 4) / 4];
    return 0;
}

static void px(u32 x, u32 y, u32 c)
{
    if (x < fw && y < fh)
        fb[(y * pitch + x * 4) / 4] = c;
}

static void fill32(u32 x, u32 y, u32 w, u32 h, u32 c)
{
    u32 r, i, x1 = x + w, y1 = y + h;
    if (x1 > fw) x1 = fw;
    if (y1 > fh) y1 = fh;
    for (r = y; r < y1; r++) {
        u32 *p = (u32 *)((u8 *)fb + r * pitch) + x;
        for (i = x; i < x1; i++)
            *p++ = c;
    }
}

static void rect(u32 x, u32 y, u32 w, u32 h, u32 c)
{
    if (x < fw && y < fh)
        fill32(x, y, w, h, c);
}

static void glyph_c(u32 x, u32 y, u8 code, u32 f, u32 b)
{
    u32 r, i;
    if (x + 8 <= fw && y + 16 <= fh) {
        for (r = 0; r < 16; r++) {
            u8 bits = font866[(u32)code * 16 + r];
            u32 *p = (u32 *)((u8 *)fb + (y + r) * pitch) + x;
            for (i = 0; i < 8; i++)
                p[i] = (bits & (0x80 >> i)) ? f : b;
        }
        return;
    }
    for (r = 0; r < 16; r++) {
        u8 bits = font866[(u32)code * 16 + r];
        for (i = 0; i < 8; i++)
            px(x + i, y + r, (bits & (0x80 >> i)) ? f : b);
    }
}

static void glyph(u32 x, u32 y, u8 code)
{
    glyph_c(x, y, code, fg, bg);
}

static void draw_text(u32 x, u32 y, const char *s, u32 f, u32 b)
{
    u32 i = 0;
    while (*s) {
        glyph_c(x + i * 8, y, (u8)*s++, f, b);
        i++;
    }
}

static u8 cmos_read(u8 r)
{
    outb(0x70, r);
    return inb(0x71);
}

static u8 bcd2(u8 v)
{
    return (u8)((v >> 4) * 10 + (v & 15));
}

static u32 read_secs(void)
{
    u8 h = cmos_read(0x04), m = cmos_read(0x02), sec = cmos_read(0x00);
    u8 b = cmos_read(0x0B);
    if (!(b & 0x04)) { h = bcd2(h); m = bcd2(m); sec = bcd2(sec); }
    return (u32)h * 3600 + m * 60 + sec;
}

static void draw_utf8(u32 x, u32 y, const char *s, u32 f, u32 b)
{
    u32 i = 0;
    while (*s) {
        u8 c = (u8)*s++;
        if (c < 0x80)
            glyph_c(x + i * 8, y, c, f, b);
        else if ((c == 0xD0 || c == 0xD1) && *s) {
            u8 c2 = (u8)*s++;
            glyph_c(x + i * 8, y, cp866(((c & 0x1F) << 6) | (c2 & 0x3F)), f, b);
        }
        i++;
    }
}

static void draw_clock(void)
{
    u8 h = cmos_read(0x04), m = cmos_read(0x02), sec = cmos_read(0x00);
    u8 b = cmos_read(0x0B);
    char t[9];
    if (!(b & 0x04)) { h = bcd2(h); m = bcd2(m); sec = bcd2(sec); }
    t[0] = (char)('0' + h / 10); t[1] = (char)('0' + h % 10); t[2] = ':';
    t[3] = (char)('0' + m / 10); t[4] = (char)('0' + m % 10); t[5] = ':';
    t[6] = (char)('0' + sec / 10); t[7] = (char)('0' + sec % 10); t[8] = 0;
    rect(fw - 112, fh - PANEL_H + 8, 96, 24, 0x141C26);
    draw_text(fw - 104, fh - PANEL_H + 12, t, 0x9FB3C8, 0x141C26);
}

static void draw_taskbar(void)
{
    rect(0, fh - PANEL_H, fw, PANEL_H, 0x141C26);
    rect(0, fh - PANEL_H, fw, 2, 0x223247);
    rect(16, fh - PANEL_H + 8, 112, 24, 0x1E2A3A);
    draw_utf8(24, fh - PANEL_H + 12, "Терминал", 0xE6EDF3, 0x1E2A3A);
    rect(136, fh - PANEL_H + 8, 112, 24, 0x1E2A3A);
    draw_utf8(144, fh - PANEL_H + 12, "О системе", 0xE6EDF3, 0x1E2A3A);
}

static void term_origin(void)
{
    con_ox = (u32)(wins[0].x + 4);
    con_oy = (u32)(wins[0].y + 34);
}

static void draw_terminal_cells(void)
{
    u32 r, c;
    term_origin();
    for (r = 0; r < CON_ROWS; r++)
        for (c = 0; c < CON_COLS; c++)
            glyph_c(con_ox + c * 8, con_oy + r * 16, conbuf[r][c], fg, bg);
}

static void draw_win(win_t *w)
{
    rect((u32)(w->x - 2), (u32)(w->y - 2), (u32)(w->w + 4), (u32)(w->h + 4), 0x0A121B);
    rect((u32)w->x, (u32)w->y, (u32)w->w, (u32)w->h, 0x223247);
    draw_utf8((u32)(w->x + 10), (u32)(w->y + 7), w->title, 0xE6EDF3, 0x223247);
    rect((u32)(w->x + w->w - 26), (u32)(w->y + 7), 16, 16, 0xE05A47);
    glyph_c((u32)(w->x + w->w - 22), (u32)(w->y + 7), 'x', 0xFFFFFF, 0xE05A47);
    rect((u32)(w->x + 2), (u32)(w->y + 30), (u32)(w->w - 4), (u32)(w->h - 32), bg);
    if (w->term) {
        draw_terminal_cells();
    } else {
        draw_utf8((u32)(w->x + 12), (u32)(w->y + 48), "KotOS x64", 0x22D3EE, bg);
        draw_utf8((u32)(w->x + 12), (u32)(w->y + 72), "Своё ядро, long mode", 0x9FB3C8, bg);
        draw_utf8((u32)(w->x + 12), (u32)(w->y + 92), "Шелл, RAMFS, GUI", 0x9FB3C8, bg);
        draw_utf8((u32)(w->x + 12), (u32)(w->y + 112), "Версия 1.0", 0x9FB3C8, bg);
    }
}

static void draw_icon(int x, int y, u32 c, const char *label)
{
    rect((u32)x, (u32)y, 48, 48, c);
    rect((u32)(x + 6), (u32)(y + 6), 36, 36, 0x0E1620);
    rect((u32)(x + 12), (u32)(y + 12), 24, 24, c);
    draw_utf8((u32)(x - 6), (u32)(y + 54), label, 0xC9D6E2, 0x0E1620);
}

static void draw_scene(void)
{
    int i;
    rect(0, 0, fw, fh, 0x0E1620);
    draw_icon(24, 64, 0x22D3EE, "Терминал");
    draw_icon(24, 160, 0xE05A47, "О системе");
    for (i = 0; i < 2; i++) {
        int k = order[i];
        if (wins[k].vis)
            draw_win(&wins[k]);
    }
    draw_taskbar();
    draw_clock();
}

static int mouse_cycle, mouse_buttons, mouse_dx, mouse_dy;
static u8 mouse_pkt[3];
static int mx = 512, my = 384;
static u32 cbg[19][12];
static int csaved;

static const unsigned char cursor_bmp_local[19][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},{1,1,0,0,0,0,0,0,0,0,0,0},
    {1,0,1,0,0,0,0,0,0,0,0,0},{1,0,0,1,0,0,0,0,0,0,0,0},
    {1,0,0,0,1,0,0,0,0,0,0,0},{1,0,0,0,0,1,0,0,0,0,0,0},
    {1,0,0,0,0,0,1,0,0,0,0,0},{1,0,0,0,0,0,0,1,0,0,0,0},
    {1,0,0,0,0,0,0,0,1,0,0,0},{1,0,0,0,0,0,0,0,0,1,0,0},
    {1,0,0,0,0,0,0,0,0,0,1,0},{1,0,0,0,0,0,1,1,1,1,1,0},
    {1,0,0,0,1,0,1,0,0,0,0,0},{1,0,0,1,0,1,0,1,0,0,0,0},
    {1,0,1,0,0,1,0,1,0,0,0,0},{1,1,0,0,0,0,1,0,1,0,0,0},
    {1,0,0,0,0,0,1,0,1,0,0,0},{0,0,0,0,0,0,0,1,0,1,0,0},
    {0,0,0,0,0,0,0,1,0,1,0,0}
};

static void cursor_draw(void)
{
    int j, i;
    for (j = 0; j < 19; j++)
        for (i = 0; i < 12; i++)
            cbg[j][i] = px_get((u32)(mx + i - 1), (u32)(my + j - 1));
    for (j = 0; j < 19; j++)
        for (i = 0; i < 12; i++) {
            if (!cursor_bmp_local[j][i]) continue;
            px((u32)(mx + i - 1), (u32)(my + j - 1), 0x000000);
            px((u32)(mx + i), (u32)(my + j), 0xFFFFFF);
        }
    csaved = 1;
}

static void cursor_erase(void)
{
    int j, i;
    if (!csaved) return;
    for (j = 0; j < 19; j++)
        for (i = 0; i < 12; i++)
            px((u32)(mx + i - 1), (u32)(my + j - 1), cbg[j][i]);
    csaved = 0;
}

static void mouse_init(void)
{
    u8 st;
    int t;
    for (t = 0; t < 100000 && (inb(0x64) & 2); t++) ;
    outb(0x64, 0xA8);
    for (t = 0; t < 100000 && (inb(0x64) & 2); t++) ;
    outb(0x64, 0x20);
    for (t = 0; t < 100000 && !(inb(0x64) & 1); t++) ;
    st = inb(0x60);
    st |= 0x02; st &= (u8)~0x20;
    for (t = 0; t < 100000 && (inb(0x64) & 2); t++) ;
    outb(0x64, 0x60);
    for (t = 0; t < 100000 && (inb(0x64) & 2); t++) ;
    outb(0x60, st);
    for (t = 0; t < 100000 && (inb(0x64) & 2); t++) ;
    outb(0x64, 0xD4);
    for (t = 0; t < 100000 && (inb(0x64) & 2); t++) ;
    outb(0x60, 0xF4);
    for (t = 0; t < 100000 && (inb(0x64) & 1); t++)
        (void)inb(0x60);
    mouse_cycle = 0;
}

static int mouse_poll(void)
{
    u8 st, d;
    int dx, dy;
    if (!(inb(0x64) & 1)) return 0;
    st = inb(0x64);
    if (!(st & 0x20)) return 0;
    d = inb(0x60);
    if (mouse_cycle == 0 && ((d & 0x08) == 0 || (d & 0x80))) return 0;
    mouse_pkt[mouse_cycle++] = d;
    if (mouse_cycle < 3) return 0;
    mouse_cycle = 0;
    dx = mouse_pkt[1]; dy = mouse_pkt[2];
    if (mouse_pkt[0] & 0x10) dx -= 256;
    if (mouse_pkt[0] & 0x20) dy -= 256;
    mouse_dx = dx; mouse_dy = dy;
    mouse_buttons = mouse_pkt[0] & 7;
    return 1;
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

static void term_redraw_cell(u32 col, u32 row)
{
    if (!wins[0].vis) return;
    term_origin();
    glyph_c(con_ox + col * 8, con_oy + row * 16, conbuf[row][col], fg, bg);
}

static void term_scroll(void)
{
    u32 r, c;
    for (r = 0; r + 1 < CON_ROWS; r++)
        for (c = 0; c < CON_COLS; c++)
            conbuf[r][c] = conbuf[r + 1][c];
    for (c = 0; c < CON_COLS; c++)
        conbuf[CON_ROWS - 1][c] = ' ';
    if (wins[0].vis)
        draw_terminal_cells();
}

static void putc_c(u8 c)
{
    if (c == '\n') { cx = 0; cy++; }
    else if (c == '\b') {
        if (cx) { cx--; conbuf[cy][cx] = ' '; term_redraw_cell(cx, cy); }
        return;
    } else {
        conbuf[cy][cx] = c;
        term_redraw_cell(cx, cy);
        cx++;
        if (cx >= CON_COLS) { cx = 0; cy++; }
    }
    if (cy >= CON_ROWS) { term_scroll(); cy = CON_ROWS - 1; }
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

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void clear_screen(void)
{
    u32 r, c;
    for (r = 0; r < CON_ROWS; r++)
        for (c = 0; c < CON_COLS; c++)
            conbuf[r][c] = ' ';
    cx = 0; cy = 0;
    if (wins[0].vis)
        draw_terminal_cells();
}

static int kbd_poll(void)
{
    static const char kmap[128] = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*', 0, ' '
    };
    static const char kmap_s[128] = {
        0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
        0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
        '*', 0, ' '
    };
    static int shift;
    u8 sc, st;
    char c;
    st = inb(0x64);
    if (!(st & 1) || (st & 0x20))
        return -1;
    sc = inb(0x60);
    if (sc == 0x2A || sc == 0x36) { shift = 1; return -1; }
    if (sc == 0xAA || sc == 0xB6) { shift = 0; return -1; }
    if (sc & 0x80)
        return -1;
    c = shift ? kmap_s[sc] : kmap[sc];
    return c ? (int)(u8)c : -1;
}


#define FS_MAX 16
#define FS_NAME 24
#define FS_DATA 256

static struct {
    char name[FS_NAME];
    char data[FS_DATA];
    u32 size;
    int used;
} fs[FS_MAX];

static void str_copy(char *d, const char *s, int max)
{
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static void fs_init(void)
{
    int i;
    for (i = 0; i < FS_MAX; i++)
        fs[i].used = 0;
}

static int fs_find(const char *name)
{
    int i;
    for (i = 1; i < FS_MAX; i++)
        if (fs[i].used && streq(fs[i].name, name))
            return i;
    return -1;
}

static int fs_create(const char *name)
{
    int i;
    if (!*name || fs_find(name) >= 0)
        return -1;
    for (i = 1; i < FS_MAX; i++) {
        if (!fs[i].used) {
            fs[i].used = 1;
            fs[i].size = 0;
            fs[i].data[0] = 0;
            str_copy(fs[i].name, name, FS_NAME);
            return i;
        }
    }
    return -1;
}

static void fs_look(void)
{
    int i, n = 0;
    for (i = 1; i < FS_MAX; i++) {
        if (fs[i].used) {
            puts_c(fs[i].name);
            puts_c("  ");
            n++;
        }
    }
    if (!n)
        puts_c("(пусто)");
    putc_c('\n');
    sport('\n');
}

static void fs_show(const char *name)
{
    int i = fs_find(name);
    if (i < 0) {
        puts_c("show: нет файла\n");
        return;
    }
    puts_c(fs[i].data);
    putc_c('\n');
    sport('\n');
}

static void fs_write(const char *name, const char *text, int append)
{
    int i = fs_find(name);
    u32 pos;
    if (i < 0)
        i = fs_create(name);
    if (i < 0) {
        puts_c("say: не создать файл\n");
        return;
    }
    pos = append ? fs[i].size : 0;
    if (append && pos && fs[i].data[pos - 1] != '\n' && pos < FS_DATA - 1)
        fs[i].data[pos++] = '\n';
    while (*text && pos < FS_DATA - 1)
        fs[i].data[pos++] = *text++;
    fs[i].data[pos] = 0;
    fs[i].size = pos;
}

static void fs_del(const char *name)
{
    int i = fs_find(name);
    if (i < 0) {
        puts_c("del: нет файла\n");
        return;
    }
    fs[i].used = 0;
}

static int next_tok(const char **pp, char *out, int max)
{
    const char *p = *pp;
    int n = 0;
    while (*p == ' ') p++;
    if (!*p) { out[0] = 0; return 0; }
    while (*p && *p != ' ' && n < max - 1)
        out[n++] = *p++;
    out[n] = 0;
    *pp = p;
    return 1;
}

static void run_cmd(const char *s)
{
    char v[16], a[FS_NAME];
    const char *p = s;
    int have = next_tok(&p, v, 16);

    if (!have)
        return;
    if (streq(v, "help")) {
        puts_c("Команды: help, about, clear, look, newfile, show, del, linux,\n");
        puts_c("         say <текст> > <файл>, say <текст> >> <файл>\n");
    } else if (streq(v, "about")) {
        puts_c("KotOS x64 — 64-битное ядро, C payload + RAMFS.\n");
    } else if (streq(v, "clear")) {
        clear_screen();
    } else if (streq(v, "linux") || streq(v, "exec")) {
        linuxabi_exec();
    } else if (streq(v, "look")) {
        fs_look();
    } else if (streq(v, "newfile")) {
        if (!next_tok(&p, a, FS_NAME) || fs_create(a) < 0)
            puts_c("newfile: не создать\n");
    } else if (streq(v, "show")) {
        if (!next_tok(&p, a, FS_NAME))
            puts_c("show: нужно имя\n");
        else
            fs_show(a);
    } else if (streq(v, "del")) {
        if (!next_tok(&p, a, FS_NAME))
            puts_c("del: нужно имя\n");
        else
            fs_del(a);
    } else if (streq(v, "say") || streq(v, "echo")) {
        const char *gt = p;
        int append = 0;
        while (*gt) {
            if (gt[0] == '>' && gt[1] == '>') { append = 1; break; }
            if (gt[0] == '>') break;
            gt++;
        }
        if (*gt == '>') {
            char text[FS_DATA], fname[FS_NAME];
            int t = 0, f = 0;
            const char *q = p;
            while (q < gt) {
                if (!(*q == ' ' && t == 0) && t < FS_DATA - 1)
                    text[t++] = *q;
                q++;
            }
            while (t > 0 && text[t - 1] == ' ')
                t--;
            text[t] = 0;
            q = gt + (append ? 2 : 1);
            while (*q == ' ') q++;
            while (*q && *q != ' ' && f < FS_NAME - 1)
                fname[f++] = *q++;
            fname[f] = 0;
            if (!f)
                puts_c("say: нужно имя файла\n");
            else
                fs_write(fname, text, append);
        } else {
            while (*p == ' ') p++;
            puts_c(p);
            putc_c('\n');
            sport('\n');
        }
    } else {
        puts_c("Неизвестно: ");
        puts_c(v);
        putc_c('\n');
        sport('\n');
    }
}

static unsigned long mb_info_addr;

void kputc(u8 c) { putc_c(c); }
void kputs(const char *s) { puts_c(s); }
void kserial_char(char c) { sport(c); }

static void print_modules(void)
{
    u32 *mbi = (u32 *)mb_info_addr;
    u32 count, addr, i;
    if (!mbi) {
        puts_c("mb: no info\n");
        return;
    }
    count = mbi[5];
    addr = mbi[6];
    puts_c("mb modules: ");
    putc_c((u8)('0' + count));
    puts_c("\n");
    for (i = 0; i < count && i < 8; i++) {
        u32 *m = (u32 *)(unsigned long)addr;
        u32 st = m[0], en = m[1];
        puts_c("  mod ");
        putc_c((u8)('0' + i));
        puts_c(": ");
        puts_c("0x");
        {
            char hx[9];
            int k;
            for (k = 0; k < 8; k++)
                hx[k] = "0123456789ABCDEF"[(st >> (28 - k * 4)) & 15];
            hx[8] = 0;
            puts_c(hx);
        }
        puts_c("-0x");
        {
            char hx[9];
            int k;
            for (k = 0; k < 8; k++)
                hx[k] = "0123456789ABCDEF"[(en >> (28 - k * 4)) & 15];
            hx[8] = 0;
            puts_c(hx);
        }
        puts_c("\n");
        addr += 16;
    }
}

void kmain64(unsigned long mb_info)
{
    char line[64];

    mb_info_addr = mb_info;
    int n = 0;
    int c;

    serial_init();
    fs_init();
    linuxabi_init((u64)mb_info);
    if (!fb_init()) {
        puts_c("KotOS x64: VBE unavailable\n");
        for (;;) __asm__ volatile("hlt");
    }
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    mouse_init();
    clear_screen();
    draw_scene();
    cursor_draw();
    cx = 0; cy = 0;
    puts_c("KotOS x64\n");
    puts_c("Framebuffer console works\n");
    puts_c("Консоль x64 работает\n\n");
    print_modules();
    puts_c("Введите help.\n\n");
    puts_c("x64> ");

    {
        int last = -1;
        for (;;) {
        if (mouse_poll()) {
            int btn = mouse_buttons & 1;
            cursor_erase();
            mx += mouse_dx;
            my -= mouse_dy;
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            if (mx > (int)fw - 1) mx = (int)fw - 1;
            if (my > (int)fh - 1) my = (int)fh - 1;
            if (btn && !mouse_prev) {
                int k = -1, i;
                for (i = 1; i >= 0; i--) {
                    int idx = order[i];
                    win_t *w = &wins[idx];
                    if (w->vis && mx >= w->x && mx < w->x + w->w &&
                        my >= w->y && my < w->y + w->h) {
                        k = idx;
                        break;
                    }
                }
                if (k >= 0) {
                    win_t *w = &wins[k];
                    if (mx >= w->x + w->w - 26 && mx < w->x + w->w - 10 &&
                        my >= w->y + 7 && my < w->y + 23) {
                        w->vis = 0;
                    } else {
                        if (k == order[0]) {
                            order[0] = order[1];
                            order[1] = k;
                        }
                        if (my < w->y + 30) {
                            drag_win = k;
                            drag_dx = mx - w->x;
                            drag_dy = my - w->y;
                        }
                    }
                    draw_scene();
                } else if (mx >= 24 && mx < 72 && my >= 64 && my < 118) {
                    wins[0].vis = 1;
                    order[0] = 1; order[1] = 0;
                    draw_scene();
                } else if (mx >= 24 && mx < 72 && my >= 160 && my < 214) {
                    wins[1].vis = 1;
                    order[0] = 0; order[1] = 1;
                    draw_scene();
                } else if (my >= (int)fh - PANEL_H) {
                    if (mx >= 16 && mx < 128) {
                        wins[0].vis = 1;
                        order[0] = 1; order[1] = 0;
                        draw_scene();
                    } else if (mx >= 136 && mx < 248) {
                        wins[1].vis = 1;
                        order[0] = 0; order[1] = 1;
                        draw_scene();
                    }
                }
            }
            if (!btn) {
                if (drag_win >= 0) {
                    drag_win = -1;
                    draw_scene();
                }
            } else if (drag_win >= 0) {
                win_t *w = &wins[drag_win];
                w->x = mx - drag_dx;
                w->y = my - drag_dy;
                if (w->x < 2) w->x = 2;
                if (w->y < 2) w->y = 2;
                if (w->x + w->w > (int)fw - 2) w->x = (int)fw - 2 - w->w;
                if (w->y + w->h > (int)fh - PANEL_H - 2)
                    w->y = (int)fh - PANEL_H - 2 - w->h;
                if ((++drag_tick & 3) == 0)
                    draw_scene();
            }
            mouse_prev = btn;
            cursor_draw();
        }
        {
            int sec = (int)read_secs();
            if (sec != last) { last = sec; draw_clock(); }
        }
        c = kbd_poll();
        if (c < 0)
            continue;
        if (c == '\n') {
            putc_c('\n');
            sport('\n');
            line[n] = 0;
            run_cmd(line);
            n = 0;
            puts_c("x64> ");
        } else if (c == '\b') {
            if (n) { n--; putc_c('\b'); sport('\b'); }
        } else if (c >= 32 && c < 127 && n < 63) {
            line[n++] = (char)c;
            putc_c((u8)c);
            sport((char)c);
        }
        }
    }
}

__attribute__((section(".text.entry"), noreturn))
void start64(unsigned long mb_info)
{
    kmain64(mb_info);
    for (;;)
        __asm__ volatile("hlt");
}
