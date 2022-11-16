# SPDX-License-Identifier: GPL-2.0
#

CFLAGS += $(shell pkg-config --cflags alsa)
LDLIBS += $(shell pkg-config --libs alsa)
ifeq ($(LDLIBS),)
LDLIBS += -lasound
endif

TEST_GEN_PROGS := mixer-test pcm-test

pcm-test: pcm-test.c conf.c

include ../lib.mk
