# SPDX-License-Identifier: GPL-2.0
CFLAGS = -Wall
CFLAGS += -Wno-nonnull
CFLAGS += -D_GNU_SOURCE

TEST_PROGS := binfmt_script.py
TEST_GEN_PROGS := execveat load_address_4096 load_address_2097152 load_address_16777216 non-regular
TEST_GEN_FILES := execveat.symlink execveat.denatured script subdir
# Makefile is a run-time dependency, since it's accessed by the execveat test
TEST_FILES := Makefile

TEST_GEN_PROGS += recursion-depth
TEST_GEN_PROGS += null-argv

EXTRA_CLEAN := $(OUTPUT)/subdir.moved $(OUTPUT)/execveat.moved $(OUTPUT)/xxxxx*	\
	       $(OUTPUT)/S_I*.test

include ../lib.mk

$(OUTPUT)/subdir:
	mkdir -p $@
$(OUTPUT)/script:
	echo '#!/bin/sh' > $@
	echo 'exit $$*' >> $@
	chmod +x $@
$(OUTPUT)/execveat.symlink: $(OUTPUT)/execveat
	cd $(OUTPUT) && ln -s -f $(shell basename $<) $(shell basename $@)
$(OUTPUT)/execveat.denatured: $(OUTPUT)/execveat
	cp $< $@
	chmod -x $@
$(OUTPUT)/load_address_4096: load_address.c
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-z,max-page-size=0x1000 -pie -static $< -o $@
$(OUTPUT)/load_address_2097152: load_address.c
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-z,max-page-size=0x200000 -pie -static $< -o $@
$(OUTPUT)/load_address_16777216: load_address.c
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-z,max-page-size=0x1000000 -pie -static $< -o $@
