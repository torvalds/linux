// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019, Red Hat, Inc.
 *
 * Verify that nothing bad happens if a KVM user exits with open
 * file descriptors while executing a nested guest.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"
#include "svm_util.h"

#include <string.h>
#include <sys/ioctl.h>

#include "kselftest.h"

enum {
	PORT_L0_EXIT = 0x2000,
};

#define L2_GUEST_STACK_SIZE 64

static void l2_guest_code(void)
{
	/* Exit to L0 */
	asm volatile("inb %%dx, %%al"
		     : : [port] "d" (PORT_L0_EXIT) : "rax");
}

static void l1_vmx_code(struct vmx_pages *vmx_pages)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];

	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_ASSERT(load_vmcs(vmx_pages));

	/* Prepare the VMCS for L2 execution. */
	prepare_vmcs(vmx_pages, l2_guest_code,
		     &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(0);
}

static void l1_svm_code(struct svm_test_data *svm)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];

	/* Prepare the VMCB for L2 execution. */
	generic_svm_setup(svm, l2_guest_code,
			  &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	run_guest(svm->vmcb, svm->vmcb_gpa);
	GUEST_ASSERT(0);
}

static void l1_guest_code(void *data)
{
	if (this_cpu_has(X86_FEATURE_VMX))
		l1_vmx_code(data);
	else
		l1_svm_code(data);
}

int main(int argc, char *argv[])
{
	vm_vaddr_t guest_gva;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX) ||
		     kvm_cpu_has(X86_FEATURE_SVM));

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);

	if (kvm_cpu_has(X86_FEATURE_VMX))
		vcpu_alloc_vmx(vm, &guest_gva);
	else
		vcpu_alloc_svm(vm, &guest_gva);

	vcpu_args_set(vcpu, 1, guest_gva);

	for (;;) {
		volatile struct kvm_run *run = vcpu->run;
		struct ucall uc;

		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		if (run->io.port == PORT_L0_EXIT)
			break;

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			/* NOT REACHED */
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}
}
