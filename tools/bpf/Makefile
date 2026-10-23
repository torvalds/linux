# SPDX-License-Identifier: GPL-2.0
include ../scripts/Makefile.include

all: bpftool

clean: bpftool_clean resolve_btfids_clean

install: bpftool_install

bpftool:
	$(call descend,bpftool)

bpftool_install:
	$(call descend,bpftool,install)

bpftool_clean:
	$(call descend,bpftool,clean)

resolve_btfids:
	$(call descend,resolve_btfids)

resolve_btfids_clean:
	$(call descend,resolve_btfids,clean)

.PHONY: all install clean bpftool bpftool_install bpftool_clean \
	resolve_btfids resolve_btfids_clean
