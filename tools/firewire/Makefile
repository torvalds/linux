# SPDX-License-Identifier: GPL-2.0
prefix = /usr
yessy-dump-version = 0.4

CC = gcc

all : yessy-dump

yessy-dump : CFLAGS = -Wall -O2 -g
yessy-dump : CPPFLAGS = -DVERSION=\"$(yessy-dump-version)\" -I../../drivers/firewire
yessy-dump : LDFLAGS = -g
yessy-dump : LDLIBS = -lpopt

yessy-dump : yessy-dump.o decode-fcp.o

clean :
	rm -rf *.o yessy-dump

install :
	install yessy-dump $(prefix)/bin/yessy-dump
