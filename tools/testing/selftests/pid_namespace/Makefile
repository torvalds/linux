# SPDX-License-Identifier: GPL-2.0
CFLAGS += -g -I../../../../usr/include/

TEST_GEN_PROGS := regression_enomem

include ../lib.mk

$(OUTPUT)/regression_enomem: regression_enomem.c ../pidfd/pidfd.h
