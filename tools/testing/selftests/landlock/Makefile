# SPDX-License-Identifier: GPL-2.0
#
# First run: make -C ../../../.. headers_install

CFLAGS += -Wall -O2 $(KHDR_INCLUDES)
LDLIBS += -lcap

LOCAL_HDRS += common.h

src_test := $(wildcard *_test.c)

TEST_GEN_PROGS := $(src_test:.c=)

TEST_GEN_PROGS_EXTENDED := true

# Static linking for short targets:
$(TEST_GEN_PROGS_EXTENDED): LDFLAGS += -static

include ../lib.mk

# Static linking for targets with $(OUTPUT)/ prefix:
$(TEST_GEN_PROGS_EXTENDED): LDFLAGS += -static
