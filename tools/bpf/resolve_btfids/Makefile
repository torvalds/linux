# SPDX-License-Identifier: GPL-2.0-only
include ../../scripts/Makefile.include
include ../../scripts/Makefile.arch

ifeq ($(srctree),)
srctree := $(patsubst %/,%,$(dir $(CURDIR)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
srctree := $(patsubst %/,%,$(dir $(srctree)))
endif

ifeq ($(V),1)
  Q =
  msg =
else
  Q = @
  msg = @printf '  %-8s %s%s\n' "$(1)" "$(notdir $(2))" "$(if $(3), $(3))";
  MAKEFLAGS=--no-print-directory
endif

# always use the host compiler
ifneq ($(LLVM),)
HOSTAR  ?= llvm-ar
HOSTCC  ?= clang
HOSTLD  ?= ld.lld
else
HOSTAR  ?= ar
HOSTCC  ?= gcc
HOSTLD  ?= ld
endif
AR       = $(HOSTAR)
CC       = $(HOSTCC)
LD       = $(HOSTLD)
ARCH     = $(HOSTARCH)

OUTPUT ?= $(srctree)/tools/bpf/resolve_btfids/

LIBBPF_SRC := $(srctree)/tools/lib/bpf/
SUBCMD_SRC := $(srctree)/tools/lib/subcmd/

BPFOBJ     := $(OUTPUT)/libbpf.a
SUBCMDOBJ  := $(OUTPUT)/libsubcmd.a

BINARY     := $(OUTPUT)/resolve_btfids
BINARY_IN  := $(BINARY)-in.o

all: $(BINARY)

$(OUTPUT):
	$(call msg,MKDIR,,$@)
	$(Q)mkdir -p $(OUTPUT)

$(SUBCMDOBJ): fixdep FORCE
	$(Q)$(MAKE) -C $(SUBCMD_SRC) OUTPUT=$(OUTPUT)

$(BPFOBJ): $(wildcard $(LIBBPF_SRC)/*.[ch] $(LIBBPF_SRC)/Makefile) | $(OUTPUT)
	$(Q)$(MAKE) $(submake_extras) -C $(LIBBPF_SRC)  OUTPUT=$(abspath $(dir $@))/ $(abspath $@)

CFLAGS := -g \
          -I$(srctree)/tools/include \
          -I$(srctree)/tools/include/uapi \
          -I$(LIBBPF_SRC) \
          -I$(SUBCMD_SRC)

LIBS = -lelf -lz

export srctree OUTPUT CFLAGS Q
include $(srctree)/tools/build/Makefile.include

$(BINARY_IN): fixdep FORCE
	$(Q)$(MAKE) $(build)=resolve_btfids

$(BINARY): $(BPFOBJ) $(SUBCMDOBJ) $(BINARY_IN)
	$(call msg,LINK,$@)
	$(Q)$(CC) $(BINARY_IN) $(LDFLAGS) -o $@ $(BPFOBJ) $(SUBCMDOBJ) $(LIBS)

libsubcmd-clean:
	$(Q)$(MAKE) -C $(SUBCMD_SRC) OUTPUT=$(OUTPUT) clean

libbpf-clean:
	$(Q)$(MAKE) -C $(LIBBPF_SRC) OUTPUT=$(OUTPUT) clean

clean: libsubcmd-clean libbpf-clean fixdep-clean
	$(call msg,CLEAN,$(BINARY))
	$(Q)$(RM) -f $(BINARY); \
	$(RM) -rf $(if $(OUTPUT),$(OUTPUT),.)/feature; \
	find $(if $(OUTPUT),$(OUTPUT),.) -name \*.o -or -name \*.o.cmd -or -name \*.o.d | xargs $(RM)

tags:
	$(call msg,GEN,,tags)
	$(Q)ctags -R . $(LIBBPF_SRC) $(SUBCMD_SRC)

FORCE:

.PHONY: all FORCE clean tags
