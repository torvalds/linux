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

bool is_supported_msr(u32 msr_index)
{
	struct kvm_msr_list *list;
	bool found = false;
	int i;

	list = kvm_get_msr_index_list();
	for (i = 0; i < list->nmsrs; ++i) {
		if (list->indices[i] == msr_index) {
			found = true;
			break;
		}
	}

	free(list);
	return found;
}

int main(int argc, char *argv[])
{
	struct kvm_cpuid_entry2 *entry;
	bool xss_supported = false;
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
	 * IA32_XSS is in the KVM_GET_MSR_INDEX_LIST.
	 */
	for (i = 0; i < MSR_BITS; ++i) {
		r = _vcpu_set_msr(vm, VCPU_ID, MSR_IA32_XSS, 1ull << i);
		TEST_ASSERT(r == 0 || is_supported_msr(MSR_IA32_XSS),
			    "IA32_XSS was able to be set, but was not found in KVM_GET_MSR_INDEX_LIST.\n");
	}

	kvm_vm_free(vm);
}
