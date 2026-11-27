typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

static u8 cp866_of(u32 cp);
static int fb_console_active;
static int gui_running;
static int gui_installed;
static u32 con_x, con_y;
static void fb_console_putc(char c);
static void fb_console_clear(void);

#include "font866.h"

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
    if (fb_console_active) {
        fb_console_clear();
        return;
    }
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
    if (fb_console_active)
        fb_console_putc(c);
    else
        vga_putc(c);
    if (c == '\n')
        serial_putc('\r');
    serial_putc(c);
}

static void emit(u8 b)
{
    if (fb_console_active)
        fb_console_putc((char)b);
    else
        vga_putc((char)b);
}

static void puts(const char *s)
{
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c < 0x80) {
            emit(c);
            serial_putc((char)c);
        } else if ((c == 0xD0 || c == 0xD1) && *s) {
            unsigned char c2 = (unsigned char)*s++;
            u32 cp = ((c & 0x1F) << 6) | (c2 & 0x3F);
            emit(cp866_of(cp));
            serial_putc((char)c);
            serial_putc((char)c2);
        } else {
            emit('?');
            serial_putc((char)c);
        }
    }
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


static u8 cp866_of(u32 cp)
{
    if (cp >= 0x0410 && cp <= 0x042F)
        return (u8)(0x80 + (cp - 0x0410));
    if (cp >= 0x0430 && cp <= 0x043F)
        return (u8)(0xA0 + (cp - 0x0430));
    if (cp >= 0x0440 && cp <= 0x044F)
        return (u8)(0xE0 + (cp - 0x0440));
    if (cp == 0x0401)
        return 0xF0;
    if (cp == 0x0451)
        return 0xF1;
    return '?';
}

static void splash(void);
static void run_user(void);
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

static void msleep(u32 ms)
{
    volatile u32 i;
    while (ms--)
        for (i = 0; i < 200000; i++)
            ;
}

static void beep(u32 hz, u32 ms)
{
    u32 d = 1193180 / hz;
    outb(0x43, 0xB6);
    outb(0x42, d & 0xFF);
    outb(0x42, (d >> 8) & 0xFF);
    outb(0x61, inb(0x61) | 3);
    msleep(ms);
    outb(0x61, inb(0x61) & 0xFC);
}

static void splash(void)
{
    beep(784, 70);
    beep(1046, 110);
    putc('\n');
    cputs(0x0E, "   /\\_/\\     ");
    cputs(0x0B, "damir@kotos\n");
    cputs(0x0E, "  ( o.o )    ");
    cputs(0x08, "-----------------\n");
    cputs(0x0E, "   > ^ <     ");
    cputs(0x0A, "ОС     ");
    cputs(0x07, "KotOS 1.0\n");
    msleep(80);
    cputs(0x0E, "             ");
    cputs(0x0A, "Ядро   ");
    cputs(0x07, "native x86 (Multiboot)\n");
    msleep(80);
    cputs(0x0E, "             ");
    cputs(0x0A, "Оболочка ");
    cputs(0x07, "kotos-sh\n");
    msleep(80);
    vga_set_attr(0x0A);
    puts("Загрузка [");
    {
        int i;
        for (i = 0; i < 12; i++) {
            putc('#');
            msleep(30);
        }
    }
    puts("] OK\n");
    beep(1318, 90);
}

static char hist[8][64];
static int hist_n = 0;
static u32 boot_sec = 0;
static u32 mem_upper_kb;

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


static int take_token(const char **pp, char *out, int max)
{
    const char *p = *pp;
    int n = 0;
    while (*p == ' ')
        p++;
    if (!*p) {
        out[0] = 0;
        return 0;
    }
    while (*p && *p != ' ' && n < max - 1)
        out[n++] = *p++;
    out[n] = 0;
    *pp = p;
    return 1;
}

static int str_contains(const char *hay, const char *needle)
{
    int i, j;
    if (!*needle)
        return 1;
    for (i = 0; hay[i]; i++) {
        for (j = 0; needle[j] && hay[i + j] == needle[j]; j++)
            ;
        if (!needle[j])
            return 1;
    }
    return 0;
}

static void fs_copy_file(const char *src, const char *dst)
{
    int si = fs_find(cwd, src), di;
    if (si < 0 || fs[si].is_dir) {
        puts("copy: no such file\n");
        return;
    }
    di = fs_find(cwd, dst);
    if (di < 0)
        di = fs_create(cwd, dst, 0);
    if (di < 0) {
        puts("copy: cannot create target\n");
        return;
    }
    if (fs[di].is_dir) {
        puts("copy: target is a directory\n");
        return;
    }
    str_copy(fs[di].data, fs[si].data, FS_DATA);
    fs[di].size = fs[si].size;
}

static void fs_move_file(const char *src, const char *dst)
{
    int si = fs_find(cwd, src), di;
    if (si < 0 || fs[si].is_dir) {
        puts("move: no such file\n");
        return;
    }
    if (streq(src, dst))
        return;
    di = fs_find(cwd, dst);
    if (di < 0)
        di = fs_create(cwd, dst, 0);
    if (di < 0 || fs[di].is_dir) {
        puts("move: cannot create target\n");
        return;
    }
    str_copy(fs[di].data, fs[si].data, FS_DATA);
    fs[di].size = fs[si].size;
    fs[si].used = 0;
}

static void fs_append_file(const char *name, const char *text)
{
    int idx = fs_find(cwd, name);
    u32 pos;
    if (idx < 0)
        idx = fs_create(cwd, name, 0);
    if (idx < 0 || fs[idx].is_dir) {
        puts("say: cannot write\n");
        return;
    }
    pos = fs[idx].size;
    if (pos && fs[idx].data[pos - 1] != '\n' && pos < FS_DATA - 1)
        fs[idx].data[pos++] = '\n';
    while (*text && pos < FS_DATA - 1)
        fs[idx].data[pos++] = *text++;
    fs[idx].data[pos] = 0;
    fs[idx].size = pos;
}

static int line_copy(const char *data, int *pos, char *out, int max)
{
    int n = 0, p = *pos;
    if (!data[p])
        return 0;
    while (data[p] && data[p] != '\n' && n < max - 1)
        out[n++] = data[p++];
    out[n] = 0;
    if (data[p] == '\n')
        p++;
    *pos = p;
    return 1;
}

static int fs_file_index(const char *name)
{
    int idx = fs_find(cwd, name);
    if (idx < 0 || fs[idx].is_dir) {
        puts("no such file\n");
        return -1;
    }
    return idx;
}

static void cmd_head(const char *name)
{
    int idx = fs_file_index(name);
    char buf[FS_DATA];
    int pos = 0, k = 0;
    if (idx < 0)
        return;
    while (k < 10 && line_copy(fs[idx].data, &pos, buf, FS_DATA)) {
        puts(buf);
        putc('\n');
        k++;
    }
}

static void cmd_tail(const char *name)
{
    int idx = fs_file_index(name);
    char buf[FS_DATA];
    int pos = 0, total = 0, skip;
    if (idx < 0)
        return;
    while (line_copy(fs[idx].data, &pos, buf, FS_DATA))
        total++;
    skip = total > 10 ? total - 10 : 0;
    pos = 0;
    while (line_copy(fs[idx].data, &pos, buf, FS_DATA)) {
        if (skip > 0) {
            skip--;
            continue;
        }
        puts(buf);
        putc('\n');
    }
}

static void cmd_count(const char *name)
{
    int idx = fs_file_index(name);
    u32 i, lines = 0, words = 0, bytes;
    int in_word = 0;
    if (idx < 0)
        return;
    bytes = fs[idx].size;
    for (i = 0; i < bytes; i++) {
        char c = fs[idx].data[i];
        if (c == '\n')
            lines++;
        if (c == ' ' || c == '\n' || c == '\t') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
    if (bytes && fs[idx].data[bytes - 1] != '\n')
        lines++;
    putu(lines);
    putc(' ');
    putu(words);
    putc(' ');
    putu(bytes);
    puts(" lines words bytes\n");
}

static void cmd_grep(const char *pattern, const char *name)
{
    int idx = fs_file_index(name);
    char buf[FS_DATA];
    int pos = 0;
    if (idx < 0)
        return;
    while (line_copy(fs[idx].data, &pos, buf, FS_DATA)) {
        if (str_contains(buf, pattern)) {
            puts(buf);
            putc('\n');
        }
    }
}

static u8 cmos_read(u8 reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static u8 from_bcd(u8 v)
{
    return (u8)((v >> 4) * 10 + (v & 0x0F));
}

static void print2(u32 v)
{
    putc((char)('0' + (v / 10) % 10));
    putc((char)('0' + v % 10));
}

static void cmd_clock(void)
{
    u8 sec = cmos_read(0x00), min = cmos_read(0x02), hour = cmos_read(0x04);
    u8 day = cmos_read(0x07), mon = cmos_read(0x08), year = cmos_read(0x09);
    u8 b = cmos_read(0x0B);
    if (!(b & 0x04)) {
        sec = from_bcd(sec); min = from_bcd(min); hour = from_bcd(hour);
        day = from_bcd(day); mon = from_bcd(mon); year = from_bcd(year);
    }
    puts("20");
    print2(year);
    putc('-');
    print2(mon);
    putc('-');
    print2(day);
    putc(' ');
    print2(hour);
    putc(':');
    print2(min);
    putc(':');
    print2(sec);
    putc('\n');
}


static u32 parse_num(const char *s)
{
    u32 v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (u32)(*s - '0');
        s++;
    }
    return v;
}

static u32 read_time_secs(void)
{
    u8 h = cmos_read(0x04), m = cmos_read(0x02), sec = cmos_read(0x00);
    u8 b = cmos_read(0x0B);
    if (!(b & 0x04)) {
        h = from_bcd(h);
        m = from_bcd(m);
        sec = from_bcd(sec);
    }
    return (u32)h * 3600 + (u32)m * 60 + sec;
}

static int str_less(const char *a, const char *b)
{
    while (*a && *a != '\n' && *b && *b != '\n') {
        if (*a != *b)
            return (unsigned char)*a < (unsigned char)*b;
        a++;
        b++;
    }
    return (*a == 0 || *a == '\n') && !(*b == 0 || *b == '\n');
}

static void tree_walk(int dir, int depth)
{
    int i, k;
    for (i = 1; i < FS_MAX; i++) {
        if (!fs[i].used || fs[i].parent != dir)
            continue;
        for (k = 0; k < depth; k++)
            puts("  ");
        puts(fs[i].name);
        if (fs[i].is_dir)
            putc('/');
        putc('\n');
        if (fs[i].is_dir && depth < 4)
            tree_walk(i, depth + 1);
    }
}

static void find_walk(int dir, const char *needle, int depth)
{
    int i, k;
    for (i = 1; i < FS_MAX; i++) {
        if (!fs[i].used || fs[i].parent != dir)
            continue;
        if (str_contains(fs[i].name, needle)) {
            for (k = 0; k < depth; k++)
                puts("  ");
            puts(fs[i].name);
            if (fs[i].is_dir)
                putc('/');
            putc('\n');
        }
        if (fs[i].is_dir && depth < 4)
            find_walk(i, needle, depth + 1);
    }
}

static void cmd_stat(const char *name)
{
    int idx = fs_find(cwd, name);
    if (idx <= 0) {
        puts("stat: no such entry\n");
        return;
    }
    puts("name: ");
    puts(fs[idx].name);
    puts("\ntype: ");
    puts(fs[idx].is_dir ? "directory" : "file");
    puts("\nsize: ");
    putu(fs[idx].size);
    puts(" bytes\n");
}

static void cmd_sort(const char *name)
{
    int idx = fs_file_index(name);
    int offs[64], n = 0, i, j, off = 0;
    if (idx < 0)
        return;
    while (fs[idx].data[off] && n < 64) {
        offs[n++] = off;
        while (fs[idx].data[off] && fs[idx].data[off] != '\n')
            off++;
        if (fs[idx].data[off] == '\n')
            off++;
    }
    for (i = 0; i < n; i++)
        for (j = 0; j + 1 < n - i; j++)
            if (str_less(fs[idx].data + offs[j + 1], fs[idx].data + offs[j])) {
                int t = offs[j];
                offs[j] = offs[j + 1];
                offs[j + 1] = t;
            }
    for (i = 0; i < n; i++) {
        const char *p = fs[idx].data + offs[i];
        while (*p && *p != '\n')
            putc(*p++);
        putc('\n');
    }
}

static void cmd_rev(const char *name)
{
    int idx = fs_file_index(name);
    char buf[FS_DATA];
    int pos = 0, len, i;
    if (idx < 0)
        return;
    while (line_copy(fs[idx].data, &pos, buf, FS_DATA)) {
        len = 0;
        while (buf[len])
            len++;
        for (i = len - 1; i >= 0; i--)
            putc(buf[i]);
        putc('\n');
    }
}

static void cmd_hex(const char *name)
{
    int idx = fs_file_index(name);
    u32 i, lim;
    static const char hx[] = "0123456789abcdef";
    if (idx < 0)
        return;
    lim = fs[idx].size < 64 ? fs[idx].size : 64;
    for (i = 0; i < lim; i++) {
        u8 b = (u8)fs[idx].data[i];
        putc(hx[b >> 4]);
        putc(hx[b & 15]);
        putc(' ');
        if ((i + 1) % 16 == 0)
            putc('\n');
    }
    putc('\n');
}

static void cmd_seq(const char *arg)
{
    u32 n = parse_num(arg), i;
    if (n > 200)
        n = 200;
    for (i = 1; i <= n; i++) {
        putu(i);
        putc('\n');
    }
}

static void cmd_uptime(void)
{
    u32 now = read_time_secs();
    u32 diff = now >= boot_sec ? now - boot_sec : now + 86400 - boot_sec;
    puts("up ");
    putu(diff / 60);
    puts(" min ");
    putu(diff % 60);
    puts(" sec\n");
}

static void cmd_sleep(const char *arg)
{
    u32 n = parse_num(arg);
    u32 target;
    if (!n)
        return;
    if (n > 10)
        n = 10;
    target = (read_time_secs() + n) % 86400;
    while (read_time_secs() != target)
        ;
}

static const char *const known_cmds[] = {
    "help", "about", "os", "gui", "install", "version", "clear", "reboot", "halt",
    "look", "ls", "dir", "go", "cd", "where", "pwd", "newdir", "mkdir",
    "newfile", "touch", "show", "cat", "say", "echo", "del", "rm",
    "copy", "cp", "move", "mv", "dropdir", "rmdir", "rename", "ren",
    "append", "top", "head", "bottom", "tail", "count", "wc",
    "findtext", "grep", "find", "tree", "sort", "rev", "hex", "seq",
    "stat", "du", "clock", "date", "uptime", "vars", "env", "host",
    "hostname", "me", "id", "ok", "true", "nope", "false", "mem",
    "free", "sysinfo", "uname", "who", "whoami", "history", "which",
    "sleep", "user", "userspace", "exit", "poweroff",
    "disk", "memmap", 0
};

static void cmd_which(const char *name)
{
    int i;
    for (i = 0; known_cmds[i]; i++) {
        if (streq(known_cmds[i], name)) {
            puts(name);
            puts(": built-in\n");
            return;
        }
    }
    puts("which: not found\n");
}

static void gui_start(void)
{
    if (gui_running) {
        puts("GUI уже запущен\n");
        return;
    }
    if (gfx_init()) {
        gui_installed = 1;
        gui_run();
    } else {
        puts("VBE недоступен, GUI не запустить\n");
    }
}


static inline u16 ata_inw(u16 port)
{
    u16 r;
    __asm__ volatile("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static u8 ata_status(void)
{
    return inb(0x1F7);
}

static int ata_wait_ready(void)
{
    u32 t = 2000000;
    while (t--) {
        u8 st = ata_status();
        if (st == 0xFF)
            return 0;
        if (!(st & 0x80)) {
            if (st & 0x01)
                return 0;
            if (st & 0x08)
                return 1;
        }
    }
    return 0;
}

static int ata_identify(u16 *id)
{
    u32 i;
    outb(0x1F6, 0xA0);
    outb(0x1F2, 0);
    outb(0x1F3, 0);
    outb(0x1F4, 0);
    outb(0x1F5, 0);
    outb(0x1F7, 0xEC);
    if (!ata_wait_ready())
        return 0;
    for (i = 0; i < 256; i++)
        id[i] = ata_inw(0x1F0);
    return 1;
}

static int ata_read28(u32 lba, u16 *buf)
{
    u32 i;
    outb(0x1F6, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(0x1F2, 1);
    outb(0x1F3, (u8)lba);
    outb(0x1F4, (u8)(lba >> 8));
    outb(0x1F5, (u8)(lba >> 16));
    outb(0x1F7, 0x20);
    if (!ata_wait_ready())
        return 0;
    for (i = 0; i < 256; i++)
        buf[i] = ata_inw(0x1F0);
    return 1;
}

static void put_hex2(u8 v)
{
    static const char hx[] = "0123456789abcdef";
    putc(hx[v >> 4]);
    putc(hx[v & 15]);
}

static void cmd_disk(void)
{
    u16 id[256];
    u16 sec[256];
    u32 i, sectors;
    if (!ata_identify(id)) {
        puts("диск не найден\n");
        return;
    }
    {
        char model[41];
        int n = 0;
        for (i = 27; i <= 46; i++) {
            char a = (char)(id[i] >> 8);
            char b = (char)(id[i] & 0xFF);
            model[n++] = a ? a : ' ';
            model[n++] = b ? b : ' ';
        }
        model[n] = 0;
        while (n > 0 && model[n - 1] == ' ')
            model[--n] = 0;
        puts("Модель: ");
        puts(model);
        putc('\n');
    }
    sectors = (u32)id[60] | ((u32)id[61] << 16);
    puts("Секторов (LBA28): ");
    putu(sectors);
    putc('\n');
    if (!ata_read28(0, sec)) {
        puts("чтение сектора 0 не удалось\n");
        return;
    }
    puts("Сектор 0: ");
    for (i = 0; i < 16; i++) {
        u8 b = (u8)((i & 1) ? (sec[i / 2] >> 8) : (sec[i / 2] & 0xFF));
        put_hex2(b);
        putc(' ');
    }
    putc('\n');
    puts("ASCII:   ");
    for (i = 0; i < 16; i++) {
        u8 b = (u8)((i & 1) ? (sec[i / 2] >> 8) : (sec[i / 2] & 0xFF));
        putc((b >= 32 && b < 127) ? (char)b : '.');
    }
    putc('\n');
}

static u32 mmap_total_kb(void)
{
    u32 addr, len, p, end, total = 0;
    if (!mb_info || !(*(volatile u32 *)mb_info & 0x40))
        return 0;
    len = *(volatile u32 *)(mb_info + 44);
    addr = *(volatile u32 *)(mb_info + 48);
    p = addr;
    end = addr + len;
    while (p + 24 <= end) {
        u32 size = *(volatile u32 *)p;
        u32 len_lo = *(volatile u32 *)(p + 12);
        u32 type = *(volatile u32 *)(p + 20);
        if (type == 1)
            total += len_lo >> 10;
        p += size + 4;
    }
    return total;
}

static void cmd_memmap(void)
{
    u32 addr, len, p, end;
    if (!mb_info || !(*(volatile u32 *)mb_info & 0x40)) {
        puts("карта памяти недоступна\n");
        return;
    }
    len = *(volatile u32 *)(mb_info + 44);
    addr = *(volatile u32 *)(mb_info + 48);
    p = addr;
    end = addr + len;
    while (p + 24 <= end) {
        u32 size = *(volatile u32 *)p;
        u32 base_lo = *(volatile u32 *)(p + 4);
        u32 len_lo = *(volatile u32 *)(p + 12);
        u32 type = *(volatile u32 *)(p + 20);
        if (!len_lo) {
            p += size + 4;
            continue;
        }
        puts("  база ");
        putu(base_lo >> 10);
        puts(" КБ, размер ");
        putu(len_lo >> 10);
        puts(" КБ — ");
        puts(type == 1 ? "доступно" : "зарезервировано");
        putc('\n');
        p += size + 4;
    }
    puts("Итого доступно: ");
    putu(mmap_total_kb() / 1024);
    puts(" МБ\n");
}

static void run_command(const char *cmd)
{
    char verb[24], arg1[FS_NAME], arg2[FS_NAME];
    const char *arg, *p;
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
        puts("Файлы: look go where newdir newfile show say del copy move dropdir\n");
        puts("       rename append\n");
        puts("Текст: top bottom count findtext sort rev hex seq find tree\n");
        puts("Система: clock uptime vars host me ok nope mem sysinfo who\n");
        puts("         which stat du history sleep user exit\n");
        puts("Прочее: os gui install version clear reboot halt\n");
        puts("Установка: install <gui|list>\n");
        puts("Диск/память: disk, memmap\n");
    } else if (streq(verb, "about")) {
        puts("KotOS — реальная ОС с собственным ядром, без Linux.\n");
    } else if (streq(verb, "os")) {
        splash();
    } else if (streq(verb, "gui")) {
        gui_start();
    } else if (streq(verb, "install")) {
        char sub[FS_NAME];
        const char *ap = arg;
        int have = take_token(&ap, sub, FS_NAME);
        if (!have || streq(sub, "list")) {
            puts("Компоненты:\n  gui  [");
            puts(gui_installed ? "установлен" : "не установлен");
            puts("]\n");
        } else if (streq(sub, "gui")) {
            gui_start();
        } else {
            puts("Неизвестный компонент: ");
            puts(sub);
            putc('\n');
            puts("Использование: install <gui|list>\n");
        }
    } else if (streq(verb, "version")) {
        puts("KotOS 1.0, собственное ядро (x86, Multiboot)\n");
    } else if (streq(verb, "clear")) {
        vga_clear();
    } else if (streq(verb, "reboot")) {
        puts("Перезагрузка...\n");
        reboot();
    } else if (streq(verb, "halt")) {
        puts("Остановлено.\n");
        for (;;)
            __asm__ volatile("cli; hlt");
    } else if (streq(verb, "look") || streq(verb, "ls") || streq(verb, "dir")) {
        fs_ls();
    } else if (streq(verb, "go") || streq(verb, "cd")) {
        fs_cd(arg);
    } else if (streq(verb, "where") || streq(verb, "pwd")) {
        fs_pwd();
    } else if (streq(verb, "newdir") || streq(verb, "mkdir")) {
        if (!*arg || fs_create(cwd, arg, 1) < 0)
            puts("newdir: cannot create directory\n");
    } else if (streq(verb, "newfile") || streq(verb, "touch")) {
        if (!*arg)
            puts("newfile: missing name\n");
        else if (fs_find(cwd, arg) < 0 && fs_create(cwd, arg, 0) < 0)
            puts("newfile: cannot create file\n");
    } else if (streq(verb, "show") || streq(verb, "cat")) {
        if (!*arg)
            puts("show: missing name\n");
        else
            fs_cat(arg);
    } else if (streq(verb, "del") || streq(verb, "rm")) {
        if (!*arg)
            puts("del: missing name\n");
        else
            fs_remove(cwd, arg);
    } else if (streq(verb, "dropdir") || streq(verb, "rmdir")) {
        int idx = fs_find(cwd, arg);
        if (idx <= 0 || !fs[idx].is_dir)
            puts("dropdir: no such directory\n");
        else if (fs_has_children(idx))
            puts("dropdir: directory not empty\n");
        else
            fs[idx].used = 0;
    } else if (streq(verb, "copy") || streq(verb, "cp")) {
        p = arg;
        if (!take_token(&p, arg1, FS_NAME) || !take_token(&p, arg2, FS_NAME))
            puts("copy: usage: copy <src> <dst>\n");
        else
            fs_copy_file(arg1, arg2);
    } else if (streq(verb, "move") || streq(verb, "mv")) {
        p = arg;
        if (!take_token(&p, arg1, FS_NAME) || !take_token(&p, arg2, FS_NAME))
            puts("move: usage: move <src> <dst>\n");
        else
            fs_move_file(arg1, arg2);
    } else if (streq(verb, "top") || streq(verb, "head")) {
        cmd_head(arg);
    } else if (streq(verb, "bottom") || streq(verb, "tail")) {
        cmd_tail(arg);
    } else if (streq(verb, "count") || streq(verb, "wc")) {
        cmd_count(arg);
    } else if (streq(verb, "findtext") || streq(verb, "grep")) {
        p = arg;
        if (!take_token(&p, arg1, FS_DATA) || !take_token(&p, arg2, FS_NAME))
            puts("findtext: usage: findtext <text> <file>\n");
        else
            cmd_grep(arg1, arg2);
    } else if (streq(verb, "say") || streq(verb, "echo")) {
        const char *gt = arg;
        int append = 0;
        while (*gt) {
            if (gt[0] == '>' && gt[1] == '>') {
                append = 1;
                break;
            }
            if (gt[0] == '>')
                break;
            gt++;
        }
        if (*gt == '>') {
            char text[FS_DATA], fname[FS_NAME];
            int t = 0, f = 0;
            const char *q = arg;
            const char *stop = gt;
            while (q < stop) {
                if (!(*q == ' ' && t == 0) && t < FS_DATA - 1)
                    text[t++] = *q;
                q++;
            }
            while (t > 0 && text[t - 1] == ' ')
                t--;
            text[t] = 0;
            q = gt + (append ? 2 : 1);
            while (*q == ' ')
                q++;
            while (*q && *q != ' ' && f < FS_NAME - 1)
                fname[f++] = *q++;
            fname[f] = 0;
            if (!f)
                puts("say: missing file name\n");
            else if (append)
                fs_append_file(fname, text);
            else
                fs_write(fname, text);
        } else {
            puts(arg);
            putc('\n');
        }
    } else if (streq(verb, "clock") || streq(verb, "date")) {
        cmd_clock();
    } else if (streq(verb, "vars") || streq(verb, "env")) {
        puts("HOME=/root\nPATH=/bin\nSHELL=/bin/sh\nTERM=linux\n");
    } else if (streq(verb, "host") || streq(verb, "hostname")) {
        puts("kotos\n");
    } else if (streq(verb, "me") || streq(verb, "id")) {
        puts("uid=0(root) gid=0(root)\n");
    } else if (streq(verb, "ok") || streq(verb, "true")) {
        return;
    } else if (streq(verb, "nope") || streq(verb, "false")) {
        return;
    } else if (streq(verb, "mem") || streq(verb, "free")) {
        u32 total = mmap_total_kb();
        puts("Memory: ");
        putu(total ? total : 1024 + mem_upper_kb);
        puts(" KB total (");
        puts(total ? "mmap" : "mem_upper");
        puts(")\n");
    } else if (streq(verb, "sysinfo") || streq(verb, "uname")) {
        puts("KotOS 1.0 i386 Multiboot\n");
    } else if (streq(verb, "who") || streq(verb, "whoami")) {
        puts("root\n");
    } else if (streq(verb, "rename") || streq(verb, "ren")) {
        p = arg;
        if (!take_token(&p, arg1, FS_NAME) || !take_token(&p, arg2, FS_NAME)) {
            puts("rename: usage: rename <src> <dst>\n");
        } else {
            int si = fs_find(cwd, arg1);
            if (si <= 0 || fs_find(cwd, arg2) >= 0)
                puts("rename: cannot rename\n");
            else
                str_copy(fs[si].name, arg2, FS_NAME);
        }
    } else if (streq(verb, "append") || streq(verb, "app")) {
        p = arg;
        if (!take_token(&p, arg1, FS_NAME) || !*p)
            puts("append: usage: append <file> <text>\n");
        else {
            while (*p == ' ')
                p++;
            fs_append_file(arg1, p);
        }
    } else if (streq(verb, "stat") || streq(verb, "info")) {
        cmd_stat(arg);
    } else if (streq(verb, "du") || streq(verb, "size")) {
        int idx = fs_find(cwd, arg);
        if (idx <= 0) {
            puts("du: no such entry\n");
        } else {
            putu(fs[idx].size);
            puts(" bytes\n");
        }
    } else if (streq(verb, "tree")) {
        puts("./\n");
        tree_walk(cwd, 1);
    } else if (streq(verb, "find")) {
        find_walk(cwd, arg, 0);
    } else if (streq(verb, "sort")) {
        cmd_sort(arg);
    } else if (streq(verb, "rev")) {
        cmd_rev(arg);
    } else if (streq(verb, "hex")) {
        cmd_hex(arg);
    } else if (streq(verb, "seq")) {
        cmd_seq(arg);
    } else if (streq(verb, "uptime")) {
        cmd_uptime();
    } else if (streq(verb, "history")) {
        int start = hist_n > 8 ? hist_n - 8 : 0, h;
        for (h = start; h < hist_n; h++) {
            puts(hist[h % 8]);
            putc('\n');
        }
    } else if (streq(verb, "which")) {
        cmd_which(arg);
    } else if (streq(verb, "sleep")) {
        cmd_sleep(arg);
    } else if (streq(verb, "disk")) {
        cmd_disk();
    } else if (streq(verb, "memmap")) {
        cmd_memmap();
    } else if (streq(verb, "user") || streq(verb, "userspace") || streq(verb, "ring3")) {
        run_user();
    } else if (streq(verb, "exit") || streq(verb, "poweroff")) {
        puts("Bye.\n");
        for (;;)
            __asm__ volatile("cli; hlt");
    } else {
        puts("Неизвестная команда: ");
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
            if (line[0]) {
                str_copy(hist[hist_n % 8], line, 64);
                hist_n++;
            }
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


#define CON_MAX_ROWS 48
#define CON_MAX_COLS 128

static struct {
    u32 ox, oy, cols, rows, cx, cy, fg, bg;
    u8 buf[CON_MAX_ROWS][CON_MAX_COLS];
} con;

static void draw_glyph(u32 x, u32 y, u8 code, u32 fg, u32 bg)
{
    u32 r, b;
    if (x + 8 <= fb_w && y + 16 <= fb_h) {
        for (r = 0; r < 16; r++) {
            u8 bits = font866[(u32)code * 16 + r];
            u32 *p = (u32 *)((u8 *)fb + (y + r) * fb_pitch) + x;
            for (b = 0; b < 8; b++)
                p[b] = (bits & (0x80 >> b)) ? fg : bg;
        }
        return;
    }
    for (r = 0; r < 16; r++) {
        u8 bits = font866[(u32)code * 16 + r];
        for (b = 0; b < 8; b++)
            px(x + b, y + r, (bits & (0x80 >> b)) ? fg : bg);
    }
}

static void con_draw_cell(u32 cx, u32 cy, u8 ch)
{
    draw_glyph(con.ox + cx * 8, con.oy + cy * 16, ch, con.fg, con.bg);
}

static void con_redraw(void)
{
    u32 x, y;
    for (y = 0; y < con.rows; y++)
        for (x = 0; x < con.cols; x++)
            con_draw_cell(x, y, con.buf[y][x]);
}

static void con_clear(void)
{
    u32 x, y;
    if (!con.cols || !con.rows)
        return;
    rect(con.ox, con.oy, con.cols * 8, con.rows * 16, con.bg);
    for (y = 0; y < con.rows; y++)
        for (x = 0; x < con.cols; x++)
            con.buf[y][x] = ' ';
    con.cx = 0;
    con.cy = 0;
}

static void con_scroll(void)
{
    u32 x, y;
    for (y = 0; y + 1 < con.rows; y++)
        for (x = 0; x < con.cols; x++)
            con.buf[y][x] = con.buf[y + 1][x];
    for (x = 0; x < con.cols; x++)
        con.buf[con.rows - 1][x] = ' ';
    con_redraw();
}

static void fb_console_putc(char c)
{
    u8 u = (u8)c;
    if (u == '\n') {
        con.cx = 0;
        con.cy++;
    } else if (u == '\r') {
        con.cx = 0;
    } else if (u == '\b') {
        if (con.cx) {
            con.cx--;
            con.buf[con.cy][con.cx] = ' ';
            con_draw_cell(con.cx, con.cy, ' ');
        }
        return;
    } else {
        con.buf[con.cy][con.cx] = u;
        con_draw_cell(con.cx, con.cy, u);
        con.cx++;
        if (con.cx >= con.cols) {
            con.cx = 0;
            con.cy++;
        }
    }
    if (con.cy >= con.rows) {
        con_scroll();
        con.cy = con.rows - 1;
    }
}

static void fb_console_clear(void)
{
    con_clear();
}

static void console_setup(u32 ox, u32 oy, u32 cols, u32 rows)
{
    con.ox = ox;
    con.oy = oy;
    con.cols = cols;
    con.rows = rows;
    con.fg = 0xE6EDF3;
    con.bg = 0x0B0F14;
    con_clear();
}

static void draw_text(u32 x, u32 y, const char *s, u32 fg, u32 bg)
{
    u32 i = 0;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        u8 code;
        if (c < 0x80) {
            code = c;
        } else if ((c == 0xD0 || c == 0xD1) && *s) {
            unsigned char c2 = (unsigned char)*s++;
            code = cp866_of(((c & 0x1F) << 6) | (c2 & 0x3F));
        } else {
            code = '?';
        }
        draw_glyph(x + i * 8, y, code, fg, bg);
        i++;
    }
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

typedef struct {
    int x, y, w, h;
    const char *title;
    int visible;
} kotwin;

static kotwin wins[2];
static int order[2] = {1, 0};
static int mouse_x = 512, mouse_y = 384;
static int mouse_dx, mouse_dy, mouse_buttons, mouse_cycle;
static u8 mouse_pkt[3];
static u32 cur_bg[19][12];
static int cur_saved;

static u32 px_get(u32 x, u32 y)
{
    if (x >= fb_w || y >= fb_h)
        return 0;
    return fb[(y * fb_pitch + x * 4) / 4];
}

static void cursor_draw(void)
{
    int j, i;
    for (j = 0; j < 19; j++)
        for (i = 0; i < 12; i++)
            cur_bg[j][i] = px_get((u32)(mouse_x + i - 1), (u32)(mouse_y + j - 1));
    for (j = 0; j < 19; j++)
        for (i = 0; i < 12; i++) {
            if (!cursor_bmp[j][i])
                continue;
            px((u32)(mouse_x + i - 1), (u32)(mouse_y + j - 1), 0x000000);
            px((u32)(mouse_x + i), (u32)(mouse_y + j), 0xFFFFFF);
        }
    cur_saved = 1;
}

static void cursor_erase(void)
{
    int j, i;
    if (!cur_saved)
        return;
    for (j = 0; j < 19; j++)
        for (i = 0; i < 12; i++)
            px((u32)(mouse_x + i - 1), (u32)(mouse_y + j - 1), cur_bg[j][i]);
    cur_saved = 0;
}

static void mouse_wait_write(void)
{
    int t = 100000;
    while (t-- > 0 && (inb(0x64) & 2))
        ;
}

static void mouse_wait_read(void)
{
    int t = 100000;
    while (t-- > 0 && !(inb(0x64) & 1))
        ;
}

static void mouse_init(void)
{
    u8 st;
    mouse_wait_write(); outb(0x64, 0xA8);
    mouse_wait_write(); outb(0x64, 0x20);
    mouse_wait_read();  st = inb(0x60);
    st |= 0x02;
    st &= (u8)~0x20;
    mouse_wait_write(); outb(0x64, 0x60);
    mouse_wait_write(); outb(0x60, st);
    mouse_wait_write(); outb(0x64, 0xD4);
    mouse_wait_write(); outb(0x60, 0xF4);
    {
        int guard = 1000;
        while (guard-- > 0 && (inb(0x64) & 1)) {
            u8 st = inb(0x64);
            if (!(st & 0x20))
                break;
            (void)inb(0x60);
        }
    }
    mouse_cycle = 0;
}

static int mouse_poll(void)
{
    u8 st, d;
    int dx, dy;
    if (!(inb(0x64) & 1))
        return 0;
    st = inb(0x64);
    if (!(st & 0x20))
        return 0;
    d = inb(0x60);
    if (mouse_cycle == 0 && !(d & 0x08))
        return 0;
    mouse_pkt[mouse_cycle++] = d;
    if (mouse_cycle < 3)
        return 0;
    mouse_cycle = 0;
    dx = mouse_pkt[1];
    dy = mouse_pkt[2];
    if (mouse_pkt[0] & 0x10)
        dx -= 256;
    if (mouse_pkt[0] & 0x20)
        dy -= 256;
    mouse_dx = dx;
    mouse_dy = dy;
    mouse_buttons = mouse_pkt[0] & 7;
    return 1;
}

static int hit_window(int x, int y)
{
    int k, i;
    for (k = 1; k >= 0; k--) {
        i = order[k];
        if (!wins[i].visible)
            continue;
        if (x >= wins[i].x && x < wins[i].x + wins[i].w &&
            y >= wins[i].y && y < wins[i].y + wins[i].h)
            return i;
    }
    return -1;
}

static void focus(int i)
{
    if (order[1] == i)
        return;
    order[0] = order[1];
    order[1] = i;
}

static void draw_clock(void)
{
    u8 h = cmos_read(0x04), m = cmos_read(0x02), sec = cmos_read(0x00);
    u8 b = cmos_read(0x0B);
    char t[9];
    if (!(b & 0x04)) {
        h = from_bcd(h); m = from_bcd(m); sec = from_bcd(sec);
    }
    t[0] = (char)('0' + h / 10); t[1] = (char)('0' + h % 10); t[2] = ':';
    t[3] = (char)('0' + m / 10); t[4] = (char)('0' + m % 10); t[5] = ':';
    t[6] = (char)('0' + sec / 10); t[7] = (char)('0' + sec % 10); t[8] = 0;
    rect(fb_w - 96, 4, 88, 28, 0x141C26);
    draw_text(fb_w - 88, 10, t, 0x9FB3C8, 0x141C26);
}

static void scene(void)
{
    u32 x, y;
    int k, i;
    for (y = 0; y < fb_h; y++) {
        u32 t = y * 255 / fb_h;
        u32 c = ((8 + t / 6) << 16) | ((12 + t / 4) << 8) | (24 + t / 3);
        u32 *row = (u32 *)((u8 *)fb + y * fb_pitch);
        for (x = 0; x < fb_w; x++)
            row[x] = c;
    }
    rect(0, 0, fb_w, 36, 0x141C26);
    rect(0, 36, fb_w, 1, 0x2A3A4A);
    rect(8, 4, 44, 28, 0x1E2A36);
    draw_text(16, 10, "KotOS", 0x22D3EE, 0x1E2A36);
    rect(60, 4, 130, 28, 0x1E2A36);
    draw_text(68, 10, "Терминал", 0xE6EDF3, 0x1E2A36);
    rect(198, 4, 140, 28, 0x1E2A36);
    draw_text(206, 10, "О системе", 0xE6EDF3, 0x1E2A36);
    for (k = 0; k < 2; k++) {
        i = order[k];
        if (!wins[i].visible)
            continue;
        rect(wins[i].x, wins[i].y, wins[i].w, wins[i].h, 0x0E141B);
        frame(wins[i].x, wins[i].y, wins[i].w, wins[i].h, 0x2A3A4A, 2);
        rect(wins[i].x, wins[i].y, wins[i].w, 28, 0x2563EB);
        draw_text(wins[i].x + 8, wins[i].y + 6, wins[i].title, 0xFFFFFF, 0x2563EB);
        rect(wins[i].x + wins[i].w - 24, wins[i].y + 7, 14, 14, 0xF87171);
        if (i == 1) {
            draw_text(wins[1].x + 12, wins[1].y + 44, "KotOS 1.0", 0x22D3EE, 0x0E141B);
            draw_text(wins[1].x + 12, wins[1].y + 70, "Своё ядро, без Linux.", 0xE6EDF3, 0x0E141B);
            draw_text(wins[1].x + 12, wins[1].y + 96, "Ring 3, RAMFS, GUI", 0xE6EDF3, 0x0E141B);
            draw_text(wins[1].x + 12, wins[1].y + 122, "Окна, мышь, часы", 0xE6EDF3, 0x0E141B);
        }
    }
    if (wins[0].visible)
        con_redraw();
    draw_clock();
}

static void taskbar_click(int x, int y)
{
    if (y >= 36)
        return;
    if (x >= 60 && x < 190) {
        wins[0].visible = !wins[0].visible;
        if (wins[0].visible)
            focus(0);
    } else if (x >= 198 && x < 338) {
        wins[1].visible = !wins[1].visible;
        if (wins[1].visible)
            focus(1);
    }
}

static int kbd_poll(void)
{
    u8 st, sc;
    char c;
    if (!(inb(0x64) & 1))
        return -1;
    st = inb(0x64);
    if (st & 0x20)
        return -1;
    sc = inb(0x60);
    if (sc == 0x2A || sc == 0x36) { shift_down = 1; return -1; }
    if (sc == 0xAA || sc == 0xB6) { shift_down = 0; return -1; }
    if (sc & 0x80)
        return -1;
    c = shift_down ? kbd_map_shift[sc] : kbd_map[sc];
    return c ? (int)(u8)c : -1;
}

static void gui_open_anim(void)
{
    int t;
    u32 x0 = wins[0].x, y0 = wins[0].y, w0 = wins[0].w, h0 = wins[0].h;
    for (t = 1; t <= 8; t++) {
        u32 w = w0 * (u32)t / 8, h = h0 * (u32)t / 8;
        u32 x = x0 + (w0 - w) / 2, y = y0 + (h0 - h) / 2;
        rect(x, y, w, h, 0x0E141B);
        frame(x, y, w, h, 0x2563EB, 2);
        msleep(25);
    }
}

static void gui_run(void)
{
    char line[128];
    int line_len = 0, dragging = -1, ddx = 0, ddy = 0, last = -1, c, drag_tick = 0;

    gui_running = 1;
    wins[0].x = 120; wins[0].y = 80; wins[0].w = 656; wins[0].h = 420;
    wins[0].title = "Терминал"; wins[0].visible = 1;
    wins[1].x = 790; wins[1].y = 120; wins[1].w = 222; wins[1].h = 220;
    wins[1].title = "О системе"; wins[1].visible = 1;
    order[0] = 0; order[1] = 1;

    mouse_init();
    console_setup(wins[0].x + 8, wins[0].y + 32, 80, 24);
    desktop();
    beep(660, 50);
    beep(880, 60);
    gui_open_anim();
    prompt();
    scene();
    cursor_draw();

    for (;;) {
        if (mouse_poll()) {
            cursor_erase();
            mouse_x += mouse_dx;
            mouse_y -= mouse_dy;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > (int)fb_w - 1) mouse_x = (int)fb_w - 1;
            if (mouse_y > (int)fb_h - 1) mouse_y = (int)fb_h - 1;
            if (mouse_buttons & 1) {
                if (dragging < 0) {
                    int w = hit_window(mouse_x, mouse_y);
                    if (w >= 0) {
                        focus(w);
                        if (mouse_x >= wins[w].x + wins[w].w - 24 && mouse_y < wins[w].y + 28)
                            wins[w].visible = 0;
                        else if (mouse_y < wins[w].y + 28) {
                            dragging = w;
                            ddx = mouse_x - wins[w].x;
                            ddy = mouse_y - wins[w].y;
                        }
                    } else {
                        taskbar_click(mouse_x, mouse_y);
                    }
                    scene();
                } else {
                    wins[dragging].x = mouse_x - ddx;
                    wins[dragging].y = mouse_y - ddy;
                    if (dragging == 0) {
                        con.ox = wins[0].x + 8;
                        con.oy = wins[0].y + 32;
                    }
                    if ((++drag_tick & 3) == 0)
                        scene();
                }
            } else if (dragging >= 0) {
                dragging = -1;
                scene();
            }
            cursor_draw();
        }

        c = kbd_poll();
        if (c >= 0) {
            if (c == 27)
                break;
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
            } else if (c >= 32 && c < 127 && line_len < 127) {
                line[line_len++] = c;
                putc(c);
            }
        }

        c = (int)read_time_secs();
        if (c != last) {
            last = c;
            draw_clock();
        }
    }

    cursor_erase();
    beep(440, 90);
    gui_running = 0;
    console_setup(0, 0, fb_w / 8, fb_h / 16);
    vga_clear();
    splash();
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


struct regs {
    u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
};

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8 base_mid;
    u8 access;
    u8 gran;
    u8 base_high;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

struct idt_entry {
    u16 off_low;
    u16 sel;
    u8 zero;
    u8 type;
    u16 off_high;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

struct tss {
    u32 prev;
    u32 esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap;
} __attribute__((packed));

extern void gdt_load(struct gdt_ptr *);
extern void tss_load(void);
extern void idt_load(struct idt_ptr *);
extern void enter_user(u32 entry, u32 stack);
extern void user_exit(void);
extern void isr80_stub(void);
extern void isr_default(void);
extern u8 user_blob_start[], user_blob_end[];

static struct gdt_entry gdt[6];
static struct gdt_ptr gp;
static struct tss tss;
static u8 kstack[8192] __attribute__((aligned(16)));

static u32 pgdir[1024] __attribute__((aligned(4096)));
static u32 pt_kernel[1024] __attribute__((aligned(4096)));
static u32 pt_user[1024] __attribute__((aligned(4096)));
static u32 pt_lfb_e[1024] __attribute__((aligned(4096)));
static u32 pt_lfb_f[1024] __attribute__((aligned(4096)));
static int paging_enabled;

void exception_panic(void)
{
    puts("\nKERNEL EXCEPTION - halted\n");
    for (;;)
        __asm__ volatile("cli; hlt");
}

void syscall_handler(struct regs *r)
{
    switch (r->eax) {
    case 1: {
        const char *p = (const char *)r->ecx;
        u32 n = r->edx, i;
        for (i = 0; i < n && p[i]; i++)
            putc(p[i]);
        r->eax = n;
        break;
    }
    case 2:
        user_exit();
        break;
    case 3:
        r->eax = (u32)kbd_getc();
        break;
    default:
        r->eax = (u32)-1;
        break;
    }
}

static void gdt_set(int i, u32 base, u32 limit, u8 access, u8 gran)
{
    gdt[i].base_low = (u16)(base & 0xFFFF);
    gdt[i].base_mid = (u8)((base >> 16) & 0xFF);
    gdt[i].base_high = (u8)((base >> 24) & 0xFF);
    gdt[i].limit_low = (u16)(limit & 0xFFFF);
    gdt[i].gran = (u8)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].access = access;
}

static void kernel_gdt_init(void)
{
    u32 i;
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xCF);
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xCF);
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xCF);
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xCF);

    for (i = 0; i < sizeof(tss); i++)
        ((u8 *)&tss)[i] = 0;
    tss.ss0 = 0x10;
    tss.esp0 = (u32)kstack + sizeof(kstack);
    tss.iomap = sizeof(tss);

    gdt_set(5, (u32)&tss, sizeof(tss) - 1, 0x89, 0x00);
    gp.limit = sizeof(gdt) - 1;
    gp.base = (u32)gdt;
    gdt_load(&gp);
    tss_load();
}

static struct idt_entry idt[256];
static struct idt_ptr idtp;

static void idt_set(int i, u32 handler, u8 type)
{
    idt[i].off_low = (u16)(handler & 0xFFFF);
    idt[i].off_high = (u16)((handler >> 16) & 0xFFFF);
    idt[i].sel = 0x08;
    idt[i].zero = 0;
    idt[i].type = type;
}

static void kernel_idt_init(void)
{
    int i;
    for (i = 0; i < 256; i++)
        idt_set(i, (u32)isr_default, 0x8E);
    idt_set(0x80, (u32)isr80_stub, 0xEE);
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (u32)idt;
    idt_load(&idtp);
}

static void paging_init(void)
{
    u32 i, cr0;
    for (i = 0; i < 1024; i++) {
        pt_kernel[i] = i * 0x1000 | 3;
        pt_user[i] = (0x400000 + i * 0x1000) | 7;
        pt_lfb_e[i] = (0xE0000000 + i * 0x1000) | 3;
        pt_lfb_f[i] = (0xFD000000 + i * 0x1000) | 3;
    }
    for (i = 0; i < 1024; i++)
        pgdir[i] = 0;
    pgdir[0] = (u32)pt_kernel | 3;
    pgdir[1] = (u32)pt_user | 7;
    pgdir[0xE0000000u >> 22] = (u32)pt_lfb_e | 3;
    pgdir[0xFD000000u >> 22] = (u32)pt_lfb_f | 3;
    __asm__ volatile("mov %0, %%cr3" : : "r"((u32)pgdir) : "memory");
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    paging_enabled = 1;
}

static void run_user(void)
{
    u32 len = (u32)(user_blob_end - user_blob_start);
    u32 i;
    u8 *dst = (u8 *)0x400000;
    if (!paging_enabled) {
        puts("user: paging is off\n");
        return;
    }
    if (!len || len > 0x3F0000) {
        puts("user: bad program size\n");
        return;
    }
    for (i = 0; i < len; i++)
        dst[i] = user_blob_start[i];
    puts("Ring 3 program at 0x400000. Enter to exit.\n");
    enter_user(0x400000, 0x7FF000);
    puts("User program exited, back to kernel.\n");
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
    boot_sec = read_time_secs();
    if (mb_info)
        mem_upper_kb = *(volatile u32 *)(mb_info + 8);
    kernel_gdt_init();
    kernel_idt_init();
    paging_init();
    if (gfx_init()) {
        fb_console_active = 1;
        console_setup(0, 0, fb_w / 8, fb_h / 16);
    }
    splash();
    puts("Введите help для списка команд.\n\n");
    prompt();

    shell();
}
