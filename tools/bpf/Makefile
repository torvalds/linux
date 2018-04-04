# SPDX-License-Identifier: GPL-2.0
prefix = /usr

CC = gcc
LEX = flex
YACC = bison
MAKE = make

CFLAGS += -Wall -O2
CFLAGS += -D__EXPORTED_HEADERS__ -I../../include/uapi -I../../include

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

FEATURE_USER = .bpf
FEATURE_TESTS = libbfd disassembler-four-args
FEATURE_DISPLAY = libbfd disassembler-four-args

check_feat := 1
NON_CHECK_FEAT_TARGETS := clean bpftool_clean
ifdef MAKECMDGOALS
ifeq ($(filter-out $(NON_CHECK_FEAT_TARGETS),$(MAKECMDGOALS)),)
  check_feat := 0
endif
endif

ifeq ($(check_feat),1)
ifeq ($(FEATURES_DUMP),)
include $(srctree)/tools/build/Makefile.feature
else
include $(FEATURES_DUMP)
endif
endif

ifeq ($(feature-disassembler-four-args), 1)
CFLAGS += -DDISASM_FOUR_ARGS_SIGNATURE
endif

%.yacc.c: %.y
	$(YACC) -o $@ -d $<

%.lex.c: %.l
	$(LEX) -o $@ $<

all: bpf_jit_disasm bpf_dbg bpf_asm bpftool

bpf_jit_disasm : CFLAGS += -DPACKAGE='bpf_jit_disasm'
bpf_jit_disasm : LDLIBS = -lopcodes -lbfd -ldl
bpf_jit_disasm : bpf_jit_disasm.o

bpf_dbg : LDLIBS = -lreadline
bpf_dbg : bpf_dbg.o

bpf_asm : LDLIBS =
bpf_asm : bpf_asm.o bpf_exp.yacc.o bpf_exp.lex.o
bpf_exp.lex.o : bpf_exp.yacc.c

clean: bpftool_clean
	rm -rf *.o bpf_jit_disasm bpf_dbg bpf_asm bpf_exp.yacc.* bpf_exp.lex.*

install: bpftool_install
	install bpf_jit_disasm $(prefix)/bin/bpf_jit_disasm
	install bpf_dbg $(prefix)/bin/bpf_dbg
	install bpf_asm $(prefix)/bin/bpf_asm

bpftool:
	$(MAKE) -C bpftool

bpftool_install:
	$(MAKE) -C bpftool install

bpftool_clean:
	$(MAKE) -C bpftool clean

.PHONY: bpftool FORCE
