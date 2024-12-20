# SPDX-License-Identifier: GPL-2.0
#
ifneq ($(shell pkg-config --exists alsa && echo 0 || echo 1),0)
$(error Package alsa not found, please install alsa development package or \
	add directory containing `alsa.pc` in PKG_CONFIG_PATH)
endif

CFLAGS += $(shell pkg-config --cflags alsa) $(KHDR_INCLUDES)
LDLIBS += $(shell pkg-config --libs alsa)
ifeq ($(LDLIBS),)
LDLIBS += -lasound
endif
CFLAGS += -L$(OUTPUT) -Wl,-rpath=./

LDLIBS+=-lpthread

OVERRIDE_TARGETS = 1

TEST_GEN_PROGS := mixer-test pcm-test test-pcmtest-driver utimer-test

TEST_GEN_PROGS_EXTENDED := libatest.so global-timer

TEST_FILES := conf.d pcm-test.conf

include ../lib.mk

$(OUTPUT)/libatest.so: conf.c alsa-local.h
	$(CC) $(CFLAGS) -shared -fPIC $< $(LDLIBS) -o $@

$(OUTPUT)/%: %.c $(OUTPUT)/libatest.so alsa-local.h
	$(CC) $(CFLAGS) $< $(LDLIBS) -latest -o $@
