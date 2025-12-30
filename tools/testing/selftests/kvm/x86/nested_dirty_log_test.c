// SPDX-License-Identifier: GPL-2.0
/*
 * KVM dirty page logging test
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"
#include "vmx.h"

/* The memory slot index to track dirty pages */
#define TEST_MEM_SLOT_INDEX		1
#define TEST_MEM_PAGES			3

/* L1 guest test virtual memory offset */
#define GUEST_TEST_MEM			0xc0000000

/* L2 guest test virtual memory offset */
#define NESTED_TEST_MEM1		0xc0001000
#define NESTED_TEST_MEM2		0xc0002000

#define L2_GUEST_STACK_SIZE 64

static void l2_guest_code(u64 *a, u64 *b)
{
	READ_ONCE(*a);
	WRITE_ONCE(*a, 1);
	GUEST_SYNC(true);
	GUEST_SYNC(false);

	WRITE_ONCE(*b, 1);
	GUEST_SYNC(true);
	WRITE_ONCE(*b, 1);
	GUEST_SYNC(true);
	GUEST_SYNC(false);

	/* Exit to L1 and never come back.  */
	vmcall();
}

static void l2_guest_code_tdp_enabled(void)
{
	l2_guest_code((u64 *)NESTED_TEST_MEM1, (u64 *)NESTED_TEST_MEM2);
}

static void l2_guest_code_tdp_disabled(void)
{
	/* Access the same L1 GPAs as l2_guest_code_tdp_enabled() */
	l2_guest_code((u64 *)GUEST_TEST_MEM, (u64 *)GUEST_TEST_MEM);
}

void l1_vmx_code(struct vmx_pages *vmx)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	void *l2_rip;

	GUEST_ASSERT(vmx->vmcs_gpa);
	GUEST_ASSERT(prepare_for_vmx_operation(vmx));
	GUEST_ASSERT(load_vmcs(vmx));

	if (vmx->eptp_gpa)
		l2_rip = l2_guest_code_tdp_enabled;
	else
		l2_rip = l2_guest_code_tdp_disabled;

	prepare_vmcs(vmx, l2_rip, &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	GUEST_SYNC(false);
	GUEST_ASSERT(!vmlaunch());
	GUEST_SYNC(false);
	GUEST_ASSERT_EQ(vmreadz(VM_EXIT_REASON), EXIT_REASON_VMCALL);
	GUEST_DONE();
}

static void l1_svm_code(struct svm_test_data *svm)
{
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	void *l2_rip;

	if (svm->ncr3_gpa)
		l2_rip = l2_guest_code_tdp_enabled;
	else
		l2_rip = l2_guest_code_tdp_disabled;

	generic_svm_setup(svm, l2_rip, &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	GUEST_SYNC(false);
	run_guest(svm->vmcb, svm->vmcb_gpa);
	GUEST_SYNC(false);
	GUEST_ASSERT_EQ(svm->vmcb->control.exit_code, SVM_EXIT_VMMCALL);
	GUEST_DONE();
}

static void l1_guest_code(void *data)
{
	if (this_cpu_has(X86_FEATURE_VMX))
		l1_vmx_code(data);
	else
		l1_svm_code(data);
}

static void test_dirty_log(bool nested_tdp)
{
	vm_vaddr_t nested_gva = 0;
	unsigned long *bmap;
	uint64_t *host_test_mem;

	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	bool done = false;

	pr_info("Nested TDP: %s\n", nested_tdp ? "enabled" : "disabled");

	/* Create VM */
	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);
	if (nested_tdp)
		vm_enable_tdp(vm);

	if (kvm_cpu_has(X86_FEATURE_VMX))
		vcpu_alloc_vmx(vm, &nested_gva);
	else
		vcpu_alloc_svm(vm, &nested_gva);

	vcpu_args_set(vcpu, 1, nested_gva);

	/* Add an extra memory slot for testing dirty logging */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    GUEST_TEST_MEM,
				    TEST_MEM_SLOT_INDEX,
				    TEST_MEM_PAGES,
				    KVM_MEM_LOG_DIRTY_PAGES);

	/*
	 * Add an identity map for GVA range [0xc0000000, 0xc0002000).  This
	 * affects both L1 and L2.  However...
	 */
	virt_map(vm, GUEST_TEST_MEM, GUEST_TEST_MEM, TEST_MEM_PAGES);

	/*
	 * ... pages in the L2 GPA range [0xc0001000, 0xc0003000) will map to
	 * 0xc0000000.
	 *
	 * When TDP is disabled, the L2 guest code will still access the same L1
	 * GPAs as the TDP enabled case.
	 */
	if (nested_tdp) {
		tdp_identity_map_default_memslots(vm);
		tdp_map(vm, NESTED_TEST_MEM1, GUEST_TEST_MEM, PAGE_SIZE);
		tdp_map(vm, NESTED_TEST_MEM2, GUEST_TEST_MEM, PAGE_SIZE);
	}

	bmap = bitmap_zalloc(TEST_MEM_PAGES);
	host_test_mem = addr_gpa2hva(vm, GUEST_TEST_MEM);

	while (!done) {
		memset(host_test_mem, 0xaa, TEST_MEM_PAGES * PAGE_SIZE);
		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			/* NOT REACHED */
		case UCALL_SYNC:
			/*
			 * The nested guest wrote at offset 0x1000 in the memslot, but the
			 * dirty bitmap must be filled in according to L1 GPA, not L2.
			 */
			kvm_vm_get_dirty_log(vm, TEST_MEM_SLOT_INDEX, bmap);
			if (uc.args[1]) {
				TEST_ASSERT(test_bit(0, bmap), "Page 0 incorrectly reported clean");
				TEST_ASSERT(host_test_mem[0] == 1, "Page 0 not written by guest");
			} else {
				TEST_ASSERT(!test_bit(0, bmap), "Page 0 incorrectly reported dirty");
				TEST_ASSERT(host_test_mem[0] == 0xaaaaaaaaaaaaaaaaULL, "Page 0 written by guest");
			}

			TEST_ASSERT(!test_bit(1, bmap), "Page 1 incorrectly reported dirty");
			TEST_ASSERT(host_test_mem[PAGE_SIZE / 8] == 0xaaaaaaaaaaaaaaaaULL, "Page 1 written by guest");
			TEST_ASSERT(!test_bit(2, bmap), "Page 2 incorrectly reported dirty");
			TEST_ASSERT(host_test_mem[PAGE_SIZE*2 / 8] == 0xaaaaaaaaaaaaaaaaULL, "Page 2 written by guest");
			break;
		case UCALL_DONE:
			done = true;
			break;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}
	}
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX) || kvm_cpu_has(X86_FEATURE_SVM));

	test_dirty_log(/*nested_tdp=*/false);

	if (kvm_cpu_has_tdp())
		test_dirty_log(/*nested_tdp=*/true);

	return 0;
}
