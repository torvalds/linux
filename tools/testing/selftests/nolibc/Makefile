# SPDX-License-Identifier: GPL-2.0
# Makefile for nolibc tests
include ../../../scripts/Makefile.include

# we're in ".../tools/testing/selftests/nolibc"
ifeq ($(srctree),)
srctree := $(patsubst %/tools/testing/selftests/,%,$(dir $(CURDIR)))
endif

ifeq ($(ARCH),)
include $(srctree)/scripts/subarch.include
ARCH = $(SUBARCH)
endif

# kernel image names by architecture
IMAGE_i386    = arch/x86/boot/bzImage
IMAGE_x86     = arch/x86/boot/bzImage
IMAGE_arm64   = arch/arm64/boot/Image
IMAGE_arm     = arch/arm/boot/zImage
IMAGE_mips    = vmlinuz
IMAGE_riscv   = arch/riscv/boot/Image
IMAGE         = $(IMAGE_$(ARCH))
IMAGE_NAME    = $(notdir $(IMAGE))

# default kernel configurations that appear to be usable
DEFCONFIG_i386    = defconfig
DEFCONFIG_x86     = defconfig
DEFCONFIG_arm64   = defconfig
DEFCONFIG_arm     = multi_v7_defconfig
DEFCONFIG_mips    = malta_defconfig
DEFCONFIG_riscv   = defconfig
DEFCONFIG         = $(DEFCONFIG_$(ARCH))

# optional tests to run (default = all)
TEST =

# QEMU_ARCH: arch names used by qemu
QEMU_ARCH_i386    = i386
QEMU_ARCH_x86     = x86_64
QEMU_ARCH_arm64   = aarch64
QEMU_ARCH_arm     = arm
QEMU_ARCH_mips    = mipsel  # works with malta_defconfig
QEMU_ARCH_riscv   = riscv64
QEMU_ARCH         = $(QEMU_ARCH_$(ARCH))

# QEMU_ARGS : some arch-specific args to pass to qemu
QEMU_ARGS_i386    = -M pc -append "console=ttyS0,9600 i8042.noaux panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_x86     = -M pc -append "console=ttyS0,9600 i8042.noaux panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_arm64   = -M virt -cpu cortex-a53 -append "panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_arm     = -M virt -append "panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_mips    = -M malta -append "panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_riscv   = -M virt -append "console=ttyS0 panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS         = $(QEMU_ARGS_$(ARCH))

# OUTPUT is only set when run from the main makefile, otherwise
# it defaults to this nolibc directory.
OUTPUT ?= $(CURDIR)/

ifeq ($(V),1)
Q=
else
Q=@
endif

CFLAGS  ?= -Os -fno-ident -fno-asynchronous-unwind-tables
LDFLAGS := -s

all: nolibc-test

sysroot: sysroot/$(ARCH)/include

sysroot/$(ARCH)/include:
	$(QUIET_MKDIR)mkdir -p sysroot
	$(Q)$(MAKE) -C ../../../include/nolibc ARCH=$(ARCH) OUTPUT=$(CURDIR)/sysroot/ headers_standalone
	$(Q)mv sysroot/sysroot sysroot/$(ARCH)

nolibc-test: nolibc-test.c sysroot/$(ARCH)/include
	$(QUIET_CC)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ \
	  -nostdlib -static -Isysroot/$(ARCH)/include $< -lgcc

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
	$(Q)grep -w FAIL "$(CURDIR)/run.out" && echo "See all results in $(CURDIR)/run.out" || echo "$$(grep -c ^[0-9].*OK $(CURDIR)/run.out) test(s) passed."

# re-run the tests from an existing kernel
rerun:
	$(Q)qemu-system-$(QEMU_ARCH) -display none -no-reboot -kernel "$(srctree)/$(IMAGE)" -serial stdio $(QEMU_ARGS) > "$(CURDIR)/run.out"
	$(Q)grep -w FAIL "$(CURDIR)/run.out" && echo "See all results in $(CURDIR)/run.out" || echo "$$(grep -c ^[0-9].*OK $(CURDIR)/run.out) test(s) passed."

clean:
	$(call QUIET_CLEAN, sysroot)
	$(Q)rm -rf sysroot
	$(call QUIET_CLEAN, nolibc-test)
	$(Q)rm -f nolibc-test
	$(call QUIET_CLEAN, initramfs)
	$(Q)rm -rf initramfs
	$(call QUIET_CLEAN, run.out)
	$(Q)rm -rf run.out
