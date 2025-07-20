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
#include "vmx.h"

/* The memory slot index to track dirty pages */
#define TEST_MEM_SLOT_INDEX		1
#define TEST_MEM_PAGES			3

/* L1 guest test virtual memory offset */
#define GUEST_TEST_MEM			0xc0000000

/* L2 guest test virtual memory offset */
#define NESTED_TEST_MEM1		0xc0001000
#define NESTED_TEST_MEM2		0xc0002000

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

static void l2_guest_code_ept_enabled(void)
{
	l2_guest_code((u64 *)NESTED_TEST_MEM1, (u64 *)NESTED_TEST_MEM2);
}

static void l2_guest_code_ept_disabled(void)
{
	/* Access the same L1 GPAs as l2_guest_code_ept_enabled() */
	l2_guest_code((u64 *)GUEST_TEST_MEM, (u64 *)GUEST_TEST_MEM);
}

void l1_guest_code(struct vmx_pages *vmx)
{
#define L2_GUEST_STACK_SIZE 64
	unsigned long l2_guest_stack[L2_GUEST_STACK_SIZE];
	void *l2_rip;

	GUEST_ASSERT(vmx->vmcs_gpa);
	GUEST_ASSERT(prepare_for_vmx_operation(vmx));
	GUEST_ASSERT(load_vmcs(vmx));

	if (vmx->eptp_gpa)
		l2_rip = l2_guest_code_ept_enabled;
	else
		l2_rip = l2_guest_code_ept_disabled;

	prepare_vmcs(vmx, l2_rip, &l2_guest_stack[L2_GUEST_STACK_SIZE]);

	GUEST_SYNC(false);
	GUEST_ASSERT(!vmlaunch());
	GUEST_SYNC(false);
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);
	GUEST_DONE();
}

static void test_vmx_dirty_log(bool enable_ept)
{
	vm_vaddr_t vmx_pages_gva = 0;
	struct vmx_pages *vmx;
	unsigned long *bmap;
	uint64_t *host_test_mem;

	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	bool done = false;

	pr_info("Nested EPT: %s\n", enable_ept ? "enabled" : "disabled");

	/* Create VM */
	vm = vm_create_with_one_vcpu(&vcpu, l1_guest_code);
	vmx = vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(vcpu, 1, vmx_pages_gva);

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
	 * Note that prepare_eptp should be called only L1's GPA map is done,
	 * meaning after the last call to virt_map.
	 *
	 * When EPT is disabled, the L2 guest code will still access the same L1
	 * GPAs as the EPT enabled case.
	 */
	if (enable_ept) {
		prepare_eptp(vmx, vm, 0);
		nested_map_memslot(vmx, vm, 0);
		nested_map(vmx, vm, NESTED_TEST_MEM1, GUEST_TEST_MEM, 4096);
		nested_map(vmx, vm, NESTED_TEST_MEM2, GUEST_TEST_MEM, 4096);
	}

	bmap = bitmap_zalloc(TEST_MEM_PAGES);
	host_test_mem = addr_gpa2hva(vm, GUEST_TEST_MEM);

	while (!done) {
		memset(host_test_mem, 0xaa, TEST_MEM_PAGES * 4096);
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
			TEST_ASSERT(host_test_mem[4096 / 8] == 0xaaaaaaaaaaaaaaaaULL, "Page 1 written by guest");
			TEST_ASSERT(!test_bit(2, bmap), "Page 2 incorrectly reported dirty");
			TEST_ASSERT(host_test_mem[8192 / 8] == 0xaaaaaaaaaaaaaaaaULL, "Page 2 written by guest");
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
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_VMX));

	test_vmx_dirty_log(/*enable_ept=*/false);

	if (kvm_cpu_has_ept())
		test_vmx_dirty_log(/*enable_ept=*/true);

	return 0;
}
