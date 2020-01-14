# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
OUTPUT := .output
CLANG := clang
LLC := llc
LLVM_STRIP := llvm-strip
DEFAULT_BPFTOOL := $(OUTPUT)/sbin/bpftool
BPFTOOL ?= $(DEFAULT_BPFTOOL)
LIBBPF_SRC := $(abspath ../../lib/bpf)
CFLAGS := -g -Wall

# Try to detect best kernel BTF source
KERNEL_REL := $(shell uname -r)
ifneq ("$(wildcard /sys/kernel/btf/vmlinux)","")
VMLINUX_BTF := /sys/kernel/btf/vmlinux
else ifneq ("$(wildcard /boot/vmlinux-$(KERNEL_REL))","")
VMLINUX_BTF := /boot/vmlinux-$(KERNEL_REL)
else
$(error "Can't detect kernel BTF, use VMLINUX_BTF to specify it explicitly")
endif

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

.PHONY: all clean runqslower
all: runqslower

runqslower: $(OUTPUT)/runqslower

clean:
	$(call msg,CLEAN)
	$(Q)rm -rf $(OUTPUT) runqslower

$(OUTPUT)/runqslower: $(OUTPUT)/runqslower.o $(OUTPUT)/libbpf.a
	$(call msg,BINARY,$@)
	$(Q)$(CC) $(CFLAGS) -lelf -lz $^ -o $@

$(OUTPUT)/runqslower.o: runqslower.h $(OUTPUT)/runqslower.skel.h	      \
			$(OUTPUT)/runqslower.bpf.o

$(OUTPUT)/runqslower.bpf.o: $(OUTPUT)/vmlinux.h runqslower.h

$(OUTPUT)/%.skel.h: $(OUTPUT)/%.bpf.o | $(BPFTOOL)
	$(call msg,GEN-SKEL,$@)
	$(Q)$(BPFTOOL) gen skeleton $< > $@

$(OUTPUT)/%.bpf.o: %.bpf.c $(OUTPUT)/libbpf.a | $(OUTPUT)
	$(call msg,BPF,$@)
	$(Q)$(CLANG) -g -O2 -target bpf -I$(OUTPUT) -I$(LIBBPF_SRC)	      \
		 -c $(filter %.c,$^) -o $@ &&				      \
	$(LLVM_STRIP) -g $@

$(OUTPUT)/%.o: %.c | $(OUTPUT)
	$(call msg,CC,$@)
	$(Q)$(CC) $(CFLAGS) -I$(LIBBPF_SRC) -I$(OUTPUT) -c $(filter %.c,$^) -o $@

$(OUTPUT):
	$(call msg,MKDIR,$@)
	$(Q)mkdir -p $(OUTPUT)

$(OUTPUT)/vmlinux.h: $(VMLINUX_BTF) | $(OUTPUT) $(BPFTOOL)
	$(call msg,GEN,$@)
	$(Q)$(BPFTOOL) btf dump file $(VMLINUX_BTF) format c > $@

$(OUTPUT)/libbpf.a: | $(OUTPUT)
	$(Q)$(MAKE) $(submake_extras) -C $(LIBBPF_SRC)			       \
		    OUTPUT=$(abs_out)/ $(abs_out)/libbpf.a

$(DEFAULT_BPFTOOL):
	$(Q)$(MAKE) $(submake_extras) -C ../bpftool			      \
		    prefix= OUTPUT=$(abs_out)/ DESTDIR=$(abs_out) install
