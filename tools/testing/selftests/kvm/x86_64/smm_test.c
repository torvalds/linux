// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018, Red Hat, Inc.
 *
 * Tests for SMM.
 */
#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"

#include "kvm_util.h"

#include "vmx.h"
#include "svm_util.h"

#define SMRAM_SIZE 65536
#define SMRAM_MEMSLOT ((1 << 16) | 1)
#define SMRAM_PAGES (SMRAM_SIZE / PAGE_SIZE)
#define SMRAM_GPA 0x1000000
#define SMRAM_STAGE 0xfe

#define STR(x) #x
#define XSTR(s) STR(s)

#define SYNC_PORT 0xe
#define DONE 0xff

/*
 * This is compiled as normal 64-bit code, however, SMI handler is executed
 * in real-address mode. To stay simple we're limiting ourselves to a mode
 * independent subset of asm here.
 * SMI handler always report back fixed stage SMRAM_STAGE.
 */
uint8_t smi_handler[] = {
	0xb0, SMRAM_STAGE,    /* mov $SMRAM_STAGE, %al */
	0xe4, SYNC_PORT,      /* in $SYNC_PORT, %al */
	0x0f, 0xaa,           /* rsm */
};

static inline void sync_with_host(uint64_t phase)
{
	asm volatile("in $" XSTR(SYNC_PORT)", %%al \n"
		     : "+a" (phase));
}

static void self_smi(void)
{
	x2apic_write_reg(APIC_ICR,
			 APIC_DEST_SELF | APIC_INT_ASSERT | APIC_DM_SMI);
}

static void l2_guest_code(void)
{
	sync_with_host(8);

	sync_with_host(10);

	vmcall();
}

static void guest_code(void *arg)
{
	#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	uint64_t apicbase = rdmsr(MSR_IA32_APICBASE);
	struct svm_test_data *svm = arg;
	struct vmx_pages *vmx_pages = arg;

	sync_with_host(1);

	wrmsr(MSR_IA32_APICBASE, apicbase | X2APIC_ENABLE);

	sync_with_host(2);

	self_smi();

	sync_with_host(4);

	if (arg) {
		if (this_cpu_has(X86_FEATURE_SVM)) {
			generic_svm_setup(svm, l2_guest_code,
					  &l2_guest_stack[L2_GUEST_STACK_SIZE]);
		} else {
			GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));
			GUEST_ASSERT(load_vmcs(vmx_pages));
			prepare_vmcs(vmx_pages, l2_guest_code,
				     &l2_guest_stack[L2_GUEST_STACK_SIZE]);
		}

		sync_with_host(5);

		self_smi();

		sync_with_host(7);

		if (this_cpu_has(X86_FEATURE_SVM)) {
			run_guest(svm->vmcb, svm->vmcb_gpa);
			run_guest(svm->vmcb, svm->vmcb_gpa);
		} else {
			vmlaunch();
			vmresume();
		}

		/* Stages 8-11 are eaten by SMM (SMRAM_STAGE reported instead) */
		sync_with_host(12);
	}

	sync_with_host(DONE);
}

void inject_smi(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_events events;

	vcpu_events_get(vcpu, &events);

	events.smi.pending = 1;
	events.flags |= KVM_VCPUEVENT_VALID_SMM;

	vcpu_events_set(vcpu, &events);
}

int main(int argc, char *argv[])
{
	vm_vaddr_t nested_gva = 0;

	struct kvm_vcpu *vcpu;
	struct kvm_regs regs;
	struct kvm_vm *vm;
	struct kvm_run *run;
	struct kvm_x86_state *state;
	int stage, stage_reported;

	/* Create VM */
	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	run = vcpu->run;

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS, SMRAM_GPA,
				    SMRAM_MEMSLOT, SMRAM_PAGES, 0);
	TEST_ASSERT(vm_phy_pages_alloc(vm, SMRAM_PAGES, SMRAM_GPA, SMRAM_MEMSLOT)
		    == SMRAM_GPA, "could not allocate guest physical addresses?");

	memset(addr_gpa2hva(vm, SMRAM_GPA), 0x0, SMRAM_SIZE);
	memcpy(addr_gpa2hva(vm, SMRAM_GPA) + 0x8000, smi_handler,
	       sizeof(smi_handler));

	vcpu_set_msr(vcpu, MSR_IA32_SMBASE, SMRAM_GPA);

	if (kvm_has_cap(KVM_CAP_NESTED_STATE)) {
		if (kvm_cpu_has(X86_FEATURE_SVM))
			vcpu_alloc_svm(vm, &nested_gva);
		else if (kvm_cpu_has(X86_FEATURE_VMX))
			vcpu_alloc_vmx(vm, &nested_gva);
	}

	if (!nested_gva)
		pr_info("will skip SMM test with VMX enabled\n");

	vcpu_args_set(vcpu, 1, nested_gva);

	for (stage = 1;; stage++) {
		vcpu_run(vcpu);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Stage %d: unexpected exit reason: %u (%s),\n",
			    stage, run->exit_reason,
			    exit_reason_str(run->exit_reason));

		memset(&regs, 0, sizeof(regs));
		vcpu_regs_get(vcpu, &regs);

		stage_reported = regs.rax & 0xff;

		if (stage_reported == DONE)
			goto done;

		TEST_ASSERT(stage_reported == stage ||
			    stage_reported == SMRAM_STAGE,
			    "Unexpected stage: #%x, got %x",
			    stage, stage_reported);

		/*
		 * Enter SMM during L2 execution and check that we correctly
		 * return from it. Do not perform save/restore while in SMM yet.
		 */
		if (stage == 8) {
			inject_smi(vcpu);
			continue;
		}

		/*
		 * Perform save/restore while the guest is in SMM triggered
		 * during L2 execution.
		 */
		if (stage == 10)
			inject_smi(vcpu);

		state = vcpu_save_state(vcpu);
		kvm_vm_release(vm);

		vcpu = vm_recreate_with_one_vcpu(vm);
		vcpu_load_state(vcpu, state);
		run = vcpu->run;
		kvm_x86_state_cleanup(state);
	}

done:
	kvm_vm_free(vm);
}
