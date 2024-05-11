# SPDX-License-Identifier: GPL-2.0
# Rules to generate bpf objs
CLANG ?= clang
SCRATCH_DIR := $(OUTPUT)/tools
BUILD_DIR := $(SCRATCH_DIR)/build
BPFDIR := $(top_srcdir)/tools/lib/bpf
APIDIR := $(top_srcdir)/tools/include/uapi

CCINCLUDE += -I$(selfdir)/bpf
CCINCLUDE += -I$(top_srcdir)/usr/include/
CCINCLUDE += -I$(SCRATCH_DIR)/include

BPFOBJ := $(BUILD_DIR)/libbpf/libbpf.a

MAKE_DIRS := $(BUILD_DIR)/libbpf
$(MAKE_DIRS):
	$(call msg,MKDIR,,$@)
	$(Q)mkdir -p $@

# Get Clang's default includes on this system, as opposed to those seen by
# '--target=bpf'. This fixes "missing" files on some architectures/distros,
# such as asm/byteorder.h, asm/socket.h, asm/sockios.h, sys/cdefs.h etc.
#
# Use '-idirafter': Don't interfere with include mechanics except where the
# build would have failed anyways.
define get_sys_includes
$(shell $(1) $(2) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }') \
$(shell $(1) $(2) -dM -E - </dev/null | grep '__riscv_xlen ' | awk '{printf("-D__riscv_xlen=%d -D__BITS_PER_LONG=%d", $$3, $$3)}')
endef

ifneq ($(CROSS_COMPILE),)
CLANG_TARGET_ARCH = --target=$(notdir $(CROSS_COMPILE:%-=%))
endif

CLANG_SYS_INCLUDES = $(call get_sys_includes,$(CLANG),$(CLANG_TARGET_ARCH))

BPF_PROG_OBJS := $(patsubst %.c,$(OUTPUT)/%.o,$(wildcard *.bpf.c))

$(BPF_PROG_OBJS): $(OUTPUT)/%.o : %.c $(BPFOBJ) | $(MAKE_DIRS)
	$(call msg,BPF_PROG,,$@)
	$(Q)$(CLANG) -O2 -g --target=bpf $(CCINCLUDE) $(CLANG_SYS_INCLUDES) \
	-c $< -o $@

$(BPFOBJ): $(wildcard $(BPFDIR)/*.[ch] $(BPFDIR)/Makefile)		       \
	   $(APIDIR)/linux/bpf.h					       \
	   | $(BUILD_DIR)/libbpf
	$(call msg,MAKE,,$@)
	$(Q)$(MAKE) $(submake_extras) -C $(BPFDIR) OUTPUT=$(BUILD_DIR)/libbpf/ \
		    EXTRA_CFLAGS='-g -O0'				       \
		    DESTDIR=$(SCRATCH_DIR) prefix= all install_headers

EXTRA_CLEAN += $(SCRATCH_DIR)
