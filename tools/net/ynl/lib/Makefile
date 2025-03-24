# SPDX-License-Identifier: GPL-2.0

CC=gcc
CFLAGS += -std=gnu11 -O2 -W -Wall -Wextra -Wno-unused-parameter -Wshadow
ifeq ("$(DEBUG)","1")
  CFLAGS += -g -fsanitize=address -fsanitize=leak -static-libasan
endif

SRCS=$(wildcard *.c)
OBJS=$(patsubst %.c,%.o,${SRCS})

include $(wildcard *.d)

all: ynl.a

ynl.a: $(OBJS)
	@echo -e "\tAR $@"
	@ar rcs $@ $(OBJS)

clean:
	rm -f *.o *.d *~

distclean: clean
	rm -f *.a

%.o: %.c
	$(COMPILE.c) -MMD -c -o $@ $<

.PHONY: all clean distclean
.DEFAULT_GOAL=all
