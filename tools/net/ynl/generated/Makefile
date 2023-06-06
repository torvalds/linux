# SPDX-License-Identifier: GPL-2.0

CC=gcc
CFLAGS=-std=gnu11 -O2 -W -Wall -Wextra -Wno-unused-parameter -Wshadow \
	-I../lib/
ifeq ("$(DEBUG)","1")
  CFLAGS += -g -fsanitize=address -fsanitize=leak -static-libasan
endif

TOOL:=../ynl-gen-c.py

GENS:=handshake fou netdev
SRCS=$(patsubst %,%-user.c,${GENS})
HDRS=$(patsubst %,%-user.h,${GENS})
OBJS=$(patsubst %,%-user.o,${GENS})

all: protos.a $(HDRS) $(SRCS) $(KHDRS) $(KSRCS) $(UAPI) regen

protos.a: $(OBJS)
	@echo -e "\tAR $@"
	@ar rcs $@ $(OBJS)

%-user.h: ../../../../Documentation/netlink/specs/%.yaml $(TOOL)
	@echo -e "\tGEN $@"
	@$(TOOL) --mode user --header --spec $< > $@

%-user.c: ../../../../Documentation/netlink/specs/%.yaml $(TOOL)
	@echo -e "\tGEN $@"
	@$(TOOL) --mode user --source --spec $< > $@

%-user.o: %-user.c %-user.h
	@echo -e "\tCC $@"
	@$(COMPILE.c) -c -o $@ $<

clean:
	rm -f *.o

hardclean: clean
	rm -f *.c *.h *.a

regen:
	@../ynl-regen.sh

.PHONY: all clean hardclean regen
.DEFAULT_GOAL: all
