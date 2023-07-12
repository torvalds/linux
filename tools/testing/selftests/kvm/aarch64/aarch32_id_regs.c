// SPDX-License-Identifier: GPL-2.0-only
/*
 * aarch32_id_regs - Test for ID register behavior on AArch64-only systems
 *
 * Copyright (c) 2022 Google LLC.
 *
 * Test that KVM handles the AArch64 views of the AArch32 ID registers as RAZ
 * and WI from userspace.
 */

#include <stdint.h>

#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"
#include <linux/bitfield.h>

#define BAD_ID_REG_VAL	0x1badc0deul

#define GUEST_ASSERT_REG_RAZ(reg)	GUEST_ASSERT_EQ(read_sysreg_s(reg), 0)

static void guest_main(void)
{
	GUEST_ASSERT_REG_RAZ(SYS_ID_PFR0_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_PFR1_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_DFR0_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_AFR0_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_MMFR0_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_MMFR1_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_MMFR2_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_MMFR3_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_ISAR0_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_ISAR1_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_ISAR2_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_ISAR3_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_ISAR4_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_ISAR5_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_MMFR4_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_ISAR6_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_MVFR0_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_MVFR1_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_MVFR2_EL1);
	GUEST_ASSERT_REG_RAZ(sys_reg(3, 0, 0, 3, 3));
	GUEST_ASSERT_REG_RAZ(SYS_ID_PFR2_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_DFR1_EL1);
	GUEST_ASSERT_REG_RAZ(SYS_ID_MMFR5_EL1);
	GUEST_ASSERT_REG_RAZ(sys_reg(3, 0, 0, 3, 7));

	GUEST_DONE();
}

static void test_guest_raz(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

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
}

static uint64_t raz_wi_reg_ids[] = {
	KVM_ARM64_SYS_REG(SYS_ID_PFR0_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_PFR1_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_DFR0_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_MMFR0_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_MMFR1_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_MMFR2_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_MMFR3_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_ISAR0_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_ISAR1_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_ISAR2_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_ISAR3_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_ISAR4_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_ISAR5_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_MMFR4_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_ISAR6_EL1),
	KVM_ARM64_SYS_REG(SYS_MVFR0_EL1),
	KVM_ARM64_SYS_REG(SYS_MVFR1_EL1),
	KVM_ARM64_SYS_REG(SYS_MVFR2_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_PFR2_EL1),
	KVM_ARM64_SYS_REG(SYS_ID_MMFR5_EL1),
};

static void test_user_raz_wi(struct kvm_vcpu *vcpu)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(raz_wi_reg_ids); i++) {
		uint64_t reg_id = raz_wi_reg_ids[i];
		uint64_t val;

		vcpu_get_reg(vcpu, reg_id, &val);
		TEST_ASSERT_EQ(val, 0);

		/*
		 * Expect the ioctl to succeed with no effect on the register
		 * value.
		 */
		vcpu_set_reg(vcpu, reg_id, BAD_ID_REG_VAL);

		vcpu_get_reg(vcpu, reg_id, &val);
		TEST_ASSERT_EQ(val, 0);
	}
}

static uint64_t raz_invariant_reg_ids[] = {
	KVM_ARM64_SYS_REG(SYS_ID_AFR0_EL1),
	KVM_ARM64_SYS_REG(sys_reg(3, 0, 0, 3, 3)),
	KVM_ARM64_SYS_REG(SYS_ID_DFR1_EL1),
	KVM_ARM64_SYS_REG(sys_reg(3, 0, 0, 3, 7)),
};

static void test_user_raz_invariant(struct kvm_vcpu *vcpu)
{
	int i, r;

	for (i = 0; i < ARRAY_SIZE(raz_invariant_reg_ids); i++) {
		uint64_t reg_id = raz_invariant_reg_ids[i];
		uint64_t val;

		vcpu_get_reg(vcpu, reg_id, &val);
		TEST_ASSERT_EQ(val, 0);

		r = __vcpu_set_reg(vcpu, reg_id, BAD_ID_REG_VAL);
		TEST_ASSERT(r < 0 && errno == EINVAL,
			    "unexpected KVM_SET_ONE_REG error: r=%d, errno=%d", r, errno);

		vcpu_get_reg(vcpu, reg_id, &val);
		TEST_ASSERT_EQ(val, 0);
	}
}



static bool vcpu_aarch64_only(struct kvm_vcpu *vcpu)
{
	uint64_t val, el0;

	vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_ID_AA64PFR0_EL1), &val);

	el0 = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64PFR0_EL0), val);
	return el0 == ID_AA64PFR0_ELx_64BIT_ONLY;
}

int main(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	vm = vm_create_with_one_vcpu(&vcpu, guest_main);

	TEST_REQUIRE(vcpu_aarch64_only(vcpu));

	test_user_raz_wi(vcpu);
	test_user_raz_invariant(vcpu);
	test_guest_raz(vcpu);

	kvm_vm_free(vm);
}
