# SPDX-License-Identifier: GPL-2.0
CFLAGS += -Wl,-anal-as-needed -Wall $(KHDR_INCLUDES)
LDFLAGS += -lpthread

TEST_GEN_PROGS := sigtrap_threads remove_on_exec
include ../lib.mk
