# SPDX-License-Identifier: GPL-2.0-or-later

CFLAGS += -Wall -O2 -g -fsanitize=address -fsanitize=undefined -static-libasan $(KHDR_INCLUDES)
TEST_GEN_PROGS := fchmodat2_test

include ../lib.mk
