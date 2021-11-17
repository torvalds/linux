// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM_SET_SREGS tests
 *
 * Copyright (C) 2018, Google LLC.
 *
 * This is a regression test for the bug fixed by the following commit:
 * d3802286fa0f ("kvm: x86: Disallow illegal IA32_APIC_BASE MSR values")
 *
 * That bug allowed a user-mode program that called the KVM_SET_SREGS
 * ioctl to put a VCPU's local APIC into an invalid state.
 */
#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"

#include "kvm_util.h"
#include "processor.h"

#define VCPU_ID                  5

static void test_cr4_feature_bit(struct kvm_vm *vm, struct kvm_sregs *orig,
				 uint64_t feature_bit)
{
	struct kvm_sregs sregs;
	int rc;

	/* Skip the sub-test, the feature is supported. */
	if (orig->cr4 & feature_bit)
		return;

	memcpy(&sregs, orig, sizeof(sregs));
	sregs.cr4 |= feature_bit;

	rc = _vcpu_sregs_set(vm, VCPU_ID, &sregs);
	TEST_ASSERT(rc, "KVM allowed unsupported CR4 bit (0x%lx)", feature_bit);

	/* Sanity check that KVM didn't change anything. */
	vcpu_sregs_get(vm, VCPU_ID, &sregs);
	TEST_ASSERT(!memcmp(&sregs, orig, sizeof(sregs)), "KVM modified sregs");
}

static uint64_t calc_cr4_feature_bits(struct kvm_vm *vm)
{
	struct kvm_cpuid_entry2 *cpuid_1, *cpuid_7;
	uint64_t cr4;

	cpuid_1 = kvm_get_supported_cpuid_entry(1);
	cpuid_7 = kvm_get_supported_cpuid_entry(7);

	cr4 = X86_CR4_VME | X86_CR4_PVI | X86_CR4_TSD | X86_CR4_DE |
	      X86_CR4_PSE | X86_CR4_PAE | X86_CR4_MCE | X86_CR4_PGE |
	      X86_CR4_PCE | X86_CR4_OSFXSR | X86_CR4_OSXMMEXCPT;
	if (cpuid_7->ecx & CPUID_UMIP)
		cr4 |= X86_CR4_UMIP;
	if (cpuid_7->ecx & CPUID_LA57)
		cr4 |= X86_CR4_LA57;
	if (cpuid_1->ecx & CPUID_VMX)
		cr4 |= X86_CR4_VMXE;
	if (cpuid_1->ecx & CPUID_SMX)
		cr4 |= X86_CR4_SMXE;
	if (cpuid_7->ebx & CPUID_FSGSBASE)
		cr4 |= X86_CR4_FSGSBASE;
	if (cpuid_1->ecx & CPUID_PCID)
		cr4 |= X86_CR4_PCIDE;
	if (cpuid_1->ecx & CPUID_XSAVE)
		cr4 |= X86_CR4_OSXSAVE;
	if (cpuid_7->ebx & CPUID_SMEP)
		cr4 |= X86_CR4_SMEP;
	if (cpuid_7->ebx & CPUID_SMAP)
		cr4 |= X86_CR4_SMAP;
	if (cpuid_7->ecx & CPUID_PKU)
		cr4 |= X86_CR4_PKE;

	return cr4;
}

int main(int argc, char *argv[])
{
	struct kvm_sregs sregs;
	struct kvm_vm *vm;
	uint64_t cr4;
	int rc;

	/* Tell stdout not to buffer its content */
	setbuf(stdout, NULL);

	/*
	 * Create a dummy VM, specifically to avoid doing KVM_SET_CPUID2, and
	 * use it to verify all supported CR4 bits can be set prior to defining
	 * the vCPU model, i.e. without doing KVM_SET_CPUID2.
	 */
	vm = vm_create(VM_MODE_DEFAULT, DEFAULT_GUEST_PHY_PAGES, O_RDWR);
	vm_vcpu_add(vm, VCPU_ID);

	vcpu_sregs_get(vm, VCPU_ID, &sregs);

	sregs.cr4 |= calc_cr4_feature_bits(vm);
	cr4 = sregs.cr4;

	rc = _vcpu_sregs_set(vm, VCPU_ID, &sregs);
	TEST_ASSERT(!rc, "Failed to set supported CR4 bits (0x%lx)", cr4);

	vcpu_sregs_get(vm, VCPU_ID, &sregs);
	TEST_ASSERT(sregs.cr4 == cr4, "sregs.CR4 (0x%llx) != CR4 (0x%lx)",
		    sregs.cr4, cr4);

	/* Verify all unsupported features are rejected by KVM. */
	test_cr4_feature_bit(vm, &sregs, X86_CR4_UMIP);
	test_cr4_feature_bit(vm, &sregs, X86_CR4_LA57);
	test_cr4_feature_bit(vm, &sregs, X86_CR4_VMXE);
	test_cr4_feature_bit(vm, &sregs, X86_CR4_SMXE);
	test_cr4_feature_bit(vm, &sregs, X86_CR4_FSGSBASE);
	test_cr4_feature_bit(vm, &sregs, X86_CR4_PCIDE);
	test_cr4_feature_bit(vm, &sregs, X86_CR4_OSXSAVE);
	test_cr4_feature_bit(vm, &sregs, X86_CR4_SMEP);
	test_cr4_feature_bit(vm, &sregs, X86_CR4_SMAP);
	test_cr4_feature_bit(vm, &sregs, X86_CR4_PKE);
	kvm_vm_free(vm);

	/* Create a "real" VM and verify APIC_BASE can be set. */
	vm = vm_create_default(VCPU_ID, 0, NULL);

	vcpu_sregs_get(vm, VCPU_ID, &sregs);
	sregs.apic_base = 1 << 10;
	rc = _vcpu_sregs_set(vm, VCPU_ID, &sregs);
	TEST_ASSERT(rc, "Set IA32_APIC_BASE to %llx (invalid)",
		    sregs.apic_base);
	sregs.apic_base = 1 << 11;
	rc = _vcpu_sregs_set(vm, VCPU_ID, &sregs);
	TEST_ASSERT(!rc, "Couldn't set IA32_APIC_BASE to %llx (valid)",
		    sregs.apic_base);

	kvm_vm_free(vm);

	return 0;
}
