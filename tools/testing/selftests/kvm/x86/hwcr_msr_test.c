// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023, Google LLC.
 */
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "vmx.h"

void test_hwcr_bit(struct kvm_vcpu *vcpu, unsigned int bit)
{
	const u64 ignored = BIT_ULL(3) | BIT_ULL(6) | BIT_ULL(8);
	u64 valid = BIT_ULL(18) | BIT_ULL(24);
	u64 val = BIT_ULL(bit);
	u64 actual;
	u64 legal;
	int r;

	if (kvm_cpu_has(X86_FEATURE_GP_ON_USER_CPUID))
		valid |= BIT_ULL(35);

	legal = ignored | valid;

	r = _vcpu_set_msr(vcpu, MSR_K7_HWCR, val);
	TEST_ASSERT(val & ~legal ? !r : r == 1,
		    "Expected KVM_SET_MSRS(MSR_K7_HWCR) = 0x%lx to %s",
		    val, val & ~legal ? "fail" : "succeed");

	actual = vcpu_get_msr(vcpu, MSR_K7_HWCR);
	TEST_ASSERT(actual == (val & valid),
		    "Bit %u: unexpected HWCR 0x%lx; expected 0x%lx",
		    bit, actual, (val & valid));

	vcpu_set_msr(vcpu, MSR_K7_HWCR, 0);
}

int main(int argc, char *argv[])
{
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	unsigned int bit;

	vm = vm_create_with_one_vcpu(&vcpu, NULL);

	for (bit = 0; bit < BITS_PER_LONG; bit++)
		test_hwcr_bit(vcpu, bit);

	kvm_vm_free(vm);
}
