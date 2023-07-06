# SPDX-License-Identifier: GPL-2.0
# Makefile for nolibc tests
include ../../../scripts/Makefile.include
# We need this for the "cc-option" macro.
include ../../../build/Build.include

# we're in ".../tools/testing/selftests/nolibc"
ifeq ($(srctree),)
srctree := $(patsubst %/tools/testing/selftests/,%,$(dir $(CURDIR)))
endif

ifeq ($(ARCH),)
include $(srctree)/scripts/subarch.include
ARCH = $(SUBARCH)
endif

# kernel image names by architecture
IMAGE_i386       = arch/x86/boot/bzImage
IMAGE_x86_64     = arch/x86/boot/bzImage
IMAGE_x86        = arch/x86/boot/bzImage
IMAGE_arm64      = arch/arm64/boot/Image
IMAGE_arm        = arch/arm/boot/zImage
IMAGE_mips       = vmlinuz
IMAGE_riscv      = arch/riscv/boot/Image
IMAGE_s390       = arch/s390/boot/bzImage
IMAGE_loongarch  = arch/loongarch/boot/vmlinuz.efi
IMAGE            = $(IMAGE_$(ARCH))
IMAGE_NAME       = $(notdir $(IMAGE))

# default kernel configurations that appear to be usable
DEFCONFIG_i386       = defconfig
DEFCONFIG_x86_64     = defconfig
DEFCONFIG_x86        = defconfig
DEFCONFIG_arm64      = defconfig
DEFCONFIG_arm        = multi_v7_defconfig
DEFCONFIG_mips       = malta_defconfig
DEFCONFIG_riscv      = defconfig
DEFCONFIG_s390       = defconfig
DEFCONFIG_loongarch  = defconfig
DEFCONFIG            = $(DEFCONFIG_$(ARCH))

# optional tests to run (default = all)
TEST =

# QEMU_ARCH: arch names used by qemu
QEMU_ARCH_i386       = i386
QEMU_ARCH_x86_64     = x86_64
QEMU_ARCH_x86        = x86_64
QEMU_ARCH_arm64      = aarch64
QEMU_ARCH_arm        = arm
QEMU_ARCH_mips       = mipsel  # works with malta_defconfig
QEMU_ARCH_riscv      = riscv64
QEMU_ARCH_s390       = s390x
QEMU_ARCH_loongarch  = loongarch64
QEMU_ARCH            = $(QEMU_ARCH_$(ARCH))

# QEMU_ARGS : some arch-specific args to pass to qemu
QEMU_ARGS_i386       = -M pc -append "console=ttyS0,9600 i8042.noaux panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_x86_64     = -M pc -append "console=ttyS0,9600 i8042.noaux panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_x86        = -M pc -append "console=ttyS0,9600 i8042.noaux panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_arm64      = -M virt -cpu cortex-a53 -append "panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_arm        = -M virt -append "panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_mips       = -M malta -append "panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_riscv      = -M virt -append "console=ttyS0 panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_s390       = -M s390-ccw-virtio -m 1G -append "console=ttyS0 panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_loongarch  = -M virt -append "console=ttyS0,115200 panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS            = $(QEMU_ARGS_$(ARCH)) $(QEMU_ARGS_EXTRA)

# OUTPUT is only set when run from the main makefile, otherwise
# it defaults to this nolibc directory.
OUTPUT ?= $(CURDIR)/

ifeq ($(V),1)
Q=
else
Q=@
endif

CFLAGS_s390 = -m64
CFLAGS_mips = -EL
CFLAGS_STACKPROTECTOR ?= $(call cc-option,-mstack-protector-guard=global $(call cc-option,-fstack-protector-all))
CFLAGS  ?= -Os -fno-ident -fno-asynchronous-unwind-tables -std=c89 \
		$(call cc-option,-fno-stack-protector) \
		$(CFLAGS_$(ARCH)) $(CFLAGS_STACKPROTECTOR)
LDFLAGS := -s

REPORT  ?= awk '/\[OK\][\r]*$$/{p++} /\[FAIL\][\r]*$$/{if (!f) printf("\n"); f++; print;} /\[SKIPPED\][\r]*$$/{s++} \
		END{ printf("\n%3d test(s): %3d passed, %3d skipped, %3d failed => status: ", p+s+f, p, s, f); \
		if (f) printf("failure\n"); else if (s) printf("warning\n"); else printf("success\n");; \
		printf("\nSee all results in %s\n", ARGV[1]); }'

help:
	@echo "Supported targets under selftests/nolibc:"
	@echo "  all          call the \"run\" target below"
	@echo "  help         this help"
	@echo "  sysroot      create the nolibc sysroot here (uses \$$ARCH)"
	@echo "  nolibc-test  build the executable (uses \$$CC and \$$CROSS_COMPILE)"
	@echo "  libc-test    build an executable using the compiler's default libc instead"
	@echo "  run-user     runs the executable under QEMU (uses \$$ARCH, \$$TEST)"
	@echo "  initramfs    prepare the initramfs with nolibc-test"
	@echo "  defconfig    create a fresh new default config (uses \$$ARCH)"
	@echo "  kernel       (re)build the kernel with the initramfs (uses \$$ARCH)"
	@echo "  run          runs the kernel in QEMU after building it (uses \$$ARCH, \$$TEST)"
	@echo "  rerun        runs a previously prebuilt kernel in QEMU (uses \$$ARCH, \$$TEST)"
	@echo "  clean        clean the sysroot, initramfs, build and output files"
	@echo ""
	@echo "The output file is \"run.out\". Test ranges may be passed using \$$TEST."
	@echo ""
	@echo "Currently using the following variables:"
	@echo "  ARCH          = $(ARCH)"
	@echo "  CROSS_COMPILE = $(CROSS_COMPILE)"
	@echo "  CC            = $(CC)"
	@echo "  OUTPUT        = $(OUTPUT)"
	@echo "  TEST          = $(TEST)"
	@echo "  QEMU_ARCH     = $(if $(QEMU_ARCH),$(QEMU_ARCH),UNKNOWN_ARCH) [determined from \$$ARCH]"
	@echo "  IMAGE_NAME    = $(if $(IMAGE_NAME),$(IMAGE_NAME),UNKNOWN_ARCH) [determined from \$$ARCH]"
	@echo ""

all: run

sysroot: sysroot/$(ARCH)/include

sysroot/$(ARCH)/include:
	$(Q)rm -rf sysroot/$(ARCH) sysroot/sysroot
	$(QUIET_MKDIR)mkdir -p sysroot
	$(Q)$(MAKE) -C ../../../include/nolibc ARCH=$(ARCH) OUTPUT=$(CURDIR)/sysroot/ headers_standalone
	$(Q)mv sysroot/sysroot sysroot/$(ARCH)

nolibc-test: nolibc-test.c sysroot/$(ARCH)/include
	$(QUIET_CC)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ \
	  -nostdlib -static -Isysroot/$(ARCH)/include $< -lgcc

libc-test: nolibc-test.c
	$(QUIET_CC)$(CC) -o $@ $<

run-libc-test: libc-test
	$(Q)./libc-test > "$(CURDIR)/run.out" || :
	$(Q)$(REPORT) $(CURDIR)/run.out

# qemu user-land test
run-user: nolibc-test
	$(Q)qemu-$(QEMU_ARCH) ./nolibc-test > "$(CURDIR)/run.out" || :
	$(Q)$(REPORT) $(CURDIR)/run.out

initramfs: nolibc-test
	$(QUIET_MKDIR)mkdir -p initramfs
	$(call QUIET_INSTALL, initramfs/init)
	$(Q)cp nolibc-test initramfs/init

defconfig:
	$(Q)$(MAKE) -C $(srctree) ARCH=$(ARCH) CC=$(CC) CROSS_COMPILE=$(CROSS_COMPILE) mrproper $(DEFCONFIG) prepare

kernel: initramfs
	$(Q)$(MAKE) -C $(srctree) ARCH=$(ARCH) CC=$(CC) CROSS_COMPILE=$(CROSS_COMPILE) $(IMAGE_NAME) CONFIG_INITRAMFS_SOURCE=$(CURDIR)/initramfs

# run the tests after building the kernel
run: kernel
	$(Q)qemu-system-$(QEMU_ARCH) -display none -no-reboot -kernel "$(srctree)/$(IMAGE)" -serial stdio $(QEMU_ARGS) > "$(CURDIR)/run.out"
	$(Q)$(REPORT) $(CURDIR)/run.out

# re-run the tests from an existing kernel
rerun:
	$(Q)qemu-system-$(QEMU_ARCH) -display none -no-reboot -kernel "$(srctree)/$(IMAGE)" -serial stdio $(QEMU_ARGS) > "$(CURDIR)/run.out"
	$(Q)$(REPORT) $(CURDIR)/run.out

clean:
	$(call QUIET_CLEAN, sysroot)
	$(Q)rm -rf sysroot
	$(call QUIET_CLEAN, nolibc-test)
	$(Q)rm -f nolibc-test
	$(call QUIET_CLEAN, libc-test)
	$(Q)rm -f libc-test
	$(call QUIET_CLEAN, initramfs)
	$(Q)rm -rf initramfs
	$(call QUIET_CLEAN, run.out)
	$(Q)rm -rf run.out

.PHONY: sysroot/$(ARCH)/include
