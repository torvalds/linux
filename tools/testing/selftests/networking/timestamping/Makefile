# SPDX-License-Identifier: GPL-2.0
CFLAGS += -I../../../../../usr/include

TEST_PROGS := hwtstamp_config rxtimestamp timestamping txtimestamp

all: $(TEST_PROGS)

include ../../lib.mk

clean:
	rm -fr $(TEST_PROGS)
