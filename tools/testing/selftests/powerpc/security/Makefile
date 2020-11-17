# SPDX-License-Identifier: GPL-2.0+

TEST_GEN_PROGS := rfi_flush entry_flush spectre_v2
top_srcdir = ../../../../..

CFLAGS += -I../../../../../usr/include

include ../../lib.mk

$(TEST_GEN_PROGS): ../harness.c ../utils.c

$(OUTPUT)/spectre_v2: CFLAGS += -m64
$(OUTPUT)/spectre_v2: ../pmu/event.c branch_loops.S
