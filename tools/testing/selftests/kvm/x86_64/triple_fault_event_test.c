// SPDX-License-Identifier: GPL-2.0-only
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#include <string.h>
#include <sys/ioctl.h>

#include "kselftest.h"

#define VCPU_ID			0
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
	struct kvm_run *run;
	struct kvm_vcpu_events events;
	vm_vaddr_t vmx_pages_gva;
	struct ucall uc;

	struct kvm_enable_cap cap = {
		.cap = KVM_CAP_TRIPLE_FAULT_EVENT,
		.args = {1}
	};

	if (!nested_vmx_supported()) {
		print_skip("Nested VMX not supported");
		exit(KSFT_SKIP);
	}

	if (!kvm_check_cap(KVM_CAP_TRIPLE_FAULT_EVENT)) {
		print_skip("KVM_CAP_TRIPLE_FAULT_EVENT not supported");
		exit(KSFT_SKIP);
	}

	vm = vm_create_default(VCPU_ID, 0, (void *) l1_guest_code);
	vm_enable_cap(vm, &cap);

	run = vcpu_state(vm, VCPU_ID);
	vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(vm, VCPU_ID, 1, vmx_pages_gva);
	vcpu_run(vm, VCPU_ID);

	TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
		    "Expected KVM_EXIT_IO, got: %u (%s)\n",
		    run->exit_reason, exit_reason_str(run->exit_reason));
	TEST_ASSERT(run->io.port == ARBITRARY_IO_PORT,
		    "Expected IN from port %d from L2, got port %d",
		    ARBITRARY_IO_PORT, run->io.port);
	vcpu_events_get(vm, VCPU_ID, &events);
	events.flags |= KVM_VCPUEVENT_VALID_TRIPLE_FAULT;
	events.triple_fault.pending = true;
	vcpu_events_set(vm, VCPU_ID, &events);
	run->immediate_exit = true;
	vcpu_run_complete_io(vm, VCPU_ID);

	vcpu_events_get(vm, VCPU_ID, &events);
	TEST_ASSERT(events.flags & KVM_VCPUEVENT_VALID_TRIPLE_FAULT,
		    "Triple fault event invalid");
	TEST_ASSERT(events.triple_fault.pending,
		    "No triple fault pending");
	vcpu_run(vm, VCPU_ID);

	switch (get_ucall(vm, VCPU_ID, &uc)) {
	case UCALL_DONE:
		break;
	case UCALL_ABORT:
		TEST_FAIL("%s", (const char *)uc.args[0]);
	default:
		TEST_FAIL("Unexpected ucall: %lu", uc.cmd);
	}

}
