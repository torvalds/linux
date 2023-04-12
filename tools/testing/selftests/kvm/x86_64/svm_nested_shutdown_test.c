// SPDX-License-Identifier: GPL-2.0-only
/*
 * svm_nested_shutdown_test
 *
 * Copyright (C) 2022, Red Hat, Inc.
 *
 * Nested SVM testing: test that unintercepted shutdown in L2 doesn't crash the host
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"

static void l2_guest_code(struct svm_test_data *svm)
{
	__asm__ __volatile__("ud2");
}

static void l1_guest_code(struct svm_test_data *svm, struct idt_entry *idt)
{
	#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	struct vmcb *vmcb = svm->vmcb;

	generic_svm_setup(svm, l2_guest_code,
			  &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	vmcb->control.intercept &= ~(BIT(INTERCEPT_SHUTDOWN));

	idt[6].p   = 0; // #UD is intercepted but its injection will cause #NP
	idt[11].p  = 0; // #NP is not intercepted and will cause another
			// #NP that will be converted to #DF
	idt[8].p   = 0; // #DF will cause #NP which will cause SHUTDOWN

	run_guest(vmcb, svm->vmcb_gpa);

	/* should not reach here */
	GUEST_ASSERT(0);
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	vm_vaddr_t svm_gva;
	struct kvm_vm *vm;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SVM));

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);
	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);

	vcpu_alloc_svm(vm, &svm_gva);

	vcpu_args_set(vcpu, 2, svm_gva, vm->idt);

	vcpu_run(vcpu);
	TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_SHUTDOWN);

	kvm_vm_free(vm);
}
