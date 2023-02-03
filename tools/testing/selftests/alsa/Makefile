# SPDX-License-Identifier: GPL-2.0
#

CFLAGS += $(shell pkg-config --cflags alsa)
LDLIBS += $(shell pkg-config --libs alsa)
ifeq ($(LDLIBS),)
LDLIBS += -lasound
endif
CFLAGS += -L$(OUTPUT) -Wl,-rpath=./

LDLIBS+=-lpthread

OVERRIDE_TARGETS = 1

TEST_GEN_PROGS := mixer-test pcm-test

TEST_GEN_PROGS_EXTENDED := libatest.so

TEST_FILES := conf.d pcm-test.conf

include ../lib.mk

$(OUTPUT)/libatest.so: conf.c alsa-local.h
	$(CC) $(CFLAGS) -shared -fPIC $< $(LDLIBS) -o $@

$(OUTPUT)/%: %.c $(TEST_GEN_PROGS_EXTENDED) alsa-local.h
	$(CC) $(CFLAGS) $< $(LDLIBS) -latest -o $@
