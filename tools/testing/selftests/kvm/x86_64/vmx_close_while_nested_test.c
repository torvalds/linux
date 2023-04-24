// SPDX-License-Identifier: GPL-2.0-only
/*
 * vmx_close_while_nested
 *
 * Copyright (C) 2019, Red Hat, Inc.
 *
 * Verify that nothing bad happens if a KVM user exits with open
 * file descriptors while executing a nested guest.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#include <string.h>
#include <sys/ioctl.h>

#include "kselftest.h"

enum {
	PORT_L0_EXIT = 0x2000,
};

static void l2_guest_code(void)
{
	/* Exit to L0 */
	asm volatile("inb %%dx, %%al"
		     : : [port] "d" (PORT_L0_EXIT) : "rax");
}

static void l1_guest_code(struct vmx_pages *vmx_pages)
{
#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];

	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_ASSERT(load_vmcs(vmx_pages));

	/* Prepare the VMCS for L2 execution. */
	prepare_vmcs(vmx_pages, l2_guest_code,
		     &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(0);
}

int main(int argc, char *argv[])
{
	vm_vaddr_t vmx_pages_gva;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX));

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);

	/* Allocate VMX pages and shared descriptors (vmx_pages). */
	vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(vcpu, 1, vmx_pages_gva);

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
