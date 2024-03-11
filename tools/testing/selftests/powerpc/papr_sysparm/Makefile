# SPDX-License-Identifier: GPL-2.0
noarg:
	$(MAKE) -C ../

TEST_GEN_PROGS := papr_sysparm

top_srcdir = ../../../../..
include ../../lib.mk

$(TEST_GEN_PROGS): ../harness.c ../utils.c

$(OUTPUT)/papr_sysparm: CFLAGS += $(KHDR_INCLUDES)
