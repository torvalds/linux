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

# binfmt_misc bpf-backed ('B') handler test: a libbpf harness plus its
# struct_ops objects and the test interpreter/app it routes between. Only
# built when clang, bpftool, the vmlinux BTF and libbpf are all present
# (HAVE_BPF_TOOLCHAIN=y forces it) so the other exec selftests don't grow
# a bpf toolchain dependency.
CLANG ?= clang
BPFTOOL ?= bpftool
VMLINUX_BTF ?= /sys/kernel/btf/vmlinux
HAVE_BPF_TOOLCHAIN ?= $(shell command -v $(CLANG) >/dev/null 2>&1 && \
			command -v $(BPFTOOL) >/dev/null 2>&1 && \
			test -r $(VMLINUX_BTF) && \
			pkg-config --exists libbpf 2>/dev/null && echo y)
ifeq ($(HAVE_BPF_TOOLCHAIN),y)
TEST_GEN_PROGS += binfmt_misc_bpf
TEST_GEN_FILES += bpf_interp.bpf.o nix_origin.bpf.o
TEST_GEN_FILES += binfmt_bpf_interp binfmt_bpf_app
else
$(info exec selftests: skipping binfmt_misc_bpf, needs clang, bpftool, vmlinux BTF and libbpf)
endif

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

# --- binfmt_misc bpf ('B') handler test ---------------------------------
# The struct_ops bpf objects are compiled against the running kernel's BTF.
# CLANG/BPFTOOL/VMLINUX_BTF are set above next to the toolchain check;
# override LIBBPF_CFLAGS/LDLIBS to point at a libbpf install.
BPF_CFLAGS ?= -I$(OUTPUT)
LIBBPF_CFLAGS ?=
LIBBPF_LDLIBS ?= -lbpf -lelf -lz

$(OUTPUT)/vmlinux.h:
	$(BPFTOOL) btf dump file $(VMLINUX_BTF) format c > $@
	sed -i '/__ksym;$$/d' $@

$(OUTPUT)/%.bpf.o: %.bpf.c $(OUTPUT)/vmlinux.h
	$(CLANG) -g -O2 -target bpf -mcpu=v3 $(BPF_CFLAGS) $(LIBBPF_CFLAGS) -c $< -o $@

$(OUTPUT)/binfmt_misc_bpf: binfmt_misc_bpf.c
	$(CC) $(CFLAGS) $(LIBBPF_CFLAGS) $(LDFLAGS) $< $(LIBBPF_LDLIBS) -o $@

$(OUTPUT)/binfmt_bpf_interp: binfmt_bpf_interp.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

# PT_INTERP is set to the literal "$ORIGIN/binfmt_bpf_interp"; the nix_origin
# handler resolves it relative to the binary at run time.
$(OUTPUT)/binfmt_bpf_app: binfmt_bpf_app.c
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--dynamic-linker,'$$ORIGIN/binfmt_bpf_interp' $< -o $@

EXTRA_CLEAN += $(OUTPUT)/vmlinux.h $(OUTPUT)/bpf_interp.bpf.o $(OUTPUT)/nix_origin.bpf.o
