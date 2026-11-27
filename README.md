# KotOS 1.0

Операционная система с **собственным ядром** — без Linux.

## Что уже работает

- загрузка через Multiboot в QEMU;
- собственный вывод в VGA-текст 80×25 и в последовательный порт COM1;
- обработка клавиатуры (PS/2, скан-код set 1, Shift);
- шелл KotOS: `look`, `go`, `where`, `newdir`, `newfile`, `show`, `say`, `del`, `copy`, `move`, `dropdir`, `rename`, `append`;
- текст и поиск: `top`, `bottom`, `count`, `findtext`, `sort`, `rev`, `hex`, `seq`, `find`, `tree`;
- системные: `clock`, `uptime`, `vars`, `host`, `me`, `ok`, `nope`, `mem`, `sysinfo`, `who`, `stat`, `du`, `which`, `history`, `sleep`, `exit`;
- графика: `os`, `install gui`; служебные: `help`, `about`, `version`, `clear`, `reboot`, `halt`;
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

## Структура

```
kernel/
  boot.S      — Multiboot-заголовок, стек, вход _start
  main.c      — kmain: VGA, serial, клавиатура, шелл, GDT/TSS/IDT, paging, syscalls
  user_asm.S  — вход в ring 3, обработчик int 0x80, возврат в ядро
  user/init.c — пользовательская программа (ring 3)
  linker.ld   — раскладка по адресу 1 МБ
  Makefile    — сборка freestanding ELF32
docs/         — скриншоты
```

## Дорожная карта

- [x] Multiboot-загрузка и текстовый вывод
- [x] Клавиатура и мини-шелл
- [x] VBE framebuffer: рабочий стол, панель, окно, курсор (стрелки)
- [x] RAMFS и базовые файловые команды
- [x] Расширенный набор команд (файлы, текст, RTC, история, uptime)
- [x] Текст в графическом режиме: framebuffer-консоль и кириллица
- [x] Ring 3, syscalls int 0x80 и пользовательская программа
- [ ] Планировщик и несколько процессов
- [x] Перетаскивание окон мышью (PS/2)
- [ ] Прерывания (IDT/PIC) и таймер
- [ ] Простая файловая система в памяти

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
