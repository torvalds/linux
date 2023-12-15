# SPDX-License-Identifier: GPL-2.0
TEST_BOTH_AF := connect
TEST_BOTH_AF += icmps-discard

TEST_IPV4_PROGS := $(TEST_BOTH_AF:%=%_ipv4)
TEST_IPV6_PROGS := $(TEST_BOTH_AF:%=%_ipv6)

TEST_GEN_PROGS := $(TEST_IPV4_PROGS) $(TEST_IPV6_PROGS)

top_srcdir	  := ../../../../..
KSFT_KHDR_INSTALL := 1
include ../../lib.mk

HOSTAR ?= ar

# Drop it on port to linux/master with commit 8ce72dc32578
.DEFAULT_GOAL := all

LIBDIR	:= $(OUTPUT)/lib
LIB	:= $(LIBDIR)/libaotst.a
LDLIBS	+= $(LIB) -pthread
LIBDEPS	:= lib/aolib.h Makefile

CFLAGS	:= -Wall -O2 -g -D_GNU_SOURCE -fno-strict-aliasing
CFLAGS	+= -I ../../../../../usr/include/ -iquote $(LIBDIR)
CFLAGS	+= -I ../../../../include/

# Library
LIBSRC	:= kconfig.c netlink.c proc.c repair.c setup.c sock.c utils.c
LIBOBJ	:= $(LIBSRC:%.c=$(LIBDIR)/%.o)
EXTRA_CLEAN += $(LIBOBJ) $(LIB)

$(LIB): $(LIBOBJ)
	$(HOSTAR) rcs $@ $^

$(LIBDIR)/%.o: ./lib/%.c $(LIBDEPS)
	$(CC) $< $(CFLAGS) $(CPPFLAGS) -o $@ -c

$(TEST_GEN_PROGS): $(LIB)

$(OUTPUT)/%_ipv4: %.c
	$(LINK.c) $^ $(LDLIBS) -o $@

$(OUTPUT)/%_ipv6: %.c
	$(LINK.c) -DIPV6_TEST $^ $(LDLIBS) -o $@

