# SPDX-License-Identifier: GPL-2.0-only
include ../../../../scripts/Kbuild.include

all:

top_srcdir = ../../../..
KSFT_KHDR_INSTALL := 1

# For cross-builds to work, UNAME_M has to map to ARCH and arch specific
# directories and targets in this Makefile. "uname -m" doesn't map to
# arch specific sub-directory names.
#
# UNAME_M variable to used to run the compiles pointing to the right arch
# directories and build the right targets for these supported architectures.
#
# TEST_GEN_PROGS and LIBKVM are set using UNAME_M variable.
# LINUX_TOOL_ARCH_INCLUDE is set using ARCH variable.
#
# x86_64 targets are named to include x86_64 as a suffix and directories
# for includes are in x86_64 sub-directory. s390x and aarch64 follow the
# same convention. "uname -m" doesn't result in the correct mapping for
# s390x and aarch64.
#
# No change necessary for x86_64
UNAME_M := $(shell uname -m)

# Set UNAME_M for arm64 compile/install to work
ifeq ($(ARCH),arm64)
	UNAME_M := aarch64
endif
# Set UNAME_M s390x compile/install to work
ifeq ($(ARCH),s390)
	UNAME_M := s390x
endif

LIBKVM = lib/assert.c lib/elf.c lib/io.c lib/kvm_util.c lib/sparsebit.c lib/test_util.c lib/guest_modes.c lib/perf_test_util.c
LIBKVM_x86_64 = lib/x86_64/processor.c lib/x86_64/vmx.c lib/x86_64/svm.c lib/x86_64/ucall.c lib/x86_64/handlers.S
LIBKVM_aarch64 = lib/aarch64/processor.c lib/aarch64/ucall.c
LIBKVM_s390x = lib/s390x/processor.c lib/s390x/ucall.c lib/s390x/diag318_test_handler.c

TEST_GEN_PROGS_x86_64 = x86_64/cr4_cpuid_sync_test
TEST_GEN_PROGS_x86_64 += x86_64/get_msr_index_features
TEST_GEN_PROGS_x86_64 += x86_64/evmcs_test
TEST_GEN_PROGS_x86_64 += x86_64/get_cpuid_test
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_clock
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_cpuid
TEST_GEN_PROGS_x86_64 += x86_64/kvm_pv_test
TEST_GEN_PROGS_x86_64 += x86_64/mmio_warning_test
TEST_GEN_PROGS_x86_64 += x86_64/platform_info_test
TEST_GEN_PROGS_x86_64 += x86_64/set_boot_cpu_id
TEST_GEN_PROGS_x86_64 += x86_64/set_sregs_test
TEST_GEN_PROGS_x86_64 += x86_64/smm_test
TEST_GEN_PROGS_x86_64 += x86_64/state_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_preemption_timer_test
TEST_GEN_PROGS_x86_64 += x86_64/svm_vmcall_test
TEST_GEN_PROGS_x86_64 += x86_64/sync_regs_test
TEST_GEN_PROGS_x86_64 += x86_64/userspace_msr_exit_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_apic_access_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_close_while_nested_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_dirty_log_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_set_nested_state_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_tsc_adjust_test
TEST_GEN_PROGS_x86_64 += x86_64/xapic_ipi_test
TEST_GEN_PROGS_x86_64 += x86_64/xss_msr_test
TEST_GEN_PROGS_x86_64 += x86_64/debug_regs
TEST_GEN_PROGS_x86_64 += x86_64/tsc_msrs_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_pmu_msrs_test
TEST_GEN_PROGS_x86_64 += x86_64/xen_shinfo_test
TEST_GEN_PROGS_x86_64 += x86_64/xen_vmcall_test
TEST_GEN_PROGS_x86_64 += demand_paging_test
TEST_GEN_PROGS_x86_64 += dirty_log_test
TEST_GEN_PROGS_x86_64 += dirty_log_perf_test
TEST_GEN_PROGS_x86_64 += hardware_disable_test
TEST_GEN_PROGS_x86_64 += kvm_create_max_vcpus
TEST_GEN_PROGS_x86_64 += kvm_page_table_test
TEST_GEN_PROGS_x86_64 += memslot_modification_stress_test
TEST_GEN_PROGS_x86_64 += set_memory_region_test
TEST_GEN_PROGS_x86_64 += steal_time

TEST_GEN_PROGS_aarch64 += aarch64/get-reg-list
TEST_GEN_PROGS_aarch64 += aarch64/get-reg-list-sve
TEST_GEN_PROGS_aarch64 += aarch64/vgic_init
TEST_GEN_PROGS_aarch64 += demand_paging_test
TEST_GEN_PROGS_aarch64 += dirty_log_test
TEST_GEN_PROGS_aarch64 += dirty_log_perf_test
TEST_GEN_PROGS_aarch64 += kvm_create_max_vcpus
TEST_GEN_PROGS_aarch64 += kvm_page_table_test
TEST_GEN_PROGS_aarch64 += set_memory_region_test
TEST_GEN_PROGS_aarch64 += steal_time

TEST_GEN_PROGS_s390x = s390x/memop
TEST_GEN_PROGS_s390x += s390x/resets
TEST_GEN_PROGS_s390x += s390x/sync_regs_test
TEST_GEN_PROGS_s390x += demand_paging_test
TEST_GEN_PROGS_s390x += dirty_log_test
TEST_GEN_PROGS_s390x += kvm_create_max_vcpus
TEST_GEN_PROGS_s390x += kvm_page_table_test
TEST_GEN_PROGS_s390x += set_memory_region_test

TEST_GEN_PROGS += $(TEST_GEN_PROGS_$(UNAME_M))
LIBKVM += $(LIBKVM_$(UNAME_M))

INSTALL_HDR_PATH = $(top_srcdir)/usr
LINUX_HDR_PATH = $(INSTALL_HDR_PATH)/include/
LINUX_TOOL_INCLUDE = $(top_srcdir)/tools/include
ifeq ($(ARCH),x86_64)
LINUX_TOOL_ARCH_INCLUDE = $(top_srcdir)/tools/arch/x86/include
else
LINUX_TOOL_ARCH_INCLUDE = $(top_srcdir)/tools/arch/$(ARCH)/include
endif
CFLAGS += -Wall -Wstrict-prototypes -Wuninitialized -O2 -g -std=gnu99 \
	-fno-stack-protector -fno-PIE -I$(LINUX_TOOL_INCLUDE) \
	-I$(LINUX_TOOL_ARCH_INCLUDE) -I$(LINUX_HDR_PATH) -Iinclude \
	-I$(<D) -Iinclude/$(UNAME_M) -I..

no-pie-option := $(call try-run, echo 'int main() { return 0; }' | \
        $(CC) -Werror -no-pie -x c - -o "$$TMP", -no-pie)

# On s390, build the testcases KVM-enabled
pgste-option = $(call try-run, echo 'int main() { return 0; }' | \
	$(CC) -Werror -Wl$(comma)--s390-pgste -x c - -o "$$TMP",-Wl$(comma)--s390-pgste)


LDFLAGS += -pthread $(no-pie-option) $(pgste-option)

# After inclusion, $(OUTPUT) is defined and
# $(TEST_GEN_PROGS) starts with $(OUTPUT)/
include ../lib.mk

STATIC_LIBS := $(OUTPUT)/libkvm.a
LIBKVM_C := $(filter %.c,$(LIBKVM))
LIBKVM_S := $(filter %.S,$(LIBKVM))
LIBKVM_C_OBJ := $(patsubst %.c, $(OUTPUT)/%.o, $(LIBKVM_C))
LIBKVM_S_OBJ := $(patsubst %.S, $(OUTPUT)/%.o, $(LIBKVM_S))
EXTRA_CLEAN += $(LIBKVM_C_OBJ) $(LIBKVM_S_OBJ) $(STATIC_LIBS) cscope.*

x := $(shell mkdir -p $(sort $(dir $(LIBKVM_C_OBJ) $(LIBKVM_S_OBJ))))
$(LIBKVM_C_OBJ): $(OUTPUT)/%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $< -o $@

$(LIBKVM_S_OBJ): $(OUTPUT)/%.o: %.S
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $< -o $@

LIBKVM_OBJS = $(LIBKVM_C_OBJ) $(LIBKVM_S_OBJ)
$(OUTPUT)/libkvm.a: $(LIBKVM_OBJS)
	$(AR) crs $@ $^

x := $(shell mkdir -p $(sort $(dir $(TEST_GEN_PROGS))))
all: $(STATIC_LIBS)
$(TEST_GEN_PROGS): $(STATIC_LIBS)

cscope: include_paths = $(LINUX_TOOL_INCLUDE) $(LINUX_HDR_PATH) include lib ..
cscope:
	$(RM) cscope.*
	(find $(include_paths) -name '*.h' \
		-exec realpath --relative-base=$(PWD) {} \;; \
	find . -name '*.c' \
		-exec realpath --relative-base=$(PWD) {} \;) | sort -u > cscope.files
	cscope -b
