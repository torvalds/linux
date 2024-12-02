# SPDX-License-Identifier: GPL-2.0-or-later

CFLAGS += -Wall -O2 -g -fsanitize=address -fsanitize=undefined
TEST_GEN_PROGS := openat2_test resolve_test rename_attack_test

include ../lib.mk

$(TEST_GEN_PROGS): helpers.c helpers.h
