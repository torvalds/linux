# SPDX-License-Identifier: GPL-2.0
CFLAGS += $(KHDR_INCLUDES)
TEST_PROGS := testptp
LDLIBS += -lrt
all: $(TEST_PROGS)

include ../lib.mk

clean:
	rm -fr $(TEST_PROGS)
