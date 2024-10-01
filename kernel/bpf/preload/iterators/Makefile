# SPDX-License-Identifier: GPL-2.0
OUTPUT := .output
abs_out := $(abspath $(OUTPUT))

CLANG ?= clang
LLC ?= llc
LLVM_STRIP ?= llvm-strip

TOOLS_PATH := $(abspath ../../../../tools)
BPFTOOL_SRC := $(TOOLS_PATH)/bpf/bpftool
BPFTOOL_OUTPUT := $(abs_out)/bpftool
DEFAULT_BPFTOOL := $(BPFTOOL_OUTPUT)/bootstrap/bpftool
BPFTOOL ?= $(DEFAULT_BPFTOOL)

LIBBPF_SRC := $(TOOLS_PATH)/lib/bpf
LIBBPF_OUTPUT := $(abs_out)/libbpf
LIBBPF_DESTDIR := $(LIBBPF_OUTPUT)
LIBBPF_INCLUDE := $(LIBBPF_DESTDIR)/include
BPFOBJ := $(LIBBPF_OUTPUT)/libbpf.a

INCLUDES := -I$(OUTPUT) -I$(LIBBPF_INCLUDE) -I$(TOOLS_PATH)/include/uapi
CFLAGS := -g -Wall

ifeq ($(V),1)
Q =
msg =
else
Q = @
msg = @printf '  %-8s %s%s\n' "$(1)" "$(notdir $(2))" "$(if $(3), $(3))";
MAKEFLAGS += --no-print-directory
submake_extras := feature_display=0
endif

.DELETE_ON_ERROR:

.PHONY: all clean

all: iterators.lskel-little-endian.h

big: iterators.lskel-big-endian.h

clean:
	$(call msg,CLEAN)
	$(Q)rm -rf $(OUTPUT) iterators

iterators.lskel-%.h: $(OUTPUT)/%/iterators.bpf.o | $(BPFTOOL)
	$(call msg,GEN-SKEL,$@)
	$(Q)$(BPFTOOL) gen skeleton -L $< > $@

$(OUTPUT)/%/iterators.bpf.o: iterators.bpf.c $(BPFOBJ) | $(OUTPUT)
	$(call msg,BPF,$@)
	$(Q)mkdir -p $(@D)
	$(Q)$(CLANG) -g -O2 --target=bpf -m$* $(INCLUDES)		      \
		 -c $(filter %.c,$^) -o $@ &&				      \
	$(LLVM_STRIP) -g $@

$(OUTPUT) $(LIBBPF_OUTPUT) $(BPFTOOL_OUTPUT):
	$(call msg,MKDIR,$@)
	$(Q)mkdir -p $@

$(BPFOBJ): $(wildcard $(LIBBPF_SRC)/*.[ch] $(LIBBPF_SRC)/Makefile) | $(LIBBPF_OUTPUT)
	$(Q)$(MAKE) $(submake_extras) -C $(LIBBPF_SRC)			       \
		    OUTPUT=$(abspath $(dir $@))/ prefix=		       \
		    DESTDIR=$(LIBBPF_DESTDIR) $(abspath $@) install_headers

$(DEFAULT_BPFTOOL): | $(BPFTOOL_OUTPUT)
	$(Q)$(MAKE) $(submake_extras) -C $(BPFTOOL_SRC)	OUTPUT=$(BPFTOOL_OUTPUT)/ bootstrap
