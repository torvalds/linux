# SPDX-License-Identifier: GPL-2.0
# Makefile for vm tools
#
include ../scripts/Makefile.include

TARGETS=page-types slabinfo page_owner_sort

LIB_DIR = ../lib/api
LIBS = $(LIB_DIR)/libapi.a

CFLAGS = -Wall -Wextra -I../lib/
LDFLAGS = $(LIBS)

all: $(TARGETS)

$(TARGETS): $(LIBS)

$(LIBS):
	make -C $(LIB_DIR)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	$(RM) page-types slabinfo page_owner_sort
	make -C $(LIB_DIR) clean

sbindir ?= /usr/sbin

install: all
	install -d $(DESTDIR)$(sbindir)
	install -m 755 -p $(TARGETS) $(DESTDIR)$(sbindir)
