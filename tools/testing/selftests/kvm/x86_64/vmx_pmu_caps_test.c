// SPDX-License-Identifier: GPL-2.0
/*
 * Test for VMX-pmu perf capability msr
 *
 * Copyright (C) 2021 Intel Corporation
 *
 * Test to check the effect of various CPUID settings on
 * MSR_IA32_PERF_CAPABILITIES MSR, and check that what
 * we write with KVM_SET_MSR is _not_ modified by the guest
 * and check it can be retrieved with KVM_GET_MSR, also test
 * the invalid LBR formats are rejected.
 */

#define _GNU_SOURCE /* for program_invocation_short_name */
#include <sys/ioctl.h>

#include "kvm_util.h"
#include "vmx.h"

#define VCPU_ID	      0

#define X86_FEATURE_PDCM	(1<<15)
#define PMU_CAP_FW_WRITES	(1ULL << 13)
#define PMU_CAP_LBR_FMT		0x3f

union cpuid10_eax {
	struct {
		unsigned int version_id:8;
		unsigned int num_counters:8;
		unsigned int bit_width:8;
		unsigned int mask_length:8;
	} split;
	unsigned int full;
};

union perf_capabilities {
	struct {
		u64	lbr_format:6;
		u64	pebs_trap:1;
		u64	pebs_arch_reg:1;
		u64	pebs_format:4;
		u64	smm_freeze:1;
		u64	full_width_write:1;
		u64 pebs_baseline:1;
		u64	perf_metrics:1;
		u64	pebs_output_pt_available:1;
		u64	anythread_deprecated:1;
	};
	u64	capabilities;
};

static void guest_code(void)
{
	wrmsr(MSR_IA32_PERF_CAPABILITIES, PMU_CAP_LBR_FMT);
}

int main(int argc, char *argv[])
{
	struct kvm_cpuid2 *cpuid;
	struct kvm_cpuid_entry2 *entry_1_0;
	struct kvm_cpuid_entry2 *entry_a_0;
	bool pdcm_supported = false;
	struct kvm_vm *vm;
	int ret;
	union cpuid10_eax eax;
	union perf_capabilities host_cap;

	host_cap.capabilities = kvm_get_feature_msr(MSR_IA32_PERF_CAPABILITIES);
	host_cap.capabilities &= (PMU_CAP_FW_WRITES | PMU_CAP_LBR_FMT);

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code);
	cpuid = kvm_get_supported_cpuid();

	if (kvm_get_cpuid_max_basic() >= 0xa) {
		entry_1_0 = kvm_get_supported_cpuid_index(1, 0);
		entry_a_0 = kvm_get_supported_cpuid_index(0xa, 0);
		pdcm_supported = entry_1_0 && !!(entry_1_0->ecx & X86_FEATURE_PDCM);
		eax.full = entry_a_0->eax;
	}
	if (!pdcm_supported) {
		print_skip("MSR_IA32_PERF_CAPABILITIES is not supported by the vCPU");
		exit(KSFT_SKIP);
	}
	if (!eax.split.version_id) {
		print_skip("PMU is not supported by the vCPU");
		exit(KSFT_SKIP);
	}

	/* testcase 1, set capabilities when we have PDCM bit */
	vcpu_set_cpuid(vm, VCPU_ID, cpuid);
	vcpu_set_msr(vm, 0, MSR_IA32_PERF_CAPABILITIES, PMU_CAP_FW_WRITES);

	/* check capabilities can be retrieved with KVM_GET_MSR */
	ASSERT_EQ(vcpu_get_msr(vm, VCPU_ID, MSR_IA32_PERF_CAPABILITIES), PMU_CAP_FW_WRITES);

	/* check whatever we write with KVM_SET_MSR is _not_ modified */
	vcpu_run(vm, VCPU_ID);
	ASSERT_EQ(vcpu_get_msr(vm, VCPU_ID, MSR_IA32_PERF_CAPABILITIES), PMU_CAP_FW_WRITES);

	/* testcase 2, check valid LBR formats are accepted */
	vcpu_set_msr(vm, 0, MSR_IA32_PERF_CAPABILITIES, 0);
	ASSERT_EQ(vcpu_get_msr(vm, VCPU_ID, MSR_IA32_PERF_CAPABILITIES), 0);

	vcpu_set_msr(vm, 0, MSR_IA32_PERF_CAPABILITIES, host_cap.lbr_format);
	ASSERT_EQ(vcpu_get_msr(vm, VCPU_ID, MSR_IA32_PERF_CAPABILITIES), (u64)host_cap.lbr_format);

	/* testcase 3, check invalid LBR format is rejected */
	/* Note, on Arch LBR capable platforms, LBR_FMT in perf capability msr is 0x3f,
	 * to avoid the failure, use a true invalid format 0x30 for the test. */
	ret = _vcpu_set_msr(vm, 0, MSR_IA32_PERF_CAPABILITIES, 0x30);
	TEST_ASSERT(ret == 0, "Bad PERF_CAPABILITIES didn't fail.");

	printf("Completed perf capability tests.\n");
	kvm_vm_free(vm);
}
