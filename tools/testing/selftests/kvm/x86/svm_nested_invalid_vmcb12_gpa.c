// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026, Google LLC.
 */
#include "kvm_util.h"
#include "vmx.h"
#include "svm_util.h"
#include "kselftest.h"


#define L2_GUEST_STACK_SIZE 64

#define SYNC_GP 101
#define SYNC_L2_STARTED 102

u64 valid_vmcb12_gpa;
int gp_triggered;

static void guest_gp_handler(struct ex_regs *regs)
{
	GUEST_ASSERT(!gp_triggered);
	GUEST_SYNC(SYNC_GP);
	gp_triggered = 1;
	regs->rax = valid_vmcb12_gpa;
}

static void l2_guest_code(void)
{
	GUEST_SYNC(SYNC_L2_STARTED);
	vmcall();
}

static void l1_guest_code(struct svm_test_data *svm, u64 invalid_vmcb12_gpa)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];

	generic_svm_setup(svm, l2_guest_code,
			  &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	valid_vmcb12_gpa = svm->vmcb_gpa;

	run_guest(svm->vmcb, invalid_vmcb12_gpa); /* #GP */

	/* GP handler should jump here */
	GUEST_ASSERT(svm->vmcb->control.exit_code == SVM_EXIT_VMMCALL);
	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	struct kvm_x86_state *state;
	vm_vaddr_t nested_gva = 0;
	struct kvm_vcpu *vcpu;
	uint32_t maxphyaddr;
	u64 max_legal_gpa;
	struct kvm_vm *vm;
	struct ucall uc;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SVM));

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);
	vm_install_exception_handler(vcpu->vm, GP_VECTOR, guest_gp_handler);

	/*
	 * Find the max legal GPA that is not backed by a memslot (i.e. cannot
	 * be mapped by KVM).
	 */
	maxphyaddr = kvm_cpuid_property(vcpu->cpuid, X86_PROPERTY_MAX_PHY_ADDR);
	max_legal_gpa = BIT_ULL(maxphyaddr) - PAGE_SIZE;
	vcpu_alloc_svm(vm, &nested_gva);
	vcpu_args_set(vcpu, 2, nested_gva, max_legal_gpa);

	/* VMRUN with max_legal_gpa, KVM injects a #GP */
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);
	TEST_ASSERT_EQ(get_ucall(vcpu, &uc), UCALL_SYNC);
	TEST_ASSERT_EQ(uc.args[1], SYNC_GP);

	/*
	 * Enter L2 (with a legit vmcb12 GPA), then overwrite vmcb12 GPA with
	 * max_legal_gpa. KVM will fail to map vmcb12 on nested VM-Exit and
	 * cause a shutdown.
	 */
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);
	TEST_ASSERT_EQ(get_ucall(vcpu, &uc), UCALL_SYNC);
	TEST_ASSERT_EQ(uc.args[1], SYNC_L2_STARTED);

	state = vcpu_save_state(vcpu);
	state->nested.hdr.svm.vmcb_pa = max_legal_gpa;
	vcpu_load_state(vcpu, state);
	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_SHUTDOWN);

	kvm_x86_state_cleanup(state);
	kvm_vm_free(vm);
	return 0;
}
