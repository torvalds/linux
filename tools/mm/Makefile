# SPDX-License-Identifier: GPL-2.0
# Makefile for vm tools
#
include ../scripts/Makefile.include

BUILD_TARGETS=page-types slabinfo page_owner_sort page_owner_filter
BUILD_TARGETS += thp_swap_allocator_test gup_bench
INSTALL_TARGETS = $(BUILD_TARGETS) thpmaps

LIB_DIR = ../lib/api
LIBS = $(LIB_DIR)/libapi.a
GUP_BENCH_OBJS = gup_bench.c ../lib/mm/hugepage_settings.c ../lib/mm/file_utils.c

CFLAGS += -Wall -Wextra -I../lib/ -I../.. -pthread
LDFLAGS += $(LIBS) -pthread

all: $(BUILD_TARGETS)

$(BUILD_TARGETS): $(LIBS)

$(LIBS):
	make -C $(LIB_DIR)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

gup_bench: $(GUP_BENCH_OBJS) $(LIBS)
	$(CC) $(CFLAGS) -o $@ $(GUP_BENCH_OBJS) $(LDFLAGS)

clean:
	$(RM) page-types slabinfo page_owner_sort page_owner_filter thp_swap_allocator_test gup_bench
	make -C $(LIB_DIR) clean

sbindir ?= /usr/sbin

install: all
	install -d $(DESTDIR)$(sbindir)
	install -m 755 -p $(INSTALL_TARGETS) $(DESTDIR)$(sbindir)
