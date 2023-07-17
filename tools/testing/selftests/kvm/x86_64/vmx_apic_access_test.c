// SPDX-License-Identifier: GPL-2.0-only
/*
 * vmx_apic_access_test
 *
 * Copyright (C) 2020, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * The first subtest simply checks to see that an L2 guest can be
 * launched with a valid APIC-access address that is backed by a
 * page of L1 physical memory.
 *
 * The second subtest sets the APIC-access address to a (valid) L1
 * physical address that is not backed by memory. KVM can't handle
 * this situation, so resuming L2 should result in a KVM exit for
 * internal error (emulation). This is not an architectural
 * requirement. It is just a shortcoming of KVM. The internal error
 * is unfortunate, but it's better than what used to happen!
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#include <string.h>
#include <sys/ioctl.h>

#include "kselftest.h"

static void l2_guest_code(void)
{
	/* Exit to L1 */
	__asm__ __volatile__("vmcall");
}

static void l1_guest_code(struct vmx_pages *vmx_pages, unsigned long high_gpa)
{
#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	uint32_t control;

	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_ASSERT(load_vmcs(vmx_pages));

	/* Prepare the VMCS for L2 execution. */
	prepare_vmcs(vmx_pages, l2_guest_code,
		     &l2_guest_stack[L2_GUEST_STACK_SIZE]);
	control = vmreadz(CPU_BASED_VM_EXEC_CONTROL);
	control |= CPU_BASED_ACTIVATE_SECONDARY_CONTROLS;
	vmwrite(CPU_BASED_VM_EXEC_CONTROL, control);
	control = vmreadz(SECONDARY_VM_EXEC_CONTROL);
	control |= SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES;
	vmwrite(SECONDARY_VM_EXEC_CONTROL, control);
	vmwrite(APIC_ACCESS_ADDR, vmx_pages->apic_access_gpa);

	/* Try to launch L2 with the memory-backed APIC-access address. */
	GUEST_SYNC(vmreadz(APIC_ACCESS_ADDR));
	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);

	vmwrite(APIC_ACCESS_ADDR, high_gpa);

	/* Try to resume L2 with the unbacked APIC-access address. */
	GUEST_SYNC(vmreadz(APIC_ACCESS_ADDR));
	GUEST_ASSERT(!vmresume());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	unsigned long apic_access_addr = ~0ul;
	vm_vaddr_t vmx_pages_gva;
	unsigned long high_gpa;
	struct vmx_pages *vmx;
	bool done = false;

	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX));

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);

	high_gpa = (vm->max_gfn - 1) << vm->page_shift;

	vmx = vcpu_alloc_vmx(vm, &vmx_pages_gva);
	prepare_virtualize_apic_accesses(vmx, vm);
	vcpu_args_set(vcpu, 2, vmx_pages_gva, high_gpa);

	while (!done) {
		volatile struct kvm_run *run = vcpu->run;
		struct ucall uc;

		vcpu_run(vcpu);
		if (apic_access_addr == high_gpa) {
			TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_INTERNAL_ERROR);
			TEST_ASSERT(run->internal.suberror ==
				    KVM_INTERNAL_ERROR_EMULATION,
				    "Got internal suberror other than KVM_INTERNAL_ERROR_EMULATION: %u\n",
				    run->internal.suberror);
			break;
		}
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			/* NOT REACHED */
		case UCALL_SYNC:
			apic_access_addr = uc.args[1];
			break;
		case UCALL_DONE:
			done = true;
			break;
		default:
			TEST_ASSERT(false, "Unknown ucall %lu", uc.cmd);
		}
	}
	kvm_vm_free(vm);
	return 0;
}
