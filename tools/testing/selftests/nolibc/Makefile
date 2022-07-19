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

nolibc-test: nolibc-test.c
	$(QUIET_CC)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ \
	  -nostdlib -static -include ../../../include/nolibc/nolibc.h $^ -lgcc

initramfs: nolibc-test
	$(QUIET_MKDIR)mkdir -p initramfs
	$(call QUIET_INSTALL, initramfs/init)
	$(Q)cp nolibc-test initramfs/init

kernel: initramfs
	$(Q)$(MAKE) -C $(srctree) ARCH=$(ARCH) CC=$(CC) CROSS_COMPILE=$(CROSS_COMPILE) $(IMAGE_NAME) CONFIG_INITRAMFS_SOURCE=$(CURDIR)/initramfs

clean:
	$(call QUIET_CLEAN, nolibc-test)
	$(Q)rm -f nolibc-test
	$(call QUIET_CLEAN, initramfs)
	$(Q)rm -rf initramfs
