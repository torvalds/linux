// SPDX-License-Identifier: GPL-2.0-only
/*
 * hello_el2 - Basic KVM selftest for VM running at EL2 with E2H=RES1
 *
 * Copyright 2025 Google LLC
 */
#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"
#include "ucall.h"

#include <asm/sysreg.h>

static void guest_code(void)
{
	u64 mmfr0 = read_sysreg_s(SYS_ID_AA64MMFR0_EL1);
	u64 mmfr1 = read_sysreg_s(SYS_ID_AA64MMFR1_EL1);
	u64 mmfr4 = read_sysreg_s(SYS_ID_AA64MMFR4_EL1);
	u8 e2h0 = SYS_FIELD_GET(ID_AA64MMFR4_EL1, E2H0, mmfr4);

	GUEST_ASSERT_EQ(get_current_el(), 2);
	GUEST_ASSERT(read_sysreg(hcr_el2) & HCR_EL2_E2H);
	GUEST_ASSERT_EQ(SYS_FIELD_GET(ID_AA64MMFR1_EL1, VH, mmfr1),
			ID_AA64MMFR1_EL1_VH_IMP);

	/*
	 * Traps of the complete ID register space are IMPDEF without FEAT_FGT,
	 * which is really annoying to deal with in KVM describing E2H as RES1.
	 *
	 * If the implementation doesn't honor the trap then expect the register
	 * to return all zeros.
	 */
	if (e2h0 == ID_AA64MMFR4_EL1_E2H0_IMP)
		GUEST_ASSERT_EQ(SYS_FIELD_GET(ID_AA64MMFR0_EL1, FGT, mmfr0),
				ID_AA64MMFR0_EL1_FGT_NI);
	else
		GUEST_ASSERT_EQ(e2h0, ID_AA64MMFR4_EL1_E2H0_NI_NV1);

	GUEST_DONE();
}

int main(void)
{
	struct kvm_vcpu_init init;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;

	TEST_REQUIRE(kvm_check_cap(KVM_CAP_ARM_EL2));

	vm = vm_create(1);

	kvm_get_default_vcpu_target(vm, &init);
	init.features[0] |= BIT(KVM_ARM_VCPU_HAS_EL2);
	vcpu = aarch64_vcpu_add(vm, 0, &init, guest_code);
	kvm_arch_vm_finalize_vcpus(vm);

	vcpu_run(vcpu);
	switch (get_ucall(vcpu, &uc)) {
	case UCALL_DONE:
		break;
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
		break;
	default:
		TEST_FAIL("Unhandled ucall: %ld\n", uc.cmd);
	}

	kvm_vm_free(vm);
	return 0;
}
