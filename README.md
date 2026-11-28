# KotOS 1.0

Операционная система с **собственным ядром** — без Linux.

## Что уже работает

- загрузка через Multiboot в QEMU;
- собственный вывод в VGA-текст 80×25 и в последовательный порт COM1;
- обработка клавиатуры (PS/2, скан-код set 1, Shift);
- шелл KotOS: `look`, `go`, `where`, `newdir`, `newfile`, `show`, `say`, `del`, `copy`, `move`, `dropdir`, `rename`, `append`;
- текст и поиск: `top`, `bottom`, `count`, `findtext`, `sort`, `rev`, `hex`, `seq`, `find`, `tree`;
- системные: `clock`, `uptime`, `vars`, `host`, `me`, `ok`, `nope`, `mem`, `sysinfo`, `who`, `stat`, `du`, `which`, `history`, `sleep`, `exit`;
- графика: `os`, `install` (установщик), `install gui|text|list|status|reset`, `tz`; служебные: `help`, `about`, `version`, `clear`, `reboot`, `halt`;
- RAMFS в памяти (64 узла, файлы до 512 байт), `say текст > файл` и `say текст >> файл`;
- Linux-имена (`ls`, `cd`, `pwd`, `mkdir`, `touch`, `cat`, `echo`, `rm`, `cp`, `mv`, `rmdir`, `head`, `tail`, `wc`, `grep`, `date`, `env`, `free`, `uname`, `whoami` и др.) работают как алиасы.
- собственная графика: Bochs VBE 1024×768×32, LFB через PCI BAR0;
- рабочий стол, панель с кнопками и часами, перетаскиваемые окна (PS/2-мышь), живой терминал в окне;
- экран приветствия в стиле fastfetch: кот, `damir@kotos`, версия, ядро и шелл;
- интерфейс на русском: текстовая консоль рисуется в framebuffer шрифтом CP866
  (Terminus, OFL-1.1), поэтому кириллица видна и в консоли, и в ring 3;
- userspace: отдельная программа в ring 3 по адресу 0x400000;
- системные вызовы `int 0x80`: `write`, `exit`, `getchar`; команда `user`;
- сборка без libc: `gcc -m32 -ffreestanding`, свой линкер-скрипт.

## Сборка и запуск

Нужны `gcc`, `ld` (binutils) и `qemu-system-x86`.

```sh
make            # собрать kernel/kernel.elf
make run        # запустить в QEMU с окном и serial в терминале
make run-gui    # сразу графический режим (gui=1)
make run-install# сразу установщик (install=1)
make iso        # пересобрать kotos.iso через grub-mkrescue
make clean
```

Или вручную:

```sh
cd kernel
make
qemu-system-x86_64 -kernel kernel.elf -m 64
```

Для кириллицы нужен стандартный VGA QEMU (Bochs VBE, по умолчанию); с `-vga cirrus` ядро падает в текстовый режим VGA без кириллицы.

Скриншоты: `docs/screenshot.png` (GUI), `docs/screenshot-splash.png` (приветствие), `docs/screenshot-shell.png` (команды), `docs/screenshot-text.png` (консоль).

## Установка, город и UTC

Команда `install` без аргументов запускает установщик: выбор режима
(терминал или графика), города из списка часовых поясов РФ (Калининград +2 …
Петропавловск-Камчатский +12) или ручного смещения UTC, затем подтверждение и
запись `/kotos.conf` в RAMFS. Настройки применяются сразу и действуют до
перезагрузки — диск пока доступен только на чтение.

```
install            # мастер установки
install gui|text   # только режим запуска
install list       # компоненты и текущие настройки
install reset      # вернуть Москву (+3) и терминал
tz                 # показать настройки
tz +05:30          # смещение UTC вручную
tz Екатеринбург    # выбрать город
```

## Загрузка с параметрами

Ядро читает командную строку Multiboot (`multiboot /boot/kotos.elf gui=1`):

- `gui=1` — сразу запустить графический интерфейс;
- `gui=0` — только терминал, даже если в конфиге выбран GUI;
- `install=1` — запустить установщик при загрузке;
- `install=0` — не запускать установщик.

В `iso/boot/grub/grub.cfg` эти параметры уже прописаны для пунктов меню
«графика», «терминал», «Установить KotOS» и «безопасный режим».

## Структура

```
kernel/
  boot.S      — Multiboot-заголовок, стек, вход _start
  main.c      — kmain: VGA, serial, клавиатура, шелл, GDT/TSS/IDT, paging, syscalls
  user_asm.S  — вход в ring 3, обработчик int 0x80, возврат в ядро
  user/init.c — пользовательская программа (ring 3)
  linker.ld   — раскладка по адресу 1 МБ
  Makefile    — сборка freestanding ELF32
kernel64/     — экспериментальное x64-ядро: long mode, GUI, Linux ABI
docs/         — скриншоты
```

## Экспериментальное ядро x64 и Linux ABI

`kernel64/` — отдельное 64-битное ядро: 32-битный Multiboot-загрузчик включает
long mode и запускает C-payload по адресу `0x300000`. В нём есть слой
совместимости с Linux — статический ELF64 x86_64 запускается командой `linux`.

Как это устроено:

- ELF-файл передаётся как Multiboot-модуль (`-initrd` в QEMU, `module` в GRUB),
  адрес берётся из `mb_info`;
- загрузчик разбирает `PT_LOAD`, копирует сегменты в `0x400000–0x600000`,
  обнуляет `.bss`, строит стек с `argc/argv/envp/auxv` (`AT_PHDR`, `AT_RANDOM`,
  `AT_PAGESZ` и т.д.);
- 2-МБ страницы `0x400000–0xA00000` помечаются user, payload остаётся supervisor;
- вход в ring 3 через `iretq` (CS `0x23`, SS `0x43`); `RDX` обнуляется, потому что
  glibc `_start` трактует его как `rtld_fini`;
- сисколлы идут через `syscall`: `LSTAR/STAR/FMASK`, `EFER.SCE`, отдельный стек
  ядра, возврат через `iretq`;
- включены SSE (`CR4.OSFXSR/OSXMMEXCPT`) — иначе glibc падает на `movd`.

Поддержаны сисколлы: `read` (EOF), `write`, `close`, `fstat`, `mmap/munmap`
(анонимный bump-аллокатор с `0x800000`), `mprotect`, `brk`, `ioctl` (ENOTTY),
`lseek`, `rt_sigaction`/`rt_sigprocmask` (заглушки), `getpid`/`gettid`/`getuid`/
`getgid`/`geteuid`/`getegid`, `uname`, `arch_prctl` (FS/GS), `set_tid_address`,
`set_robust_list`, `prlimit64`, `getrandom`; `futex`/`rseq` возвращают ENOSYS.

Ограничения: один процесс, без `fork/exec` и сигналов; только статический
`ET_EXEC` (не PIE и не динамические библиотеки); нет ФС и сети; изоляции нет —
программа видит всю identity-память `0x400000–0xA00000`; RAM 64 МБ.

Сборка и запуск:

```sh
cd kernel64
make                 # kernel64.elf, linux_app.elf, linux_glibc.elf
make run             # QEMU с linux_app.elf как Multiboot-модулем
make run-glibc       # QEMU со статическим glibc-тестом
# в консоли KotOS x64:
x64> linux
```

Файлы: `payload/linuxabi.c` — ELF-загрузчик и сисколлы, `payload/syscall64.S` —
вход syscall и ring 3, `linux_app.c` — тест без libc, `linux_glibc.c` — статический
glibc-тест.

## Дорожная карта

- [x] Multiboot-загрузка и текстовый вывод
- [x] Клавиатура и мини-шелл
- [x] VBE framebuffer: рабочий стол, панель, окно, курсор (стрелки)
- [x] RAMFS и базовые файловые команды
- [x] Расширенный набор команд (файлы, текст, RTC, история, uptime)
- [x] Текст в графическом режиме: framebuffer-консоль и кириллица
- [x] Ring 3, syscalls int 0x80 и пользовательская программа
- [x] Установщик: режим запуска, город и UTC, `gui=1` / `install=1`
- [ ] Планировщик и несколько процессов
- [x] Перетаскивание окон мышью (PS/2)
- [ ] Прерывания (IDT/PIC) и таймер
- [x] Простая файловая система в памяти
- [ ] Сохранение настроек установщика на диск (ATA PIO write)
- [x] x64: Linux ABI — статический ELF64, syscall, glibc
- [ ] x64: несколько процессов, fork/exec и ФС для Linux-программ

## Лицензия

GPL-3.0 — см. `LICENSE`.

## Скрипт для хоста

`tools/os` печатает такой же экран приветствия в терминале Linux Mint:

```sh
~/kotos/tools/os
```

## Управление

- мышь — курсор, перетаскивание окна за заголовок, кнопки панели;
- `Esc` — выход из GUI в полноэкранный терминал;
- в текстовом режиме команды вводятся в шелле `kotos>`.
