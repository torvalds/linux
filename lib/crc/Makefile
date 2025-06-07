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
endif

obj-$(CONFIG_CRC32) += crc32.o
crc32-y := crc32-main.o
ifeq ($(CONFIG_CRC32_ARCH),y)
CFLAGS_crc32-main.o += -I$(src)/$(SRCARCH)
endif

obj-$(CONFIG_CRC64) += crc64.o
crc64-y := crc64-main.o
ifeq ($(CONFIG_CRC64_ARCH),y)
CFLAGS_crc64-main.o += -I$(src)/$(SRCARCH)
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
