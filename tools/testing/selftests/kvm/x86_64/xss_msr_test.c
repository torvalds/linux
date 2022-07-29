// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019, Google LLC.
 *
 * Tests for the IA32_XSS MSR.
 */

#define _GNU_SOURCE /* for program_invocation_short_name */
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "vmx.h"

#define MSR_BITS      64

int main(int argc, char *argv[])
{
	bool xss_in_msr_list;
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	uint64_t xss_val;
	int i, r;

	/* Create VM */
	vm = vm_create_with_one_vcpu(&vcpu, NULL);

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_XSAVES));

	xss_val = vcpu_get_msr(vcpu, MSR_IA32_XSS);
	TEST_ASSERT(xss_val == 0,
		    "MSR_IA32_XSS should be initialized to zero\n");

	vcpu_set_msr(vcpu, MSR_IA32_XSS, xss_val);

	/*
	 * At present, KVM only supports a guest IA32_XSS value of 0. Verify
	 * that trying to set the guest IA32_XSS to an unsupported value fails.
	 * Also, in the future when a non-zero value succeeds check that
	 * IA32_XSS is in the list of MSRs to save/restore.
	 */
	xss_in_msr_list = kvm_msr_is_in_save_restore_list(MSR_IA32_XSS);
	for (i = 0; i < MSR_BITS; ++i) {
		r = _vcpu_set_msr(vcpu, MSR_IA32_XSS, 1ull << i);

		/*
		 * Setting a list of MSRs returns the entry that "faulted", or
		 * the last entry +1 if all MSRs were successfully written.
		 */
		TEST_ASSERT(!r || r == 1, KVM_IOCTL_ERROR(KVM_SET_MSRS, r));
		TEST_ASSERT(r != 1 || xss_in_msr_list,
			    "IA32_XSS was able to be set, but was not in save/restore list");
	}

	kvm_vm_free(vm);
}
