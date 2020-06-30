# SPDX-License-Identifier: GPL-2.0-only
include ../../scripts/Makefile.include
include ../../scripts/utilities.mak

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

ifeq ($(V),1)
  Q =
else
  Q = @
endif

BPF_DIR = $(srctree)/tools/lib/bpf/

ifneq ($(OUTPUT),)
  LIBBPF_OUTPUT = $(OUTPUT)/libbpf/
  LIBBPF_PATH = $(LIBBPF_OUTPUT)
else
  LIBBPF_PATH = $(BPF_DIR)
endif

LIBBPF = $(LIBBPF_PATH)libbpf.a

BPFTOOL_VERSION := $(shell make -rR --no-print-directory -sC ../../.. kernelversion)

$(LIBBPF): FORCE
	$(if $(LIBBPF_OUTPUT),@mkdir -p $(LIBBPF_OUTPUT))
	$(Q)$(MAKE) -C $(BPF_DIR) OUTPUT=$(LIBBPF_OUTPUT) $(LIBBPF_OUTPUT)libbpf.a

$(LIBBPF)-clean:
	$(call QUIET_CLEAN, libbpf)
	$(Q)$(MAKE) -C $(BPF_DIR) OUTPUT=$(LIBBPF_OUTPUT) clean >/dev/null

prefix ?= /usr/local
bash_compdir ?= /usr/share/bash-completion/completions

CFLAGS += -O2
CFLAGS += -W -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
CFLAGS += $(filter-out -Wswitch-enum,$(EXTRA_WARNINGS))
CFLAGS += -DPACKAGE='"bpftool"' -D__EXPORTED_HEADERS__ \
	-I$(srctree)/kernel/bpf/ \
	-I$(srctree)/tools/include \
	-I$(srctree)/tools/include/uapi \
	-I$(srctree)/tools/lib \
	-I$(srctree)/tools/perf
CFLAGS += -DBPFTOOL_VERSION='"$(BPFTOOL_VERSION)"'
ifneq ($(EXTRA_CFLAGS),)
CFLAGS += $(EXTRA_CFLAGS)
endif
ifneq ($(EXTRA_LDFLAGS),)
LDFLAGS += $(EXTRA_LDFLAGS)
endif

INSTALL ?= install
RM ?= rm -f
CLANG ?= clang

FEATURE_USER = .bpftool
FEATURE_TESTS = libbfd disassembler-four-args reallocarray zlib libcap \
	clang-bpf-global-var
FEATURE_DISPLAY = libbfd disassembler-four-args zlib libcap \
	clang-bpf-global-var

check_feat := 1
NON_CHECK_FEAT_TARGETS := clean uninstall doc doc-clean doc-install doc-uninstall
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

ifeq ($(feature-reallocarray), 0)
CFLAGS += -DCOMPAT_NEED_REALLOCARRAY
endif

LIBS = $(LIBBPF) -lelf -lz
ifeq ($(feature-libcap), 1)
CFLAGS += -DUSE_LIBCAP
LIBS += -lcap
endif

include $(wildcard $(OUTPUT)*.d)

all: $(OUTPUT)bpftool

BFD_SRCS = jit_disasm.c

SRCS = $(filter-out $(BFD_SRCS),$(wildcard *.c))

ifeq ($(feature-libbfd),1)
  LIBS += -lbfd -ldl -lopcodes
else ifeq ($(feature-libbfd-liberty),1)
  LIBS += -lbfd -ldl -lopcodes -liberty
else ifeq ($(feature-libbfd-liberty-z),1)
  LIBS += -lbfd -ldl -lopcodes -liberty -lz
endif

ifneq ($(filter -lbfd,$(LIBS)),)
CFLAGS += -DHAVE_LIBBFD_SUPPORT
SRCS += $(BFD_SRCS)
endif

OBJS = $(patsubst %.c,$(OUTPUT)%.o,$(SRCS)) $(OUTPUT)disasm.o
_OBJS = $(filter-out $(OUTPUT)prog.o,$(OBJS)) $(OUTPUT)_prog.o

ifeq ($(feature-clang-bpf-global-var),1)
	__OBJS = $(OBJS)
else
	__OBJS = $(_OBJS)
endif

$(OUTPUT)_prog.o: prog.c
	$(QUIET_CC)$(CC) $(CFLAGS) -c -MMD -DBPFTOOL_WITHOUT_SKELETONS -o $@ $<

$(OUTPUT)_bpftool: $(_OBJS) $(LIBBPF)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(_OBJS) $(LIBS)

skeleton/profiler.bpf.o: skeleton/profiler.bpf.c $(LIBBPF)
	$(QUIET_CLANG)$(CLANG) \
		-I$(srctree)/tools/include/uapi/ \
		-I$(LIBBPF_PATH) -I$(srctree)/tools/lib \
		-g -O2 -target bpf -c $< -o $@

profiler.skel.h: $(OUTPUT)_bpftool skeleton/profiler.bpf.o
	$(QUIET_GEN)$(OUTPUT)./_bpftool gen skeleton skeleton/profiler.bpf.o > $@

$(OUTPUT)prog.o: prog.c profiler.skel.h
	$(QUIET_CC)$(CC) $(CFLAGS) -c -MMD -o $@ $<

$(OUTPUT)disasm.o: $(srctree)/kernel/bpf/disasm.c
	$(QUIET_CC)$(CC) $(CFLAGS) -c -MMD -o $@ $<

$(OUTPUT)feature.o: | zdep

$(OUTPUT)bpftool: $(__OBJS) $(LIBBPF)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(__OBJS) $(LIBS)

$(OUTPUT)%.o: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) -c -MMD -o $@ $<

clean: $(LIBBPF)-clean
	$(call QUIET_CLEAN, bpftool)
	$(Q)$(RM) -- $(OUTPUT)bpftool $(OUTPUT)*.o $(OUTPUT)*.d
	$(Q)$(RM) -- $(OUTPUT)_bpftool profiler.skel.h skeleton/profiler.bpf.o
	$(Q)$(RM) -r -- $(OUTPUT)libbpf/
	$(call QUIET_CLEAN, core-gen)
	$(Q)$(RM) -- $(OUTPUT)FEATURE-DUMP.bpftool
	$(Q)$(RM) -r -- $(OUTPUT)feature/

install: $(OUTPUT)bpftool
	$(call QUIET_INSTALL, bpftool)
	$(Q)$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/sbin
	$(Q)$(INSTALL) $(OUTPUT)bpftool $(DESTDIR)$(prefix)/sbin/bpftool
	$(Q)$(INSTALL) -m 0755 -d $(DESTDIR)$(bash_compdir)
	$(Q)$(INSTALL) -m 0644 bash-completion/bpftool $(DESTDIR)$(bash_compdir)

uninstall:
	$(call QUIET_UNINST, bpftool)
	$(Q)$(RM) -- $(DESTDIR)$(prefix)/sbin/bpftool
	$(Q)$(RM) -- $(DESTDIR)$(bash_compdir)/bpftool

doc:
	$(call descend,Documentation)

doc-clean:
	$(call descend,Documentation,clean)

doc-install:
	$(call descend,Documentation,install)

doc-uninstall:
	$(call descend,Documentation,uninstall)

FORCE:

zdep:
	@if [ "$(feature-zlib)" != "1" ]; then echo "No zlib found"; exit 1 ; fi

.PHONY: all FORCE clean install uninstall zdep
.PHONY: doc doc-clean doc-install doc-uninstall
.DEFAULT_GOAL := all
