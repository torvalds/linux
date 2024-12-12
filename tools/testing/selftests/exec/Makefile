# SPDX-License-Identifier: GPL-2.0
CFLAGS = -Wall
CFLAGS += -Wno-nonnull
CFLAGS += $(KHDR_INCLUDES)

LDLIBS += -lcap

ALIGNS := 0x1000 0x200000 0x1000000
ALIGN_PIES        := $(patsubst %,load_address.%,$(ALIGNS))
ALIGN_STATIC_PIES := $(patsubst %,load_address.static.%,$(ALIGNS))
ALIGNMENT_TESTS   := $(ALIGN_PIES) $(ALIGN_STATIC_PIES)

TEST_PROGS := binfmt_script.py check-exec-tests.sh
TEST_GEN_PROGS := execveat non-regular $(ALIGNMENT_TESTS)
TEST_GEN_PROGS_EXTENDED := false inc set-exec script-exec.inc script-noexec.inc
TEST_GEN_FILES := execveat.symlink execveat.denatured script subdir
# Makefile is a run-time dependency, since it's accessed by the execveat test
TEST_FILES := Makefile

TEST_GEN_PROGS += recursion-depth
TEST_GEN_PROGS += null-argv
TEST_GEN_PROGS += check-exec

EXTRA_CLEAN := $(OUTPUT)/subdir.moved $(OUTPUT)/execveat.moved $(OUTPUT)/xxxxx*	\
	       $(OUTPUT)/S_I*.test

include ../lib.mk

CHECK_EXEC_SAMPLES := $(top_srcdir)/samples/check-exec

$(OUTPUT)/subdir:
	mkdir -p $@
$(OUTPUT)/script: Makefile
	echo '#!/bin/bash' > $@
	echo 'exit $$*' >> $@
	chmod +x $@
$(OUTPUT)/execveat.symlink: $(OUTPUT)/execveat
	cd $(OUTPUT) && ln -s -f $(shell basename $<) $(shell basename $@)
$(OUTPUT)/execveat.denatured: $(OUTPUT)/execveat
	cp $< $@
	chmod -x $@
$(OUTPUT)/load_address.0x%: load_address.c
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-z,max-page-size=$(lastword $(subst ., ,$@)) \
		-fPIE -pie $< -o $@
$(OUTPUT)/load_address.static.0x%: load_address.c
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-z,max-page-size=$(lastword $(subst ., ,$@)) \
		-fPIE -static-pie $< -o $@
$(OUTPUT)/false: false.c
	$(CC) $(CFLAGS) $(LDFLAGS) -static $< -o $@
$(OUTPUT)/inc: $(CHECK_EXEC_SAMPLES)/inc.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@
$(OUTPUT)/set-exec: $(CHECK_EXEC_SAMPLES)/set-exec.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@
$(OUTPUT)/script-exec.inc: $(CHECK_EXEC_SAMPLES)/script-exec.inc
	cp $< $@
$(OUTPUT)/script-noexec.inc: $(CHECK_EXEC_SAMPLES)/script-noexec.inc
	cp $< $@
