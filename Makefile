# gOS top-level build system
# Targets: build, iso, run, debug, clean

CC          := x86_64-elf-gcc
LD          := x86_64-elf-gcc
NASM        := nasm

KERNEL_DIR  := kernel
BUILD_DIR   := build
ISO_DIR     := $(BUILD_DIR)/iso_root
LIMINE_DIR  := third_party/limine
OVMF_DIR    := third_party/ovmf

KERNEL_ELF  := $(BUILD_DIR)/gos.elf
ISO_IMAGE   := $(BUILD_DIR)/gos.iso

CFLAGS := -g -O0 -Wall -Wextra \
          -ffreestanding -fno-stack-protector -fno-stack-check \
          -fno-pic -fno-pie -m64 -march=x86-64 -mno-80387 -mno-mmx \
          -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel \
          -std=gnu11 -I $(KERNEL_DIR)/include -I $(LIMINE_DIR)

LDFLAGS := -T $(KERNEL_DIR)/linker.ld -nostdlib -static \
           -z max-page-size=0x1000 -no-pie

C_SOURCES := $(shell find $(KERNEL_DIR)/src -name '*.c' 2>/dev/null)
ASM_SOURCES := $(shell find $(KERNEL_DIR)/src -name '*.asm' 2>/dev/null)
OBJS := $(C_SOURCES:$(KERNEL_DIR)/src/%.c=$(BUILD_DIR)/obj/%.c.o) \
        $(ASM_SOURCES:$(KERNEL_DIR)/src/%.asm=$(BUILD_DIR)/obj/%.asm.o)

.PHONY: all build iso run debug clean

all: build

build: $(KERNEL_ELF)

$(BUILD_DIR)/obj/%.c.o: $(KERNEL_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/obj/%.asm.o: $(KERNEL_DIR)/src/%.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(KERNEL_ELF): $(OBJS) $(KERNEL_DIR)/linker.ld
	@mkdir -p $(BUILD_DIR)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

iso: build
	@mkdir -p $(ISO_DIR)/boot/limine $(ISO_DIR)/EFI/BOOT
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/gos.elf
	cp boot/limine.conf $(ISO_DIR)/boot/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
	   $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_DIR)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_DIR)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISO_DIR) -o $(ISO_IMAGE)
	$(LIMINE_DIR)/limine bios-install $(ISO_IMAGE)

run: iso
	qemu-system-x86_64 \
		-M q35 -m 256M \
		-drive if=pflash,format=raw,unit=0,file=$(OVMF_DIR)/OVMF_CODE.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=$(BUILD_DIR)/OVMF_VARS.fd \
		-cdrom $(ISO_IMAGE) \
		-serial stdio

$(BUILD_DIR)/OVMF_VARS.fd: $(OVMF_DIR)/OVMF_VARS.fd
	@mkdir -p $(BUILD_DIR)
	cp $< $@

debug: iso $(BUILD_DIR)/OVMF_VARS.fd
	qemu-system-x86_64 \
		-M q35 -m 256M \
		-drive if=pflash,format=raw,unit=0,file=$(OVMF_DIR)/OVMF_CODE.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=$(BUILD_DIR)/OVMF_VARS.fd \
		-cdrom $(ISO_IMAGE) \
		-serial stdio -s -S

clean:
	rm -rf $(BUILD_DIR)
