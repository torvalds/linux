// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018, Red Hat, Inc.
 *
 * Tests for Enlightened VMCS, including nested guest state.
 */
#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"

#include "kvm_util.h"

#include "vmx.h"

#define VCPU_ID		5

void l2_guest_code(void)
{
	GUEST_SYNC(7);

	GUEST_SYNC(8);

	/* Done, exit to L1 and never come back.  */
	vmcall();
}

void l1_guest_code(struct vmx_pages *vmx_pages)
{
#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];

	enable_vp_assist(vmx_pages->vp_assist_gpa, vmx_pages->vp_assist);

	GUEST_ASSERT(vmx_pages->vmcs_gpa);
	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_SYNC(3);
	GUEST_ASSERT(load_vmcs(vmx_pages));
	GUEST_ASSERT(vmptrstz() == vmx_pages->enlightened_vmcs_gpa);

	GUEST_SYNC(4);
	GUEST_ASSERT(vmptrstz() == vmx_pages->enlightened_vmcs_gpa);

	prepare_vmcs(vmx_pages, l2_guest_code,
		     &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	GUEST_SYNC(5);
	GUEST_ASSERT(vmptrstz() == vmx_pages->enlightened_vmcs_gpa);
	current_evmcs->revision_id = -1u;
	GUEST_ASSERT(vmlaunch());
	current_evmcs->revision_id = EVMCS_VERSION;
	GUEST_SYNC(6);

	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmptrstz() == vmx_pages->enlightened_vmcs_gpa);
	GUEST_SYNC(9);
	GUEST_ASSERT(!vmresume());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);
	GUEST_SYNC(10);
}

void guest_code(struct vmx_pages *vmx_pages)
{
	GUEST_SYNC(1);
	GUEST_SYNC(2);

	if (vmx_pages)
		l1_guest_code(vmx_pages);

	GUEST_DONE();

	/* Try enlightened vmptrld with an incorrect GPA */
	evmcs_vmptrld(0xdeadbeef, vmx_pages->enlightened_vmcs);
	GUEST_ASSERT(vmlaunch());
}

int main(int argc, char *argv[])
{
	vm_vaddr_t vmx_pages_gva = 0;

	struct kvm_regs regs1, regs2;
	struct kvm_vm *vm;
	struct kvm_run *run;
	struct kvm_x86_state *state;
	struct ucall uc;
	int stage;

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code);

	if (!nested_vmx_supported() ||
	    !kvm_check_cap(KVM_CAP_NESTED_STATE) ||
	    !kvm_check_cap(KVM_CAP_HYPERV_ENLIGHTENED_VMCS)) {
		print_skip("Enlightened VMCS is unsupported");
		exit(KSFT_SKIP);
	}

	vcpu_set_hv_cpuid(vm, VCPU_ID);
	vcpu_enable_evmcs(vm, VCPU_ID);

	run = vcpu_state(vm, VCPU_ID);

	vcpu_regs_get(vm, VCPU_ID, &regs1);

	vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(vm, VCPU_ID, 1, vmx_pages_gva);

	for (stage = 1;; stage++) {
		_vcpu_run(vm, VCPU_ID);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Stage %d: unexpected exit reason: %u (%s),\n",
			    stage, run->exit_reason,
			    exit_reason_str(run->exit_reason));

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_ABORT:
			TEST_FAIL("%s at %s:%ld", (const char *)uc.args[0],
		      		  __FILE__, uc.args[1]);
			/* NOT REACHED */
		case UCALL_SYNC:
			break;
		case UCALL_DONE:
			goto part1_done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}

		/* UCALL_SYNC is handled here.  */
		TEST_ASSERT(!strcmp((const char *)uc.args[0], "hello") &&
			    uc.args[1] == stage, "Stage %d: Unexpected register values vmexit, got %lx",
			    stage, (ulong)uc.args[1]);

		state = vcpu_save_state(vm, VCPU_ID);
		memset(&regs1, 0, sizeof(regs1));
		vcpu_regs_get(vm, VCPU_ID, &regs1);

		kvm_vm_release(vm);

		/* Restore state in a new VM.  */
		kvm_vm_restart(vm, O_RDWR);
		vm_vcpu_add(vm, VCPU_ID);
		vcpu_set_hv_cpuid(vm, VCPU_ID);
		vcpu_enable_evmcs(vm, VCPU_ID);
		vcpu_load_state(vm, VCPU_ID, state);
		run = vcpu_state(vm, VCPU_ID);
		free(state);

		memset(&regs2, 0, sizeof(regs2));
		vcpu_regs_get(vm, VCPU_ID, &regs2);
		TEST_ASSERT(!memcmp(&regs1, &regs2, sizeof(regs2)),
			    "Unexpected register values after vcpu_load_state; rdi: %lx rsi: %lx",
			    (ulong) regs2.rdi, (ulong) regs2.rsi);
	}

part1_done:
	_vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(run->exit_reason == KVM_EXIT_SHUTDOWN,
		    "Unexpected successful VMEnter with invalid eVMCS pointer!");

	kvm_vm_free(vm);
}
