CC := gcc
LD := ld

all: kernel64.elf

boot.o: boot.S
	$(CC) -m32 -c $< -o $@

kernel64.elf: boot.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -o $@ boot.o

run: kernel64.elf
	qemu-system-x86_64 -kernel kernel64.elf -m 64 -serial stdio

clean:
	rm -f boot.o kernel64.elf

.PHONY: all run clean
