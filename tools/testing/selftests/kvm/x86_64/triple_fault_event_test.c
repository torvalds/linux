// SPDX-License-Identifier: GPL-2.0-only
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#include <string.h>
#include <sys/ioctl.h>

#include "kselftest.h"

#define ARBITRARY_IO_PORT	0x2000

/* The virtual machine object. */
static struct kvm_vm *vm;

static void l2_guest_code(void)
{
	asm volatile("inb %%dx, %%al"
		     : : [port] "d" (ARBITRARY_IO_PORT) : "rax");
}

void l1_guest_code(struct vmx_pages *vmx)
{
#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];

	GUEST_ASSERT(vmx->vmcs_gpa);
	GUEST_ASSERT(prepare_for_vmx_operation(vmx));
	GUEST_ASSERT(load_vmcs(vmx));

	prepare_vmcs(vmx, l2_guest_code,
		     &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	GUEST_ASSERT(!vmlaunch());
	/* L2 should triple fault after a triple fault event injected. */
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_TRIPLE_FAULT);
	GUEST_DONE();
}

int main(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_run *run;
	struct kvm_vcpu_events events;
	vm_vaddr_t vmx_pages_gva;
	struct ucall uc;

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX));

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_X86_TRIPLE_FAULT_EVENT));

	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);
	vm_enable_cap(vm, KVM_CAP_X86_TRIPLE_FAULT_EVENT, 1);

	run = vcpu->run;
	vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(vcpu, 1, vmx_pages_gva);
	vcpu_run(vcpu);

	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Expected KVM_EXIT_IO, got: %u (%s)\n",
		    run->exit_reason, exit_reason_str(run->exit_reason));
	TEST_ASSERT(run->io.port == ARBITRARY_IO_PORT,
		    "Expected IN from port %d from L2, got port %d",
		    ARBITRARY_IO_PORT, run->io.port);
	vcpu_events_get(vcpu, &events);
	events.flags |= KVM_VCPUEVENT_VALID_TRIPLE_FAULT;
	events.triple_fault.pending = true;
	vcpu_events_set(vcpu, &events);
	run->immediate_exit = true;
	vcpu_run_complete_io(vcpu);

	vcpu_events_get(vcpu, &events);
	TEST_ASSERT(events.flags & KVM_VCPUEVENT_VALID_TRIPLE_FAULT,
		    "Triple fault event invalid");
	TEST_ASSERT(events.triple_fault.pending,
		    "No triple fault pending");
	vcpu_run(vcpu);

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_DONE:
		break;
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
	default:
		TEST_FAIL("Unexpected ucall: %lu", uc.cmd);
	}

}
