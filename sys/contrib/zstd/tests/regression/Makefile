# ################################################################
# Copyright (c) 2015-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under both the BSD-style license (found in the
# LICENSE file in the root directory of this source tree) and the GPLv2 (found
# in the COPYING file in the root directory of this source tree).
# ################################################################

CFLAGS ?= -O3

CURL_CFLAGS := $(shell curl-config --cflags)
CURL_LDFLAGS := $(shell curl-config --libs) -pthread

PROGDIR := ../../programs
LIBDIR := ../../lib
ZSTD_CPPFLAGS := -I$(PROGDIR) -I$(LIBDIR) -I$(LIBDIR)/common

REGRESSION_CFLAGS = $(CFLAGS) $(CURL_CFLAGS)
REGRESSION_CPPFLAGS = $(CPPFLAGS) $(ZSTD_CPPFLAGS)
REGRESSION_LDFLAGS = $(LDFLAGS) $(CURL_LDFLAGS)

all: test

xxhash.o: $(LIBDIR)/common/xxhash.c $(LIBDIR)/common/xxhash.h
	$(CC) $(REGRESSION_CFLAGS) $(REGRESSION_CPPFLAGS) $< -c -o $@

util.o: $(PROGDIR)/util.c $(PROGDIR)/util.h
	$(CC) $(REGRESSION_CFLAGS) $(REGRESSION_CPPFLAGS) $< -c -o $@

data.o: data.c data.h $(PROGDIR)/util.h $(LIBDIR)/common/xxhash.h
	$(CC) $(REGRESSION_CFLAGS) $(REGRESSION_CPPFLAGS) $< -c -o $@

config.o: config.c config.h levels.h
	$(CC) $(REGRESSION_CFLAGS) $(REGRESSION_CPPFLAGS) $< -c -o $@

method.h: data.h config.h result.h

method.o: method.c method.h
	$(CC) $(REGRESSION_CFLAGS) $(REGRESSION_CPPFLAGS) $< -c -o $@

result.o: result.c result.h
	$(CC) $(REGRESSION_CFLAGS) $(REGRESSION_CPPFLAGS) $< -c -o $@

test.o: test.c data.h config.h method.h
	$(CC) $(REGRESSION_CFLAGS) $(REGRESSION_CPPFLAGS) $< -c -o $@

libzstd.a:
	$(MAKE) -C $(LIBDIR) libzstd.a-mt
	cp $(LIBDIR)/libzstd.a .

test: test.o data.o config.o util.o method.o result.o xxhash.o libzstd.a
	$(CC) $^ $(REGRESSION_LDFLAGS) -o $@

.PHONY: clean
clean:
	$(MAKE) -C $(LIBDIR) clean
	$(RM) *.o *.a test
