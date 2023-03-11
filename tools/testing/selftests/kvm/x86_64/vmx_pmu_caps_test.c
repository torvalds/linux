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

#include <linux/bitmap.h>

#include "kvm_util.h"
#include "vmx.h"

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

/*
 * The LBR format and most PEBS features are immutable, all other features are
 * fungible (if supported by the host and KVM).
 */
static const union perf_capabilities immutable_caps = {
	.lbr_format = -1,
	.pebs_trap  = 1,
	.pebs_arch_reg = 1,
	.pebs_format = -1,
	.pebs_baseline = 1,
};

static void guest_code(void)
{
	wrmsr(MSR_IA32_PERF_CAPABILITIES, PMU_CAP_LBR_FMT);
}

/*
 * Verify KVM allows writing PERF_CAPABILITIES with all KVM-supported features
 * enabled, as well as '0' (to disable all features).
 */
static void test_basic_perf_capabilities(union perf_capabilities host_cap)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = vm_create_with_one_vcpu(&vcpu, NULL);

	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, 0);
	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, host_cap.capabilities);

	kvm_vm_free(vm);
}

static void test_fungible_perf_capabilities(union perf_capabilities host_cap)
{
	const uint64_t fungible_caps = host_cap.capabilities & ~immutable_caps.capabilities;

	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	int bit;

	for_each_set_bit(bit, &fungible_caps, 64) {
		vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, BIT_ULL(bit));
		vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES,
			     host_cap.capabilities & ~BIT_ULL(bit));
	}
	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, host_cap.capabilities);

	/* check whatever we write with KVM_SET_MSR is _not_ modified */
	vcpu_run(vcpu);
	ASSERT_EQ(vcpu_get_msr(vcpu, MSR_IA32_PERF_CAPABILITIES), host_cap.capabilities);

	kvm_vm_free(vm);
}

static void test_immutable_perf_capabilities(union perf_capabilities host_cap)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm = vm_create_with_one_vcpu(&vcpu, NULL);
	uint64_t val;
	int ret;

	/*
	 * KVM only supports the host's native LBR format, as well as '0' (to
	 * disable LBR support).  Verify KVM rejects all other LBR formats.
	 */
	for (val = 1; val <= PMU_CAP_LBR_FMT; val++) {
		if (val == (host_cap.capabilities & PMU_CAP_LBR_FMT))
			continue;

		ret = _vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, val);
		TEST_ASSERT(!ret, "Bad LBR FMT = 0x%lx didn't fail", val);
	}
	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	union perf_capabilities host_cap;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_PDCM));

	TEST_REQUIRE(kvm_cpu_has_p(X86_PROPERTY_PMU_VERSION));
	TEST_REQUIRE(kvm_cpu_property(X86_PROPERTY_PMU_VERSION) > 0);

	host_cap.capabilities = kvm_get_feature_msr(MSR_IA32_PERF_CAPABILITIES);

	TEST_ASSERT(host_cap.full_width_write,
		    "Full-width writes should always be supported");

	test_basic_perf_capabilities(host_cap);
	test_fungible_perf_capabilities(host_cap);
	test_immutable_perf_capabilities(host_cap);

	printf("Completed perf capability tests.\n");
}
