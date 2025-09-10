# SPDX-License-Identifier: GPL-2.0-only

# Makefile for the kernel's cyclic redundancy check (CRC) library code

obj-$(CONFIG_CRC4) += crc4.o
obj-$(CONFIG_CRC7) += crc7.o
obj-$(CONFIG_CRC8) += crc8.o
obj-$(CONFIG_CRC16) += crc16.o
obj-$(CONFIG_CRC_CCITT) += crc-ccitt.o
obj-$(CONFIG_CRC_ITU_T) += crc-itu-t.o

obj-$(CONFIG_CRC_T10DIF) += crc-t10dif.o
crc-t10dif-y := crc-t10dif-main.o
ifeq ($(CONFIG_CRC_T10DIF_ARCH),y)
CFLAGS_crc-t10dif-main.o += -I$(src)/$(SRCARCH)
crc-t10dif-$(CONFIG_ARM) += arm/crc-t10dif-core.o
crc-t10dif-$(CONFIG_ARM64) += arm64/crc-t10dif-core.o
crc-t10dif-$(CONFIG_PPC) += powerpc/crct10dif-vpmsum_asm.o
crc-t10dif-$(CONFIG_RISCV) += riscv/crc16_msb.o
crc-t10dif-$(CONFIG_X86) += x86/crc16-msb-pclmul.o
endif

obj-$(CONFIG_CRC32) += crc32.o
crc32-y := crc32-main.o
ifeq ($(CONFIG_CRC32_ARCH),y)
CFLAGS_crc32-main.o += -I$(src)/$(SRCARCH)
crc32-$(CONFIG_ARM) += arm/crc32-core.o
crc32-$(CONFIG_ARM64) += arm64/crc32-core.o
crc32-$(CONFIG_PPC) += powerpc/crc32c-vpmsum_asm.o
crc32-$(CONFIG_RISCV) += riscv/crc32_lsb.o riscv/crc32_msb.o
crc32-$(CONFIG_S390) += s390/crc32le-vx.o s390/crc32be-vx.o
crc32-$(CONFIG_SPARC) += sparc/crc32c_asm.o
crc32-$(CONFIG_X86) += x86/crc32-pclmul.o
crc32-$(CONFIG_X86_64) += x86/crc32c-3way.o
endif

obj-$(CONFIG_CRC64) += crc64.o
crc64-y := crc64-main.o
ifeq ($(CONFIG_CRC64_ARCH),y)
CFLAGS_crc64-main.o += -I$(src)/$(SRCARCH)
crc64-$(CONFIG_RISCV) += riscv/crc64_lsb.o riscv/crc64_msb.o
crc64-$(CONFIG_X86) += x86/crc64-pclmul.o
endif

obj-y += tests/

hostprogs := gen_crc32table gen_crc64table
clean-files := crc32table.h crc64table.h

$(obj)/crc32-main.o: $(obj)/crc32table.h
$(obj)/crc64-main.o: $(obj)/crc64table.h

quiet_cmd_crc32 = GEN     $@
      cmd_crc32 = $< > $@

quiet_cmd_crc64 = GEN     $@
      cmd_crc64 = $< > $@

$(obj)/crc32table.h: $(obj)/gen_crc32table
	$(call cmd,crc32)

$(obj)/crc64table.h: $(obj)/gen_crc64table
	$(call cmd,crc64)
