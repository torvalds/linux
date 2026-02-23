# SPDX-License-Identifier: GPL-2.0-only

include ../Makefile.deps

INSTALL	?= install
prefix  ?= /usr

CC := gcc
CFLAGS := -Wall -Wextra -Werror -O2
ifeq ("$(DEBUG)","1")
  CFLAGS += -g -fsanitize=address -fsanitize=leak -static-libasan
endif
CFLAGS += -I../lib -I../generated -I../../../include/uapi/

SRC_VERSION := \
	$(shell make --no-print-directory -sC ../../../.. kernelversion 2>/dev/null || \
		echo "unknown")

CFLAGS += -DSRC_VERSION='"$(SRC_VERSION)"'

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,$(OUTPUT)%.o,$(SRCS))

YNLTOOL := $(OUTPUT)ynltool

include $(wildcard *.d)

all: $(YNLTOOL)

Q = @

$(YNLTOOL): ../libynl.a $(OBJS)
	$(Q)echo -e "\tLINK $@"
	$(Q)$(CC) $(CFLAGS) -o $@ $(OBJS) ../libynl.a -lm

%.o: %.c ../libynl.a
	$(Q)echo -e "\tCC $@"
	$(Q)$(COMPILE.c) -MMD -c -o $@ $<

../libynl.a:
	$(Q)$(MAKE) -C ../

clean:
	rm -f *.o *.d *~

distclean: clean
	rm -f $(YNLTOOL)

bindir ?= /usr/bin

install: $(YNLTOOL)
	$(INSTALL) -m 0755 $(YNLTOOL) $(DESTDIR)$(bindir)/$(YNLTOOL)

.PHONY: all clean distclean
.DEFAULT_GOAL=all
