# SPDX-License-Identifier: GPL-2.0

CC=gcc
CFLAGS += -std=gnu11 -O2 -W -Wall -Wextra -Wno-unused-parameter -Wshadow \
	-I../lib/ -idirafter $(UAPI_PATH)
ifeq ("$(DEBUG)","1")
  CFLAGS += -g -fsanitize=address -fsanitize=leak -static-libasan
endif

INSTALL     ?= install
prefix      ?= /usr
datarootdir ?= $(prefix)/share
docdir      ?= $(datarootdir)/doc
includedir  ?= $(prefix)/include

include ../Makefile.deps

YNL_GEN_ARG_ethtool:=--user-header linux/ethtool_netlink.h \
	--exclude-op stats-get

TOOL:=../pyynl/ynl_gen_c.py
TOOL_RST:=../pyynl/ynl_gen_rst.py

SPECS_DIR:=../../../../Documentation/netlink/specs
SPECS_PATHS=$(wildcard $(SPECS_DIR)/*.yaml)
GENS_UNSUP=conntrack nftables
GENS=$(filter-out ${GENS_UNSUP},$(patsubst $(SPECS_DIR)/%.yaml,%,${SPECS_PATHS}))
SRCS=$(patsubst %,%-user.c,${GENS})
HDRS=$(patsubst %,%-user.h,${GENS})
OBJS=$(patsubst %,%-user.o,${GENS})

SPECS_PATHS=$(wildcard $(SPECS_DIR)/*.yaml)
SPECS=$(patsubst $(SPECS_DIR)/%.yaml,%,${SPECS_PATHS})
RSTS=$(patsubst %,%.rst,${SPECS})

all: protos.a $(HDRS) $(SRCS) $(KHDRS) $(KSRCS) $(UAPI) $(RSTS)

protos.a: $(OBJS)
	@echo -e "\tAR $@"
	@ar rcs $@ $(OBJS)

%-user.h: $(SPECS_DIR)/%.yaml $(TOOL)
	@echo -e "\tGEN $@"
	@$(TOOL) --mode user --header --spec $< -o $@ $(YNL_GEN_ARG_$*)

%-user.c: $(SPECS_DIR)/%.yaml $(TOOL)
	@echo -e "\tGEN $@"
	@$(TOOL) --mode user --source --spec $< -o $@ $(YNL_GEN_ARG_$*)

%-user.o: %-user.c %-user.h
	@echo -e "\tCC $@"
	@$(COMPILE.c) $(CFLAGS_$*) -o $@ $<

%.rst: $(SPECS_DIR)/%.yaml $(TOOL_RST)
	@echo -e "\tGEN_RST $@"
	@$(TOOL_RST) -o $@ -i $<

clean:
	rm -f *.o

distclean: clean
	rm -f *.c *.h *.a *.rst

regen:
	@../ynl-regen.sh

install-headers: $(HDRS)
	@echo -e "\tINSTALL generated headers"
	@$(INSTALL) -d $(DESTDIR)$(includedir)/ynl
	@$(INSTALL) -m 0644 *.h $(DESTDIR)$(includedir)/ynl/

install-rsts: $(RSTS)
	@echo -e "\tINSTALL generated docs"
	@$(INSTALL) -d $(DESTDIR)$(docdir)/ynl
	@$(INSTALL) -m 0644 $(RSTS) $(DESTDIR)$(docdir)/ynl/

install-specs:
	@echo -e "\tINSTALL specs"
	@$(INSTALL) -d $(DESTDIR)$(datarootdir)/ynl
	@$(INSTALL) -m 0644 ../../../../Documentation/netlink/*.yaml $(DESTDIR)$(datarootdir)/ynl/
	@$(INSTALL) -d $(DESTDIR)$(datarootdir)/ynl/specs
	@$(INSTALL) -m 0644 $(SPECS_DIR)/*.yaml $(DESTDIR)$(datarootdir)/ynl/specs/

install: install-headers install-rsts install-specs

.PHONY: all clean distclean regen install install-headers install-rsts install-specs
.DEFAULT_GOAL: all
