# SPDX-License-Identifier: GPL-2.0
# Makefile for io_uring test tools
CFLAGS += -Wall -Wextra -g -D_GNU_SOURCE
LDLIBS += -lpthread

all: io_uring-cp io_uring-bench
%: %.c
	$(CC) $(CFLAGS) -o $@ $^

io_uring-bench: syscall.o io_uring-bench.o
	$(CC) $(CFLAGS) $(LDLIBS) -o $@ $^

io_uring-cp: setup.o syscall.o queue.o

clean:
	$(RM) io_uring-cp io_uring-bench *.o

.PHONY: all clean
