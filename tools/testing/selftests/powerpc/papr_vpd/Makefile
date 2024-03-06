# SPDX-License-Identifier: GPL-2.0
noarg:
	$(MAKE) -C ../

TEST_GEN_PROGS := papr_vpd

top_srcdir = ../../../../..
include ../../lib.mk

$(TEST_GEN_PROGS): ../harness.c ../utils.c

$(OUTPUT)/papr_vpd: CFLAGS += $(KHDR_INCLUDES)
