// SPDX-License-Identifier: GPL-2.0-only
/*
 * VMX control MSR test
 *
 * Copyright (C) 2022 Google LLC.
 *
 * Tests for KVM ownership of bits in the VMX entry/exit control MSRs. Checks
 * that KVM will set owned bits where appropriate, and will not if
 * KVM_X86_QUIRK_TWEAK_VMX_CTRL_MSRS is disabled.
 */
#include <linux/bitmap.h>
#include "kvm_util.h"
#include "vmx.h"

static void vmx_fixed1_msr_test(struct kvm_vcpu *vcpu, uint32_t msr_index,
				  uint64_t mask)
{
	uint64_t val = vcpu_get_msr(vcpu, msr_index);
	uint64_t bit;

	mask &= val;

	for_each_set_bit(bit, &mask, 64) {
		vcpu_set_msr(vcpu, msr_index, val & ~BIT_ULL(bit));
		vcpu_set_msr(vcpu, msr_index, val);
	}
}

static void vmx_fixed0_msr_test(struct kvm_vcpu *vcpu, uint32_t msr_index,
				uint64_t mask)
{
	uint64_t val = vcpu_get_msr(vcpu, msr_index);
	uint64_t bit;

	mask = ~mask | val;

	for_each_clear_bit(bit, &mask, 64) {
		vcpu_set_msr(vcpu, msr_index, val | BIT_ULL(bit));
		vcpu_set_msr(vcpu, msr_index, val);
	}
}

static void vmx_fixed0and1_msr_test(struct kvm_vcpu *vcpu, uint32_t msr_index)
{
	vmx_fixed0_msr_test(vcpu, msr_index, GENMASK_ULL(31, 0));
	vmx_fixed1_msr_test(vcpu, msr_index, GENMASK_ULL(63, 32));
}

static void vmx_save_restore_msrs_test(struct kvm_vcpu *vcpu)
{
	vcpu_set_msr(vcpu, MSR_IA32_VMX_VMCS_ENUM, 0);
	vcpu_set_msr(vcpu, MSR_IA32_VMX_VMCS_ENUM, -1ull);

	vmx_fixed1_msr_test(vcpu, MSR_IA32_VMX_BASIC,
			    BIT_ULL(49) | BIT_ULL(54) | BIT_ULL(55));

	vmx_fixed1_msr_test(vcpu, MSR_IA32_VMX_MISC,
			    BIT_ULL(5) | GENMASK_ULL(8, 6) | BIT_ULL(14) |
			    BIT_ULL(15) | BIT_ULL(28) | BIT_ULL(29) | BIT_ULL(30));

	vmx_fixed0and1_msr_test(vcpu, MSR_IA32_VMX_PROCBASED_CTLS2);
	vmx_fixed1_msr_test(vcpu, MSR_IA32_VMX_EPT_VPID_CAP, -1ull);
	vmx_fixed0and1_msr_test(vcpu, MSR_IA32_VMX_TRUE_PINBASED_CTLS);
	vmx_fixed0and1_msr_test(vcpu, MSR_IA32_VMX_TRUE_PROCBASED_CTLS);
	vmx_fixed0and1_msr_test(vcpu, MSR_IA32_VMX_TRUE_EXIT_CTLS);
	vmx_fixed0and1_msr_test(vcpu, MSR_IA32_VMX_TRUE_ENTRY_CTLS);
	vmx_fixed1_msr_test(vcpu, MSR_IA32_VMX_VMFUNC, -1ull);
}

int main(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_DISABLE_QUIRKS2));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX));

	/* No need to actually do KVM_RUN, thus no guest code. */
	vm = vm_create_with_one_vcpu(&vcpu, NULL);

	vmx_save_restore_msrs_test(vcpu);

	kvm_vm_free(vm);
}
