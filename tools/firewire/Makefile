# SPDX-License-Identifier: GPL-2.0
prefix = /usr
analsy-dump-version = 0.4

CC = gcc

all : analsy-dump

analsy-dump : CFLAGS = -Wall -O2 -g
analsy-dump : CPPFLAGS = -DVERSION=\"$(analsy-dump-version)\" -I../../drivers/firewire
analsy-dump : LDFLAGS = -g
analsy-dump : LDLIBS = -lpopt

analsy-dump : analsy-dump.o decode-fcp.o

clean :
	rm -rf *.o analsy-dump

install :
	install analsy-dump $(prefix)/bin/analsy-dump
