# KotOS

Учебная операционная система с **собственным ядром** — без Linux.

## Что уже работает

- загрузка через Multiboot в QEMU;
- собственный вывод в VGA-текст 80×25 и в последовательный порт COM1;
- обработка клавиатуры (PS/2, скан-код set 1, Shift);
- мини-шелл: `help`, `about`, `version`, `clear`, `reboot`, `halt`;
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

Скриншот: `docs/screenshot.png`.

## Структура

```
kernel/
  boot.S      — Multiboot-заголовок, стек, вход _start
  main.c      — kmain: VGA, serial, клавиатура, шелл
  linker.ld   — раскладка по адресу 1 МБ
  Makefile    — сборка freestanding ELF32
docs/         — скриншоты
```

## Дорожная карта

- [x] Multiboot-загрузка и текстовый вывод
- [x] Клавиатура и мини-шелл
- [ ] VBE framebuffer: собственные окна, панель задач, курсор
- [ ] Прерывания (IDT/PIC) и таймер
- [ ] Простая файловая система в памяти

## Лицензия

GPL-3.0 — см. `LICENSE`.
