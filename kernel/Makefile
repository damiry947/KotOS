CC := gcc
LD := ld
OBJCOPY := objcopy

CFLAGS := -m32 -std=gnu11 -ffreestanding -fno-pie -fno-pic \
          -fno-stack-protector -fno-builtin -mno-sse -mno-mmx -msoft-float \
          -O2 -Wall -Wextra -fno-asynchronous-unwind-tables -fno-unwind-tables

LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z noexecstack

OBJS := boot.o main.o user_asm.o ata.o fat32.o

all: kernel.elf

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) -m32 -c $< -o $@

user/init.o: user/init.c
	$(CC) $(CFLAGS) -c $< -o $@

user/init.elf: user/init.o user/linker.ld
	$(LD) -m elf_i386 -T user/linker.ld -nostdlib -o $@ user/init.o

user.bin: user/init.elf
	$(OBJCOPY) -O binary $< $@

user_asm.o: user_asm.S user.bin
	$(CC) -m32 -c $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

run: kernel.elf disk.img
	qemu-system-x86_64 -kernel kernel.elf -m 64 -serial stdio \
		-drive file=disk.img,format=raw,if=ide

run-gui: kernel.elf disk.img
	qemu-system-x86_64 -kernel kernel.elf -m 64 -serial stdio \
		-append "gui=1" \
		-drive file=disk.img,format=raw,if=ide

run-install: kernel.elf disk.img
	qemu-system-x86_64 -kernel kernel.elf -m 64 -serial stdio \
		-append "install=1" \
		-drive file=disk.img,format=raw,if=ide

disk.img:
	dd if=/dev/zero of=$@ bs=1M count=1 status=none

clean:
	rm -f $(OBJS) kernel.elf user.bin user/init.o user/init.elf disk.img

.PHONY: all run run-gui run-install clean
