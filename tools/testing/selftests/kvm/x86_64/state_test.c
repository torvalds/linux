// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM_GET/SET_* tests
 *
 * Copyright (C) 2018, Red Hat, Inc.
 *
 * Tests for vCPU state save/restore, including nested guest state.
 */
#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"

#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"
#include "svm_util.h"

#define VCPU_ID		5
#define L2_GUEST_STACK_SIZE 256

void svm_l2_guest_code(void)
{
	GUEST_SYNC(4);
	/* Exit to L1 */
	vmcall();
	GUEST_SYNC(6);
	/* Done, exit to L1 and never come back.  */
	vmcall();
}

static void svm_l1_guest_code(struct svm_test_data *svm)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	struct vmcb *vmcb = svm->vmcb;

	GUEST_ASSERT(svm->vmcb_gpa);
	/* Prepare for L2 execution. */
	generic_svm_setup(svm, svm_l2_guest_code,
			  &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	GUEST_SYNC(3);
	run_guest(vmcb, svm->vmcb_gpa);
	GUEST_ASSERT(vmcb->control.exit_code == SVM_EXIT_VMMCALL);
	GUEST_SYNC(5);
	vmcb->save.rip += 3;
	run_guest(vmcb, svm->vmcb_gpa);
	GUEST_ASSERT(vmcb->control.exit_code == SVM_EXIT_VMMCALL);
	GUEST_SYNC(7);
}

void vmx_l2_guest_code(void)
{
	GUEST_SYNC(6);

	/* Exit to L1 */
	vmcall();

	/* L1 has now set up a shadow VMCS for us.  */
	GUEST_ASSERT(vmreadz(GUEST_RIP) == 0xc0ffee);
	GUEST_SYNC(10);
	GUEST_ASSERT(vmreadz(GUEST_RIP) == 0xc0ffee);
	GUEST_ASSERT(!vmwrite(GUEST_RIP, 0xc0fffee));
	GUEST_SYNC(11);
	GUEST_ASSERT(vmreadz(GUEST_RIP) == 0xc0fffee);
	GUEST_ASSERT(!vmwrite(GUEST_RIP, 0xc0ffffee));
	GUEST_SYNC(12);

	/* Done, exit to L1 and never come back.  */
	vmcall();
}

static void vmx_l1_guest_code(struct vmx_pages *vmx_pages)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];

	GUEST_ASSERT(vmx_pages->vmcs_gpa);
	GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
	GUEST_SYNC(3);
	GUEST_ASSERT(load_vmcs(vmx_pages));
	GUEST_ASSERT(vmptrstz() == vmx_pages->vmcs_gpa);

	GUEST_SYNC(4);
	GUEST_ASSERT(vmptrstz() == vmx_pages->vmcs_gpa);

	prepare_vmcs(vmx_pages, vmx_l2_guest_code,
		     &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	GUEST_SYNC(5);
	GUEST_ASSERT(vmptrstz() == vmx_pages->vmcs_gpa);
	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmptrstz() == vmx_pages->vmcs_gpa);
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);

	/* Check that the launched state is preserved.  */
	GUEST_ASSERT(vmlaunch());

	GUEST_ASSERT(!vmresume());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);

	GUEST_SYNC(7);
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);

	GUEST_ASSERT(!vmresume());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);

	vmwrite(GUEST_RIP, vmreadz(GUEST_RIP) + 3);

	vmwrite(SECONDARY_VM_EXEC_CONTROL, SECONDARY_EXEC_SHADOW_VMCS);
	vmwrite(VMCS_LINK_POINTER, vmx_pages->shadow_vmcs_gpa);

	GUEST_ASSERT(!vmptrld(vmx_pages->shadow_vmcs_gpa));
	GUEST_ASSERT(vmlaunch());
	GUEST_SYNC(8);
	GUEST_ASSERT(vmlaunch());
	GUEST_ASSERT(vmresume());

	vmwrite(GUEST_RIP, 0xc0ffee);
	GUEST_SYNC(9);
	GUEST_ASSERT(vmreadz(GUEST_RIP) == 0xc0ffee);

	GUEST_ASSERT(!vmptrld(vmx_pages->vmcs_gpa));
	GUEST_ASSERT(!vmresume());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);

	GUEST_ASSERT(!vmptrld(vmx_pages->shadow_vmcs_gpa));
	GUEST_ASSERT(vmreadz(GUEST_RIP) == 0xc0ffffee);
	GUEST_ASSERT(vmlaunch());
	GUEST_ASSERT(vmresume());
	GUEST_SYNC(13);
	GUEST_ASSERT(vmreadz(GUEST_RIP) == 0xc0ffffee);
	GUEST_ASSERT(vmlaunch());
	GUEST_ASSERT(vmresume());
}

static void __attribute__((__flatten__)) guest_code(void *arg)
{
	GUEST_SYNC(1);
	GUEST_SYNC(2);

	if (arg) {
		if (cpu_has_svm())
			svm_l1_guest_code(arg);
		else
			vmx_l1_guest_code(arg);
	}

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	vm_vaddr_t nested_gva = 0;

	struct kvm_regs regs1, regs2;
	struct kvm_vm *vm;
	struct kvm_run *run;
	struct kvm_x86_state *state;
	struct ucall uc;
	int stage;

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code);
	run = vcpu_state(vm, VCPU_ID);

	vcpu_regs_get(vm, VCPU_ID, &regs1);

	if (kvm_check_cap(KVM_CAP_NESTED_STATE)) {
		if (nested_svm_supported())
			vcpu_alloc_svm(vm, &nested_gva);
		else if (nested_vmx_supported())
			vcpu_alloc_vmx(vm, &nested_gva);
	}

	if (!nested_gva)
		pr_info("will skip nested state checks\n");

	vcpu_args_set(vm, VCPU_ID, 1, nested_gva);

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
			goto done;
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
		vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());
		vcpu_load_state(vm, VCPU_ID, state);
		run = vcpu_state(vm, VCPU_ID);
		free(state);

		memset(&regs2, 0, sizeof(regs2));
		vcpu_regs_get(vm, VCPU_ID, &regs2);
		TEST_ASSERT(!memcmp(&regs1, &regs2, sizeof(regs2)),
			    "Unexpected register values after vcpu_load_state; rdi: %lx rsi: %lx",
			    (ulong) regs2.rdi, (ulong) regs2.rsi);
	}

done:
	kvm_vm_free(vm);
}
