# SPDX-License-Identifier: GPL-2.0
#
# First run: make -C ../../../.. headers_install

CFLAGS += -Wall -O2 $(KHDR_INCLUDES)

LOCAL_HDRS += common.h

src_test := $(wildcard *_test.c)

TEST_GEN_PROGS := $(src_test:.c=)

TEST_GEN_PROGS_EXTENDED := \
	true \
	sandbox-and-launch \
	wait-pipe \
	wait-pipe-sandbox

# Short targets:
$(TEST_GEN_PROGS): LDLIBS += -lcap -lpthread
$(TEST_GEN_PROGS_EXTENDED): LDFLAGS += -static

include ../lib.mk

# Targets with $(OUTPUT)/ prefix:
$(TEST_GEN_PROGS): LDLIBS += -lcap -lpthread
$(TEST_GEN_PROGS_EXTENDED): LDFLAGS += -static
