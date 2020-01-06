// SPDX-License-Identifier: GPL-2.0
/*
 * KVM dirty page logging test
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */

#define _GNU_SOURCE /* for program_invocation_name */

#include <stdio.h>
#include <stdlib.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#define VCPU_ID				1

/* The memory slot index to track dirty pages */
#define TEST_MEM_SLOT_INDEX		1
#define TEST_MEM_SIZE			3

/* L1 guest test virtual memory offset */
#define GUEST_TEST_MEM			0xc0000000

/* L2 guest test virtual memory offset */
#define NESTED_TEST_MEM1		0xc0001000
#define NESTED_TEST_MEM2		0xc0002000

static void l2_guest_code(void)
{
	*(volatile uint64_t *)NESTED_TEST_MEM1;
	*(volatile uint64_t *)NESTED_TEST_MEM1 = 1;
	GUEST_SYNC(true);
	GUEST_SYNC(false);

	*(volatile uint64_t *)NESTED_TEST_MEM2 = 1;
	GUEST_SYNC(true);
	*(volatile uint64_t *)NESTED_TEST_MEM2 = 1;
	GUEST_SYNC(true);
	GUEST_SYNC(false);

	/* Exit to L1 and never come back.  */
	vmcall();
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

	GUEST_SYNC(false);
	GUEST_ASSERT(!vmlaunch());
	GUEST_SYNC(false);
	GUEST_ASSERT(vmreadz(VM_EXIT_REASON) == EXIT_REASON_VMCALL);
	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	vm_vaddr_t vmx_pages_gva = 0;
	struct vmx_pages *vmx;
	unsigned long *bmap;
	uint64_t *host_test_mem;

	struct kvm_vm *vm;
	struct kvm_run *run;
	struct ucall uc;
	bool done = false;

	nested_vmx_check_supported();

	/* Create VM */
	vm = vm_create_default(VCPU_ID, 0, l1_guest_code);
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());
	vmx = vcpu_alloc_vmx(vm, &vmx_pages_gva);
	vcpu_args_set(vm, VCPU_ID, 1, vmx_pages_gva);
	run = vcpu_state(vm, VCPU_ID);

	/* Add an extra memory slot for testing dirty logging */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    GUEST_TEST_MEM,
				    TEST_MEM_SLOT_INDEX,
				    TEST_MEM_SIZE,
				    KVM_MEM_LOG_DIRTY_PAGES);

	/*
	 * Add an identity map for GVA range [0xc0000000, 0xc0002000).  This
	 * affects both L1 and L2.  However...
	 */
	virt_map(vm, GUEST_TEST_MEM, GUEST_TEST_MEM,
		 TEST_MEM_SIZE * 4096, 0);

	/*
	 * ... pages in the L2 GPA range [0xc0001000, 0xc0003000) will map to
	 * 0xc0000000.
	 *
	 * Note that prepare_eptp should be called only L1's GPA map is done,
	 * meaning after the last call to virt_map.
	 */
	prepare_eptp(vmx, vm, 0);
	nested_map_memslot(vmx, vm, 0, 0);
	nested_map(vmx, vm, NESTED_TEST_MEM1, GUEST_TEST_MEM, 4096, 0);
	nested_map(vmx, vm, NESTED_TEST_MEM2, GUEST_TEST_MEM, 4096, 0);

	bmap = bitmap_alloc(TEST_MEM_SIZE);
	host_test_mem = addr_gpa2hva(vm, GUEST_TEST_MEM);

	while (!done) {
		memset(host_test_mem, 0xaa, TEST_MEM_SIZE * 4096);
		_vcpu_run(vm, VCPU_ID);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Unexpected exit reason: %u (%s),\n",
			    run->exit_reason,
			    exit_reason_str(run->exit_reason));

		switch (get_ucall(vm, VCPU_ID, &uc)) {
		case UCALL_ABORT:
			TEST_ASSERT(false, "%s at %s:%d", (const char *)uc.args[0],
				    __FILE__, uc.args[1]);
			/* NOT REACHED */
		case UCALL_SYNC:
			/*
			 * The nested guest wrote at offset 0x1000 in the memslot, but the
			 * dirty bitmap must be filled in according to L1 GPA, not L2.
			 */
			kvm_vm_get_dirty_log(vm, TEST_MEM_SLOT_INDEX, bmap);
			if (uc.args[1]) {
				TEST_ASSERT(test_bit(0, bmap), "Page 0 incorrectly reported clean\n");
				TEST_ASSERT(host_test_mem[0] == 1, "Page 0 not written by guest\n");
			} else {
				TEST_ASSERT(!test_bit(0, bmap), "Page 0 incorrectly reported dirty\n");
				TEST_ASSERT(host_test_mem[0] == 0xaaaaaaaaaaaaaaaaULL, "Page 0 written by guest\n");
			}

			TEST_ASSERT(!test_bit(1, bmap), "Page 1 incorrectly reported dirty\n");
			TEST_ASSERT(host_test_mem[4096 / 8] == 0xaaaaaaaaaaaaaaaaULL, "Page 1 written by guest\n");
			TEST_ASSERT(!test_bit(2, bmap), "Page 2 incorrectly reported dirty\n");
			TEST_ASSERT(host_test_mem[8192 / 8] == 0xaaaaaaaaaaaaaaaaULL, "Page 2 written by guest\n");
			break;
		case UCALL_DONE:
			done = true;
			break;
		default:
			TEST_ASSERT(false, "Unknown ucall 0x%x.", uc.cmd);
		}
	}
}
