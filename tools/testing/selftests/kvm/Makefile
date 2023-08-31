# SPDX-License-Identifier: GPL-2.0-only
include ../../../build/Build.include

all:

top_srcdir = ../../../..
include $(top_srcdir)/scripts/subarch.include
ARCH            ?= $(SUBARCH)

ifeq ($(ARCH),x86)
	ARCH_DIR := x86_64
else ifeq ($(ARCH),arm64)
	ARCH_DIR := aarch64
else ifeq ($(ARCH),s390)
	ARCH_DIR := s390x
else
	ARCH_DIR := $(ARCH)
endif

LIBKVM += lib/assert.c
LIBKVM += lib/elf.c
LIBKVM += lib/guest_modes.c
LIBKVM += lib/io.c
LIBKVM += lib/kvm_util.c
LIBKVM += lib/memstress.c
LIBKVM += lib/guest_sprintf.c
LIBKVM += lib/rbtree.c
LIBKVM += lib/sparsebit.c
LIBKVM += lib/test_util.c
LIBKVM += lib/ucall_common.c
LIBKVM += lib/userfaultfd_util.c

LIBKVM_STRING += lib/string_override.c

LIBKVM_x86_64 += lib/x86_64/apic.c
LIBKVM_x86_64 += lib/x86_64/handlers.S
LIBKVM_x86_64 += lib/x86_64/hyperv.c
LIBKVM_x86_64 += lib/x86_64/memstress.c
LIBKVM_x86_64 += lib/x86_64/processor.c
LIBKVM_x86_64 += lib/x86_64/svm.c
LIBKVM_x86_64 += lib/x86_64/ucall.c
LIBKVM_x86_64 += lib/x86_64/vmx.c

LIBKVM_aarch64 += lib/aarch64/gic.c
LIBKVM_aarch64 += lib/aarch64/gic_v3.c
LIBKVM_aarch64 += lib/aarch64/handlers.S
LIBKVM_aarch64 += lib/aarch64/processor.c
LIBKVM_aarch64 += lib/aarch64/spinlock.c
LIBKVM_aarch64 += lib/aarch64/ucall.c
LIBKVM_aarch64 += lib/aarch64/vgic.c

LIBKVM_s390x += lib/s390x/diag318_test_handler.c
LIBKVM_s390x += lib/s390x/processor.c
LIBKVM_s390x += lib/s390x/ucall.c

LIBKVM_riscv += lib/riscv/processor.c
LIBKVM_riscv += lib/riscv/ucall.c

# Non-compiled test targets
TEST_PROGS_x86_64 += x86_64/nx_huge_pages_test.sh

# Compiled test targets
TEST_GEN_PROGS_x86_64 = x86_64/cpuid_test
TEST_GEN_PROGS_x86_64 += x86_64/cr4_cpuid_sync_test
TEST_GEN_PROGS_x86_64 += x86_64/dirty_log_page_splitting_test
TEST_GEN_PROGS_x86_64 += x86_64/get_msr_index_features
TEST_GEN_PROGS_x86_64 += x86_64/exit_on_emulation_failure_test
TEST_GEN_PROGS_x86_64 += x86_64/fix_hypercall_test
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_clock
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_cpuid
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_evmcs
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_extended_hypercalls
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_features
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_ipi
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_svm_test
TEST_GEN_PROGS_x86_64 += x86_64/hyperv_tlb_flush
TEST_GEN_PROGS_x86_64 += x86_64/kvm_clock_test
TEST_GEN_PROGS_x86_64 += x86_64/kvm_pv_test
TEST_GEN_PROGS_x86_64 += x86_64/mmio_warning_test
TEST_GEN_PROGS_x86_64 += x86_64/monitor_mwait_test
TEST_GEN_PROGS_x86_64 += x86_64/nested_exceptions_test
TEST_GEN_PROGS_x86_64 += x86_64/platform_info_test
TEST_GEN_PROGS_x86_64 += x86_64/pmu_event_filter_test
TEST_GEN_PROGS_x86_64 += x86_64/set_boot_cpu_id
TEST_GEN_PROGS_x86_64 += x86_64/set_sregs_test
TEST_GEN_PROGS_x86_64 += x86_64/smaller_maxphyaddr_emulation_test
TEST_GEN_PROGS_x86_64 += x86_64/smm_test
TEST_GEN_PROGS_x86_64 += x86_64/state_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_preemption_timer_test
TEST_GEN_PROGS_x86_64 += x86_64/svm_vmcall_test
TEST_GEN_PROGS_x86_64 += x86_64/svm_int_ctl_test
TEST_GEN_PROGS_x86_64 += x86_64/svm_nested_shutdown_test
TEST_GEN_PROGS_x86_64 += x86_64/svm_nested_soft_inject_test
TEST_GEN_PROGS_x86_64 += x86_64/tsc_scaling_sync
TEST_GEN_PROGS_x86_64 += x86_64/sync_regs_test
TEST_GEN_PROGS_x86_64 += x86_64/ucna_injection_test
TEST_GEN_PROGS_x86_64 += x86_64/userspace_io_test
TEST_GEN_PROGS_x86_64 += x86_64/userspace_msr_exit_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_apic_access_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_close_while_nested_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_dirty_log_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_exception_with_invalid_guest_state
TEST_GEN_PROGS_x86_64 += x86_64/vmx_msrs_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_invalid_nested_guest_state
TEST_GEN_PROGS_x86_64 += x86_64/vmx_set_nested_state_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_tsc_adjust_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_nested_tsc_scaling_test
TEST_GEN_PROGS_x86_64 += x86_64/xapic_ipi_test
TEST_GEN_PROGS_x86_64 += x86_64/xapic_state_test
TEST_GEN_PROGS_x86_64 += x86_64/xcr0_cpuid_test
TEST_GEN_PROGS_x86_64 += x86_64/xss_msr_test
TEST_GEN_PROGS_x86_64 += x86_64/debug_regs
TEST_GEN_PROGS_x86_64 += x86_64/tsc_msrs_test
TEST_GEN_PROGS_x86_64 += x86_64/vmx_pmu_caps_test
TEST_GEN_PROGS_x86_64 += x86_64/xen_shinfo_test
TEST_GEN_PROGS_x86_64 += x86_64/xen_vmcall_test
TEST_GEN_PROGS_x86_64 += x86_64/sev_migrate_tests
TEST_GEN_PROGS_x86_64 += x86_64/amx_test
TEST_GEN_PROGS_x86_64 += x86_64/max_vcpuid_cap_test
TEST_GEN_PROGS_x86_64 += x86_64/triple_fault_event_test
TEST_GEN_PROGS_x86_64 += x86_64/recalc_apic_map_test
TEST_GEN_PROGS_x86_64 += access_tracking_perf_test
TEST_GEN_PROGS_x86_64 += demand_paging_test
TEST_GEN_PROGS_x86_64 += dirty_log_test
TEST_GEN_PROGS_x86_64 += dirty_log_perf_test
TEST_GEN_PROGS_x86_64 += guest_print_test
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

# Compiled outputs used by test targets
TEST_GEN_PROGS_EXTENDED_x86_64 += x86_64/nx_huge_pages_test

TEST_GEN_PROGS_aarch64 += aarch64/aarch32_id_regs
TEST_GEN_PROGS_aarch64 += aarch64/arch_timer
TEST_GEN_PROGS_aarch64 += aarch64/debug-exceptions
TEST_GEN_PROGS_aarch64 += aarch64/get-reg-list
TEST_GEN_PROGS_aarch64 += aarch64/hypercalls
TEST_GEN_PROGS_aarch64 += aarch64/page_fault_test
TEST_GEN_PROGS_aarch64 += aarch64/psci_test
TEST_GEN_PROGS_aarch64 += aarch64/smccc_filter
TEST_GEN_PROGS_aarch64 += aarch64/vcpu_width_config
TEST_GEN_PROGS_aarch64 += aarch64/vgic_init
TEST_GEN_PROGS_aarch64 += aarch64/vgic_irq
TEST_GEN_PROGS_aarch64 += access_tracking_perf_test
TEST_GEN_PROGS_aarch64 += demand_paging_test
TEST_GEN_PROGS_aarch64 += dirty_log_test
TEST_GEN_PROGS_aarch64 += dirty_log_perf_test
TEST_GEN_PROGS_aarch64 += guest_print_test
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
TEST_GEN_PROGS_s390x += s390x/cmma_test
TEST_GEN_PROGS_s390x += s390x/debug_test
TEST_GEN_PROGS_s390x += demand_paging_test
TEST_GEN_PROGS_s390x += dirty_log_test
TEST_GEN_PROGS_s390x += guest_print_test
TEST_GEN_PROGS_s390x += kvm_create_max_vcpus
TEST_GEN_PROGS_s390x += kvm_page_table_test
TEST_GEN_PROGS_s390x += rseq_test
TEST_GEN_PROGS_s390x += set_memory_region_test
TEST_GEN_PROGS_s390x += kvm_binary_stats_test

TEST_GEN_PROGS_riscv += demand_paging_test
TEST_GEN_PROGS_riscv += dirty_log_test
TEST_GEN_PROGS_riscv += guest_print_test
TEST_GEN_PROGS_riscv += kvm_create_max_vcpus
TEST_GEN_PROGS_riscv += kvm_page_table_test
TEST_GEN_PROGS_riscv += set_memory_region_test
TEST_GEN_PROGS_riscv += kvm_binary_stats_test

TEST_PROGS += $(TEST_PROGS_$(ARCH_DIR))
TEST_GEN_PROGS += $(TEST_GEN_PROGS_$(ARCH_DIR))
TEST_GEN_PROGS_EXTENDED += $(TEST_GEN_PROGS_EXTENDED_$(ARCH_DIR))
LIBKVM += $(LIBKVM_$(ARCH_DIR))

OVERRIDE_TARGETS = 1

# lib.mak defines $(OUTPUT), prepends $(OUTPUT)/ to $(TEST_GEN_PROGS), and most
# importantly defines, i.e. overwrites, $(CC) (unless `make -e` or `make CC=`,
# which causes the environment variable to override the makefile).
include ../lib.mk

INSTALL_HDR_PATH = $(top_srcdir)/usr
LINUX_HDR_PATH = $(INSTALL_HDR_PATH)/include/
LINUX_TOOL_INCLUDE = $(top_srcdir)/tools/include
ifeq ($(ARCH),x86_64)
LINUX_TOOL_ARCH_INCLUDE = $(top_srcdir)/tools/arch/x86/include
else
LINUX_TOOL_ARCH_INCLUDE = $(top_srcdir)/tools/arch/$(ARCH)/include
endif
CFLAGS += -Wall -Wstrict-prototypes -Wuninitialized -O2 -g -std=gnu99 \
	-Wno-gnu-variable-sized-type-not-at-end -MD\
	-fno-builtin-memcmp -fno-builtin-memcpy -fno-builtin-memset \
	-fno-builtin-strnlen \
	-fno-stack-protector -fno-PIE -I$(LINUX_TOOL_INCLUDE) \
	-I$(LINUX_TOOL_ARCH_INCLUDE) -I$(LINUX_HDR_PATH) -Iinclude \
	-I$(<D) -Iinclude/$(ARCH_DIR) -I ../rseq -I.. $(EXTRA_CFLAGS) \
	$(KHDR_INCLUDES)
ifeq ($(ARCH),s390)
	CFLAGS += -march=z10
endif

no-pie-option := $(call try-run, echo 'int main(void) { return 0; }' | \
        $(CC) -Werror $(CFLAGS) -no-pie -x c - -o "$$TMP", -no-pie)

# On s390, build the testcases KVM-enabled
pgste-option = $(call try-run, echo 'int main(void) { return 0; }' | \
	$(CC) -Werror -Wl$(comma)--s390-pgste -x c - -o "$$TMP",-Wl$(comma)--s390-pgste)

LDLIBS += -ldl
LDFLAGS += -pthread $(no-pie-option) $(pgste-option)

LIBKVM_C := $(filter %.c,$(LIBKVM))
LIBKVM_S := $(filter %.S,$(LIBKVM))
LIBKVM_C_OBJ := $(patsubst %.c, $(OUTPUT)/%.o, $(LIBKVM_C))
LIBKVM_S_OBJ := $(patsubst %.S, $(OUTPUT)/%.o, $(LIBKVM_S))
LIBKVM_STRING_OBJ := $(patsubst %.c, $(OUTPUT)/%.o, $(LIBKVM_STRING))
LIBKVM_OBJS = $(LIBKVM_C_OBJ) $(LIBKVM_S_OBJ) $(LIBKVM_STRING_OBJ)

TEST_GEN_OBJ = $(patsubst %, %.o, $(TEST_GEN_PROGS))
TEST_GEN_OBJ += $(patsubst %, %.o, $(TEST_GEN_PROGS_EXTENDED))
TEST_DEP_FILES = $(patsubst %.o, %.d, $(TEST_GEN_OBJ))
TEST_DEP_FILES += $(patsubst %.o, %.d, $(LIBKVM_OBJS))
-include $(TEST_DEP_FILES)

$(TEST_GEN_PROGS) $(TEST_GEN_PROGS_EXTENDED): %: %.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH) $< $(LIBKVM_OBJS) $(LDLIBS) -o $@
$(TEST_GEN_OBJ): $(OUTPUT)/%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $< -o $@

EXTRA_CLEAN += $(LIBKVM_OBJS) $(TEST_DEP_FILES) $(TEST_GEN_OBJ) cscope.*

x := $(shell mkdir -p $(sort $(dir $(LIBKVM_C_OBJ) $(LIBKVM_S_OBJ))))
$(LIBKVM_C_OBJ): $(OUTPUT)/%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $< -o $@

$(LIBKVM_S_OBJ): $(OUTPUT)/%.o: %.S
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $< -o $@

# Compile the string overrides as freestanding to prevent the compiler from
# generating self-referential code, e.g. without "freestanding" the compiler may
# "optimize" memcmp() by invoking memcmp(), thus causing infinite recursion.
$(LIBKVM_STRING_OBJ): $(OUTPUT)/%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c -ffreestanding $< -o $@

x := $(shell mkdir -p $(sort $(dir $(TEST_GEN_PROGS))))
$(TEST_GEN_PROGS): $(LIBKVM_OBJS)
$(TEST_GEN_PROGS_EXTENDED): $(LIBKVM_OBJS)

cscope: include_paths = $(LINUX_TOOL_INCLUDE) $(LINUX_HDR_PATH) include lib ..
cscope:
	$(RM) cscope.*
	(find $(include_paths) -name '*.h' \
		-exec realpath --relative-base=$(PWD) {} \;; \
	find . -name '*.c' \
		-exec realpath --relative-base=$(PWD) {} \;) | sort -u > cscope.files
	cscope -b
