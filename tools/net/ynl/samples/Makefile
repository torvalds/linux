# SPDX-License-Identifier: GPL-2.0

include ../Makefile.deps

CC=gcc
CFLAGS += -std=gnu11 -O2 -W -Wall -Wextra -Wno-unused-parameter -Wshadow \
	-I../lib/ -I../generated/ -idirafter $(UAPI_PATH)
ifeq ("$(DEBUG)","1")
  CFLAGS += -g -fsanitize=address -fsanitize=leak -static-libasan
endif

LDLIBS=../lib/ynl.a ../generated/protos.a

SRCS=$(wildcard *.c)
BINS=$(patsubst %.c,%,${SRCS})

include $(wildcard *.d)

all: $(BINS)

CFLAGS_page-pool=$(CFLAGS_netdev)

$(BINS): ../lib/ynl.a ../generated/protos.a $(SRCS)
	@echo -e '\tCC sample $@'
	@$(COMPILE.c) $(CFLAGS_$@) $@.c -o $@.o
	@$(LINK.c) $@.o -o $@  $(LDLIBS)

clean:
	rm -f *.o *.d *~

distclean: clean
	rm -f $(BINS)

.PHONY: all clean distclean
.DEFAULT_GOAL=all
