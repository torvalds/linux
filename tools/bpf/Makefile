# SPDX-License-Identifier: GPL-2.0
include ../scripts/Makefile.include

prefix ?= /usr/local

LEX = flex
YACC = bison
MAKE = make
INSTALL ?= install

CFLAGS += -Wall -O2
CFLAGS += -D__EXPORTED_HEADERS__ -I$(srctree)/tools/include/uapi \
	  -I$(srctree)/tools/include

# This will work when bpf is built in tools env. where srctree
# isn't set and when invoked from selftests build, where srctree
# is set to ".". building_out_of_srctree is undefined for in srctree
# builds
ifeq ($(srctree),)
update_srctree := 1
endif
ifndef building_out_of_srctree
update_srctree := 1
endif
ifeq ($(update_srctree),1)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

ifeq ($(V),1)
  Q =
else
  Q = @
endif

FEATURE_USER = .bpf
FEATURE_TESTS = libbfd disassembler-four-args disassembler-init-styled
FEATURE_DISPLAY = libbfd

check_feat := 1
NON_CHECK_FEAT_TARGETS := clean bpftool_clean runqslower_clean resolve_btfids_clean
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
ifeq ($(feature-disassembler-init-styled), 1)
CFLAGS += -DDISASM_INIT_STYLED
endif

$(OUTPUT)%.yacc.c: $(srctree)/tools/bpf/%.y
	$(QUIET_BISON)$(YACC) -o $@ -d $<

$(OUTPUT)%.lex.c: $(srctree)/tools/bpf/%.l
	$(QUIET_FLEX)$(LEX) -o $@ $<

$(OUTPUT)%.o: $(srctree)/tools/bpf/%.c
	$(QUIET_CC)$(CC) $(CFLAGS) -c -o $@ $<

$(OUTPUT)%.yacc.o: $(OUTPUT)%.yacc.c
	$(QUIET_CC)$(CC) $(CFLAGS) -c -o $@ $<
$(OUTPUT)%.lex.o: $(OUTPUT)%.lex.c
	$(QUIET_CC)$(CC) $(CFLAGS) -c -o $@ $<

PROGS = $(OUTPUT)bpf_jit_disasm $(OUTPUT)bpf_dbg $(OUTPUT)bpf_asm

all: $(PROGS) bpftool runqslower

$(OUTPUT)bpf_jit_disasm: CFLAGS += -DPACKAGE='bpf_jit_disasm'
$(OUTPUT)bpf_jit_disasm: $(OUTPUT)bpf_jit_disasm.o
	$(QUIET_LINK)$(CC) $(CFLAGS) -o $@ $^ -lopcodes -lbfd -ldl

$(OUTPUT)bpf_dbg: $(OUTPUT)bpf_dbg.o
	$(QUIET_LINK)$(CC) $(CFLAGS) -o $@ $^ -lreadline

$(OUTPUT)bpf_asm: $(OUTPUT)bpf_asm.o $(OUTPUT)bpf_exp.yacc.o $(OUTPUT)bpf_exp.lex.o
	$(QUIET_LINK)$(CC) $(CFLAGS) -o $@ $^

$(OUTPUT)bpf_exp.lex.c: $(OUTPUT)bpf_exp.yacc.c
$(OUTPUT)bpf_exp.yacc.o: $(OUTPUT)bpf_exp.yacc.c
$(OUTPUT)bpf_exp.lex.o: $(OUTPUT)bpf_exp.lex.c

clean: bpftool_clean runqslower_clean resolve_btfids_clean
	$(call QUIET_CLEAN, bpf-progs)
	$(Q)$(RM) -r -- $(OUTPUT)*.o $(OUTPUT)bpf_jit_disasm $(OUTPUT)bpf_dbg \
	       $(OUTPUT)bpf_asm $(OUTPUT)bpf_exp.yacc.* $(OUTPUT)bpf_exp.lex.*
	$(call QUIET_CLEAN, core-gen)
	$(Q)$(RM) -- $(OUTPUT)FEATURE-DUMP.bpf
	$(Q)$(RM) -r -- $(OUTPUT)feature

install: $(PROGS) bpftool_install
	$(call QUIET_INSTALL, bpf_jit_disasm)
	$(Q)$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/bin
	$(Q)$(INSTALL) $(OUTPUT)bpf_jit_disasm $(DESTDIR)$(prefix)/bin/bpf_jit_disasm
	$(call QUIET_INSTALL, bpf_dbg)
	$(Q)$(INSTALL) $(OUTPUT)bpf_dbg $(DESTDIR)$(prefix)/bin/bpf_dbg
	$(call QUIET_INSTALL, bpf_asm)
	$(Q)$(INSTALL) $(OUTPUT)bpf_asm $(DESTDIR)$(prefix)/bin/bpf_asm

bpftool:
	$(call descend,bpftool)

bpftool_install:
	$(call descend,bpftool,install)

bpftool_clean:
	$(call descend,bpftool,clean)

runqslower:
	$(call descend,runqslower)

runqslower_clean:
	$(call descend,runqslower,clean)

resolve_btfids:
	$(call descend,resolve_btfids)

resolve_btfids_clean:
	$(call descend,resolve_btfids,clean)

.PHONY: all install clean bpftool bpftool_install bpftool_clean \
	runqslower runqslower_clean \
	resolve_btfids resolve_btfids_clean
