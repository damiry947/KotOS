CC := gcc
LD := ld
OBJCOPY := objcopy

PFLAGS := -m64 -ffreestanding -fno-pie -fno-pic -mno-sse -mno-mmx \
          -msoft-float -mno-red-zone -O2 -Wall -Wextra

POBJS := payload/kmain64.o payload/linuxabi.o payload/syscall64.o

all: kernel64.elf linux_app.elf linux_glibc.elf

payload/%.o: payload/%.c
	$(CC) $(PFLAGS) -c $< -o $@

payload/syscall64.o: payload/syscall64.S
	$(CC) -m64 -c $< -o $@

payload/payload.elf: $(POBJS) payload/linker.ld
	$(LD) -m elf_x86_64 -T payload/linker.ld -nostdlib -o $@ $(POBJS)

payload.bin: payload/payload.elf
	$(OBJCOPY) -O binary $< $@

boot.o: boot.S payload.bin
	$(CC) -m32 -c $< -o $@

kernel64.elf: boot.o linker.ld
	$(LD) -m elf_i386 -T linker.ld -o $@ boot.o

linux_app.elf: linux_app.c
	$(CC) -static -nostdlib -no-pie -fno-pie -O2 -o $@ $<

linux_glibc.elf: linux_glibc.c
	$(CC) -static -O2 -no-pie -fno-pie -o $@ $<

run: kernel64.elf linux_app.elf
	qemu-system-x86_64 -kernel kernel64.elf -m 64 -serial stdio \
		-initrd linux_app.elf

run-headless: kernel64.elf linux_app.elf
	qemu-system-x86_64 -kernel kernel64.elf -m 64 -display none \
		-serial stdio -initrd linux_app.elf

run-glibc: kernel64.elf linux_glibc.elf
	qemu-system-x86_64 -kernel kernel64.elf -m 64 -display none \
		-serial stdio -initrd linux_glibc.elf

clean:
	rm -f boot.o kernel64.elf payload/*.o payload/payload.elf payload.bin

.PHONY: all run run-headless run-glibc clean
