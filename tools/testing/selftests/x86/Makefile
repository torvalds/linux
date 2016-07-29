all:

include ../lib.mk

.PHONY: all all_32 all_64 warn_32bit_failure clean

TARGETS_C_BOTHBITS := single_step_syscall sysret_ss_attrs syscall_nt ptrace_syscall test_mremap_vdso \
			check_initial_reg_state sigreturn ldt_gdt iopl \
			protection_keys
TARGETS_C_32BIT_ONLY := entry_from_vm86 syscall_arg_fault test_syscall_vdso unwind_vdso \
			test_FCMOV test_FCOMI test_FISTTP \
			vdso_restorer
TARGETS_C_64BIT_ONLY := fsgsbase

TARGETS_C_32BIT_ALL := $(TARGETS_C_BOTHBITS) $(TARGETS_C_32BIT_ONLY)
TARGETS_C_64BIT_ALL := $(TARGETS_C_BOTHBITS) $(TARGETS_C_64BIT_ONLY)
BINARIES_32 := $(TARGETS_C_32BIT_ALL:%=%_32)
BINARIES_64 := $(TARGETS_C_64BIT_ALL:%=%_64)

CFLAGS := -O2 -g -std=gnu99 -pthread -Wall

UNAME_M := $(shell uname -m)
CAN_BUILD_I386 := $(shell ./check_cc.sh $(CC) trivial_32bit_program.c -m32)
CAN_BUILD_X86_64 := $(shell ./check_cc.sh $(CC) trivial_64bit_program.c)

ifeq ($(CAN_BUILD_I386),1)
all: all_32
TEST_PROGS += $(BINARIES_32)
endif

ifeq ($(CAN_BUILD_X86_64),1)
all: all_64
TEST_PROGS += $(BINARIES_64)
endif

all_32: $(BINARIES_32)

all_64: $(BINARIES_64)

clean:
	$(RM) $(BINARIES_32) $(BINARIES_64)

$(TARGETS_C_32BIT_ALL:%=%_32): %_32: %.c
	$(CC) -m32 -o $@ $(CFLAGS) $(EXTRA_CFLAGS) $^ -lrt -ldl -lm

$(TARGETS_C_64BIT_ALL:%=%_64): %_64: %.c
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
	exit 0;
endif

# Some tests have additional dependencies.
sysret_ss_attrs_64: thunks.S
ptrace_syscall_32: raw_syscall_helper_32.S
test_syscall_vdso_32: thunks_32.S

# check_initial_reg_state is special: it needs a custom entry, and it
# needs to be static so that its interpreter doesn't destroy its initial
# state.
check_initial_reg_state_32: CFLAGS += -Wl,-ereal_start -static
check_initial_reg_state_64: CFLAGS += -Wl,-ereal_start -static
