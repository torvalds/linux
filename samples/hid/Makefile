# SPDX-License-Identifier: GPL-2.0

HID_SAMPLES_PATH ?= $(abspath $(src))
TOOLS_PATH := $(HID_SAMPLES_PATH)/../../tools

pound := \#

# List of programs to build
tprogs-y += hid_mouse
tprogs-y += hid_surface_dial

# Libbpf dependencies
LIBBPF_SRC = $(TOOLS_PATH)/lib/bpf
LIBBPF_OUTPUT = $(abspath $(HID_SAMPLES_PATH))/libbpf
LIBBPF_DESTDIR = $(LIBBPF_OUTPUT)
LIBBPF_INCLUDE = $(LIBBPF_DESTDIR)/include
LIBBPF = $(LIBBPF_OUTPUT)/libbpf.a

EXTRA_HEADERS := hid_bpf_attach.h
EXTRA_BPF_HEADERS := hid_bpf_helpers.h

hid_mouse-objs := hid_mouse.o
hid_surface_dial-objs := hid_surface_dial.o

# Tell kbuild to always build the programs
always-y := $(tprogs-y)

ifeq ($(ARCH), arm)
# Strip all except -D__LINUX_ARM_ARCH__ option needed to handle linux
# headers when arm instruction set identification is requested.
ARM_ARCH_SELECTOR := $(filter -D__LINUX_ARM_ARCH__%, $(KBUILD_CFLAGS))
BPF_EXTRA_CFLAGS := $(ARM_ARCH_SELECTOR)
TPROGS_CFLAGS += $(ARM_ARCH_SELECTOR)
endif

ifeq ($(ARCH), mips)
TPROGS_CFLAGS += -D__SANE_USERSPACE_TYPES__
ifdef CONFIG_MACH_LOONGSON64
BPF_EXTRA_CFLAGS += -I$(srctree)/arch/mips/include/asm/mach-loongson64
BPF_EXTRA_CFLAGS += -I$(srctree)/arch/mips/include/asm/mach-generic
endif
endif

TPROGS_CFLAGS += -Wall -O2
TPROGS_CFLAGS += -Wmissing-prototypes
TPROGS_CFLAGS += -Wstrict-prototypes

TPROGS_CFLAGS += -I$(objtree)/usr/include
TPROGS_CFLAGS += -I$(LIBBPF_INCLUDE)
TPROGS_CFLAGS += -I$(srctree)/tools/include

ifdef SYSROOT
TPROGS_CFLAGS += --sysroot=$(SYSROOT)
TPROGS_LDFLAGS := -L$(SYSROOT)/usr/lib
endif

TPROGS_LDLIBS			+= $(LIBBPF) -lelf -lz

# Allows pointing LLC/CLANG to a LLVM backend with bpf support, redefine on cmdline:
# make M=samples/bpf LLC=~/git/llvm-project/llvm/build/bin/llc CLANG=~/git/llvm-project/llvm/build/bin/clang
LLC ?= llc
CLANG ?= clang
OPT ?= opt
LLVM_DIS ?= llvm-dis
LLVM_OBJCOPY ?= llvm-objcopy
LLVM_READELF ?= llvm-readelf
BTF_PAHOLE ?= pahole

# Detect that we're cross compiling and use the cross compiler
ifdef CROSS_COMPILE
CLANG_ARCH_ARGS = --target=$(notdir $(CROSS_COMPILE:%-=%))
endif

# Don't evaluate probes and warnings if we need to run make recursively
ifneq ($(src),)
HDR_PROBE := $(shell printf "$(pound)include <linux/types.h>\n struct list_head { int a; }; int main() { return 0; }" | \
	$(CC) $(TPROGS_CFLAGS) $(TPROGS_LDFLAGS) -x c - \
	-o /dev/null 2>/dev/null && echo okay)

ifeq ($(HDR_PROBE),)
$(warning WARNING: Detected possible issues with include path.)
$(warning WARNING: Please install kernel headers locally (make headers_install).)
endif

BTF_LLC_PROBE := $(shell $(LLC) -march=bpf -mattr=help 2>&1 | grep dwarfris)
BTF_PAHOLE_PROBE := $(shell $(BTF_PAHOLE) --help 2>&1 | grep BTF)
BTF_OBJCOPY_PROBE := $(shell $(LLVM_OBJCOPY) --help 2>&1 | grep -i 'usage.*llvm')
BTF_LLVM_PROBE := $(shell echo "int main() { return 0; }" | \
			  $(CLANG) --target=bpf -O2 -g -c -x c - -o ./llvm_btf_verify.o; \
			  $(LLVM_READELF) -S ./llvm_btf_verify.o | grep BTF; \
			  /bin/rm -f ./llvm_btf_verify.o)

BPF_EXTRA_CFLAGS += -fno-stack-protector
ifneq ($(BTF_LLVM_PROBE),)
	BPF_EXTRA_CFLAGS += -g
else
ifneq ($(and $(BTF_LLC_PROBE),$(BTF_PAHOLE_PROBE),$(BTF_OBJCOPY_PROBE)),)
	BPF_EXTRA_CFLAGS += -g
	LLC_FLAGS += -mattr=dwarfris
	DWARF2BTF = y
endif
endif
endif

# Trick to allow make to be run from this directory
all:
	$(MAKE) -C ../../ M=$(CURDIR) HID_SAMPLES_PATH=$(CURDIR)

clean:
	$(MAKE) -C ../../ M=$(CURDIR) clean
	@find $(CURDIR) -type f -name '*~' -delete
	@$(RM) -r $(CURDIR)/libbpf $(CURDIR)/bpftool

$(LIBBPF): $(wildcard $(LIBBPF_SRC)/*.[ch] $(LIBBPF_SRC)/Makefile) | $(LIBBPF_OUTPUT)
# Fix up variables inherited from Kbuild that tools/ build system won't like
	$(MAKE) -C $(LIBBPF_SRC) RM='rm -rf' EXTRA_CFLAGS="$(TPROGS_CFLAGS)" \
		LDFLAGS=$(TPROGS_LDFLAGS) srctree=$(HID_SAMPLES_PATH)/../../ \
		O= OUTPUT=$(LIBBPF_OUTPUT)/ DESTDIR=$(LIBBPF_DESTDIR) prefix= \
		$@ install_headers

BPFTOOLDIR := $(TOOLS_PATH)/bpf/bpftool
BPFTOOL_OUTPUT := $(abspath $(HID_SAMPLES_PATH))/bpftool
BPFTOOL := $(BPFTOOL_OUTPUT)/bootstrap/bpftool
$(BPFTOOL): $(wildcard $(BPFTOOLDIR)/*.[ch] $(BPFTOOLDIR)/Makefile) | $(BPFTOOL_OUTPUT)
	$(MAKE) -C $(BPFTOOLDIR) srctree=$(HID_SAMPLES_PATH)/../../ 		\
		OUTPUT=$(BPFTOOL_OUTPUT)/ bootstrap

$(LIBBPF_OUTPUT) $(BPFTOOL_OUTPUT):
	$(call msg,MKDIR,$@)
	$(Q)mkdir -p $@

FORCE:


# Verify LLVM compiler tools are available and bpf target is supported by llc
.PHONY: verify_cmds verify_target_bpf $(CLANG) $(LLC)

verify_cmds: $(CLANG) $(LLC)
	@for TOOL in $^ ; do \
		if ! (which -- "$${TOOL}" > /dev/null 2>&1); then \
			echo "*** ERROR: Cannot find LLVM tool $${TOOL}" ;\
			exit 1; \
		else true; fi; \
	done

verify_target_bpf: verify_cmds
	@if ! (${LLC} -march=bpf -mattr=help > /dev/null 2>&1); then \
		echo "*** ERROR: LLVM (${LLC}) does not support 'bpf' target" ;\
		echo "   NOTICE: LLVM version >= 3.7.1 required" ;\
		exit 2; \
	else true; fi

$(HID_SAMPLES_PATH)/*.c: verify_target_bpf $(LIBBPF)
$(src)/*.c: verify_target_bpf $(LIBBPF)

libbpf_hdrs: $(LIBBPF)

.PHONY: libbpf_hdrs

$(obj)/hid_mouse.o: $(obj)/hid_mouse.skel.h
$(obj)/hid_surface_dial.o: $(obj)/hid_surface_dial.skel.h

-include $(HID_SAMPLES_PATH)/Makefile.target

VMLINUX_BTF_PATHS ?= $(abspath $(if $(O),$(O)/vmlinux))				\
		     $(abspath $(if $(KBUILD_OUTPUT),$(KBUILD_OUTPUT)/vmlinux))	\
		     $(abspath ./vmlinux)
VMLINUX_BTF ?= $(abspath $(firstword $(wildcard $(VMLINUX_BTF_PATHS))))

$(obj)/vmlinux.h: $(VMLINUX_BTF) $(BPFTOOL)
ifeq ($(VMLINUX_H),)
ifeq ($(VMLINUX_BTF),)
	$(error Cannot find a vmlinux for VMLINUX_BTF at any of "$(VMLINUX_BTF_PATHS)",\
		build the kernel or set VMLINUX_BTF or VMLINUX_H variable)
endif
	$(Q)$(BPFTOOL) btf dump file $(VMLINUX_BTF) format c > $@
else
	$(Q)cp "$(VMLINUX_H)" $@
endif

clean-files += vmlinux.h

# Get Clang's default includes on this system, as opposed to those seen by
# '--target=bpf'. This fixes "missing" files on some architectures/distros,
# such as asm/byteorder.h, asm/socket.h, asm/sockios.h, sys/cdefs.h etc.
#
# Use '-idirafter': Don't interfere with include mechanics except where the
# build would have failed anyways.
define get_sys_includes
$(shell $(1) -v -E - </dev/null 2>&1 \
        | sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }') \
$(shell $(1) -dM -E - </dev/null | grep '#define __riscv_xlen ' | sed 's/#define /-D/' | sed 's/ /=/')
endef

CLANG_SYS_INCLUDES = $(call get_sys_includes,$(CLANG))

EXTRA_BPF_HEADERS_SRC := $(addprefix $(src)/,$(EXTRA_BPF_HEADERS))

$(obj)/%.bpf.o: $(src)/%.bpf.c $(EXTRA_BPF_HEADERS_SRC) $(obj)/vmlinux.h
	@echo "  CLANG-BPF " $@
	$(Q)$(CLANG) -g -O2 --target=bpf -D__TARGET_ARCH_$(SRCARCH) \
		-Wno-compare-distinct-pointer-types -I$(srctree)/include \
		-I$(srctree)/samples/bpf -I$(srctree)/tools/include \
		-I$(LIBBPF_INCLUDE) $(CLANG_SYS_INCLUDES) \
		-c $(filter %.bpf.c,$^) -o $@

LINKED_SKELS := hid_mouse.skel.h hid_surface_dial.skel.h
clean-files += $(LINKED_SKELS)

hid_mouse.skel.h-deps := hid_mouse.bpf.o hid_bpf_attach.bpf.o
hid_surface_dial.skel.h-deps := hid_surface_dial.bpf.o hid_bpf_attach.bpf.o

LINKED_BPF_SRCS := $(patsubst %.bpf.o,%.bpf.c,$(foreach skel,$(LINKED_SKELS),$($(skel)-deps)))

BPF_SRCS_LINKED := $(notdir $(wildcard $(src)/*.bpf.c))
BPF_OBJS_LINKED := $(patsubst %.bpf.c,$(obj)/%.bpf.o, $(BPF_SRCS_LINKED))
BPF_SKELS_LINKED := $(addprefix $(obj)/,$(LINKED_SKELS))

$(BPF_SKELS_LINKED): $(BPF_OBJS_LINKED) $(BPFTOOL)
	@echo "  BPF GEN-OBJ " $(@:.skel.h=)
	$(Q)$(BPFTOOL) gen object $(@:.skel.h=.lbpf.o) $(addprefix $(obj)/,$($(@F)-deps))
	@echo "  BPF GEN-SKEL" $(@:.skel.h=)
	$(Q)$(BPFTOOL) gen skeleton $(@:.skel.h=.lbpf.o) name $(notdir $(@:.skel.h=)) > $@

# asm/sysreg.h - inline assembly used by it is incompatible with llvm.
# But, there is no easy way to fix it, so just exclude it since it is
# useless for BPF samples.
# below we use long chain of commands, clang | opt | llvm-dis | llc,
# to generate final object file. 'clang' compiles the source into IR
# with native target, e.g., x64, arm64, etc. 'opt' does bpf CORE IR builtin
# processing (llvm12) and IR optimizations. 'llvm-dis' converts
# 'opt' output to IR, and finally 'llc' generates bpf byte code.
$(obj)/%.o: $(src)/%.c
	@echo "  CLANG-bpf " $@
	$(Q)$(CLANG) $(NOSTDINC_FLAGS) $(LINUXINCLUDE) $(BPF_EXTRA_CFLAGS) \
		-I$(obj) -I$(srctree)/tools/testing/selftests/bpf/ \
		-I$(LIBBPF_INCLUDE) \
		-D__KERNEL__ -D__BPF_TRACING__ -Wno-unused-value -Wno-pointer-sign \
		-D__TARGET_ARCH_$(SRCARCH) -Wno-compare-distinct-pointer-types \
		-Wno-gnu-variable-sized-type-not-at-end \
		-Wno-address-of-packed-member -Wno-tautological-compare \
		-Wno-unknown-warning-option $(CLANG_ARCH_ARGS) \
		-fno-asynchronous-unwind-tables \
		-I$(srctree)/samples/hid/ \
		-O2 -emit-llvm -Xclang -disable-llvm-passes -c $< -o - | \
		$(OPT) -O2 -mtriple=bpf-pc-linux | $(LLVM_DIS) | \
		$(LLC) -march=bpf $(LLC_FLAGS) -filetype=obj -o $@
ifeq ($(DWARF2BTF),y)
	$(BTF_PAHOLE) -J $@
endif
