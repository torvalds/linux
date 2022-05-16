// SPDX-License-Identifier: GPL-2.0-only
/*
 * svm_vmcall_test
 *
 * Copyright (C) 2020, Red Hat, Inc.
 *
 * Nested SVM testing: VMCALL
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"

#define VCPU_ID		5

static struct kvm_vm *vm;

static void l2_guest_code(struct svm_test_data *svm)
{
	__asm__ __volatile__("vmcall");
}

static void l1_guest_code(struct svm_test_data *svm)
{
	#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	struct vmcb *vmcb = svm->vmcb;

	/* Prepare for L2 execution. */
	generic_svm_setup(svm, l2_guest_code,
			  &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	run_guest(vmcb, svm->vmcb_gpa);

	GUEST_ASSERT(vmcb->control.exit_code == SVM_EXIT_VMMCALL);
	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	vm_vaddr_t svm_gva;

	nested_svm_check_supported();

	vm = vm_create_default(VCPU_ID, 0, (void *) l1_guest_code);
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());

	vcpu_alloc_svm(vm, &svm_gva);
	vcpu_args_set(vm, VCPU_ID, 1, svm_gva);

	for (;;) {
		volatile struct kvm_run *run = vcpu_state(vm, VCPU_ID);
		struct ucall uc;

		vcpu_run(vm, VCPU_ID);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Got exit_reason other than KVM_EXIT_IO: %u (%s)\n",
			    run->exit_reason,
			    exit_reason_str(run->exit_reason));

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_ABORT:
			TEST_FAIL("%s", (const char *)uc.args[0]);
			/* NOT REACHED */
		case UCALL_SYNC:
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unknown ucall 0x%lx.", uc.cmd);
		}
	}
done:
	kvm_vm_free(vm);
	return 0;
}
