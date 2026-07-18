// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026, Google LLC.
 */
#include "kvm_util.h"
#include "vmx.h"
#include "svm_util.h"
#include "kselftest.h"


#define L2_GUEST_STACK_SIZE 64

static void l2_guest_code(void)
{
	unsigned long efer = rdmsr(MSR_EFER);

	/* generic_svm_setup() initializes EFER_SVME set for L2 */
	GUEST_ASSERT(efer & EFER_SVME);
	wrmsr(MSR_EFER, efer & ~EFER_SVME);

	/* Unreachable, L1 should be shutdown */
	GUEST_ASSERT(0);
}

static void l1_guest_code(struct svm_test_data *svm)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];

	generic_svm_setup(svm, l2_guest_code,
			  &l2_guest_stack[L2_GUEST_STACK_SIZE]);
	run_guest(svm->vmcb, svm->vmcb_gpa);

	/* Unreachable, L1 should be shutdown */
	GUEST_ASSERT(0);
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	gva_t nested_gva = 0;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SVM));

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);

	vcpu_alloc_svm(vm, &nested_gva);
	vcpu_args_set(vcpu, 1, nested_gva);

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_SHUTDOWN);

	kvm_vm_free(vm);
	return 0;
}
