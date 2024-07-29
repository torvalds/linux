# SPDX-License-Identifier: GPL-2.0
# Makefile for nolibc tests
include ../../../scripts/Makefile.include
include ../../../scripts/utilities.mak
# We need this for the "cc-option" macro.
include ../../../build/Build.include

ifneq ($(O),)
ifneq ($(call is-absolute,$(O)),y)
$(error Only absolute O= parameters are supported)
endif
endif

# we're in ".../tools/testing/selftests/nolibc"
ifeq ($(srctree),)
srctree := $(patsubst %/tools/testing/selftests/,%,$(dir $(CURDIR)))
endif

ifeq ($(ARCH),)
include $(srctree)/scripts/subarch.include
ARCH = $(SUBARCH)
endif

objtree ?= $(srctree)

# XARCH extends the kernel's ARCH with a few variants of the same
# architecture that only differ by the configuration, the toolchain
# and the Qemu program used. It is copied as-is into ARCH except for
# a few specific values which are mapped like this:
#
#  XARCH        | ARCH      | config
#  -------------|-----------|-------------------------
#  ppc          | powerpc   | 32 bits
#  ppc64        | powerpc   | 64 bits big endian
#  ppc64le      | powerpc   | 64 bits little endian
#
# It is recommended to only use XARCH, though it does not harm if
# ARCH is already set. For simplicity, ARCH is sufficient for all
# architectures where both are equal.

# configure default variants for target kernel supported architectures
XARCH_powerpc    = ppc
XARCH_mips       = mips32le
XARCH            = $(or $(XARCH_$(ARCH)),$(ARCH))

# map from user input variants to their kernel supported architectures
ARCH_ppc         = powerpc
ARCH_ppc64       = powerpc
ARCH_ppc64le     = powerpc
ARCH_mips32le    = mips
ARCH_mips32be    = mips
ARCH            := $(or $(ARCH_$(XARCH)),$(XARCH))

# kernel image names by architecture
IMAGE_i386       = arch/x86/boot/bzImage
IMAGE_x86_64     = arch/x86/boot/bzImage
IMAGE_x86        = arch/x86/boot/bzImage
IMAGE_arm64      = arch/arm64/boot/Image
IMAGE_arm        = arch/arm/boot/zImage
IMAGE_mips32le   = vmlinuz
IMAGE_mips32be   = vmlinuz
IMAGE_ppc        = vmlinux
IMAGE_ppc64      = vmlinux
IMAGE_ppc64le    = arch/powerpc/boot/zImage
IMAGE_riscv      = arch/riscv/boot/Image
IMAGE_s390       = arch/s390/boot/bzImage
IMAGE_loongarch  = arch/loongarch/boot/vmlinuz.efi
IMAGE            = $(objtree)/$(IMAGE_$(XARCH))
IMAGE_NAME       = $(notdir $(IMAGE))

# default kernel configurations that appear to be usable
DEFCONFIG_i386       = defconfig
DEFCONFIG_x86_64     = defconfig
DEFCONFIG_x86        = defconfig
DEFCONFIG_arm64      = defconfig
DEFCONFIG_arm        = multi_v7_defconfig
DEFCONFIG_mips32le   = malta_defconfig
DEFCONFIG_mips32be   = malta_defconfig
DEFCONFIG_ppc        = pmac32_defconfig
DEFCONFIG_ppc64      = powernv_be_defconfig
DEFCONFIG_ppc64le    = powernv_defconfig
DEFCONFIG_riscv      = defconfig
DEFCONFIG_s390       = defconfig
DEFCONFIG_loongarch  = defconfig
DEFCONFIG            = $(DEFCONFIG_$(XARCH))

EXTRACONFIG_mips32be = -d CONFIG_CPU_LITTLE_ENDIAN -e CONFIG_CPU_BIG_ENDIAN
EXTRACONFIG           = $(EXTRACONFIG_$(XARCH))

# optional tests to run (default = all)
TEST =

# QEMU_ARCH: arch names used by qemu
QEMU_ARCH_i386       = i386
QEMU_ARCH_x86_64     = x86_64
QEMU_ARCH_x86        = x86_64
QEMU_ARCH_arm64      = aarch64
QEMU_ARCH_arm        = arm
QEMU_ARCH_mips32le   = mipsel  # works with malta_defconfig
QEMU_ARCH_mips32be  = mips
QEMU_ARCH_ppc        = ppc
QEMU_ARCH_ppc64      = ppc64
QEMU_ARCH_ppc64le    = ppc64
QEMU_ARCH_riscv      = riscv64
QEMU_ARCH_s390       = s390x
QEMU_ARCH_loongarch  = loongarch64
QEMU_ARCH            = $(QEMU_ARCH_$(XARCH))

QEMU_ARCH_USER_ppc64le = ppc64le
QEMU_ARCH_USER         = $(or $(QEMU_ARCH_USER_$(XARCH)),$(QEMU_ARCH_$(XARCH)))

QEMU_BIOS_DIR = /usr/share/edk2/
QEMU_BIOS_loongarch = $(QEMU_BIOS_DIR)/loongarch64/OVMF_CODE.fd

ifneq ($(QEMU_BIOS_$(XARCH)),)
QEMU_ARGS_BIOS = -bios $(QEMU_BIOS_$(XARCH))
endif

# QEMU_ARGS : some arch-specific args to pass to qemu
QEMU_ARGS_i386       = -M pc -append "console=ttyS0,9600 i8042.noaux panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_x86_64     = -M pc -append "console=ttyS0,9600 i8042.noaux panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_x86        = -M pc -append "console=ttyS0,9600 i8042.noaux panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_arm64      = -M virt -cpu cortex-a53 -append "panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_arm        = -M virt -append "panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_mips32le   = -M malta -append "panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_mips32be   = -M malta -append "panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_ppc        = -M g3beige -append "console=ttyS0 panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_ppc64      = -M powernv -append "console=hvc0 panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_ppc64le    = -M powernv -append "console=hvc0 panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_riscv      = -M virt -append "console=ttyS0 panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_s390       = -M s390-ccw-virtio -m 1G -append "console=ttyS0 panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS_loongarch  = -M virt -append "console=ttyS0,115200 panic=-1 $(TEST:%=NOLIBC_TEST=%)"
QEMU_ARGS            = $(QEMU_ARGS_$(XARCH)) $(QEMU_ARGS_BIOS) $(QEMU_ARGS_EXTRA)

# OUTPUT is only set when run from the main makefile, otherwise
# it defaults to this nolibc directory.
OUTPUT ?= $(CURDIR)/

ifeq ($(V),1)
Q=
else
Q=@
endif

CFLAGS_i386 = $(call cc-option,-m32)
CFLAGS_ppc = -m32 -mbig-endian -mno-vsx $(call cc-option,-mmultiple)
CFLAGS_ppc64 = -m64 -mbig-endian -mno-vsx $(call cc-option,-mmultiple)
CFLAGS_ppc64le = -m64 -mlittle-endian -mno-vsx $(call cc-option,-mabi=elfv2)
CFLAGS_s390 = -m64
CFLAGS_mips32le = -EL -mabi=32 -fPIC
CFLAGS_mips32be = -EB -mabi=32
CFLAGS_STACKPROTECTOR ?= $(call cc-option,-mstack-protector-guard=global $(call cc-option,-fstack-protector-all))
CFLAGS  ?= -Os -fno-ident -fno-asynchronous-unwind-tables -std=c89 -W -Wall -Wextra \
		$(call cc-option,-fno-stack-protector) \
		$(CFLAGS_$(XARCH)) $(CFLAGS_STACKPROTECTOR) $(CFLAGS_EXTRA)
LDFLAGS :=

REPORT  ?= awk '/\[OK\][\r]*$$/{p++} /\[FAIL\][\r]*$$/{if (!f) printf("\n"); f++; print;} /\[SKIPPED\][\r]*$$/{s++} \
		END{ printf("\n%3d test(s): %3d passed, %3d skipped, %3d failed => status: ", p+s+f, p, s, f); \
		if (f) printf("failure\n"); else if (s) printf("warning\n"); else printf("success\n");; \
		printf("\nSee all results in %s\n", ARGV[1]); }'

help:
	@echo "Supported targets under selftests/nolibc:"
	@echo "  all               call the \"run\" target below"
	@echo "  help              this help"
	@echo "  sysroot           create the nolibc sysroot here (uses \$$ARCH)"
	@echo "  nolibc-test       build the executable (uses \$$CC and \$$CROSS_COMPILE)"
	@echo "  libc-test         build an executable using the compiler's default libc instead"
	@echo "  run-user          runs the executable under QEMU (uses \$$XARCH, \$$TEST)"
	@echo "  initramfs.cpio    prepare the initramfs archive with nolibc-test"
	@echo "  initramfs         prepare the initramfs tree with nolibc-test"
	@echo "  defconfig         create a fresh new default config (uses \$$XARCH)"
	@echo "  kernel            (re)build the kernel (uses \$$XARCH)"
	@echo "  kernel-standalone (re)build the kernel with the initramfs (uses \$$XARCH)"
	@echo "  run               runs the kernel in QEMU after building it (uses \$$XARCH, \$$TEST)"
	@echo "  rerun             runs a previously prebuilt kernel in QEMU (uses \$$XARCH, \$$TEST)"
	@echo "  clean             clean the sysroot, initramfs, build and output files"
	@echo ""
	@echo "The output file is \"run.out\". Test ranges may be passed using \$$TEST."
	@echo ""
	@echo "Currently using the following variables:"
	@echo "  ARCH          = $(ARCH)"
	@echo "  XARCH         = $(XARCH)"
	@echo "  CROSS_COMPILE = $(CROSS_COMPILE)"
	@echo "  CC            = $(CC)"
	@echo "  OUTPUT        = $(OUTPUT)"
	@echo "  TEST          = $(TEST)"
	@echo "  QEMU_ARCH     = $(if $(QEMU_ARCH),$(QEMU_ARCH),UNKNOWN_ARCH) [determined from \$$XARCH]"
	@echo "  IMAGE_NAME    = $(if $(IMAGE_NAME),$(IMAGE_NAME),UNKNOWN_ARCH) [determined from \$$XARCH]"
	@echo ""

all: run

sysroot: sysroot/$(ARCH)/include

sysroot/$(ARCH)/include:
	$(Q)rm -rf sysroot/$(ARCH) sysroot/sysroot
	$(QUIET_MKDIR)mkdir -p sysroot
	$(Q)$(MAKE) -C $(srctree) outputmakefile
	$(Q)$(MAKE) -C $(srctree)/tools/include/nolibc ARCH=$(ARCH) OUTPUT=$(CURDIR)/sysroot/ headers_standalone
	$(Q)mv sysroot/sysroot sysroot/$(ARCH)

ifneq ($(NOLIBC_SYSROOT),0)
nolibc-test: nolibc-test.c nolibc-test-linkage.c sysroot/$(ARCH)/include
	$(QUIET_CC)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ \
	  -nostdlib -nostdinc -static -Isysroot/$(ARCH)/include nolibc-test.c nolibc-test-linkage.c -lgcc
else
nolibc-test: nolibc-test.c nolibc-test-linkage.c
	$(QUIET_CC)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ \
	  -nostdlib -static -include $(srctree)/tools/include/nolibc/nolibc.h nolibc-test.c nolibc-test-linkage.c -lgcc
endif

libc-test: nolibc-test.c nolibc-test-linkage.c
	$(QUIET_CC)$(HOSTCC) -o $@ nolibc-test.c nolibc-test-linkage.c

# local libc-test
run-libc-test: libc-test
	$(Q)./libc-test > "$(CURDIR)/run.out" || :
	$(Q)$(REPORT) $(CURDIR)/run.out

# local nolibc-test
run-nolibc-test: nolibc-test
	$(Q)./nolibc-test > "$(CURDIR)/run.out" || :
	$(Q)$(REPORT) $(CURDIR)/run.out

# qemu user-land test
run-user: nolibc-test
	$(Q)qemu-$(QEMU_ARCH_USER) ./nolibc-test > "$(CURDIR)/run.out" || :
	$(Q)$(REPORT) $(CURDIR)/run.out

initramfs.cpio: kernel nolibc-test
	$(QUIET_GEN)echo 'file /init nolibc-test 755 0 0' | $(objtree)/usr/gen_init_cpio - > initramfs.cpio

initramfs: nolibc-test
	$(QUIET_MKDIR)mkdir -p initramfs
	$(call QUIET_INSTALL, initramfs/init)
	$(Q)cp nolibc-test initramfs/init

defconfig:
	$(Q)$(MAKE) -C $(srctree) ARCH=$(ARCH) CC=$(CC) CROSS_COMPILE=$(CROSS_COMPILE) mrproper $(DEFCONFIG) prepare
	$(Q)if [ -n "$(EXTRACONFIG)" ]; then \
		$(srctree)/scripts/config --file $(objtree)/.config $(EXTRACONFIG); \
		$(MAKE) -C $(srctree) ARCH=$(ARCH) CC=$(CC) CROSS_COMPILE=$(CROSS_COMPILE) olddefconfig < /dev/null; \
	fi

kernel:
	$(Q)$(MAKE) -C $(srctree) ARCH=$(ARCH) CC=$(CC) CROSS_COMPILE=$(CROSS_COMPILE) $(IMAGE_NAME) < /dev/null

kernel-standalone: initramfs
	$(Q)$(MAKE) -C $(srctree) ARCH=$(ARCH) CC=$(CC) CROSS_COMPILE=$(CROSS_COMPILE) $(IMAGE_NAME) CONFIG_INITRAMFS_SOURCE=$(CURDIR)/initramfs < /dev/null

# run the tests after building the kernel
run: kernel initramfs.cpio
	$(Q)qemu-system-$(QEMU_ARCH) -display none -no-reboot -kernel "$(IMAGE)" -initrd initramfs.cpio -serial stdio $(QEMU_ARGS) > "$(CURDIR)/run.out"
	$(Q)$(REPORT) $(CURDIR)/run.out

# re-run the tests from an existing kernel
rerun:
	$(Q)qemu-system-$(QEMU_ARCH) -display none -no-reboot -kernel "$(IMAGE)" -initrd initramfs.cpio -serial stdio $(QEMU_ARGS) > "$(CURDIR)/run.out"
	$(Q)$(REPORT) $(CURDIR)/run.out

# report with existing test log
report:
	$(Q)$(REPORT) $(CURDIR)/run.out

clean:
	$(call QUIET_CLEAN, sysroot)
	$(Q)rm -rf sysroot
	$(call QUIET_CLEAN, nolibc-test)
	$(Q)rm -f nolibc-test
	$(call QUIET_CLEAN, libc-test)
	$(Q)rm -f libc-test
	$(call QUIET_CLEAN, initramfs.cpio)
	$(Q)rm -rf initramfs.cpio
	$(call QUIET_CLEAN, initramfs)
	$(Q)rm -rf initramfs
	$(call QUIET_CLEAN, run.out)
	$(Q)rm -rf run.out

.PHONY: sysroot/$(ARCH)/include
