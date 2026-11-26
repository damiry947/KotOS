CC := gcc
LD := ld

CFLAGS := -m32 -std=gnu11 -ffreestanding -fno-pie -fno-pic \
          -fno-stack-protector -fno-builtin -mno-sse -mno-mmx -msoft-float \
          -O2 -Wall -Wextra -fno-asynchronous-unwind-tables -fno-unwind-tables

LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z noexecstack

OBJS := boot.o main.o

all: kernel.elf

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) -m32 -c $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

run: kernel.elf
	qemu-system-x86_64 -kernel kernel.elf -m 64 -serial stdio

clean:
	rm -f $(OBJS) kernel.elf

.PHONY: all run clean
