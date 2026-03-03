// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026, Google, Inc.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"


#define L2_GUEST_STACK_SIZE 64

#define DO_BRANCH() do { asm volatile("jmp 1f\n 1: nop"); } while (0)

struct lbr_branch {
	u64 from, to;
};

volatile struct lbr_branch l2_branch;

#define RECORD_AND_CHECK_BRANCH(b)					\
do {									\
	wrmsr(MSR_IA32_DEBUGCTLMSR, DEBUGCTLMSR_LBR);			\
	DO_BRANCH();							\
	(b)->from = rdmsr(MSR_IA32_LASTBRANCHFROMIP);			\
	(b)->to = rdmsr(MSR_IA32_LASTBRANCHTOIP);			\
	/* Disable LBR right after to avoid overriding the IPs */	\
	wrmsr(MSR_IA32_DEBUGCTLMSR, 0);					\
									\
	GUEST_ASSERT_NE((b)->from, 0);					\
	GUEST_ASSERT_NE((b)->to, 0);					\
} while (0)

#define CHECK_BRANCH_MSRS(b)						\
do {									\
	GUEST_ASSERT_EQ((b)->from, rdmsr(MSR_IA32_LASTBRANCHFROMIP));	\
	GUEST_ASSERT_EQ((b)->to, rdmsr(MSR_IA32_LASTBRANCHTOIP));	\
} while (0)

#define CHECK_BRANCH_VMCB(b, vmcb)					\
do {									\
	GUEST_ASSERT_EQ((b)->from, vmcb->save.br_from);			\
	GUEST_ASSERT_EQ((b)->to, vmcb->save.br_to);			\
} while (0)

static void l2_guest_code(struct svm_test_data *svm)
{
	/* Record a branch, trigger save/restore, and make sure LBRs are intact */
	RECORD_AND_CHECK_BRANCH(&l2_branch);
	GUEST_SYNC(true);
	CHECK_BRANCH_MSRS(&l2_branch);
	vmmcall();
}

static void l1_guest_code(struct svm_test_data *svm, bool nested_lbrv)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	struct vmcb *vmcb = svm->vmcb;
	struct lbr_branch l1_branch;

	/* Record a branch, trigger save/restore, and make sure LBRs are intact */
	RECORD_AND_CHECK_BRANCH(&l1_branch);
	GUEST_SYNC(true);
	CHECK_BRANCH_MSRS(&l1_branch);

	/* Run L2, which will also do the same */
	generic_svm_setup(svm, l2_guest_code,
			  &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	if (nested_lbrv)
		vmcb->control.virt_ext = LBR_CTL_ENABLE_MASK;
	else
		vmcb->control.virt_ext &= ~LBR_CTL_ENABLE_MASK;

	run_guest(vmcb, svm->vmcb_gpa);
	GUEST_ASSERT(svm->vmcb->control.exit_code == SVM_EXIT_VMMCALL);

	/* Trigger save/restore one more time before checking, just for kicks */
	GUEST_SYNC(true);

	/*
	 * If LBR_CTL_ENABLE is set, L1 and L2 should have separate LBR MSRs, so
	 * expect L1's LBRs to remain intact and L2 LBRs to be in the VMCB.
	 * Otherwise, the MSRs are shared between L1 & L2 so expect L2's LBRs.
	 */
	if (nested_lbrv) {
		CHECK_BRANCH_MSRS(&l1_branch);
		CHECK_BRANCH_VMCB(&l2_branch, vmcb);
	} else {
		CHECK_BRANCH_MSRS(&l2_branch);
	}
	GUEST_DONE();
}

void test_lbrv_nested_state(bool nested_lbrv)
{
	struct kvm_x86_state *state = NULL;
	struct kvm_vcpu *vcpu;
	vm_vaddr_t svm_gva;
	struct kvm_vm *vm;
	struct ucall uc;

	pr_info("Testing with nested LBRV %s\n", nested_lbrv ? "enabled" : "disabled");

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);
	vcpu_alloc_svm(vm, &svm_gva);
	vcpu_args_set(vcpu, 2, svm_gva, nested_lbrv);

	for (;;) {
		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);
		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			/* Save the vCPU state and restore it in a new VM on sync */
			pr_info("Guest triggered save/restore.\n");
			state = vcpu_save_state(vcpu);
			kvm_vm_release(vm);
			vcpu = vm_recreate_with_one_vcpu(vm);
			vcpu_load_state(vcpu, state);
			kvm_x86_state_cleanup(state);
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			/* NOT REACHED */
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}
done:
	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SVM));
	TEST_REQUIRE(kvm_is_lbrv_enabled());

	test_lbrv_nested_state(/*nested_lbrv=*/false);
	test_lbrv_nested_state(/*nested_lbrv=*/true);

	return 0;
}
