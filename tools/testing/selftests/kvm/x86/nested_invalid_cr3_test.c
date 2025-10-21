// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Google LLC.
 *
 * This test verifies that L1 fails to enter L2 with an invalid CR3, and
 * succeeds otherwise.
 */
#include "kvm_util.h"
#include "vmx.h"
#include "kselftest.h"


#define L2_GUEST_STACK_SIZE 64

static void l2_guest_code(void)
{
	vmcall();
}

static void l1_vmx_code(struct vmx_pages *vmx_pages)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	uintptr_t save_cr3;

	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_ASSERT(load_vmcs(vmx_pages));

	prepare_vmcs(vmx_pages, l2_guest_code,
		     &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	/* Try to run L2 with invalid CR3 and make sure it fails */
	save_cr3 = vmreadz(GUEST_CR3);
	vmwrite(GUEST_CR3, -1ull);
	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) ==
		     (EXIT_REASON_FAILED_VMENTRY | EXIT_REASON_INVALID_STATE));

	/* Now restore CR3 and make sure L2 runs successfully */
	vmwrite(GUEST_CR3, save_cr3);
	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	vm_vaddr_t guest_gva = 0;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX));

	vm = vm_create_with_one_vcpu(&vcpu, l1_vmx_code);
	vcpu_alloc_vmx(vm, &guest_gva);
	vcpu_args_set(vcpu, 1, guest_gva);

	for (;;) {
		struct ucall uc;

		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
		case UCALL_SYNC:
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}

done:
	kvm_vm_free(vm);
	return 0;
}
