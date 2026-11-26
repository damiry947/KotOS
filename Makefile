all:
	$(MAKE) -C kernel

run:
	$(MAKE) -C kernel run

clean:
	$(MAKE) -C kernel clean

.PHONY: all run clean
