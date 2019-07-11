/*
 * vmx_close_while_nested
 *
 * Copyright (C) 2019, Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
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

#define VCPU_ID		5

enum {
	PORT_L0_EXIT = 0x2000,
};

/* The virtual machine object. */
static struct kvm_vm *vm;

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
	uint32_t control;
	uintptr_t save_cr3;

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
	struct vmx_pages *vmx_pages;
	vm_vaddr_t vmx_pages_gva;
	struct kvm_cpuid_entry2 *entry = kvm_get_supported_cpuid_entry(1);

	if (!(entry->ecx & CPUID_VMX)) {
		fprintf(stderr, "nested VMX not enabled, skipping test\n");
		exit(KSFT_SKIP);
	}

	vm = vm_create_default(VCPU_ID, 0, (void *) l1_guest_code);
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());

	/* Allocate VMX pages and shared descriptors (vmx_pages). */
	vmx_pages = vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(vm, VCPU_ID, 1, vmx_pages_gva);

	for (;;) {
		volatile struct kvm_run *run = vcpu_state(vm, VCPU_ID);
		struct ucall uc;

		vcpu_run(vm, VCPU_ID);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Got exit_reason other than KVM_EXIT_IO: %u (%s)\n",
			    run->exit_reason,
			    exit_reason_str(run->exit_reason));

		if (run->io.port == PORT_L0_EXIT)
			break;

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_ABORT:
			TEST_ASSERT(false, "%s", (const char *)uc.args[0]);
			/* NOT REACHED */
		default:
			TEST_ASSERT(false, "Unknown ucall 0x%x.", uc.cmd);
		}
	}
}
