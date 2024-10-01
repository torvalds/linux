# SPDX-License-Identifier: GPL-2.0
INCLUDEDIR := -I.
CFLAGS := $(CFLAGS) $(INCLUDEDIR) -Wall -O2 -g

TEST_GEN_FILES := adi-test

all: $(TEST_GEN_FILES)

$(TEST_GEN_FILES): adi-test.c

TEST_PROGS := drivers_test.sh

include ../../lib.mk

$(OUTPUT)/adi-test: adi-test.c
