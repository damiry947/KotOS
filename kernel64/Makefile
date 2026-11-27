CC := gcc
LD := ld
OBJCOPY := objcopy

all: kernel64.elf

payload/payload.o: payload/kmain64.c
	$(CC) -m64 -ffreestanding -fno-pie -fno-pic -mno-sse -mno-mmx \
		-msoft-float -mno-red-zone -O2 -Wall -Wextra -c $< -o $@

payload/payload.elf: payload/payload.o payload/linker.ld
	$(LD) -m elf_x86_64 -T payload/linker.ld -nostdlib -o $@ payload/payload.o

payload.bin: payload/payload.elf
	$(OBJCOPY) -O binary $< $@

boot.o: boot.S payload.bin
	$(CC) -m32 -c $< -o $@

kernel64.elf: boot.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -o $@ boot.o

run: kernel64.elf
	qemu-system-x86_64 -kernel kernel64.elf -m 64 -serial stdio

clean:
	rm -f boot.o kernel64.elf payload/payload.o payload/payload.elf payload.bin

.PHONY: all run clean
