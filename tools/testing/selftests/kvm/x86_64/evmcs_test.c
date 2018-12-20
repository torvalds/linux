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

static bool have_nested_state;

void l2_guest_code(void)
{
	GUEST_SYNC(6);

	GUEST_SYNC(7);

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
	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT(vmptrstz() == vmx_pages->enlightened_vmcs_gpa);
	GUEST_SYNC(8);
	GUEST_ASSERT(!vmresume());
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);
	GUEST_SYNC(9);
}

void guest_code(struct vmx_pages *vmx_pages)
{
	GUEST_SYNC(1);
	GUEST_SYNC(2);

	if (vmx_pages)
		l1_guest_code(vmx_pages);

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	struct vmx_pages *vmx_pages = NULL;
	vm_vaddr_t vmx_pages_gva = 0;

	struct kvm_regs regs1, regs2;
	struct kvm_vm *vm;
	struct kvm_run *run;
	struct kvm_x86_state *state;
	struct ucall uc;
	int stage;
	uint16_t evmcs_ver;
	struct kvm_enable_cap enable_evmcs_cap = {
		.cap = KVM_CAP_HYPERV_ENLIGHTENED_VMCS,
		 .args[0] = (unsigned long)&evmcs_ver
	};

	struct kvm_cpuid_entry2 *entry = kvm_get_supported_cpuid_entry(1);

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code);

	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());

	if (!kvm_check_cap(KVM_CAP_NESTED_STATE) ||
	    !kvm_check_cap(KVM_CAP_HYPERV_ENLIGHTENED_VMCS)) {
		printf("capabilities not available, skipping test\n");
		exit(KSFT_SKIP);
	}

	vcpu_ioctl(vm, VCPU_ID, KVM_ENABLE_CAP, &enable_evmcs_cap);

	run = vcpu_state(vm, VCPU_ID);

	vcpu_regs_get(vm, VCPU_ID, &regs1);

	vmx_pages = vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(vm, VCPU_ID, 1, vmx_pages_gva);

	for (stage = 1;; stage++) {
		_vcpu_run(vm, VCPU_ID);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Unexpected exit reason: %u (%s),\n",
			    run->exit_reason,
			    exit_reason_str(run->exit_reason));

		memset(&regs1, 0, sizeof(regs1));
		vcpu_regs_get(vm, VCPU_ID, &regs1);
		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_ABORT:
			TEST_ASSERT(false, "%s at %s:%d", (const char *)uc.args[0],
				    __FILE__, uc.args[1]);
			/* NOT REACHED */
		case UCALL_SYNC:
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_ASSERT(false, "Unknown ucall 0x%x.", uc.cmd);
		}

		/* UCALL_SYNC is handled here.  */
		TEST_ASSERT(!strcmp((const char *)uc.args[0], "hello") &&
			    uc.args[1] == stage, "Unexpected register values vmexit #%lx, got %lx",
			    stage, (ulong)uc.args[1]);

		state = vcpu_save_state(vm, VCPU_ID);
		kvm_vm_release(vm);

		/* Restore state in a new VM.  */
		kvm_vm_restart(vm, O_RDWR);
		vm_vcpu_add(vm, VCPU_ID, 0, 0);
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
