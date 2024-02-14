# SPDX-License-Identifier: GPL-2.0
all:

include ../lib.mk

.PHONY: all all_32 all_64 warn_32bit_failure clean

UNAME_M := $(shell uname -m)
CAN_BUILD_I386 := $(shell ./check_cc.sh "$(CC)" trivial_32bit_program.c -m32)
CAN_BUILD_X86_64 := $(shell ./check_cc.sh "$(CC)" trivial_64bit_program.c)
CAN_BUILD_WITH_NOPIE := $(shell ./check_cc.sh "$(CC)" trivial_program.c -no-pie)

TARGETS_C_BOTHBITS := single_step_syscall sysret_ss_attrs syscall_nt test_mremap_vdso \
			check_initial_reg_state sigreturn iopl ioperm \
			test_vsyscall mov_ss_trap \
			syscall_arg_fault fsgsbase_restore sigaltstack
TARGETS_C_BOTHBITS += nx_stack
TARGETS_C_32BIT_ONLY := entry_from_vm86 test_syscall_vdso unwind_vdso \
			test_FCMOV test_FCOMI test_FISTTP \
			vdso_restorer
TARGETS_C_64BIT_ONLY := fsgsbase sysret_rip syscall_numbering \
			corrupt_xstate_header amx lam test_shadow_stack
# Some selftests require 32bit support enabled also on 64bit systems
TARGETS_C_32BIT_NEEDED := ldt_gdt ptrace_syscall

TARGETS_C_32BIT_ALL := $(TARGETS_C_BOTHBITS) $(TARGETS_C_32BIT_ONLY) $(TARGETS_C_32BIT_NEEDED)
TARGETS_C_64BIT_ALL := $(TARGETS_C_BOTHBITS) $(TARGETS_C_64BIT_ONLY)
ifeq ($(CAN_BUILD_I386)$(CAN_BUILD_X86_64),11)
TARGETS_C_64BIT_ALL += $(TARGETS_C_32BIT_NEEDED)
endif

BINARIES_32 := $(TARGETS_C_32BIT_ALL:%=%_32)
BINARIES_64 := $(TARGETS_C_64BIT_ALL:%=%_64)

BINARIES_32 := $(patsubst %,$(OUTPUT)/%,$(BINARIES_32))
BINARIES_64 := $(patsubst %,$(OUTPUT)/%,$(BINARIES_64))

CFLAGS := -O2 -g -std=gnu99 -pthread -Wall $(KHDR_INCLUDES)

# call32_from_64 in thunks.S uses absolute addresses.
ifeq ($(CAN_BUILD_WITH_NOPIE),1)
CFLAGS += -no-pie
endif

define gen-target-rule-32
$(1) $(1)_32: $(OUTPUT)/$(1)_32
.PHONY: $(1) $(1)_32
endef

define gen-target-rule-64
$(1) $(1)_64: $(OUTPUT)/$(1)_64
.PHONY: $(1) $(1)_64
endef

ifeq ($(CAN_BUILD_I386),1)
all: all_32
TEST_PROGS += $(BINARIES_32)
EXTRA_CFLAGS += -DCAN_BUILD_32
$(foreach t,$(TARGETS_C_32BIT_ALL),$(eval $(call gen-target-rule-32,$(t))))
endif

ifeq ($(CAN_BUILD_X86_64),1)
all: all_64
TEST_PROGS += $(BINARIES_64)
EXTRA_CFLAGS += -DCAN_BUILD_64
$(foreach t,$(TARGETS_C_64BIT_ALL),$(eval $(call gen-target-rule-64,$(t))))
endif

all_32: $(BINARIES_32)

all_64: $(BINARIES_64)

EXTRA_CLEAN := $(BINARIES_32) $(BINARIES_64)

$(BINARIES_32): $(OUTPUT)/%_32: %.c helpers.h
	$(CC) -m32 -o $@ $(CFLAGS) $(EXTRA_CFLAGS) $^ -lrt -ldl -lm

$(BINARIES_64): $(OUTPUT)/%_64: %.c helpers.h
	$(CC) -m64 -o $@ $(CFLAGS) $(EXTRA_CFLAGS) $^ -lrt -ldl

# x86_64 users should be encouraged to install 32-bit libraries
ifeq ($(CAN_BUILD_I386)$(CAN_BUILD_X86_64),01)
all: warn_32bit_failure

warn_32bit_failure:
	@echo "Warning: you seem to have a broken 32-bit build" 2>&1; 	\
	echo "environment.  This will reduce test coverage of 64-bit" 2>&1; \
	echo "kernels.  If you are using a Debian-like distribution," 2>&1; \
	echo "try:"; 2>&1; \
	echo "";							\
	echo "  apt-get install gcc-multilib libc6-i386 libc6-dev-i386"; \
	echo "";							\
	echo "If you are using a Fedora-like distribution, try:";	\
	echo "";							\
	echo "  yum install glibc-devel.*i686";				\
	echo "";							\
	echo "If you are using a SUSE-like distribution, try:";		\
	echo "";							\
	echo "  zypper install gcc-32bit glibc-devel-static-32bit";	\
	exit 0;
endif

# Some tests have additional dependencies.
$(OUTPUT)/sysret_ss_attrs_64: thunks.S
$(OUTPUT)/ptrace_syscall_32: raw_syscall_helper_32.S
$(OUTPUT)/test_syscall_vdso_32: thunks_32.S

# check_initial_reg_state is special: it needs a custom entry, and it
# needs to be static so that its interpreter doesn't destroy its initial
# state.
$(OUTPUT)/check_initial_reg_state_32: CFLAGS += -Wl,-ereal_start -static
$(OUTPUT)/check_initial_reg_state_64: CFLAGS += -Wl,-ereal_start -static

$(OUTPUT)/nx_stack_32: CFLAGS += -Wl,-z,noexecstack
$(OUTPUT)/nx_stack_64: CFLAGS += -Wl,-z,noexecstack
