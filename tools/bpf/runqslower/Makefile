# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
include ../../scripts/Makefile.include

OUTPUT ?= $(abspath .output)/

BPFTOOL_OUTPUT := $(OUTPUT)bpftool/
DEFAULT_BPFTOOL := $(BPFTOOL_OUTPUT)bpftool
BPFTOOL ?= $(DEFAULT_BPFTOOL)
LIBBPF_SRC := $(abspath ../../lib/bpf)
BPFOBJ_OUTPUT := $(OUTPUT)libbpf/
BPFOBJ := $(BPFOBJ_OUTPUT)libbpf.a
BPF_INCLUDE := $(BPFOBJ_OUTPUT)
INCLUDES := -I$(OUTPUT) -I$(BPF_INCLUDE) -I$(abspath ../../lib)        \
       -I$(abspath ../../include/uapi)
CFLAGS := -g -Wall

# Try to detect best kernel BTF source
KERNEL_REL := $(shell uname -r)
VMLINUX_BTF_PATHS := /sys/kernel/btf/vmlinux /boot/vmlinux-$(KERNEL_REL)
VMLINUX_BTF_PATH := $(or $(VMLINUX_BTF),$(firstword			       \
					  $(wildcard $(VMLINUX_BTF_PATHS))))

ifeq ($(V),1)
Q =
else
Q = @
MAKEFLAGS += --no-print-directory
submake_extras := feature_display=0
endif

.DELETE_ON_ERROR:

.PHONY: all clean runqslower
all: runqslower

runqslower: $(OUTPUT)/runqslower

clean:
	$(call QUIET_CLEAN, runqslower)
	$(Q)$(RM) -r $(BPFOBJ_OUTPUT) $(BPFTOOL_OUTPUT)
	$(Q)$(RM) $(OUTPUT)*.o $(OUTPUT)*.d
	$(Q)$(RM) $(OUTPUT)*.skel.h $(OUTPUT)vmlinux.h
	$(Q)$(RM) $(OUTPUT)runqslower
	$(Q)$(RM) -r .output

$(OUTPUT)/runqslower: $(OUTPUT)/runqslower.o $(BPFOBJ)
	$(QUIET_LINK)$(CC) $(CFLAGS) $^ -lelf -lz -o $@

$(OUTPUT)/runqslower.o: runqslower.h $(OUTPUT)/runqslower.skel.h	      \
			$(OUTPUT)/runqslower.bpf.o

$(OUTPUT)/runqslower.bpf.o: $(OUTPUT)/vmlinux.h runqslower.h

$(OUTPUT)/%.skel.h: $(OUTPUT)/%.bpf.o | $(BPFTOOL)
	$(QUIET_GEN)$(BPFTOOL) gen skeleton $< > $@

$(OUTPUT)/%.bpf.o: %.bpf.c $(BPFOBJ) | $(OUTPUT)
	$(QUIET_GEN)$(CLANG) -g -O2 -target bpf $(INCLUDES)		      \
		 -c $(filter %.c,$^) -o $@ &&				      \
	$(LLVM_STRIP) -g $@

$(OUTPUT)/%.o: %.c | $(OUTPUT)
	$(QUIET_CC)$(CC) $(CFLAGS) $(INCLUDES) -c $(filter %.c,$^) -o $@

$(OUTPUT) $(BPFOBJ_OUTPUT) $(BPFTOOL_OUTPUT):
	$(QUIET_MKDIR)mkdir -p $@

$(OUTPUT)/vmlinux.h: $(VMLINUX_BTF_PATH) | $(OUTPUT) $(BPFTOOL)
	$(Q)if [ ! -e "$(VMLINUX_BTF_PATH)" ] ; then \
		echo "Couldn't find kernel BTF; set VMLINUX_BTF to"	       \
			"specify its location." >&2;			       \
		exit 1;\
	fi
	$(QUIET_GEN)$(BPFTOOL) btf dump file $(VMLINUX_BTF_PATH) format c > $@

$(BPFOBJ): $(wildcard $(LIBBPF_SRC)/*.[ch] $(LIBBPF_SRC)/Makefile) | $(BPFOBJ_OUTPUT)
	$(Q)$(MAKE) $(submake_extras) -C $(LIBBPF_SRC) OUTPUT=$(BPFOBJ_OUTPUT) $@

$(DEFAULT_BPFTOOL): | $(BPFTOOL_OUTPUT)
	$(Q)$(MAKE) $(submake_extras) -C ../bpftool OUTPUT=$(BPFTOOL_OUTPUT)   \
		    CC=$(HOSTCC) LD=$(HOSTLD)
