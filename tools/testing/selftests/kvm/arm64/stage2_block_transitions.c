// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Google LLC
 * Author: Fuad Tabba <fuad.tabba@linux.dev>
 *
 * stage2_block_transitions - Exercise stage-2 block/page granularity changes
 * that dirty logging forces at fault time, and assert the guest completes.
 *
 * Both scenarios need the fault handler to allocate at fault time (a fresh
 * mapping and/or page-table pages while holding mmu_lock), so a fault path
 * that fails to stage that memory manifests as a KVM_RUN error or, worse, a
 * host crash. The asserted property is host-agnostic: the guest runs the
 * sequence to completion and every KVM_RUN succeeds. On a pKVM host, where a
 * non-protected guest's stage-2 faults are serviced by the pkvm_pgtable_*()
 * backend, the same sequences also guard that backend's fault-time staging.
 *
 * Scenario 1 - block collapse on dirty-logging disable:
 *   A write under dirty logging installs a 4K page; GET_DIRTY_LOG
 *   re-write-protects it; logging is disabled; a second write takes a
 *   permission fault that collapses the page into a hugetlb-backed block,
 *   which requires a fresh mapping object under mmu_lock.
 *
 * Scenario 2 - block split under dirty logging:
 *   Several hugetlb-backed blocks are faulted in as non-executable blocks,
 *   dirty logging is enabled (write-protect only), then the guest executes
 *   into each block. Each instruction fetch takes an execute permission
 *   fault that must split the block into pages during logging, draining
 *   page-table pages. Skipped on CTR_EL0.DIC hardware, where mappings are
 *   made executable eagerly and the execute fault never occurs.
 */
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/mman.h>
#include <linux/sizes.h>
#include <sys/mman.h>

#include <asm/sysreg.h>

#include "kvm_util.h"
#include "processor.h"
#include "test_util.h"
#include "ucall.h"

#define DATA_SLOT		1
#define TEST_GVA		0xc0000000UL
#define BLOCK_SIZE		SZ_2M

/* AArch64 "ret" (ret x30): a self-contained, returnable executable payload. */
#define RET_INSN		0xd65f03c0U

/*
 * A non-protected guest's per-VM stage-2 pool is seeded only with the PGD
 * donation, which stage-2 init immediately consumes, so the page-table budget
 * for a fault that does not top up is just the handful (~2x the stage-2 min
 * pages) of memcache leftovers. Executing into this many distinct blocks
 * demands far more than that budget: a fault path that tops up on every fault
 * completes all of them, one that skips non-write faults runs out mid-sequence.
 */
#define NR_BLOCKS		16

/* Scenario 2 guest -> host sync stages. */
#define STAGE_SKIP_DIC		1
#define STAGE_BLOCKS_READY	2

static void collapse_guest_code(u64 gva)
{
	u64 *data = (u64 *)gva;

	/* Under dirty logging: install a 4K writable page. */
	WRITE_ONCE(*data, 0x1);
	GUEST_SYNC(1);

	/* Logging disabled: a permission fault collapses the page into a block. */
	WRITE_ONCE(*data, 0x2);
	GUEST_SYNC(2);

	GUEST_DONE();
}

static void test_block_collapse(void)
{
	struct kvm_vcpu *vcpu;
	unsigned long *bmap;
	struct kvm_vm *vm;
	struct ucall uc;
	size_t npages;
	u64 gpa;

	vm = vm_create_with_one_vcpu(&vcpu, collapse_guest_code);
	npages = BLOCK_SIZE / vm->page_size;

	gpa = (vm_compute_max_gfn(vm) * vm->page_size) - BLOCK_SIZE;
	gpa = align_down(gpa, BLOCK_SIZE);

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS_HUGETLB_2MB, gpa,
				    DATA_SLOT, npages, KVM_MEM_LOG_DIRTY_PAGES);
	virt_map(vm, TEST_GVA, gpa, npages);
	vcpu_args_set(vcpu, 1, TEST_GVA);

	bmap = bitmap_zalloc(BLOCK_SIZE / getpagesize());

	vcpu_run(vcpu);
	TEST_ASSERT(get_ucall(vcpu, &uc) == UCALL_SYNC && uc.args[1] == 1,
		    "Expected first sync, got cmd %lu arg %lu", uc.cmd, uc.args[1]);

	/* GET_DIRTY_LOG re-write-protects the dirtied page; then stop logging. */
	kvm_vm_get_dirty_log(vm, DATA_SLOT, bmap);
	vm_mem_region_set_flags(vm, DATA_SLOT, 0);

	/* The collapsing permission fault: a broken fault path faults here. */
	vcpu_run(vcpu);
	TEST_ASSERT(get_ucall(vcpu, &uc) == UCALL_SYNC && uc.args[1] == 2,
		    "Expected second sync, got cmd %lu arg %lu", uc.cmd, uc.args[1]);

	vcpu_run(vcpu);
	TEST_ASSERT(get_ucall(vcpu, &uc) == UCALL_DONE,
		    "Expected done, got cmd %lu", uc.cmd);

	free(bmap);
	kvm_vm_free(vm);
}

static void guest_sync_insn(u64 va)
{
	/* Make the just-written instruction coherent for execution (!DIC). */
	asm volatile("dc cvau, %0\n"
		     "dsb ish\n"
		     "ic ivau, %0\n"
		     "dsb ish\n"
		     "isb\n"
		     :: "r" (va) : "memory");
}

static void split_guest_code(u64 base_gva, u64 nblocks)
{
	u64 i, va;

	if (FIELD_GET(CTR_EL0_DIC_MASK, read_sysreg(ctr_el0))) {
		GUEST_SYNC(STAGE_SKIP_DIC);
		GUEST_DONE();
		return;
	}

	/* Fault in each block (non-executable) and stage an executable payload. */
	for (i = 0; i < nblocks; i++) {
		va = base_gva + i * BLOCK_SIZE;
		WRITE_ONCE(*(u32 *)va, RET_INSN);
		guest_sync_insn(va);
	}
	GUEST_SYNC(STAGE_BLOCKS_READY);

	/* Logging is now on: executing into each block splits it into pages. */
	for (i = 0; i < nblocks; i++) {
		va = base_gva + i * BLOCK_SIZE;
		((void (*)(void))va)();
	}

	GUEST_DONE();
}

static void test_exec_split_drain(void)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	size_t npages;
	u64 gpa;

	vm = vm_create_with_one_vcpu(&vcpu, split_guest_code);
	npages = NR_BLOCKS * (BLOCK_SIZE / vm->page_size);

	gpa = (vm_compute_max_gfn(vm) * vm->page_size) - NR_BLOCKS * BLOCK_SIZE;
	gpa = align_down(gpa, BLOCK_SIZE);

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS_HUGETLB_2MB, gpa,
				    DATA_SLOT, npages, 0);
	virt_map(vm, TEST_GVA, gpa, npages);
	vcpu_args_set(vcpu, 2, TEST_GVA, (u64)NR_BLOCKS);

	vcpu_run(vcpu);
	TEST_ASSERT(get_ucall(vcpu, &uc) == UCALL_SYNC,
		    "Expected sync, got cmd %lu", uc.cmd);
	if (uc.args[1] == STAGE_SKIP_DIC) {
		ksft_print_msg("SKIP block split: CTR_EL0.DIC == 1\n");
		kvm_vm_free(vm);
		return;
	}
	TEST_ASSERT(uc.args[1] == STAGE_BLOCKS_READY,
		    "Expected blocks-ready sync, got arg %lu", uc.args[1]);

	/* Write-protect the blocks; the guest then splits them by executing. */
	vm_mem_region_set_flags(vm, DATA_SLOT, KVM_MEM_LOG_DIRTY_PAGES);

	vcpu_run(vcpu);
	TEST_ASSERT(get_ucall(vcpu, &uc) == UCALL_DONE,
		    "Expected done, got cmd %lu", uc.cmd);

	kvm_vm_free(vm);
}

/*
 * The explicit-size hugetlb backing hard-fails region creation if the pages
 * are not already reserved, so probe here and skip rather than abort. The
 * peak reservation is scenario 2's; the two scenarios run and free in turn.
 */
static void require_hugepages(size_t bytes)
{
	void *mem = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB,
			 -1, 0);

	if (mem == MAP_FAILED)
		ksft_exit_skip("Need %zu bytes of reserved 2M hugepages\n", bytes);
	munmap(mem, bytes);
}

int main(void)
{
	require_hugepages(NR_BLOCKS * BLOCK_SIZE);

	test_block_collapse();
	test_exec_split_drain();

	ksft_print_msg("All ok!\n");
	return 0;
}
