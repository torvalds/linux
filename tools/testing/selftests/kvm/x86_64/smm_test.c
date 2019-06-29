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

#define VCPU_ID	      1

#define PAGE_SIZE  4096

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

void sync_with_host(uint64_t phase)
{
	asm volatile("in $" XSTR(SYNC_PORT)", %%al \n"
		     : : "a" (phase));
}

void self_smi(void)
{
	wrmsr(APIC_BASE_MSR + (APIC_ICR >> 4),
	      APIC_DEST_SELF | APIC_INT_ASSERT | APIC_DM_SMI);
}

void guest_code(struct vmx_pages *vmx_pages)
{
	uint64_t apicbase = rdmsr(MSR_IA32_APICBASE);

	sync_with_host(1);

	wrmsr(MSR_IA32_APICBASE, apicbase | X2APIC_ENABLE);

	sync_with_host(2);

	self_smi();

	sync_with_host(4);

	if (vmx_pages) {
		GUEST_ASSERT(prepare_for_vmx_operation(vmx_pages));

		sync_with_host(5);

		self_smi();

		sync_with_host(7);
	}

	sync_with_host(DONE);
}

int main(int argc, char *argv[])
{
	vm_vaddr_t vmx_pages_gva = 0;

	struct kvm_regs regs;
	struct kvm_vm *vm;
	struct kvm_run *run;
	struct kvm_x86_state *state;
	int stage, stage_reported;

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, guest_code);

	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());

	run = vcpu_state(vm, VCPU_ID);

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS, SMRAM_GPA,
				    SMRAM_MEMSLOT, SMRAM_PAGES, 0);
	TEST_ASSERT(vm_phy_pages_alloc(vm, SMRAM_PAGES, SMRAM_GPA, SMRAM_MEMSLOT)
		    == SMRAM_GPA, "could not allocate guest physical addresses?");

	memset(addr_gpa2hva(vm, SMRAM_GPA), 0x0, SMRAM_SIZE);
	memcpy(addr_gpa2hva(vm, SMRAM_GPA) + 0x8000, smi_handler,
	       sizeof(smi_handler));

	vcpu_set_msr(vm, VCPU_ID, MSR_IA32_SMBASE, SMRAM_GPA);

	if (kvm_check_cap(KVM_CAP_NESTED_STATE)) {
		vcpu_alloc_vmx(vm, &vmx_pages_gva);
		vcpu_args_set(vm, VCPU_ID, 1, vmx_pages_gva);
	} else {
		printf("will skip SMM test with VMX enabled\n");
		vcpu_args_set(vm, VCPU_ID, 1, 0);
	}

	for (stage = 1;; stage++) {
		_vcpu_run(vm, VCPU_ID);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Stage %d: unexpected exit reason: %u (%s),\n",
			    stage, run->exit_reason,
			    exit_reason_str(run->exit_reason));

		memset(&regs, 0, sizeof(regs));
		vcpu_regs_get(vm, VCPU_ID, &regs);

		stage_reported = regs.rax & 0xff;

		if (stage_reported == DONE)
			goto done;

		TEST_ASSERT(stage_reported == stage ||
			    stage_reported == SMRAM_STAGE,
			    "Unexpected stage: #%x, got %x",
			    stage, stage_reported);

		state = vcpu_save_state(vm, VCPU_ID);
		kvm_vm_release(vm);
		kvm_vm_restart(vm, O_RDWR);
		vm_vcpu_add(vm, VCPU_ID, 0, 0);
		vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());
		vcpu_load_state(vm, VCPU_ID, state);
		run = vcpu_state(vm, VCPU_ID);
		free(state);
	}

done:
	kvm_vm_free(vm);
}
