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
#include <sys/ioctl.h>

#include <linux/bitmap.h>

#include "kvm_test_harness.h"
#include "kvm_util.h"
#include "vmx.h"

static union perf_capabilities {
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
} host_cap;

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

static const union perf_capabilities format_caps = {
	.lbr_format = -1,
	.pebs_format = -1,
};

static void guest_test_perf_capabilities_gp(uint64_t val)
{
	uint8_t vector = wrmsr_safe(MSR_IA32_PERF_CAPABILITIES, val);

	__GUEST_ASSERT(vector == GP_VECTOR,
		       "Expected #GP for value '0x%lx', got vector '0x%x'",
		       val, vector);
}

static void guest_code(uint64_t current_val)
{
	int i;

	guest_test_perf_capabilities_gp(current_val);
	guest_test_perf_capabilities_gp(0);

	for (i = 0; i < 64; i++)
		guest_test_perf_capabilities_gp(current_val ^ BIT_ULL(i));

	GUEST_DONE();
}

KVM_ONE_VCPU_TEST_SUITE(vmx_pmu_caps);

/*
 * Verify that guest WRMSRs to PERF_CAPABILITIES #GP regardless of the value
 * written, that the guest always sees the userspace controlled value, and that
 * PERF_CAPABILITIES is immutable after KVM_RUN.
 */
KVM_ONE_VCPU_TEST(vmx_pmu_caps, guest_wrmsr_perf_capabilities, guest_code)
{
	struct ucall uc;
	int r, i;

	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, host_cap.capabilities);

	vcpu_args_set(vcpu, 1, host_cap.capabilities);
	vcpu_run(vcpu);

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
		break;
	case UCALL_DONE:
		break;
	default:
		TEST_FAIL("Unexpected ucall: %lu", uc.cmd);
	}

	TEST_ASSERT_EQ(vcpu_get_msr(vcpu, MSR_IA32_PERF_CAPABILITIES),
			host_cap.capabilities);

	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, host_cap.capabilities);

	r = _vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, 0);
	TEST_ASSERT(!r, "Post-KVM_RUN write '0' didn't fail");

	for (i = 0; i < 64; i++) {
		r = _vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES,
				  host_cap.capabilities ^ BIT_ULL(i));
		TEST_ASSERT(!r, "Post-KVM_RUN write '0x%llx'didn't fail",
			    host_cap.capabilities ^ BIT_ULL(i));
	}
}

/*
 * Verify KVM allows writing PERF_CAPABILITIES with all KVM-supported features
 * enabled, as well as '0' (to disable all features).
 */
KVM_ONE_VCPU_TEST(vmx_pmu_caps, basic_perf_capabilities, guest_code)
{
	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, 0);
	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, host_cap.capabilities);
}

KVM_ONE_VCPU_TEST(vmx_pmu_caps, fungible_perf_capabilities, guest_code)
{
	const uint64_t fungible_caps = host_cap.capabilities & ~immutable_caps.capabilities;
	int bit;

	for_each_set_bit(bit, &fungible_caps, 64) {
		vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, BIT_ULL(bit));
		vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES,
			     host_cap.capabilities & ~BIT_ULL(bit));
	}
	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, host_cap.capabilities);
}

/*
 * Verify KVM rejects attempts to set unsupported and/or immutable features in
 * PERF_CAPABILITIES.  Note, LBR format and PEBS format need to be validated
 * separately as they are multi-bit values, e.g. toggling or setting a single
 * bit can generate a false positive without dedicated safeguards.
 */
KVM_ONE_VCPU_TEST(vmx_pmu_caps, immutable_perf_capabilities, guest_code)
{
	const uint64_t reserved_caps = (~host_cap.capabilities |
					immutable_caps.capabilities) &
				       ~format_caps.capabilities;
	union perf_capabilities val = host_cap;
	int r, bit;

	for_each_set_bit(bit, &reserved_caps, 64) {
		r = _vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES,
				  host_cap.capabilities ^ BIT_ULL(bit));
		TEST_ASSERT(!r, "%s immutable feature 0x%llx (bit %d) didn't fail",
			    host_cap.capabilities & BIT_ULL(bit) ? "Setting" : "Clearing",
			    BIT_ULL(bit), bit);
	}

	/*
	 * KVM only supports the host's native LBR format, as well as '0' (to
	 * disable LBR support).  Verify KVM rejects all other LBR formats.
	 */
	for (val.lbr_format = 1; val.lbr_format; val.lbr_format++) {
		if (val.lbr_format == host_cap.lbr_format)
			continue;

		r = _vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, val.capabilities);
		TEST_ASSERT(!r, "Bad LBR FMT = 0x%x didn't fail, host = 0x%x",
			    val.lbr_format, host_cap.lbr_format);
	}

	/* Ditto for the PEBS format. */
	for (val.pebs_format = 1; val.pebs_format; val.pebs_format++) {
		if (val.pebs_format == host_cap.pebs_format)
			continue;

		r = _vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, val.capabilities);
		TEST_ASSERT(!r, "Bad PEBS FMT = 0x%x didn't fail, host = 0x%x",
			    val.pebs_format, host_cap.pebs_format);
	}
}

/*
 * Test that LBR MSRs are writable when LBRs are enabled, and then verify that
 * disabling the vPMU via CPUID also disables LBR support.  Set bits 2:0 of
 * LBR_TOS as those bits are writable across all uarch implementations (arch
 * LBRs will need to poke a different MSR).
 */
KVM_ONE_VCPU_TEST(vmx_pmu_caps, lbr_perf_capabilities, guest_code)
{
	int r;

	if (!host_cap.lbr_format)
		return;

	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, host_cap.capabilities);
	vcpu_set_msr(vcpu, MSR_LBR_TOS, 7);

	vcpu_clear_cpuid_entry(vcpu, X86_PROPERTY_PMU_VERSION.function);

	r = _vcpu_set_msr(vcpu, MSR_LBR_TOS, 7);
	TEST_ASSERT(!r, "Writing LBR_TOS should fail after disabling vPMU");
}

KVM_ONE_VCPU_TEST(vmx_pmu_caps, perf_capabilities_unsupported, guest_code)
{
	uint64_t val;
	int i, r;

	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, host_cap.capabilities);
	val = vcpu_get_msr(vcpu, MSR_IA32_PERF_CAPABILITIES);
	TEST_ASSERT_EQ(val, host_cap.capabilities);

	vcpu_clear_cpuid_feature(vcpu, X86_FEATURE_PDCM);

	val = vcpu_get_msr(vcpu, MSR_IA32_PERF_CAPABILITIES);
	TEST_ASSERT_EQ(val, 0);

	vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, 0);

	for (i = 0; i < 64; i++) {
		r = _vcpu_set_msr(vcpu, MSR_IA32_PERF_CAPABILITIES, BIT_ULL(i));
		TEST_ASSERT(!r, "Setting PERF_CAPABILITIES bit %d (= 0x%llx) should fail without PDCM",
			    i, BIT_ULL(i));
	}
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_is_pmu_enabled());
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_PDCM));

	TEST_REQUIRE(kvm_cpu_has_p(X86_PROPERTY_PMU_VERSION));
	TEST_REQUIRE(kvm_cpu_property(X86_PROPERTY_PMU_VERSION) > 0);

	host_cap.capabilities = kvm_get_feature_msr(MSR_IA32_PERF_CAPABILITIES);

	TEST_ASSERT(host_cap.full_width_write,
		    "Full-width writes should always be supported");

	return test_harness_run(argc, argv);
}
