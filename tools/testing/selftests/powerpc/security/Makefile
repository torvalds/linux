# SPDX-License-Identifier: GPL-2.0+

TEST_GEN_PROGS := rfi_flush entry_flush uaccess_flush spectre_v2
TEST_PROGS := mitigation-patching.sh

top_srcdir = ../../../../..

include ../../lib.mk
include ../flags.mk

CFLAGS += $(KHDR_INCLUDES)

$(TEST_GEN_PROGS): ../harness.c ../utils.c

$(OUTPUT)/spectre_v2: CFLAGS += -m64
$(OUTPUT)/spectre_v2: ../pmu/event.c branch_loops.S
$(OUTPUT)/rfi_flush: flush_utils.c
$(OUTPUT)/entry_flush: flush_utils.c
$(OUTPUT)/uaccess_flush: flush_utils.c
