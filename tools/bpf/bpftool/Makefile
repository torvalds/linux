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
  BOOTSTRAP_OUTPUT = $(OUTPUT)/bootstrap/
else
  LIBBPF_OUTPUT =
  LIBBPF_PATH = $(BPF_DIR)
  BOOTSTRAP_OUTPUT = $(CURDIR)/bootstrap/
endif

LIBBPF = $(LIBBPF_PATH)libbpf.a
LIBBPF_BOOTSTRAP_OUTPUT = $(BOOTSTRAP_OUTPUT)libbpf/
LIBBPF_BOOTSTRAP = $(LIBBPF_BOOTSTRAP_OUTPUT)libbpf.a

ifeq ($(BPFTOOL_VERSION),)
BPFTOOL_VERSION := $(shell make -rR --no-print-directory -sC ../../.. kernelversion)
endif

$(LIBBPF_OUTPUT) $(BOOTSTRAP_OUTPUT) $(LIBBPF_BOOTSTRAP_OUTPUT):
	$(QUIET_MKDIR)mkdir -p $@

$(LIBBPF): FORCE | $(LIBBPF_OUTPUT)
	$(Q)$(MAKE) -C $(BPF_DIR) OUTPUT=$(LIBBPF_OUTPUT) $(LIBBPF_OUTPUT)libbpf.a

$(LIBBPF_BOOTSTRAP): FORCE | $(LIBBPF_BOOTSTRAP_OUTPUT)
	$(Q)$(MAKE) -C $(BPF_DIR) OUTPUT=$(LIBBPF_BOOTSTRAP_OUTPUT) \
		ARCH= CC=$(HOSTCC) LD=$(HOSTLD) $@

$(LIBBPF)-clean: FORCE | $(LIBBPF_OUTPUT)
	$(call QUIET_CLEAN, libbpf)
	$(Q)$(MAKE) -C $(BPF_DIR) OUTPUT=$(LIBBPF_OUTPUT) clean >/dev/null

$(LIBBPF_BOOTSTRAP)-clean: FORCE | $(LIBBPF_BOOTSTRAP_OUTPUT)
	$(call QUIET_CLEAN, libbpf-bootstrap)
	$(Q)$(MAKE) -C $(BPF_DIR) OUTPUT=$(LIBBPF_BOOTSTRAP_OUTPUT) clean >/dev/null

prefix ?= /usr/local
bash_compdir ?= /usr/share/bash-completion/completions

CFLAGS += -O2
CFLAGS += -W -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
CFLAGS += $(filter-out -Wswitch-enum -Wnested-externs,$(EXTRA_WARNINGS))
CFLAGS += -DPACKAGE='"bpftool"' -D__EXPORTED_HEADERS__ \
	-I$(if $(OUTPUT),$(OUTPUT),.) \
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

FEATURE_USER = .bpftool
FEATURE_TESTS = libbfd disassembler-four-args reallocarray zlib libcap \
	clang-bpf-co-re
FEATURE_DISPLAY = libbfd disassembler-four-args zlib libcap \
	clang-bpf-co-re

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
LIBS_BOOTSTRAP = $(LIBBPF_BOOTSTRAP) -lelf -lz
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

BPFTOOL_BOOTSTRAP := $(BOOTSTRAP_OUTPUT)bpftool

BOOTSTRAP_OBJS = $(addprefix $(BOOTSTRAP_OUTPUT),main.o common.o json_writer.o gen.o btf.o)
OBJS = $(patsubst %.c,$(OUTPUT)%.o,$(SRCS)) $(OUTPUT)disasm.o

VMLINUX_BTF_PATHS ?= $(if $(O),$(O)/vmlinux)				\
		     $(if $(KBUILD_OUTPUT),$(KBUILD_OUTPUT)/vmlinux)	\
		     ../../../vmlinux					\
		     /sys/kernel/btf/vmlinux				\
		     /boot/vmlinux-$(shell uname -r)
VMLINUX_BTF ?= $(abspath $(firstword $(wildcard $(VMLINUX_BTF_PATHS))))

bootstrap: $(BPFTOOL_BOOTSTRAP)

ifneq ($(VMLINUX_BTF)$(VMLINUX_H),)
ifeq ($(feature-clang-bpf-co-re),1)

BUILD_BPF_SKELS := 1

$(OUTPUT)vmlinux.h: $(VMLINUX_BTF) $(BPFTOOL_BOOTSTRAP)
ifeq ($(VMLINUX_H),)
	$(QUIET_GEN)$(BPFTOOL_BOOTSTRAP) btf dump file $< format c > $@
else
	$(Q)cp "$(VMLINUX_H)" $@
endif

$(OUTPUT)%.bpf.o: skeleton/%.bpf.c $(OUTPUT)vmlinux.h $(LIBBPF)
	$(QUIET_CLANG)$(CLANG) \
		-I$(if $(OUTPUT),$(OUTPUT),.) \
		-I$(srctree)/tools/include/uapi/ \
		-I$(LIBBPF_PATH) \
		-I$(srctree)/tools/lib \
		-g -O2 -Wall -target bpf -c $< -o $@ && $(LLVM_STRIP) -g $@

$(OUTPUT)%.skel.h: $(OUTPUT)%.bpf.o $(BPFTOOL_BOOTSTRAP)
	$(QUIET_GEN)$(BPFTOOL_BOOTSTRAP) gen skeleton $< > $@

$(OUTPUT)prog.o: $(OUTPUT)profiler.skel.h

$(OUTPUT)pids.o: $(OUTPUT)pid_iter.skel.h

endif
endif

CFLAGS += $(if $(BUILD_BPF_SKELS),,-DBPFTOOL_WITHOUT_SKELETONS)

$(OUTPUT)disasm.o: $(srctree)/kernel/bpf/disasm.c
	$(QUIET_CC)$(CC) $(CFLAGS) -c -MMD -o $@ $<

$(OUTPUT)feature.o: | zdep

$(BPFTOOL_BOOTSTRAP): $(BOOTSTRAP_OBJS) $(LIBBPF_BOOTSTRAP)
	$(QUIET_LINK)$(HOSTCC) $(CFLAGS) $(LDFLAGS) -o $@ $(BOOTSTRAP_OBJS) \
		$(LIBS_BOOTSTRAP)

$(OUTPUT)bpftool: $(OBJS) $(LIBBPF)
	$(QUIET_LINK)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(BOOTSTRAP_OUTPUT)%.o: %.c | $(BOOTSTRAP_OUTPUT)
	$(QUIET_CC)$(HOSTCC) $(CFLAGS) -c -MMD -o $@ $<

$(OUTPUT)%.o: %.c
	$(QUIET_CC)$(CC) $(CFLAGS) -c -MMD -o $@ $<

feature-detect-clean:
	$(call QUIET_CLEAN, feature-detect)
	$(Q)$(MAKE) -C $(srctree)/tools/build/feature/ clean >/dev/null

clean: $(LIBBPF)-clean $(LIBBPF_BOOTSTRAP)-clean feature-detect-clean
	$(call QUIET_CLEAN, bpftool)
	$(Q)$(RM) -- $(OUTPUT)bpftool $(OUTPUT)*.o $(OUTPUT)*.d
	$(Q)$(RM) -- $(OUTPUT)*.skel.h $(OUTPUT)vmlinux.h
	$(Q)$(RM) -r -- $(LIBBPF_OUTPUT) $(BOOTSTRAP_OUTPUT)
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

.SECONDARY:
.PHONY: all FORCE clean install uninstall zdep
.PHONY: doc doc-clean doc-install doc-uninstall
.DEFAULT_GOAL := all
