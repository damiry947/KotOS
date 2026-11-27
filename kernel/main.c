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

static char hist[8][64];
static int hist_n = 0;
static u32 boot_sec = 0;

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
    "help", "about", "os", "gui", "version", "clear", "reboot", "halt",
    "look", "ls", "dir", "go", "cd", "where", "pwd", "newdir", "mkdir",
    "newfile", "touch", "show", "cat", "say", "echo", "del", "rm",
    "copy", "cp", "move", "mv", "dropdir", "rmdir", "rename", "ren",
    "append", "top", "head", "bottom", "tail", "count", "wc",
    "findtext", "grep", "find", "tree", "sort", "rev", "hex", "seq",
    "stat", "du", "clock", "date", "uptime", "vars", "env", "host",
    "hostname", "me", "id", "ok", "true", "nope", "false", "mem",
    "free", "sysinfo", "uname", "who", "whoami", "history", "which",
    "sleep", "exit", "poweroff", 0
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

static void run_command(const char *cmd)
{
    char verb[24], arg1[FS_NAME], arg2[FS_NAME], rest[FS_DATA];
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
        puts("Core: look go where newdir newfile show say del copy move dropdir rename append\n");
        puts("Text: top bottom count findtext sort rev hex seq find tree\n");
        puts("Sys:  clock uptime vars host me ok nope mem sysinfo who which stat du\n");
        puts("More: history sleep exit os install gui version clear reboot halt\n");
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
        u32 up = 0;
        if (mb_info)
            up = *(volatile u32 *)(mb_info + 8);
        puts("Memory: ");
        putu(1024 + up);
        puts(" KB total (from Multiboot)\n");
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
    } else if (streq(verb, "exit") || streq(verb, "poweroff")) {
        puts("Bye.\n");
        for (;;)
            __asm__ volatile("cli; hlt");
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
            for (;;)
                __asm__ volatile("cli; hlt");
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
    boot_sec = read_time_secs();
    splash();
    puts("Type help for a list of commands.\n\n");
    prompt();

    shell();
}
