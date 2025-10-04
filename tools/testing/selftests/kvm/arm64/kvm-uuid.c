// SPDX-License-Identifier: GPL-2.0

// Check that nobody has tampered with KVM's UID

#include <errno.h>
#include <linux/arm-smccc.h>
#include <asm/kvm.h>
#include <kvm_util.h>

#include "processor.h"

/*
 * Do NOT redefine these constants, or try to replace them with some
 * "common" version. They are hardcoded here to detect any potential
 * breakage happening in the rest of the kernel.
 *
 * KVM UID value: 28b46fb6-2ec5-11e9-a9ca-4b564d003a74
 */
#define ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_0	0xb66fb428U
#define ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_1	0xe911c52eU
#define ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_2	0x564bcaa9U
#define ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_3	0x743a004dU

static void guest_code(void)
{
	struct arm_smccc_res res = {};

	do_smccc(ARM_SMCCC_VENDOR_HYP_CALL_UID_FUNC_ID, 0, 0, 0, 0, 0, 0, 0, &res);

	__GUEST_ASSERT(res.a0 == ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_0 &&
		       res.a1 == ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_1 &&
		       res.a2 == ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_2 &&
		       res.a3 == ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_3,
		       "Unexpected KVM-specific UID %lx %lx %lx %lx\n", res.a0, res.a1, res.a2, res.a3);
	GUEST_DONE();
}

int main (int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	bool guest_done = false;

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	while (!guest_done) {
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			break;
		case UCALL_DONE:
			guest_done = true;
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		case UCALL_PRINTF:
			printf("%s", uc.buffer);
			break;
		default:
			TEST_FAIL("Unexpected guest exit");
		}
	}

	kvm_vm_free(vm);

	return 0;
}
