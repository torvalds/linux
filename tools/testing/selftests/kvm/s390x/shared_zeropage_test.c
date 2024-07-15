// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test shared zeropage handling (with/without storage keys)
 *
 * Copyright (C) 2024, Red Hat, Inc.
 */
#include <sys/mman.h>

#include <linux/fs.h>

#include "test_util.h"
#include "kvm_util.h"
#include "kselftest.h"
#include "ucall_common.h"

static void set_storage_key(void *addr, uint8_t skey)
{
	asm volatile("sske %0,%1" : : "d" (skey), "a" (addr));
}

static void guest_code(void)
{
	/* Issue some storage key instruction. */
	set_storage_key((void *)0, 0x98);
	GUEST_DONE();
}

/*
 * Returns 1 if the shared zeropage is mapped, 0 if something else is mapped.
 * Returns < 0 on error or if nothing is mapped.
 */
static int maps_shared_zeropage(int pagemap_fd, void *addr)
{
	struct page_region region;
	struct pm_scan_arg arg = {
		.start = (uintptr_t)addr,
		.end = (uintptr_t)addr + 4096,
		.vec = (uintptr_t)&region,
		.vec_len = 1,
		.size = sizeof(struct pm_scan_arg),
		.category_mask = PAGE_IS_PFNZERO,
		.category_anyof_mask = PAGE_IS_PRESENT,
		.return_mask = PAGE_IS_PFNZERO,
	};
	return ioctl(pagemap_fd, PAGEMAP_SCAN, &arg);
}

int main(int argc, char *argv[])
{
	char *mem, *page0, *page1, *page2, tmp;
	const size_t pagesize = getpagesize();
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;
	int pagemap_fd;

	ksft_print_header();
	ksft_set_plan(3);

	/*
	 * We'll use memory that is not mapped into the VM for simplicity.
	 * Shared zeropages are enabled/disabled per-process.
	 */
	mem = mmap(0, 3 * pagesize, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);
	TEST_ASSERT(mem != MAP_FAILED, "mmap() failed");

	/* Disable THP. Ignore errors on older kernels. */
	madvise(mem, 3 * pagesize, MADV_NOHUGEPAGE);

	page0 = mem;
	page1 = page0 + pagesize;
	page2 = page1 + pagesize;

	/* Can we even detect shared zeropages? */
	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	TEST_REQUIRE(pagemap_fd >= 0);

	tmp = *page0;
	asm volatile("" : "+r" (tmp));
	TEST_REQUIRE(maps_shared_zeropage(pagemap_fd, page0) == 1);

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	/* Verify that we get the shared zeropage after VM creation. */
	tmp = *page1;
	asm volatile("" : "+r" (tmp));
	ksft_test_result(maps_shared_zeropage(pagemap_fd, page1) == 1,
			 "Shared zeropages should be enabled\n");

	/*
	 * Let our VM execute a storage key instruction that should
	 * unshare all shared zeropages.
	 */
	vcpu_run(vcpu);
	get_ucall(vcpu, &uc);
	TEST_ASSERT_EQ(uc.cmd, UCALL_DONE);

	/* Verify that we don't have a shared zeropage anymore. */
	ksft_test_result(!maps_shared_zeropage(pagemap_fd, page1),
			 "Shared zeropage should be gone\n");

	/* Verify that we don't get any new shared zeropages. */
	tmp = *page2;
	asm volatile("" : "+r" (tmp));
	ksft_test_result(!maps_shared_zeropage(pagemap_fd, page2),
			 "Shared zeropages should be disabled\n");

	kvm_vm_free(vm);

	ksft_finished();
}
