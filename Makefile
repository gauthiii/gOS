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
DISK_IMG    := disk_images/gos_disk.img

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

.PHONY: all build iso run debug clean disk diagnostic

all: build

build: $(KERNEL_ELF)

$(BUILD_DIR)/obj/%.c.o: $(KERNEL_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/obj/%.asm.o: $(KERNEL_DIR)/src/%.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

# Milestone 19.1: ring3_test_blob.asm incbin's build/ring3_test.bin, so it
# needs that flat binary to exist first - a dependency the generic %.asm.o
# rule above can't express (incbin isn't visible to make).
$(BUILD_DIR)/obj/ring3_test_blob.asm.o: $(KERNEL_DIR)/src/ring3_test_blob.asm $(BUILD_DIR)/ring3_test.bin
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(BUILD_DIR)/ring3_test.bin: tools/userland/ring3_test.asm
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f bin $< -o $@

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

# FAT32-formatted disk image for Phase 8 filesystem work. Created once via
# mtools (mformat); NOT recreated automatically on subsequent `make disk` or
# `make run` calls once it exists, since Phase 8.3's persistence tests
# specifically depend on data surviving across reboots/rebuilds. Delete it
# manually (rm $(DISK_IMG)) to force a fresh, empty filesystem.
#
# Finding #21: the seed recipe below (size + mformat args) previously had
# no versioning - if it ever changed, an existing checkout would silently
# keep using a stale image built with the OLD recipe, with no warning.
# DISK_RECIPE_HASH is computed from the recipe text itself, so editing
# DISK_RECIPE below automatically changes the hash; check-disk-recipe
# compares it against the hash stored from the last successful build and
# deletes the stale image (forcing $(DISK_IMG)'s own rule to rebuild it)
# if they differ - a normal, unrelated `make`/`make run` with an
# unchanged recipe still leaves an existing image (and its data) alone.
# Phase 15.3: the seed recipe also bundles the wallpaper BMP into the fresh
# image (WALLPAPR.BMP in the root, 8.3 name matching the FAT32 driver).
# Changing this line changes DISK_RECIPE_HASH, so existing images rebuild
# once (per Finding #21's mechanism) and pick up the wallpaper.
# Milestone 19.3: also bundles the real user-mode ELF64 test binary
# (HELLO.ELF), read and executed by usermode_run_elf() at boot under
# GOS_TEST_USERMODE.
# Milestone 20.1/20.2: also bundles the Phase 20 multi-process test
# binaries (SPIN1-5.ELF, CHILD.ELF, PARENT.ELF), read and run by
# process_spawn() under GOS_TEST_MULTITASKING.
PROC_BINS := tools/userland/spin1.elf tools/userland/spin2.elf tools/userland/spin3.elf \
             tools/userland/spin4.elf tools/userland/spin5.elf \
             tools/userland/child.elf tools/userland/parent.elf tools/userland/badptr.elf \
             tools/userland/waitpid_test.elf tools/userland/infloop.elf
DISK_RECIPE := truncate -s 64M $(DISK_IMG) && mformat -F -i $(DISK_IMG) -v GOSDISK :: && \
	mcopy -i $(DISK_IMG) tools/wallpaper.bmp ::WALLPAPR.BMP && \
	mcopy -i $(DISK_IMG) tools/custom.bmp ::CUSTOM.BMP && \
	mcopy -i $(DISK_IMG) tools/mac.bmp ::MAC.BMP && \
	mcopy -i $(DISK_IMG) tools/windows.bmp ::WINDOWS.BMP && \
	mcopy -i $(DISK_IMG) tools/userland/hello.elf ::HELLO.ELF && \
	mcopy -i $(DISK_IMG) tools/userland/spin1.elf ::SPIN1.ELF && \
	mcopy -i $(DISK_IMG) tools/userland/spin2.elf ::SPIN2.ELF && \
	mcopy -i $(DISK_IMG) tools/userland/spin3.elf ::SPIN3.ELF && \
	mcopy -i $(DISK_IMG) tools/userland/spin4.elf ::SPIN4.ELF && \
	mcopy -i $(DISK_IMG) tools/userland/spin5.elf ::SPIN5.ELF && \
	mcopy -i $(DISK_IMG) tools/userland/child.elf ::CHILD.ELF && \
	mcopy -i $(DISK_IMG) tools/userland/parent.elf ::PARENT.ELF && \
	mcopy -i $(DISK_IMG) tools/userland/badptr.elf ::BADPTR.ELF && \
	mcopy -i $(DISK_IMG) tools/userland/waitpid_test.elf ::WPTEST.ELF && \
	mcopy -i $(DISK_IMG) tools/userland/infloop.elf ::INFLOOP.ELF
DISK_RECIPE_HASH := $(shell echo "$(DISK_RECIPE)" | shasum -a 256 | cut -d' ' -f1)
DISK_HASH_FILE := disk_images/.disk_recipe_hash

disk: check-disk-recipe $(DISK_IMG)

.PHONY: check-disk-recipe
check-disk-recipe:
	@mkdir -p disk_images
	@if [ ! -f $(DISK_HASH_FILE) ] || [ "$$(cat $(DISK_HASH_FILE) 2>/dev/null)" != "$(DISK_RECIPE_HASH)" ]; then \
		echo "Disk image seed recipe changed (or first run) - forcing rebuild of $(DISK_IMG)"; \
		echo "$(DISK_RECIPE_HASH)" > $(DISK_HASH_FILE); \
		rm -f $(DISK_IMG); \
	fi

$(DISK_IMG): tools/wallpaper.bmp tools/custom.bmp tools/mac.bmp tools/windows.bmp tools/userland/hello.elf $(PROC_BINS)
	@mkdir -p disk_images
	$(DISK_RECIPE)

tools/wallpaper.bmp: tools/make_wallpaper.py
	python3 tools/make_wallpaper.py

# Milestone 19.3: the bundled user-mode ELF64 test binary. Assembled and
# linked entirely independently of the kernel build (no libc, no crt0,
# just nasm + a raw linker script).
#
# Milestone 25.2 bugfix (found via the process-teardown leak test): this
# used to link against tools/userland/user.ld (base 0x141000000, outside
# PML4 slot 1) - fine for the Phase 19 usermode_run_elf() demo (which maps
# directly into the kernel's own single PML4, no per-process isolation),
# but HELLO.ELF is ALSO spawned via process_spawn() (Phase 20/24's real
# per-process path - the Terminal's `run HELLO.ELF`, and Phase 25.2's own
# leak-regression test) where PROC_LOAD_BASE/slot 1 is the only region
# vmm_destroy_process_pml4() is safe to walk (slot 0 is shared kernel
# state and must never be freed). A user.ld-linked HELLO.ELF's code
# segment landed at 0x141000000 - inside slot 0, not slot 1 - so its
# tables/pages were invisible to teardown and leaked permanently on every
# process_spawn()-based run. Switched to proc.ld (PROC_LOAD_BASE-based,
# same as spin/child/parent) so every spawn path maps it into the
# genuinely per-process-private slot. hello.asm makes no address
# assumptions of its own (RIP-relative addressing throughout), so this is
# a build-config-only change.
tools/userland/hello.elf: tools/userland/hello.asm tools/userland/proc.ld
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f elf64 tools/userland/hello.asm -o $(BUILD_DIR)/userland_hello.o
	$(LD) -T tools/userland/proc.ld -nostdlib -static -no-pie -z max-page-size=0x1000 \
		$(BUILD_DIR)/userland_hello.o -o $@

# Milestone 20.1/20.3: five copies of spinner.asm, each with a distinct
# marker character/iteration count baked in at assemble time (-D), used to
# prove real preemptive interleaving (20.1: 2 processes) and scheduler
# fairness under load (20.3: all 5 at once).
tools/userland/spin%.elf: tools/userland/spinner.asm tools/userland/proc.ld
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f elf64 -DMARKER="'$*'" -DITERS=20 tools/userland/spinner.asm -o $(BUILD_DIR)/userland_spin$*.o
	$(LD) -T tools/userland/proc.ld -nostdlib -static -no-pie -z max-page-size=0x1000 \
		$(BUILD_DIR)/userland_spin$*.o -o $@

# Milestone 20.2: parent/child spawn+waitpid test pair.
tools/userland/child.elf: tools/userland/child.asm tools/userland/proc.ld
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f elf64 tools/userland/child.asm -o $(BUILD_DIR)/userland_child.o
	$(LD) -T tools/userland/proc.ld -nostdlib -static -no-pie -z max-page-size=0x1000 \
		$(BUILD_DIR)/userland_child.o -o $@

tools/userland/parent.elf: tools/userland/parent.asm tools/userland/proc.ld
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f elf64 tools/userland/parent.asm -o $(BUILD_DIR)/userland_parent.o
	$(LD) -T tools/userland/proc.ld -nostdlib -static -no-pie -z max-page-size=0x1000 \
		$(BUILD_DIR)/userland_parent.o -o $@

# Milestone 25.1 (audit2 Critical #1): deliberately calls SYS_WRITE with an
# unmapped pointer, to prove the kernel rejects it instead of page-faulting.
tools/userland/badptr.elf: tools/userland/badptr.asm tools/userland/proc.ld
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f elf64 tools/userland/badptr.asm -o $(BUILD_DIR)/userland_badptr.o
	$(LD) -T tools/userland/proc.ld -nostdlib -static -no-pie -z max-page-size=0x1000 \
		$(BUILD_DIR)/userland_badptr.o -o $@

# Milestone 26.3 (audit2 High #8): tries SYS_WAITPID on every pid even
# though none are its own child, proving the ownership check rejects them.
tools/userland/waitpid_test.elf: tools/userland/waitpid_test.asm tools/userland/proc.ld
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f elf64 tools/userland/waitpid_test.asm -o $(BUILD_DIR)/userland_waitpid_test.o
	$(LD) -T tools/userland/proc.ld -nostdlib -static -no-pie -z max-page-size=0x1000 \
		$(BUILD_DIR)/userland_waitpid_test.o -o $@

# Milestone 26.2 (audit2 High #7): an intentional infinite loop, never
# calling SYS_EXIT, to prove the scheduler's watchdog kills it instead of
# hanging the whole desktop forever.
tools/userland/infloop.elf: tools/userland/infloop.asm tools/userland/proc.ld
	@mkdir -p $(BUILD_DIR)
	$(NASM) -f elf64 tools/userland/infloop.asm -o $(BUILD_DIR)/userland_infloop.o
	$(LD) -T tools/userland/proc.ld -nostdlib -static -no-pie -z max-page-size=0x1000 \
		$(BUILD_DIR)/userland_infloop.o -o $@

run: iso disk $(BUILD_DIR)/OVMF_VARS.fd
	qemu-system-x86_64 \
		-M q35 -m 256M \
		-drive if=pflash,format=raw,unit=0,file=$(OVMF_DIR)/OVMF_CODE.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=$(BUILD_DIR)/OVMF_VARS.fd \
		-cdrom $(ISO_IMAGE) \
		-device piix3-ide,id=ide \
		-drive id=gosdisk,file=$(DISK_IMG),if=none,format=raw \
		-device ide-hd,drive=gosdisk,bus=ide.0 \
		-serial stdio

$(BUILD_DIR)/OVMF_VARS.fd: $(OVMF_DIR)/OVMF_VARS.fd
	@mkdir -p $(BUILD_DIR)
	cp $< $@

debug: iso $(BUILD_DIR)/OVMF_VARS.fd disk
	qemu-system-x86_64 \
		-M q35 -m 256M \
		-drive if=pflash,format=raw,unit=0,file=$(OVMF_DIR)/OVMF_CODE.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=$(BUILD_DIR)/OVMF_VARS.fd \
		-cdrom $(ISO_IMAGE) \
		-device piix3-ide,id=ide \
		-drive id=gosdisk,file=$(DISK_IMG),if=none,format=raw \
		-device ide-hd,drive=gosdisk,bus=ide.0 \
		-serial stdio -s -S

clean:
	rm -rf $(BUILD_DIR)

# Phase 18 (Milestone 18.2): rebuilds with GOS_DIAGNOSTIC_BOOT defined,
# restoring the full pre-Phase-18 boot sequence (bouncing-rectangle
# animation, "Hello, gOS!" hold, mouse test window, full 2000ms timer
# self-test, and the file/window stress test) for regression testing.
# `make run`/`make build` no longer define this flag, so their object files
# aren't diagnostic-flag-tagged - `clean` first here avoids silently
# linking a mix of diagnostic and non-diagnostic .o files (the same class
# of staleness hazard Finding #21 fixed for the disk image).
diagnostic: clean
	$(MAKE) iso CFLAGS="$(CFLAGS) -DGOS_DIAGNOSTIC_BOOT"
	$(MAKE) disk $(BUILD_DIR)/OVMF_VARS.fd
	qemu-system-x86_64 \
		-M q35 -m 256M \
		-drive if=pflash,format=raw,unit=0,file=$(OVMF_DIR)/OVMF_CODE.fd,readonly=on \
		-drive if=pflash,format=raw,unit=1,file=$(BUILD_DIR)/OVMF_VARS.fd \
		-cdrom $(ISO_IMAGE) \
		-device piix3-ide,id=ide \
		-drive id=gosdisk,file=$(DISK_IMG),if=none,format=raw \
		-device ide-hd,drive=gosdisk,bus=ide.0 \
		-serial stdio
