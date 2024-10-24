# SPDX-License-Identifier: GPL-2.0

CFLAGS += -I$(top_srcdir)/tools/include

TEST_GEN_PROGS := pointer_masking

include ../../lib.mk

$(OUTPUT)/pointer_masking: pointer_masking.c
	$(CC) -static -o$@ $(CFLAGS) $(LDFLAGS) $^
