# SPDX-License-Identifier: GPL-2.0
CFLAGS += -I../../../../../usr/include

TEST_PROGS := hwtstamp_config rxtimestamp timestamping txtimestamp

all: $(TEST_PROGS)

top_srcdir = ../../../../..
KSFT_KHDR_INSTALL := 1
include ../../lib.mk

clean:
	rm -fr $(TEST_PROGS)
