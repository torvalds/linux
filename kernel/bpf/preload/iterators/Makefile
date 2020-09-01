# SPDX-License-Identifier: GPL-2.0
OUTPUT := .output
CLANG ?= clang
LLC ?= llc
LLVM_STRIP ?= llvm-strip
DEFAULT_BPFTOOL := $(OUTPUT)/sbin/bpftool
BPFTOOL ?= $(DEFAULT_BPFTOOL)
LIBBPF_SRC := $(abspath ../../../../tools/lib/bpf)
BPFOBJ := $(OUTPUT)/libbpf.a
BPF_INCLUDE := $(OUTPUT)
INCLUDES := -I$(OUTPUT) -I$(BPF_INCLUDE) -I$(abspath ../../../../tools/lib)        \
       -I$(abspath ../../../../tools/include/uapi)
CFLAGS := -g -Wall

abs_out := $(abspath $(OUTPUT))
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

all: iterators.skel.h

clean:
	$(call msg,CLEAN)
	$(Q)rm -rf $(OUTPUT) iterators

iterators.skel.h: $(OUTPUT)/iterators.bpf.o | $(BPFTOOL)
	$(call msg,GEN-SKEL,$@)
	$(Q)$(BPFTOOL) gen skeleton $< > $@


$(OUTPUT)/iterators.bpf.o: iterators.bpf.c $(BPFOBJ) | $(OUTPUT)
	$(call msg,BPF,$@)
	$(Q)$(CLANG) -g -O2 -target bpf $(INCLUDES)			      \
		 -c $(filter %.c,$^) -o $@ &&				      \
	$(LLVM_STRIP) -g $@

$(OUTPUT):
	$(call msg,MKDIR,$@)
	$(Q)mkdir -p $(OUTPUT)

$(BPFOBJ): $(wildcard $(LIBBPF_SRC)/*.[ch] $(LIBBPF_SRC)/Makefile) | $(OUTPUT)
	$(Q)$(MAKE) $(submake_extras) -C $(LIBBPF_SRC)			       \
		    OUTPUT=$(abspath $(dir $@))/ $(abspath $@)

$(DEFAULT_BPFTOOL):
	$(Q)$(MAKE) $(submake_extras) -C ../../../../tools/bpf/bpftool			      \
		    prefix= OUTPUT=$(abs_out)/ DESTDIR=$(abs_out) install
