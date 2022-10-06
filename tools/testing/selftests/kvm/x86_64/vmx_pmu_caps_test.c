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

#define PMU_CAP_FW_WRITES	(1ULL << 13)
#define PMU_CAP_LBR_FMT		0x3f

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
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	int ret;
	union perf_capabilities host_cap;
	uint64_t val;

	host_cap.capabilities = kvm_get_feature_msr(MSR_IA32_PERF_CAPABILITIES);
	host_cap.capabilities &= (PMU_CAP_FW_WRITES | PMU_CAP_LBR_FMT);

	/* Create VM */
	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_PDCM));

	TEST_REQUIRE(kvm_cpu_has_p(X86_PROPERTY_PMU_VERSION));
	TEST_REQUIRE(kvm_cpu_property(X86_PROPERTY_PMU_VERSION) > 0);

	/* testcase 1, set capabilities when we have PDCM bit */
	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, PMU_CAP_FW_WRITES);

	/* check capabilities can be retrieved with KVM_GET_MSR */
	ASSERT_EQ(vcpu_get_msr(vcpu, MSR_IA32_PERF_CAPABILITIES), PMU_CAP_FW_WRITES);

	/* check whatever we write with KVM_SET_MSR is _not_ modified */
	vcpu_run(vcpu);
	ASSERT_EQ(vcpu_get_msr(vcpu, MSR_IA32_PERF_CAPABILITIES), PMU_CAP_FW_WRITES);

	/* testcase 2, check valid LBR formats are accepted */
	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, 0);
	ASSERT_EQ(vcpu_get_msr(vcpu, MSR_IA32_PERF_CAPABILITIES), 0);

	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, host_cap.lbr_format);
	ASSERT_EQ(vcpu_get_msr(vcpu, MSR_IA32_PERF_CAPABILITIES), (u64)host_cap.lbr_format);

	/*
	 * Testcase 3, check that an "invalid" LBR format is rejected.  Only an
	 * exact match of the host's format (and 0/disabled) is allowed.
	 */
	for (val = 1; val <= PMU_CAP_LBR_FMT; val++) {
		if (val == (host_cap.capabilities & PMU_CAP_LBR_FMT))
			continue;

		ret = _vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, val);
		TEST_ASSERT(!ret, "Bad LBR FMT = 0x%lx didn't fail", val);
	}

	printf("Completed perf capability tests.\n");
	kvm_vm_free(vm);
}
