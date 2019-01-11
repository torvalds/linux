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
  BPF_PATH = $(OUTPUT)
else
  BPF_PATH = $(BPF_DIR)
endif

LIBBPF = $(BPF_PATH)libbpf.a

BPFTOOL_VERSION := $(shell make --no-print-directory -sC ../../.. kernelversion)

$(LIBBPF): FORCE
	$(Q)$(MAKE) -C $(BPF_DIR) OUTPUT=$(OUTPUT) $(OUTPUT)libbpf.a

$(LIBBPF)-clean:
	$(call QUIET_CLEAN, libbpf)
	$(Q)$(MAKE) -C $(BPF_DIR) OUTPUT=$(OUTPUT) clean >/dev/null

prefix ?= /usr/local
bash_compdir ?= /usr/share/bash-completion/completions

CFLAGS += -O2
CFLAGS += -W -Wall -Wextra -Wno-unused-parameter -Wshadow -Wno-missing-field-initializers
CFLAGS += -DPACKAGE='"bpftool"' -D__EXPORTED_HEADERS__ \
	-I$(srctree)/kernel/bpf/ \
	-I$(srctree)/tools/include \
	-I$(srctree)/tools/include/uapi \
	-I$(srctree)/tools/lib/bpf \
	-I$(srctree)/tools/perf
CFLAGS += -DBPFTOOL_VERSION='"$(BPFTOOL_VERSION)"'
ifneq ($(EXTRA_CFLAGS),)
CFLAGS += $(EXTRA_CFLAGS)
endif
ifneq ($(EXTRA_LDFLAGS),)
LDFLAGS += $(EXTRA_LDFLAGS)
endif

LIBS = -lelf $(LIBBPF)

INSTALL ?= install
RM ?= rm -f

FEATURE_USER = .bpftool
FEATURE_TESTS = libbfd disassembler-four-args reallocarray
FEATURE_DISPLAY = libbfd disassembler-four-args

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

include $(wildcard $(OUTPUT)*.d)

all: $(OUTPUT)bpftool

BFD_SRCS = jit_disasm.c

SRCS = $(filter-out $(BFD_SRCS),$(wildcard *.c))

ifeq ($(feature-libbfd),1)
CFLAGS += -DHAVE_LIBBFD_SUPPORT
SRCS += $(BFD_SRCS)
LIBS += -lbfd -lopcodes
endif

OBJS = $(patsubst %.c,$(OUTPUT)%.o,$(SRCS)) $(OUTPUT)disasm.o

$(OUTPUT)disasm.o: $(srctree)/kernel/bpf/disasm.c
	$(QUIET_CC)$(COMPILE.c) -MMD -o $@ $<

$(OUTPUT)bpftool: $(OBJS) $(LIBBPF)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OUTPUT)%.o: %.c
	$(QUIET_CC)$(COMPILE.c) -MMD -o $@ $<

clean: $(LIBBPF)-clean
	$(call QUIET_CLEAN, bpftool)
	$(Q)$(RM) $(OUTPUT)bpftool $(OUTPUT)*.o $(OUTPUT)*.d
	$(call QUIET_CLEAN, core-gen)
	$(Q)$(RM) $(OUTPUT)FEATURE-DUMP.bpftool

install: $(OUTPUT)bpftool
	$(call QUIET_INSTALL, bpftool)
	$(Q)$(INSTALL) -m 0755 -d $(DESTDIR)$(prefix)/sbin
	$(Q)$(INSTALL) $(OUTPUT)bpftool $(DESTDIR)$(prefix)/sbin/bpftool
	$(Q)$(INSTALL) -m 0755 -d $(DESTDIR)$(bash_compdir)
	$(Q)$(INSTALL) -m 0644 bash-completion/bpftool $(DESTDIR)$(bash_compdir)

uninstall:
	$(call QUIET_UNINST, bpftool)
	$(Q)$(RM) $(DESTDIR)$(prefix)/sbin/bpftool
	$(Q)$(RM) $(DESTDIR)$(bash_compdir)/bpftool

doc:
	$(call descend,Documentation)

doc-clean:
	$(call descend,Documentation,clean)

doc-install:
	$(call descend,Documentation,install)

doc-uninstall:
	$(call descend,Documentation,uninstall)

FORCE:

.PHONY: all FORCE clean install uninstall
.PHONY: doc doc-clean doc-install doc-uninstall
.DEFAULT_GOAL := all
