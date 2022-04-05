# SPDX-License-Identifier: GPL-2.0-only
include ../../../build/Build.include

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
# Set UNAME_M riscv compile/install to work
ifeq ($(ARCH),riscv)
	UNAME_M := riscv
endif

LIBKVM = lib/assert.c lib/elf.c lib/io.c lib/kvm_util.c lib/rbtree.c lib/sparsebit.c lib/test_util.c lib/guest_modes.c lib/perf_test_util.c
LIBKVM_x86_64 = lib/x86_64/apic.c lib/x86_64/processor.c lib/x86_64/vmx.c lib/x86_64/svm.c lib/x86_64/ucall.c lib/x86_64/handlers.S
LIBKVM_aarch64 = lib/aarch64/processor.c lib/aarch64/ucall.c lib/aarch64/handlers.S lib/aarch64/spinlock.c lib/aarch64/gic.c lib/aarch64/gic_v3.c lib/aarch64/vgic.c
LIBKVM_s390x = lib/s390x/processor.c lib/s390x/ucall.c lib/s390x/diag318_test_handler.c
LIBKVM_riscv = lib/riscv/processor.c lib/riscv/ucall.c

TEST_GEN_PROGS_x86_64 = x86_64/cpuid_test
TEST_GEN_PROGS_x86_64 += x86_64/cr4_cpuid_sync_test
TEST_GEN_PROGS_x86_64 += x86_64/get_msr_index_features
TEST_GEN_PROGS_x86_64 += x86_64/evmcs_test
TEST_GEN_PROGS_x86_64 += x86_64/emulator_error_test
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_clock
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_cpuid
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_features
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_svm_test
TEST_GEN_PROGS_x86_64 += x86_64/kvm_clock_test
TEST_GEN_PROGS_x86_64 += x86_64/kvm_pv_test
TEST_GEN_PROGS_x86_64 += x86_64/mmio_warning_test
TEST_GEN_PROGS_x86_64 += x86_64/mmu_role_test
TEST_GEN_PROGS_x86_64 += x86_64/platform_info_test
TEST_GEN_PROGS_x86_64 += x86_64/pmu_event_filter_test
TEST_GEN_PROGS_x86_64 += x86_64/set_boot_cpu_id
TEST_GEN_PROGS_x86_64 += x86_64/set_sregs_test
TEST_GEN_PROGS_x86_64 += x86_64/smm_test
TEST_GEN_PROGS_x86_64 += x86_64/state_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_preemption_timer_test
TEST_GEN_PROGS_x86_64 += x86_64/svm_vmcall_test
TEST_GEN_PROGS_x86_64 += x86_64/svm_int_ctl_test
TEST_GEN_PROGS_x86_64 += x86_64/sync_regs_test
TEST_GEN_PROGS_x86_64 += x86_64/userspace_io_test
TEST_GEN_PROGS_x86_64 += x86_64/userspace_msr_exit_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_apic_access_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_close_while_nested_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_dirty_log_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_exception_with_invalid_guest_state
TEST_GEN_PROGS_x86_64 += x86_64/vmx_invalid_nested_guest_state
TEST_GEN_PROGS_x86_64 += x86_64/vmx_set_nested_state_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_tsc_adjust_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_nested_tsc_scaling_test
TEST_GEN_PROGS_x86_64 += x86_64/xapic_ipi_test
TEST_GEN_PROGS_x86_64 += x86_64/xapic_state_test
TEST_GEN_PROGS_x86_64 += x86_64/xss_msr_test
TEST_GEN_PROGS_x86_64 += x86_64/debug_regs
TEST_GEN_PROGS_x86_64 += x86_64/tsc_msrs_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_pmu_msrs_test
TEST_GEN_PROGS_x86_64 += x86_64/xen_shinfo_test
TEST_GEN_PROGS_x86_64 += x86_64/xen_vmcall_test
TEST_GEN_PROGS_x86_64 += x86_64/sev_migrate_tests
TEST_GEN_PROGS_x86_64 += x86_64/amx_test
TEST_GEN_PROGS_x86_64 += access_tracking_perf_test
TEST_GEN_PROGS_x86_64 += demand_paging_test
TEST_GEN_PROGS_x86_64 += dirty_log_test
TEST_GEN_PROGS_x86_64 += dirty_log_perf_test
TEST_GEN_PROGS_x86_64 += hardware_disable_test
TEST_GEN_PROGS_x86_64 += kvm_create_max_vcpus
TEST_GEN_PROGS_x86_64 += kvm_page_table_test
TEST_GEN_PROGS_x86_64 += max_guest_memory_test
TEST_GEN_PROGS_x86_64 += memslot_modification_stress_test
TEST_GEN_PROGS_x86_64 += memslot_perf_test
TEST_GEN_PROGS_x86_64 += rseq_test
TEST_GEN_PROGS_x86_64 += set_memory_region_test
TEST_GEN_PROGS_x86_64 += steal_time
TEST_GEN_PROGS_x86_64 += kvm_binary_stats_test
TEST_GEN_PROGS_x86_64 += system_counter_offset_test

TEST_GEN_PROGS_aarch64 += aarch64/arch_timer
TEST_GEN_PROGS_aarch64 += aarch64/debug-exceptions
TEST_GEN_PROGS_aarch64 += aarch64/get-reg-list
TEST_GEN_PROGS_aarch64 += aarch64/psci_cpu_on_test
TEST_GEN_PROGS_aarch64 += aarch64/vgic_init
TEST_GEN_PROGS_aarch64 += aarch64/vgic_irq
TEST_GEN_PROGS_aarch64 += demand_paging_test
TEST_GEN_PROGS_aarch64 += dirty_log_test
TEST_GEN_PROGS_aarch64 += dirty_log_perf_test
TEST_GEN_PROGS_aarch64 += kvm_create_max_vcpus
TEST_GEN_PROGS_aarch64 += kvm_page_table_test
TEST_GEN_PROGS_aarch64 += memslot_modification_stress_test
TEST_GEN_PROGS_aarch64 += memslot_perf_test
TEST_GEN_PROGS_aarch64 += rseq_test
TEST_GEN_PROGS_aarch64 += set_memory_region_test
TEST_GEN_PROGS_aarch64 += steal_time
TEST_GEN_PROGS_aarch64 += kvm_binary_stats_test

TEST_GEN_PROGS_s390x = s390x/memop
TEST_GEN_PROGS_s390x += s390x/resets
TEST_GEN_PROGS_s390x += s390x/sync_regs_test
TEST_GEN_PROGS_s390x += s390x/tprot
TEST_GEN_PROGS_s390x += demand_paging_test
TEST_GEN_PROGS_s390x += dirty_log_test
TEST_GEN_PROGS_s390x += kvm_create_max_vcpus
TEST_GEN_PROGS_s390x += kvm_page_table_test
TEST_GEN_PROGS_s390x += rseq_test
TEST_GEN_PROGS_s390x += set_memory_region_test
TEST_GEN_PROGS_s390x += kvm_binary_stats_test

TEST_GEN_PROGS_riscv += demand_paging_test
TEST_GEN_PROGS_riscv += dirty_log_test
TEST_GEN_PROGS_riscv += kvm_create_max_vcpus
TEST_GEN_PROGS_riscv += kvm_page_table_test
TEST_GEN_PROGS_riscv += set_memory_region_test
TEST_GEN_PROGS_riscv += kvm_binary_stats_test

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
	-I$(<D) -Iinclude/$(UNAME_M) -I.. $(EXTRA_CFLAGS) $(KHDR_INCLUDES)

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
