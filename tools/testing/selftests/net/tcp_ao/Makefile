# SPDX-License-Identifier: GPL-2.0
TEST_BOTH_AF := bench-lookups
TEST_BOTH_AF += connect
TEST_BOTH_AF += connect-deny
TEST_BOTH_AF += icmps-accept icmps-discard
TEST_BOTH_AF += key-management
TEST_BOTH_AF += restore
TEST_BOTH_AF += rst
TEST_BOTH_AF += self-connect
TEST_BOTH_AF += seq-ext
TEST_BOTH_AF += setsockopt-closed
TEST_BOTH_AF += unsigned-md5

TEST_IPV4_PROGS := $(TEST_BOTH_AF:%=%_ipv4)
TEST_IPV6_PROGS := $(TEST_BOTH_AF:%=%_ipv6)

TEST_GEN_PROGS := $(TEST_IPV4_PROGS) $(TEST_IPV6_PROGS)

top_srcdir	  := ../../../../..
include ../../lib.mk

HOSTAR ?= ar

LIBDIR	:= $(OUTPUT)/lib
LIB	:= $(LIBDIR)/libaotst.a
LDLIBS	+= $(LIB) -pthread
LIBDEPS	:= lib/aolib.h Makefile

CFLAGS	:= -Wall -O2 -g -D_GNU_SOURCE -fno-strict-aliasing
CFLAGS	+= $(KHDR_INCLUDES)
CFLAGS	+= -iquote ./lib/ -I ../../../../include/

# Library
LIBSRC	:= kconfig.c netlink.c proc.c repair.c setup.c sock.c utils.c
LIBOBJ	:= $(LIBSRC:%.c=$(LIBDIR)/%.o)
EXTRA_CLEAN += $(LIBOBJ) $(LIB)

$(LIB): $(LIBOBJ)
	$(HOSTAR) rcs $@ $^

$(LIBDIR)/%.o: ./lib/%.c $(LIBDEPS)
	mkdir -p $(LIBDIR)
	$(CC) $< $(CFLAGS) $(CPPFLAGS) -o $@ -c

$(TEST_GEN_PROGS): $(LIB)

$(OUTPUT)/%_ipv4: %.c
	$(LINK.c) $^ $(LDLIBS) -o $@

$(OUTPUT)/%_ipv6: %.c
	$(LINK.c) -DIPV6_TEST $^ $(LDLIBS) -o $@

$(OUTPUT)/icmps-accept_ipv4: CFLAGS+= -DTEST_ICMPS_ACCEPT
$(OUTPUT)/icmps-accept_ipv6: CFLAGS+= -DTEST_ICMPS_ACCEPT
$(OUTPUT)/bench-lookups_ipv4: LDLIBS+= -lm
$(OUTPUT)/bench-lookups_ipv6: LDLIBS+= -lm
