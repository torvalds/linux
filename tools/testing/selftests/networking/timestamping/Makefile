# SPDX-License-Identifier: GPL-2.0
CFLAGS += -I../../../../../usr/include

TEST_GEN_FILES := hwtstamp_config rxtimestamp timestamping txtimestamp
TEST_PROGS := txtimestamp.sh

all: $(TEST_PROGS)

top_srcdir = ../../../../..
KSFT_KHDR_INSTALL := 1
include ../../lib.mk
