// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020, Red Hat, Inc.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

static bool is_kvm_controlled_msr(uint32_t msr)
{
	return msr == MSR_IA32_VMX_CR0_FIXED1 || msr == MSR_IA32_VMX_CR4_FIXED1;
}

/*
 * For VMX MSRs with a "true" variant, KVM requires userspace to set the "true"
 * MSR, and doesn't allow setting the hidden version.
 */
static bool is_hidden_vmx_msr(uint32_t msr)
{
	switch (msr) {
	case MSR_IA32_VMX_PINBASED_CTLS:
	case MSR_IA32_VMX_PROCBASED_CTLS:
	case MSR_IA32_VMX_EXIT_CTLS:
	case MSR_IA32_VMX_ENTRY_CTLS:
		return true;
	default:
		return false;
	}
}

static bool is_quirked_msr(uint32_t msr)
{
	return msr != MSR_AMD64_DE_CFG;
}

static void test_feature_msr(uint32_t msr)
{
	const uint64_t supported_mask = kvm_get_feature_msr(msr);
	uint64_t reset_value = is_quirked_msr(msr) ? supported_mask : 0;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	/*
	 * Don't bother testing KVM-controlled MSRs beyond verifying that the
	 * MSR can be read from userspace.  Any value is effectively legal, as
	 * KVM is bound by x86 architecture, not by ABI.
	 */
	if (is_kvm_controlled_msr(msr))
		return;

	/*
	 * More goofy behavior.  KVM reports the host CPU's actual revision ID,
	 * but initializes the vCPU's revision ID to an arbitrary value.
	 */
	if (msr == MSR_IA32_UCODE_REV)
		reset_value = host_cpu_is_intel ? 0x100000000ULL : 0x01000065;

	/*
	 * For quirked MSRs, KVM's ABI is to initialize the vCPU's value to the
	 * full set of features supported by KVM.  For non-quirked MSRs, and
	 * when the quirk is disabled, KVM must zero-initialize the MSR and let
	 * userspace do the configuration.
	 */
	vm = vm_create_with_one_vcpu(&vcpu, NULL);
	TEST_ASSERT(vcpu_get_msr(vcpu, msr) == reset_value,
		    "Wanted 0x%lx for %squirked MSR 0x%x, got 0x%lx",
		    reset_value, is_quirked_msr(msr) ? "" : "non-", msr,
		    vcpu_get_msr(vcpu, msr));
	if (!is_hidden_vmx_msr(msr))
		vcpu_set_msr(vcpu, msr, supported_mask);
	kvm_vm_free(vm);

	if (is_hidden_vmx_msr(msr))
		return;

	if (!kvm_has_cap(KVM_CAP_DISABLE_QUIRKS2) ||
	    !(kvm_check_cap(KVM_CAP_DISABLE_QUIRKS2) & KVM_X86_QUIRK_STUFF_FEATURE_MSRS))
		return;

	vm = vm_create(1);
	vm_enable_cap(vm, KVM_CAP_DISABLE_QUIRKS2, KVM_X86_QUIRK_STUFF_FEATURE_MSRS);

	vcpu = vm_vcpu_add(vm, 0, NULL);
	TEST_ASSERT(!vcpu_get_msr(vcpu, msr),
		    "Quirk disabled, wanted '0' for MSR 0x%x, got 0x%lx",
		    msr, vcpu_get_msr(vcpu, msr));
	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	const struct kvm_msr_list *feature_list;
	int i;

	/*
	 * Skip the entire test if MSR_FEATURES isn't supported, other tests
	 * will cover the "regular" list of MSRs, the coverage here is purely
	 * opportunistic and not interesting on its own.
	 */
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_GET_MSR_FEATURES));

	(void)kvm_get_msr_index_list();

	feature_list = kvm_get_feature_msr_index_list();
	for (i = 0; i < feature_list->nmsrs; i++)
		test_feature_msr(feature_list->indices[i]);
}
