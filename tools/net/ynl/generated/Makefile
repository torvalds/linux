# SPDX-License-Identifier: GPL-2.0

CC=gcc
CFLAGS=-std=gnu11 -O2 -W -Wall -Wextra -Wno-unused-parameter -Wshadow \
	-I../lib/ -idirafter $(UAPI_PATH)
ifeq ("$(DEBUG)","1")
  CFLAGS += -g -fsanitize=address -fsanitize=leak -static-libasan
endif

include ../Makefile.deps

YNL_GEN_ARG_ethtool:=--user-header linux/ethtool_netlink.h \
	--exclude-op stats-get

TOOL:=../ynl-gen-c.py

GENS:=ethtool devlink handshake fou netdev nfsd
SRCS=$(patsubst %,%-user.c,${GENS})
HDRS=$(patsubst %,%-user.h,${GENS})
OBJS=$(patsubst %,%-user.o,${GENS})

all: protos.a $(HDRS) $(SRCS) $(KHDRS) $(KSRCS) $(UAPI) regen

protos.a: $(OBJS)
	@echo -e "\tAR $@"
	@ar rcs $@ $(OBJS)

%-user.h: ../../../../Documentation/netlink/specs/%.yaml $(TOOL)
	@echo -e "\tGEN $@"
	@$(TOOL) --mode user --header --spec $< $(YNL_GEN_ARG_$*) > $@

%-user.c: ../../../../Documentation/netlink/specs/%.yaml $(TOOL)
	@echo -e "\tGEN $@"
	@$(TOOL) --mode user --source --spec $< $(YNL_GEN_ARG_$*) > $@

%-user.o: %-user.c %-user.h
	@echo -e "\tCC $@"
	@$(COMPILE.c) $(CFLAGS_$*) -o $@ $<

clean:
	rm -f *.o

hardclean: clean
	rm -f *.c *.h *.a

regen:
	@../ynl-regen.sh

.PHONY: all clean hardclean regen
.DEFAULT_GOAL: all
