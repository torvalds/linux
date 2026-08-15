# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(srctree),)
  srctree	:= $(patsubst %/,%,$(dir $(CURDIR)))
  srctree	:= $(patsubst %/,%,$(dir $(srctree)))
  srctree	:= $(patsubst %/,%,$(dir $(srctree)))
endif

include $(srctree)/tools/scripts/Makefile.include

# O is an alias for OUTPUT
OUTPUT          := $(O)

ifeq ($(OUTPUT),)
  OUTPUT        := $(CURDIR)
else
  # subdir is used by the ../Makefile in $(call descend,)
  ifneq ($(subdir),)
    OUTPUT        := $(OUTPUT)/$(subdir)
  endif
endif

ifneq ($(patsubst %/,,$(lastword $(OUTPUT))),)
  OUTPUT        := $(OUTPUT)/
endif

RTLA		:= $(OUTPUT)rtla
RTLA_IN		:= $(RTLA)-in.o

LIBSUBCMD_DIR = $(srctree)/tools/lib/subcmd/
ifneq ($(OUTPUT),)
  LIBSUBCMD_OUTPUT = $(abspath $(OUTPUT))/libsubcmd
else
  LIBSUBCMD_OUTPUT = $(CURDIR)/libsubcmd
endif
LIBSUBCMD = $(LIBSUBCMD_OUTPUT)/libsubcmd.a
LIBSUBCMD_INCLUDES = $(LIBSUBCMD_OUTPUT)/include
LIBSUBCMD_MAKEFLAGS = O=$(LIBSUBCMD_OUTPUT) DESTDIR=$(LIBSUBCMD_OUTPUT) prefix= subdir=

TOOLS_INCLUDES = -I$(srctree)/tools/include

ifneq ($(OUTPUT),)
  LIB_OUTPUT = $(abspath $(OUTPUT))/lib
else
  LIB_OUTPUT = $(CURDIR)/lib
endif

LIB_STRING = $(LIB_OUTPUT)/string.o
LIB_STRING_SRC = $(srctree)/tools/lib/string.c

LIB_STR_ERROR_R = $(LIB_OUTPUT)/str_error_r.o
LIB_STR_ERROR_R_SRC = $(srctree)/tools/lib/str_error_r.c

VERSION		:= $(shell sh -c "make -sC ../../.. kernelversion | grep -v make")
DOCSRC		:= ../../../Documentation/tools/rtla/

FEATURE_TESTS	:= libtraceevent
FEATURE_TESTS	+= libtracefs
FEATURE_TESTS	+= libcpupower
FEATURE_TESTS	+= libcheck
FEATURE_TESTS	+= libbpf
FEATURE_TESTS	+= clang-bpf-co-re
FEATURE_TESTS	+= bpftool-skeletons
FEATURE_DISPLAY	:= libtraceevent
FEATURE_DISPLAY	+= libtracefs
FEATURE_DISPLAY	+= libcpupower
FEATURE_DISPLAY	+= libcheck
FEATURE_DISPLAY	+= libbpf
FEATURE_DISPLAY	+= clang-bpf-co-re
FEATURE_DISPLAY	+= bpftool-skeletons

all: $(RTLA)

include $(srctree)/tools/build/Makefile.include
include Makefile.rtla
include tests/unit/Makefile.unit

# check for dependencies only on required targets
NON_CONFIG_TARGETS := clean install tarball doc doc_clean doc_install

config		:= 1
ifdef MAKECMDGOALS
ifeq ($(filter-out $(NON_CONFIG_TARGETS),$(MAKECMDGOALS)),)
 config		:= 0
endif
endif

ifeq ($(config),1)
  include $(srctree)/tools/build/Makefile.feature
  include Makefile.config
endif

CFLAGS		+= $(INCLUDES) $(LIB_INCLUDES) $(TOOLS_INCLUDES) -I$(LIBSUBCMD_INCLUDES)

export CFLAGS OUTPUT srctree

ifeq ($(BUILD_BPF_SKEL),1)
src/timerlat.bpf.o: src/timerlat.bpf.c
	$(QUIET_CLANG)$(CLANG) -g -O2 -target bpf -c $(filter %.c,$^) -o $@

src/timerlat.skel.h: src/timerlat.bpf.o
	$(QUIET_GENSKEL)$(SYSTEM_BPFTOOL) gen skeleton $< > $@

example/timerlat_bpf_action.o: example/timerlat_bpf_action.c
	$(QUIET_CLANG)$(CLANG) -g -O2 -target bpf -c $(filter %.c,$^) -o $@

tests/bpf/bpf_action_map.o: tests/bpf/bpf_action_map.c
	$(QUIET_CLANG)$(CLANG) -g -O2 -target bpf -c $(filter %.c,$^) -o $@
else
src/timerlat.skel.h:
	$(Q)echo '/* BPF skeleton is disabled */' > src/timerlat.skel.h

example/timerlat_bpf_action.o: example/timerlat_bpf_action.c
	$(Q)echo "BPF skeleton support is disabled, skipping example/timerlat_bpf_action.o"

tests/bpf/bpf_action_map.o: tests/bpf/bpf_action_map.c
	$(Q)echo "BPF skeleton support is disabled, skipping tests/bpf/bpf_action_map.o"
endif

$(RTLA): $(RTLA_IN) $(LIBSUBCMD) $(LIB_STRING) $(LIB_STR_ERROR_R)
	$(QUIET_LINK)$(CC) $(LDFLAGS) -o $(RTLA) $(RTLA_IN) $(LIBSUBCMD) $(LIB_STRING) $(LIB_STR_ERROR_R) $(EXTLIBS)

static: $(RTLA_IN) $(LIBSUBCMD) $(LIB_STRING) $(LIB_STR_ERROR_R)
	$(eval LDFLAGS += -static)
	$(QUIET_LINK)$(CC) -static $(LDFLAGS) -o $(RTLA)-static $(RTLA_IN) $(LIBSUBCMD) $(LIB_STRING) $(LIB_STR_ERROR_R) $(EXTLIBS)

rtla.%: fixdep FORCE
	make -f $(srctree)/tools/build/Makefile.build dir=. $@

$(RTLA_IN): fixdep FORCE src/timerlat.skel.h $(LIBSUBCMD_INCLUDES)
	make $(build)=rtla

$(LIBSUBCMD_OUTPUT):
	$(Q)$(MKDIR) -p $@

$(LIBSUBCMD_INCLUDES): FORCE | $(LIBSUBCMD_OUTPUT)
	$(Q)$(MAKE) -C $(LIBSUBCMD_DIR) $(LIBSUBCMD_MAKEFLAGS) \
		install_headers

$(LIBSUBCMD): FORCE | $(LIBSUBCMD_OUTPUT)
	$(Q)$(MAKE) -C $(LIBSUBCMD_DIR) $(LIBSUBCMD_MAKEFLAGS) \
		$@

$(LIB_OUTPUT):
	$(Q)$(MKDIR) -p $@

$(LIB_STR_ERROR_R): $(LIB_STR_ERROR_R_SRC) | $(LIB_OUTPUT)
	$(QUIET_CC)$(CC) $(CFLAGS) -c -o $@ $<

$(LIB_STRING): $(LIB_STRING_SRC) | $(LIB_OUTPUT)
	$(QUIET_CC)$(CC) $(CFLAGS) -c -o $@ $<

libsubcmd-clean:
	$(call QUIET_CLEAN, libsubcmd)
	$(Q)$(RM) -r -- $(LIBSUBCMD_OUTPUT)

lib-clean:
	$(call QUIET_CLEAN, lib)
	$(Q)$(RM) -r -- $(LIB_OUTPUT)

clean: doc_clean fixdep-clean libsubcmd-clean lib-clean
	$(call QUIET_CLEAN, rtla)
	$(Q)find . -name '*.o' -delete -o -name '\.*.cmd' -delete -o -name '\.*.d' -delete
	$(Q)rm -f rtla rtla-static fixdep FEATURE-DUMP rtla-*
	$(Q)rm -rf feature
	$(Q)rm -f src/timerlat.bpf.o src/timerlat.skel.h example/timerlat_bpf_action.o
	$(Q)rm -f $(UNIT_TESTS)

check: $(RTLA) tests/bpf/bpf_action_map.o
	RTLA=$(RTLA) BPFTOOL=$(SYSTEM_BPFTOOL) prove -o -f -v tests/
examples: example/timerlat_bpf_action.o
.PHONY: FORCE clean check
