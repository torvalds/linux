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

#define VCPU_ID	      1
#define MSR_BITS      64

#define X86_FEATURE_XSAVES	(1<<3)

int main(int argc, char *argv[])
{
	struct kvm_cpuid_entry2 *entry;
	bool xss_supported = false;
	bool xss_in_msr_list;
	struct kvm_vm *vm;
	uint64_t xss_val;
	int i, r;

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, 0);

	if (kvm_get_cpuid_max_basic() >= 0xd) {
		entry = kvm_get_supported_cpuid_index(0xd, 1);
		xss_supported = entry && !!(entry->eax & X86_FEATURE_XSAVES);
	}
	if (!xss_supported) {
		print_skip("IA32_XSS is not supported by the vCPU");
		exit(KSFT_SKIP);
	}

	xss_val = vcpu_get_msr(vm, VCPU_ID, MSR_IA32_XSS);
	TEST_ASSERT(xss_val == 0,
		    "MSR_IA32_XSS should be initialized to zero\n");

	vcpu_set_msr(vm, VCPU_ID, MSR_IA32_XSS, xss_val);
	/*
	 * At present, KVM only supports a guest IA32_XSS value of 0. Verify
	 * that trying to set the guest IA32_XSS to an unsupported value fails.
	 * Also, in the future when a non-zero value succeeds check that
	 * IA32_XSS is in the list of MSRs to save/restore.
	 */
	xss_in_msr_list = kvm_msr_is_in_save_restore_list(MSR_IA32_XSS);
	for (i = 0; i < MSR_BITS; ++i) {
		r = _vcpu_set_msr(vm, VCPU_ID, MSR_IA32_XSS, 1ull << i);

		TEST_ASSERT(r == 0 || xss_in_msr_list,
			    "IA32_XSS was able to be set, but was not in save/restore list");
	}

	kvm_vm_free(vm);
}
