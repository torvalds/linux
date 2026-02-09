// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025, Google LLC.
 *
 * Test KVM's ability to save and restore nested state when the L1 guest
 * is using 5-level paging and the L2 guest is using 4-level paging.
 *
 * This test would have failed prior to commit 9245fd6b8531 ("KVM: x86:
 * model canonical checks more precisely").
 */
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#define LA57_GS_BASE 0xff2bc0311fb00000ull

static void l2_guest_code(void)
{
	/*
	 * Sync with L0 to trigger save/restore.  After
	 * resuming, execute VMCALL to exit back to L1.
	 */
	GUEST_SYNC(1);
	vmcall();
}

static void l1_guest_code(struct vmx_pages *vmx_pages)
{
#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	u64 guest_cr4;
	vm_paddr_t pml5_pa, pml4_pa;
	u64 *pml5;
	u64 exit_reason;

	/* Set GS_BASE to a value that is only canonical with LA57. */
	wrmsr(MSR_GS_BASE, LA57_GS_BASE);
	GUEST_ASSERT(rdmsr(MSR_GS_BASE) == LA57_GS_BASE);

	GUEST_ASSERT(vmx_pages->vmcs_gpa);
	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_ASSERT(load_vmcs(vmx_pages));

	prepare_vmcs(vmx_pages, l2_guest_code,
		     &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	/*
	 * Set up L2 with a 4-level page table by pointing its CR3 to
	 * L1's first PML4 table and clearing CR4.LA57. This creates
	 * the CR4.LA57 mismatch that exercises the bug.
	 */
	pml5_pa = get_cr3() & PHYSICAL_PAGE_MASK;
	pml5 = (u64 *)pml5_pa;
	pml4_pa = pml5[0] & PHYSICAL_PAGE_MASK;
	vmwrite(GUEST_CR3, pml4_pa);

	guest_cr4 = vmreadz(GUEST_CR4);
	guest_cr4 &= ~X86_CR4_LA57;
	vmwrite(GUEST_CR4, guest_cr4);

	GUEST_ASSERT(!vmlaunch());

	exit_reason = vmreadz(VM_EXIT_REASON);
	GUEST_ASSERT(exit_reason == EXIT_REASON_VMCALL);
}

void guest_code(struct vmx_pages *vmx_pages)
{
	l1_guest_code(vmx_pages);
	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	vm_vaddr_t vmx_pages_gva = 0;
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	struct kvm_x86_state *state;
	struct ucall uc;
	int stage;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_LA57));
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_NESTED_STATE));

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	/*
	 * L1 needs to read its own PML5 table to set up L2. Identity map
	 * the PML5 table to facilitate this.
	 */
	virt_map(vm, vm->mmu.pgd, vm->mmu.pgd, 1);

	vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(vcpu, 1, vmx_pages_gva);

	for (stage = 1;; stage++) {
		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			/* NOT REACHED */
		case UCALL_SYNC:
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}

		TEST_ASSERT(uc.args[1] == stage,
			    "Expected stage %d, got stage %lu", stage, (ulong)uc.args[1]);
		if (stage == 1) {
			pr_info("L2 is active; performing save/restore.\n");
			state = vcpu_save_state(vcpu);

			kvm_vm_release(vm);

			/* Restore state in a new VM. */
			vcpu = vm_recreate_with_one_vcpu(vm);
			vcpu_load_state(vcpu, state);
			kvm_x86_state_cleanup(state);
		}
	}

done:
	kvm_vm_free(vm);
	return 0;
}
