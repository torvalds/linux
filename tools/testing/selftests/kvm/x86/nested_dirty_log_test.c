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

/*
 * Allocate four pages total.  Two pages are used to verify that the KVM marks
 * the accessed page/GFN as marked dirty, but not the "other" page.  Times two
 * so that each "normal" page can be accessed from L2 via an aliased L2 GVA+GPA
 * (when TDP is enabled), to verify KVM marks _L1's_ page/GFN as dirty (to
 * detect failures, L2 => L1 GPAs can't be identity mapped in the TDP page
 * tables, as marking L2's GPA dirty would get a false pass if L1 == L2).
 */
#define TEST_MEM_PAGES			4

#define TEST_MEM_BASE			0xc0000000
#define TEST_MEM_ALIAS_BASE		0xc0002000

#define TEST_GUEST_ADDR(base, idx)	((base) + (idx) * PAGE_SIZE)

#define TEST_GVA(idx)			TEST_GUEST_ADDR(TEST_MEM_BASE, idx)
#define TEST_GPA(idx)			TEST_GUEST_ADDR(TEST_MEM_BASE, idx)

#define TEST_ALIAS_GPA(idx)		TEST_GUEST_ADDR(TEST_MEM_ALIAS_BASE, idx)

#define TEST_HVA(vm, idx)		addr_gpa2hva(vm, TEST_GPA(idx))

#define L2_GUEST_STACK_SIZE 64

/* Use the page offset bits to communicate the access+fault type. */
#define TEST_SYNC_READ_FAULT		BIT(0)
#define TEST_SYNC_WRITE_FAULT		BIT(1)
#define TEST_SYNC_NO_FAULT		BIT(2)

static void l2_guest_code(vm_vaddr_t base)
{
	vm_vaddr_t page0 = TEST_GUEST_ADDR(base, 0);
	vm_vaddr_t page1 = TEST_GUEST_ADDR(base, 1);

	READ_ONCE(*(u64 *)page0);
	GUEST_SYNC(page0 | TEST_SYNC_READ_FAULT);
	WRITE_ONCE(*(u64 *)page0, 1);
	GUEST_SYNC(page0 | TEST_SYNC_WRITE_FAULT);
	READ_ONCE(*(u64 *)page0);
	GUEST_SYNC(page0 | TEST_SYNC_NO_FAULT);

	WRITE_ONCE(*(u64 *)page1, 1);
	GUEST_SYNC(page1 | TEST_SYNC_WRITE_FAULT);
	WRITE_ONCE(*(u64 *)page1, 1);
	GUEST_SYNC(page1 | TEST_SYNC_WRITE_FAULT);
	READ_ONCE(*(u64 *)page1);
	GUEST_SYNC(page1 | TEST_SYNC_NO_FAULT);

	/* Exit to L1 and never come back.  */
	vmcall();
}

static void l2_guest_code_tdp_enabled(void)
{
	/*
	 * Use the aliased virtual addresses when running with TDP to verify
	 * that KVM correctly handles the case where a page is dirtied via a
	 * different GPA than would be used by L1.
	 */
	l2_guest_code(TEST_MEM_ALIAS_BASE);
}

static void l2_guest_code_tdp_disabled(void)
{
	/*
	 * Use the "normal" virtual addresses when running without TDP enabled,
	 * in which case L2 will use the same page tables as L1, and thus needs
	 * to use the same virtual addresses that are mapped into L1.
	 */
	l2_guest_code(TEST_MEM_BASE);
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

	GUEST_SYNC(TEST_SYNC_NO_FAULT);
	GUEST_ASSERT(!vmlaunch());
	GUEST_SYNC(TEST_SYNC_NO_FAULT);
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

	GUEST_SYNC(TEST_SYNC_NO_FAULT);
	run_guest(svm->vmcb, svm->vmcb_gpa);
	GUEST_SYNC(TEST_SYNC_NO_FAULT);
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

static void test_handle_ucall_sync(struct kvm_vm *vm, u64 arg,
				   unsigned long *bmap)
{
	vm_vaddr_t gva = arg & ~(PAGE_SIZE - 1);
	int page_nr, i;

	/*
	 * Extract the page number of underlying physical page, which is also
	 * the _L1_ page number.  The dirty bitmap _must_ be updated based on
	 * the L1 GPA, not L2 GPA, i.e. whether or not L2 used an aliased GPA
	 * (i.e. if TDP enabled for L2) is irrelevant with respect to the dirty
	 * bitmap and which underlying physical page is accessed.
	 *
	 * Note, gva will be '0' if there was no access, i.e. if the purpose of
	 * the sync is to verify all pages are clean.
	 */
	if (!gva)
		page_nr = 0;
	else if (gva >= TEST_MEM_ALIAS_BASE)
		page_nr = (gva - TEST_MEM_ALIAS_BASE) >> PAGE_SHIFT;
	else
		page_nr = (gva - TEST_MEM_BASE) >> PAGE_SHIFT;
	TEST_ASSERT(page_nr == 0 || page_nr == 1,
		    "Test bug, unexpected frame number '%u' for arg = %lx", page_nr, arg);
	TEST_ASSERT(gva || (arg & TEST_SYNC_NO_FAULT),
		    "Test bug, gva must be valid if a fault is expected");

	kvm_vm_get_dirty_log(vm, TEST_MEM_SLOT_INDEX, bmap);

	/*
	 * Check all pages to verify the correct physical page was modified (or
	 * not), and that all pages are clean/dirty as expected.
	 *
	 * If a fault of any kind is expected, the target page should be dirty
	 * as the Dirty bit is set in the gPTE.  KVM should create a writable
	 * SPTE even on a read fault, *and* KVM must mark the GFN as dirty
	 * when doing so.
	 */
	for (i = 0; i < TEST_MEM_PAGES; i++) {
		if (i == page_nr && (arg & TEST_SYNC_WRITE_FAULT))
			TEST_ASSERT(*(u64 *)TEST_HVA(vm, i) == 1,
				    "Page %u incorrectly not written by guest", i);
		else
			TEST_ASSERT(*(u64 *)TEST_HVA(vm, i) == 0xaaaaaaaaaaaaaaaaULL,
				    "Page %u incorrectly written by guest", i);

		if (i == page_nr && !(arg & TEST_SYNC_NO_FAULT))
			TEST_ASSERT(test_bit(i, bmap),
				    "Page %u incorrectly reported clean on %s fault",
				    i, arg & TEST_SYNC_READ_FAULT ? "read" : "write");
		else
			TEST_ASSERT(!test_bit(i, bmap),
				    "Page %u incorrectly reported dirty", i);
	}
}

static void test_dirty_log(bool nested_tdp)
{
	vm_vaddr_t nested_gva = 0;
	unsigned long *bmap;
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
				    TEST_MEM_BASE,
				    TEST_MEM_SLOT_INDEX,
				    TEST_MEM_PAGES,
				    KVM_MEM_LOG_DIRTY_PAGES);

	/*
	 * Add an identity map for GVA range [0xc0000000, 0xc0004000).  This
	 * affects both L1 and L2.  However...
	 */
	virt_map(vm, TEST_MEM_BASE, TEST_MEM_BASE, TEST_MEM_PAGES);

	/*
	 * ... pages in the L2 GPA address range [0xc0002000, 0xc0004000) will
	 * map to [0xc0000000, 0xc0002000) when TDP is enabled (for L2).
	 *
	 * When TDP is disabled, the L2 guest code will still access the same L1
	 * GPAs as the TDP enabled case.
	 *
	 * Set the Dirty bit in the PTEs used by L2 so that KVM will create
	 * writable SPTEs when handling read faults (if the Dirty bit isn't
	 * set, KVM must intercept the next write to emulate the Dirty bit
	 * update).
	 */
	if (nested_tdp) {
		tdp_identity_map_default_memslots(vm);
		tdp_map(vm, TEST_ALIAS_GPA(0), TEST_GPA(0), PAGE_SIZE);
		tdp_map(vm, TEST_ALIAS_GPA(1), TEST_GPA(1), PAGE_SIZE);

		*tdp_get_pte(vm, TEST_ALIAS_GPA(0)) |= PTE_DIRTY_MASK(&vm->stage2_mmu);
		*tdp_get_pte(vm, TEST_ALIAS_GPA(1)) |= PTE_DIRTY_MASK(&vm->stage2_mmu);
	} else {
		*vm_get_pte(vm, TEST_GVA(0)) |= PTE_DIRTY_MASK(&vm->mmu);
		*vm_get_pte(vm, TEST_GVA(1)) |= PTE_DIRTY_MASK(&vm->mmu);
	}

	bmap = bitmap_zalloc(TEST_MEM_PAGES);

	while (!done) {
		memset(TEST_HVA(vm, 0), 0xaa, TEST_MEM_PAGES * PAGE_SIZE);

		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			/* NOT REACHED */
		case UCALL_SYNC:
			test_handle_ucall_sync(vm, uc.args[1], bmap);
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
