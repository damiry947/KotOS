all:
	$(MAKE) -C kernel

run:
	$(MAKE) -C kernel run

run-gui:
	$(MAKE) -C kernel run-gui

run-install:
	$(MAKE) -C kernel run-install

iso: all
	$(MAKE) -C kernel64
	cp kernel/kernel.elf iso/boot/kotos.elf
	cp kernel64/kernel64.elf iso/boot/kotos64.elf
	cp kernel64/linux_glibc.elf iso/boot/linux_glibc.elf
	grub-mkrescue -o kotos.iso iso

clean:
	$(MAKE) -C kernel clean

.PHONY: all run run-gui run-install iso clean
