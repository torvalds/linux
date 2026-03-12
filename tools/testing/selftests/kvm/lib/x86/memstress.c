// SPDX-License-Identifier: GPL-2.0
/*
 * x86-specific extensions to memstress.c.
 *
 * Copyright (C) 2022, Google, Inc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>

#include "test_util.h"
#include "kvm_util.h"
#include "memstress.h"
#include "processor.h"
#include "svm_util.h"
#include "vmx.h"

void memstress_l2_guest_code(uint64_t vcpu_id)
{
	memstress_guest_code(vcpu_id);
	vmcall();
}

extern char memstress_l2_guest_entry[];
__asm__(
"memstress_l2_guest_entry:"
"	mov (%rsp), %rdi;"
"	call memstress_l2_guest_code;"
"	ud2;"
);

#define L2_GUEST_STACK_SIZE 64

static void l1_vmx_code(struct vmx_pages *vmx, uint64_t vcpu_id)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	unsigned long *rsp;

	GUEST_ASSERT(vmx->vmcs_gpa);
	GUEST_ASSERT(prepare_for_vmx_operation(vmx));
	GUEST_ASSERT(load_vmcs(vmx));
	GUEST_ASSERT(ept_1g_pages_supported());

	rsp = &l2_guest_stack[L2_GUEST_STACK_SIZE - 1];
	*rsp = vcpu_id;
	prepare_vmcs(vmx, memstress_l2_guest_entry, rsp);

	GUEST_ASSERT(!vmlaunch());
	GUEST_ASSERT_EQ(vmreadz(VM_EXIT_REASON), EXIT_REASON_VMCALL);
	GUEST_DONE();
}

static void l1_svm_code(struct svm_test_data *svm, uint64_t vcpu_id)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	unsigned long *rsp;


	rsp = &l2_guest_stack[L2_GUEST_STACK_SIZE - 1];
	*rsp = vcpu_id;
	generic_svm_setup(svm, memstress_l2_guest_entry, rsp);

	run_guest(svm->vmcb, svm->vmcb_gpa);
	GUEST_ASSERT_EQ(svm->vmcb->control.exit_code, SVM_EXIT_VMMCALL);
	GUEST_DONE();
}


static void memstress_l1_guest_code(void *data, uint64_t vcpu_id)
{
	if (this_cpu_has(X86_FEATURE_VMX))
		l1_vmx_code(data, vcpu_id);
	else
		l1_svm_code(data, vcpu_id);
}

uint64_t memstress_nested_pages(int nr_vcpus)
{
	/*
	 * 513 page tables is enough to identity-map 256 TiB of L2 with 1G
	 * pages and 4-level paging, plus a few pages per-vCPU for data
	 * structures such as the VMCS.
	 */
	return 513 + 10 * nr_vcpus;
}

static void memstress_setup_ept_mappings(struct kvm_vm *vm)
{
	uint64_t start, end;

	/*
	 * Identity map the first 4G and the test region with 1G pages so that
	 * KVM can shadow the EPT12 with the maximum huge page size supported
	 * by the backing source.
	 */
	tdp_identity_map_1g(vm, 0, 0x100000000ULL);

	start = align_down(memstress_args.gpa, PG_SIZE_1G);
	end = align_up(memstress_args.gpa + memstress_args.size, PG_SIZE_1G);
	tdp_identity_map_1g(vm, start, end - start);
}

void memstress_setup_nested(struct kvm_vm *vm, int nr_vcpus, struct kvm_vcpu *vcpus[])
{
	struct kvm_regs regs;
	vm_vaddr_t nested_gva;
	int vcpu_id;

	TEST_REQUIRE(kvm_cpu_has_tdp());

	vm_enable_tdp(vm);
	memstress_setup_ept_mappings(vm);
	for (vcpu_id = 0; vcpu_id < nr_vcpus; vcpu_id++) {
		if (kvm_cpu_has(X86_FEATURE_VMX))
			vcpu_alloc_vmx(vm, &nested_gva);
		else
			vcpu_alloc_svm(vm, &nested_gva);

		/*
		 * Override the vCPU to run memstress_l1_guest_code() which will
		 * bounce it into L2 before calling memstress_guest_code().
		 */
		vcpu_regs_get(vcpus[vcpu_id], &regs);
		regs.rip = (unsigned long) memstress_l1_guest_code;
		vcpu_regs_set(vcpus[vcpu_id], &regs);
		vcpu_args_set(vcpus[vcpu_id], 2, nested_gva, vcpu_id);
	}
}
