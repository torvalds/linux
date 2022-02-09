# SPDX-License-Identifier: GPL-2.0-only
include ../../scripts/Makefile.include
include ../../scripts/Makefile.arch

srctree := $(abspath $(CURDIR)/../../../)

ifeq ($(V),1)
  Q =
  msg =
else
  Q = @
  ifeq ($(silent),1)
    msg =
  else
    msg = @printf '  %-8s %s%s\n' "$(1)" "$(notdir $(2))" "$(if $(3), $(3))";
  endif
  MAKEFLAGS=--no-print-directory
endif

# always use the host compiler
AR       = $(HOSTAR)
CC       = $(HOSTCC)
LD       = $(HOSTLD)
ARCH     = $(HOSTARCH)
RM      ?= rm

OUTPUT ?= $(srctree)/tools/bpf/resolve_btfids/

LIBBPF_SRC := $(srctree)/tools/lib/bpf/
SUBCMD_SRC := $(srctree)/tools/lib/subcmd/

BPFOBJ     := $(OUTPUT)/libbpf/libbpf.a
SUBCMDOBJ  := $(OUTPUT)/libsubcmd/libsubcmd.a

BINARY     := $(OUTPUT)/resolve_btfids
BINARY_IN  := $(BINARY)-in.o

all: $(BINARY)

$(OUTPUT) $(OUTPUT)/libbpf $(OUTPUT)/libsubcmd:
	$(call msg,MKDIR,,$@)
	$(Q)mkdir -p $(@)

$(SUBCMDOBJ): fixdep FORCE | $(OUTPUT)/libsubcmd
	$(Q)$(MAKE) -C $(SUBCMD_SRC) OUTPUT=$(abspath $(dir $@))/ $(abspath $@)

$(BPFOBJ): $(wildcard $(LIBBPF_SRC)/*.[ch] $(LIBBPF_SRC)/Makefile) | $(OUTPUT)/libbpf
	$(Q)$(MAKE) $(submake_extras) -C $(LIBBPF_SRC)  OUTPUT=$(abspath $(dir $@))/ $(abspath $@)

CFLAGS := -g \
          -I$(srctree)/tools/include \
          -I$(srctree)/tools/include/uapi \
          -I$(LIBBPF_SRC) \
          -I$(SUBCMD_SRC)

LIBS = -lelf -lz

export srctree OUTPUT CFLAGS Q
include $(srctree)/tools/build/Makefile.include

$(BINARY_IN): fixdep FORCE | $(OUTPUT)
	$(Q)$(MAKE) $(build)=resolve_btfids

$(BINARY): $(BPFOBJ) $(SUBCMDOBJ) $(BINARY_IN)
	$(call msg,LINK,$@)
	$(Q)$(CC) $(BINARY_IN) $(LDFLAGS) -o $@ $(BPFOBJ) $(SUBCMDOBJ) $(LIBS)

clean_objects := $(wildcard $(OUTPUT)/*.o                \
                            $(OUTPUT)/.*.o.cmd           \
                            $(OUTPUT)/.*.o.d             \
                            $(OUTPUT)/libbpf             \
                            $(OUTPUT)/libsubcmd          \
                            $(OUTPUT)/resolve_btfids)

ifneq ($(clean_objects),)
clean: fixdep-clean
	$(call msg,CLEAN,$(BINARY))
	$(Q)$(RM) -rf $(clean_objects)
else
clean:
endif

tags:
	$(call msg,GEN,,tags)
	$(Q)ctags -R . $(LIBBPF_SRC) $(SUBCMD_SRC)

FORCE:

.PHONY: all FORCE clean tags
