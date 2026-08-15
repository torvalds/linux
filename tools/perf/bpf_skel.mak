# SPDX-License-Identifier: GPL-2.0
# Shared BPF Skeleton Generator Rules

include $(srctree)/tools/scripts/Makefile.include

# Shared foundational tooling always lives in util/bpf_skel
SKEL_TOOL_OUT := $(abspath $(OUTPUT)util/bpf_skel)
SKEL_TOOL_TMP_OUT := $(abspath $(SKEL_TOOL_OUT)/.tmp)

# Component specific output lives in $(dir)/bpf_skel
SKEL_OUT := $(abspath $(OUTPUT)$(dir)/bpf_skel)
SKEL_TMP_OUT := $(abspath $(SKEL_OUT)/.tmp)

ifeq ($(CONFIG_PERF_BPF_SKEL),y)
BPFTOOL := $(SKEL_TOOL_TMP_OUT)/bootstrap/bpftool
VMLINUX_H := $(SKEL_TOOL_OUT)/vmlinux.h

.PHONY: bpf-skel-prepare
bpf-skel-prepare: $(BPFTOOL) $(VMLINUX_H)
	@:

define get_sys_includes
$(shell $(1) $(2) -v -E - </dev/null 2>&1 \
       | sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }') \
$(shell $(1) $(2) -dM -E - </dev/null | grep '__riscv_xlen ' | awk '{printf("-D__riscv_xlen=%d -D__BITS_PER_LONG=%d", $$3, $$3)}')
endef

ifneq ($(CROSS_COMPILE),)
CLANG_TARGET_ARCH = --target=$(notdir $(CROSS_COMPILE:%-=%))
endif

CLANG_OPTIONS = -Wall -mcpu=v3
CLANG_SYS_INCLUDES = $(call get_sys_includes,$(CLANG),$(CLANG_TARGET_ARCH))
LIBBPF_INCLUDE := $(abspath $(or $(OUTPUT),.))/libbpf/include
BPF_INCLUDE := -I$(SKEL_TMP_OUT)/.. -I$(SKEL_TOOL_OUT) -I$(LIBBPF_INCLUDE) $(CLANG_SYS_INCLUDES)
TOOLS_UAPI_INCLUDE := -I$(srctree)/tools/include/uapi

ifneq ($(WERROR),0)
  CLANG_OPTIONS += -Werror
endif

$(BPFTOOL):
	$(Q)mkdir -p $(SKEL_TOOL_TMP_OUT)
	$(Q)CFLAGS= $(MAKE) -C ../bpf/bpftool OUTPUT=$(SKEL_TOOL_TMP_OUT)/ bootstrap

# Paths to search for a kernel to generate vmlinux.h from.
VMLINUX_BTF_ELF_PATHS ?= $(if $(O),$(O)/vmlinux)			\
		     $(if $(KBUILD_OUTPUT),$(KBUILD_OUTPUT)/vmlinux)	\
		     ../../vmlinux					\
		     /boot/vmlinux-$(shell uname -r)

# Paths to BTF information.
VMLINUX_BTF_BTF_PATHS ?= /sys/kernel/btf/vmlinux

# Filter out kernels that don't exist or without a BTF section.
VMLINUX_BTF_ELF_ABSPATHS ?= $(abspath $(wildcard $(VMLINUX_BTF_ELF_PATHS)))
VMLINUX_BTF_PATHS ?= $(shell for file in $(VMLINUX_BTF_ELF_ABSPATHS); \
			do \
				if [ -f $$file ] && ($(READELF) -S "$$file" | grep -q .BTF); \
				then \
					echo "$$file"; \
				fi; \
			done) \
			$(wildcard $(VMLINUX_BTF_BTF_PATHS))

# Select the first as the source of vmlinux.h.
VMLINUX_BTF ?= $(firstword $(VMLINUX_BTF_PATHS))

ifeq ($(VMLINUX_H_FILE),)
  ifeq ($(VMLINUX_BTF),)
    $(error Missing bpftool input for generating vmlinux.h)
  endif
endif

$(VMLINUX_H): $(VMLINUX_BTF) $(BPFTOOL) $(VMLINUX_H_FILE)
	$(call rule_mkdir)
ifeq ($(VMLINUX_H_FILE),)
	$(QUIET_GEN)$(BPFTOOL) btf dump file $< format c > $@
else
	$(Q)cp "$(VMLINUX_H_FILE)" $@
endif

# Consolidated Pattern rule for $(dir)/bpf_skel/
$(SKEL_TMP_OUT)/%.bpf.o: $(srctree)/tools/perf/$(dir)/bpf_skel/%.bpf.c $(LIBBPF) $(VMLINUX_H) $(OUTPUT)PERF-VERSION-FILE util/bpf_skel/perf_version.h
	$(call rule_mkdir)
	$(QUIET_CLANG)
	$(Q)$(CLANG) -g -O2 -fno-stack-protector --target=bpf \
	  $(CLANG_OPTIONS) $(EXTRA_BPF_FLAGS) $(BPF_INCLUDE) $(TOOLS_UAPI_INCLUDE) \
	  -include $(OUTPUT)PERF-VERSION-FILE -include util/bpf_skel/perf_version.h \
	  -fms-extensions -Wno-microsoft-anon-tag \
	  -c $< -o $@

$(SKEL_OUT)/%.skel.h: $(SKEL_TMP_OUT)/%.bpf.o $(BPFTOOL)
	$(call rule_mkdir)
	$(QUIET_GENSKEL)
	$(Q)$(BPFTOOL) gen skeleton $< > $@

.PRECIOUS: $(SKEL_TMP_OUT)/%.bpf.o

else # CONFIG_PERF_BPF_SKEL
.PHONY: bpf-skel-prepare
bpf-skel-prepare:
	@:
endif # CONFIG_PERF_BPF_SKEL

clean:
	$(call QUIET_CLEAN, bpf-skel) $(RM) -r $(SKEL_TOOL_TMP_OUT) $(OUTPUT)bench/bpf_skel/.tmp $(SKEL_TOOL_OUT)/*.skel.h $(OUTPUT)bench/bpf_skel/*.skel.h $(SKEL_TOOL_OUT)/vmlinux.h

.PHONY: clean
