# SPDX-License-Identifier: GPL-2.0
#

CFLAGS += $(shell pkg-config --cflags alsa)
LDLIBS += $(shell pkg-config --libs alsa)
ifeq ($(LDLIBS),)
LDLIBS += -lasound
endif

TEST_GEN_PROGS := mixer-test

include ../lib.mk
