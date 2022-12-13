// SPDX-License-Identifier: GPL-2.0-only
/*
 * tools/testing/selftests/kvm/nx_huge_page_test.c
 *
 * Usage: to be run via nx_huge_page_test.sh, which does the necessary
 * environment setup and teardown
 *
 * Copyright (C) 2022, Google LLC.
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdint.h>
#include <time.h>

#include <test_util.h>
#include "kvm_util.h"
#include "processor.h"

#define HPAGE_SLOT		10
#define HPAGE_GPA		(4UL << 30) /* 4G prevents collision w/ slot 0 */
#define HPAGE_GVA		HPAGE_GPA /* GVA is arbitrary, so use GPA. */
#define PAGES_PER_2MB_HUGE_PAGE 512
#define HPAGE_SLOT_NPAGES	(3 * PAGES_PER_2MB_HUGE_PAGE)

/*
 * Passed by nx_huge_pages_test.sh to provide an easy warning if this test is
 * being run without it.
 */
#define MAGIC_TOKEN 887563923

/*
 * x86 opcode for the return instruction. Used to call into, and then
 * immediately return from, memory backed with hugepages.
 */
#define RETURN_OPCODE 0xC3

/* Call the specified memory address. */
static void guest_do_CALL(uint64_t target)
{
	((void (*)(void)) target)();
}

/*
 * Exit the VM after each memory access so that the userspace component of the
 * test can make assertions about the pages backing the VM.
 *
 * See the below for an explanation of how each access should affect the
 * backing mappings.
 */
void guest_code(void)
{
	uint64_t hpage_1 = HPAGE_GVA;
	uint64_t hpage_2 = hpage_1 + (PAGE_SIZE * 512);
	uint64_t hpage_3 = hpage_2 + (PAGE_SIZE * 512);

	READ_ONCE(*(uint64_t *)hpage_1);
	GUEST_SYNC(1);

	READ_ONCE(*(uint64_t *)hpage_2);
	GUEST_SYNC(2);

	guest_do_CALL(hpage_1);
	GUEST_SYNC(3);

	guest_do_CALL(hpage_3);
	GUEST_SYNC(4);

	READ_ONCE(*(uint64_t *)hpage_1);
	GUEST_SYNC(5);

	READ_ONCE(*(uint64_t *)hpage_3);
	GUEST_SYNC(6);
}

static void check_2m_page_count(struct kvm_vm *vm, int expected_pages_2m)
{
	int actual_pages_2m;

	actual_pages_2m = vm_get_stat(vm, "pages_2m");

	TEST_ASSERT(actual_pages_2m == expected_pages_2m,
		    "Unexpected 2m page count. Expected %d, got %d",
		    expected_pages_2m, actual_pages_2m);
}

static void check_split_count(struct kvm_vm *vm, int expected_splits)
{
	int actual_splits;

	actual_splits = vm_get_stat(vm, "nx_lpage_splits");

	TEST_ASSERT(actual_splits == expected_splits,
		    "Unexpected NX huge page split count. Expected %d, got %d",
		    expected_splits, actual_splits);
}

static void wait_for_reclaim(int reclaim_period_ms)
{
	long reclaim_wait_ms;
	struct timespec ts;

	reclaim_wait_ms = reclaim_period_ms * 5;
	ts.tv_sec = reclaim_wait_ms / 1000;
	ts.tv_nsec = (reclaim_wait_ms - (ts.tv_sec * 1000)) * 1000000;
	nanosleep(&ts, NULL);
}

void run_test(int reclaim_period_ms, bool disable_nx_huge_pages,
	      bool reboot_permissions)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	uint64_t nr_bytes;
	void *hva;
	int r;

	vm = vm_create(1);

	if (disable_nx_huge_pages) {
		r = __vm_disable_nx_huge_pages(vm);
		if (reboot_permissions) {
			TEST_ASSERT(!r, "Disabling NX huge pages should succeed if process has reboot permissions");
		} else {
			TEST_ASSERT(r == -1 && errno == EPERM,
				    "This process should not have permission to disable NX huge pages");
			return;
		}
	}

	vcpu = vm_vcpu_add(vm, 0, guest_code);

	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS_HUGETLB,
				    HPAGE_GPA, HPAGE_SLOT,
				    HPAGE_SLOT_NPAGES, 0);

	nr_bytes = HPAGE_SLOT_NPAGES * vm->page_size;

	/*
	 * Ensure that KVM can map HPAGE_SLOT with huge pages by mapping the
	 * region into the guest with 2MiB pages whenever TDP is disabled (i.e.
	 * whenever KVM is shadowing the guest page tables).
	 *
	 * When TDP is enabled, KVM should be able to map HPAGE_SLOT with huge
	 * pages irrespective of the guest page size, so map with 4KiB pages
	 * to test that that is the case.
	 */
	if (kvm_is_tdp_enabled())
		virt_map_level(vm, HPAGE_GVA, HPAGE_GPA, nr_bytes, PG_LEVEL_4K);
	else
		virt_map_level(vm, HPAGE_GVA, HPAGE_GPA, nr_bytes, PG_LEVEL_2M);

	hva = addr_gpa2hva(vm, HPAGE_GPA);
	memset(hva, RETURN_OPCODE, nr_bytes);

	check_2m_page_count(vm, 0);
	check_split_count(vm, 0);

	/*
	 * The guest code will first read from the first hugepage, resulting
	 * in a huge page mapping being created.
	 */
	vcpu_run(vcpu);
	check_2m_page_count(vm, 1);
	check_split_count(vm, 0);

	/*
	 * Then the guest code will read from the second hugepage, resulting
	 * in another huge page mapping being created.
	 */
	vcpu_run(vcpu);
	check_2m_page_count(vm, 2);
	check_split_count(vm, 0);

	/*
	 * Next, the guest will execute from the first huge page, causing it
	 * to be remapped at 4k.
	 *
	 * If NX huge pages are disabled, this should have no effect.
	 */
	vcpu_run(vcpu);
	check_2m_page_count(vm, disable_nx_huge_pages ? 2 : 1);
	check_split_count(vm, disable_nx_huge_pages ? 0 : 1);

	/*
	 * Executing from the third huge page (previously unaccessed) will
	 * cause part to be mapped at 4k.
	 *
	 * If NX huge pages are disabled, it should be mapped at 2M.
	 */
	vcpu_run(vcpu);
	check_2m_page_count(vm, disable_nx_huge_pages ? 3 : 1);
	check_split_count(vm, disable_nx_huge_pages ? 0 : 2);

	/* Reading from the first huge page again should have no effect. */
	vcpu_run(vcpu);
	check_2m_page_count(vm, disable_nx_huge_pages ? 3 : 1);
	check_split_count(vm, disable_nx_huge_pages ? 0 : 2);

	/* Give recovery thread time to run. */
	wait_for_reclaim(reclaim_period_ms);

	/*
	 * Now that the reclaimer has run, all the split pages should be gone.
	 *
	 * If NX huge pages are disabled, the relaimer will not run, so
	 * nothing should change from here on.
	 */
	check_2m_page_count(vm, disable_nx_huge_pages ? 3 : 1);
	check_split_count(vm, 0);

	/*
	 * The 4k mapping on hpage 3 should have been removed, so check that
	 * reading from it causes a huge page mapping to be installed.
	 */
	vcpu_run(vcpu);
	check_2m_page_count(vm, disable_nx_huge_pages ? 3 : 2);
	check_split_count(vm, 0);

	kvm_vm_free(vm);
}

static void help(char *name)
{
	puts("");
	printf("usage: %s [-h] [-p period_ms] [-t token]\n", name);
	puts("");
	printf(" -p: The NX reclaim period in miliseconds.\n");
	printf(" -t: The magic token to indicate environment setup is done.\n");
	printf(" -r: The test has reboot permissions and can disable NX huge pages.\n");
	puts("");
	exit(0);
}

int main(int argc, char **argv)
{
	int reclaim_period_ms = 0, token = 0, opt;
	bool reboot_permissions = false;

	while ((opt = getopt(argc, argv, "hp:t:r")) != -1) {
		switch (opt) {
		case 'p':
			reclaim_period_ms = atoi(optarg);
			break;
		case 't':
			token = atoi(optarg);
			break;
		case 'r':
			reboot_permissions = true;
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_VM_DISABLE_NX_HUGE_PAGES));
	TEST_REQUIRE(reclaim_period_ms > 0);

	__TEST_REQUIRE(token == MAGIC_TOKEN,
		       "This test must be run with the magic token %d.\n"
		       "This is done by nx_huge_pages_test.sh, which\n"
		       "also handles environment setup for the test.");

	run_test(reclaim_period_ms, false, reboot_permissions);
	run_test(reclaim_period_ms, true, reboot_permissions);

	return 0;
}

