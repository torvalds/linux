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

# binfmt_misc must not be reachable as an exec source or as a stacking layer,
# or an 'F' entry can pin the instance that owns it. Unprivileged, no bpf.
TEST_GEN_PROGS += binfmt_misc_selfpin

# 'D' (register disabled) binfmt_misc test: an entry that exists but does
# not dispatch until it is enabled. Static magic entry, no bpf toolchain.
TEST_GEN_PROGS += binfmt_misc_disabled

# Static ('T' flag) transparent binfmt_misc test; the asserting interpreter
# is shared with the bpf harness's transparent case. No bpf toolchain needed.
TEST_GEN_PROGS += binfmt_misc_transparent
TEST_GEN_FILES += binfmt_transparent_interp

# 'L' (loader substitution) binfmt_misc test: the payload runs as the main
# image with a copy of the system loader substituted for its PT_INTERP and
# asserts the native identity from inside; the static build proves the
# override is dropped for a binary without PT_INTERP.
TEST_GEN_PROGS += binfmt_misc_loader
TEST_GEN_FILES += binfmt_loader_payload binfmt_loader_payload_static

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
TEST_GEN_FILES += bpf_interp.bpf.o nix_origin.bpf.o transparent.bpf.o
TEST_GEN_FILES += loader.bpf.o interp_bind.bpf.o
TEST_GEN_FILES += binfmt_bpf_interp binfmt_bpf_app binfmt_bind_interp
else
$(info exec selftests: skipping binfmt_misc_bpf, needs clang, bpftool, vmlinux BTF and libbpf)
endif

EXTRA_CLEAN := $(OUTPUT)/subdir.moved $(OUTPUT)/execveat.moved $(OUTPUT)/xxxxx*	\
	       $(OUTPUT)/S_I*.test

LOCAL_HDRS += binfmt_misc_common.h

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

# Reuses setup_userns()/write_file() from the filesystems selftests. Their
# wrappers.h wants the uapi headers, so ask for them here rather than widening
# CFLAGS for every program in this directory.
$(OUTPUT)/binfmt_misc_selfpin: CFLAGS += $(TOOLS_INCLUDES)
$(OUTPUT)/binfmt_misc_selfpin: ../filesystems/utils.c

# --- binfmt_misc bpf ('B') handler test ---------------------------------
# The struct_ops bpf objects are compiled against the running kernel's BTF.
# CLANG/BPFTOOL/VMLINUX_BTF are set above next to the toolchain check;
# override LIBBPF_CFLAGS/LDLIBS to point at a libbpf install.
BPF_CFLAGS ?= -I$(OUTPUT)
LIBBPF_CFLAGS ?=
LIBBPF_LDLIBS ?= -lbpf -lelf -lz

$(OUTPUT)/vmlinux.h:
	$(BPFTOOL) btf dump file $(VMLINUX_BTF) format c > $@

# BPF_NO_KFUNC_PROTOTYPES: the programs declare the kfuncs they use themselves.
$(OUTPUT)/%.bpf.o: %.bpf.c $(OUTPUT)/vmlinux.h
	$(CLANG) -g -O2 -target bpf -mcpu=v3 -DBPF_NO_KFUNC_PROTOTYPES \
		$(BPF_CFLAGS) $(LIBBPF_CFLAGS) -c $< -o $@

$(OUTPUT)/binfmt_misc_bpf: binfmt_misc_bpf.c binfmt_misc_common.h
	$(CC) $(CFLAGS) $(LIBBPF_CFLAGS) $(LDFLAGS) $< $(LIBBPF_LDLIBS) -o $@

$(OUTPUT)/binfmt_bpf_interp: binfmt_bpf_interp.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

$(OUTPUT)/binfmt_bind_interp: binfmt_bind_interp.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

$(OUTPUT)/binfmt_loader_payload: binfmt_loader_payload.c binfmt_misc_common.h
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIE -pie $< -o $@

$(OUTPUT)/binfmt_loader_payload_static: binfmt_loader_payload.c binfmt_misc_common.h
	$(CC) $(CFLAGS) $(LDFLAGS) -static $< -o $@

# PT_INTERP is set to the literal "$ORIGIN/binfmt_bpf_interp"; the nix_origin
# handler resolves it relative to the binary at run time.
$(OUTPUT)/binfmt_bpf_app: binfmt_bpf_app.c
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--dynamic-linker,'$$ORIGIN/binfmt_bpf_interp' $< -o $@

EXTRA_CLEAN += $(OUTPUT)/vmlinux.h $(OUTPUT)/*.bpf.o
