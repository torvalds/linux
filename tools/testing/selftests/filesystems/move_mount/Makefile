# SPDX-License-Identifier: GPL-2.0-or-later

CFLAGS += -Wall -O2 -g $(KHDR_INCLUDES) $(TOOLS_INCLUDES)
LDLIBS += -lcap

TEST_GEN_PROGS := move_mount_test

include ../../lib.mk

$(OUTPUT)/move_mount_test: ../utils.c
