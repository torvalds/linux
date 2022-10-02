// SPDX-License-Identifier: GPL-2.0
/*
 * access_tracking_perf_test
 *
 * Copyright (C) 2021, Google, Inc.
 *
 * This test measures the performance effects of KVM's access tracking.
 * Access tracking is driven by the MMU notifiers test_young, clear_young, and
 * clear_flush_young. These notifiers do not have a direct userspace API,
 * however the clear_young notifier can be triggered by marking a pages as idle
 * in /sys/kernel/mm/page_idle/bitmap. This test leverages that mechanism to
 * enable access tracking on guest memory.
 *
 * To measure performance this test runs a VM with a configurable number of
 * vCPUs that each touch every page in disjoint regions of memory. Performance
 * is measured in the time it takes all vCPUs to finish touching their
 * predefined region.
 *
 * Note that a deterministic correctness test of access tracking is not possible
 * by using page_idle as it exists today. This is for a few reasons:
 *
 * 1. page_idle only issues clear_young notifiers, which lack a TLB flush. This
 *    means subsequent guest accesses are not guaranteed to see page table
 *    updates made by KVM until some time in the future.
 *
 * 2. page_idle only operates on LRU pages. Newly allocated pages are not
 *    immediately allocated to LRU lists. Instead they are held in a "pagevec",
 *    which is drained to LRU lists some time in the future. There is no
 *    userspace API to force this drain to occur.
 *
 * These limitations are worked around in this test by using a large enough
 * region of memory for each vCPU such that the number of translations cached in
 * the TLB and the number of pages held in pagevecs are a small fraction of the
 * overall workload. And if either of those conditions are not true this test
 * will fail rather than silently passing.
 */
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "kvm_util.h"
#include "test_util.h"
#include "perf_test_util.h"
#include "guest_modes.h"

/* Global variable used to synchronize all of the vCPU threads. */
static int iteration;

/* Defines what vCPU threads should do during a given iteration. */
static enum {
	/* Run the vCPU to access all its memory. */
	ITERATION_ACCESS_MEMORY,
	/* Mark the vCPU's memory idle in page_idle. */
	ITERATION_MARK_IDLE,
} iteration_work;

/* Set to true when vCPU threads should exit. */
static bool done;

/* The iteration that was last completed by each vCPU. */
static int vcpu_last_completed_iteration[KVM_MAX_VCPUS];

/* Whether to overlap the regions of memory vCPUs access. */
static bool overlap_memory_access;

struct test_params {
	/* The backing source for the region of memory. */
	enum vm_mem_backing_src_type backing_src;

	/* The amount of memory to allocate for each vCPU. */
	uint64_t vcpu_memory_bytes;

	/* The number of vCPUs to create in the VM. */
	int nr_vcpus;
};

static uint64_t pread_uint64(int fd, const char *filename, uint64_t index)
{
	uint64_t value;
	off_t offset = index * sizeof(value);

	TEST_ASSERT(pread(fd, &value, sizeof(value), offset) == sizeof(value),
		    "pread from %s offset 0x%" PRIx64 " failed!",
		    filename, offset);

	return value;

}

#define PAGEMAP_PRESENT (1ULL << 63)
#define PAGEMAP_PFN_MASK ((1ULL << 55) - 1)

static uint64_t lookup_pfn(int pagemap_fd, struct kvm_vm *vm, uint64_t gva)
{
	uint64_t hva = (uint64_t) addr_gva2hva(vm, gva);
	uint64_t entry;
	uint64_t pfn;

	entry = pread_uint64(pagemap_fd, "pagemap", hva / getpagesize());
	if (!(entry & PAGEMAP_PRESENT))
		return 0;

	pfn = entry & PAGEMAP_PFN_MASK;
	__TEST_REQUIRE(pfn, "Looking up PFNs requires CAP_SYS_ADMIN");

	return pfn;
}

static bool is_page_idle(int page_idle_fd, uint64_t pfn)
{
	uint64_t bits = pread_uint64(page_idle_fd, "page_idle", pfn / 64);

	return !!((bits >> (pfn % 64)) & 1);
}

static void mark_page_idle(int page_idle_fd, uint64_t pfn)
{
	uint64_t bits = 1ULL << (pfn % 64);

	TEST_ASSERT(pwrite(page_idle_fd, &bits, 8, 8 * (pfn / 64)) == 8,
		    "Set page_idle bits for PFN 0x%" PRIx64, pfn);
}

static void mark_vcpu_memory_idle(struct kvm_vm *vm,
				  struct perf_test_vcpu_args *vcpu_args)
{
	int vcpu_idx = vcpu_args->vcpu_idx;
	uint64_t base_gva = vcpu_args->gva;
	uint64_t pages = vcpu_args->pages;
	uint64_t page;
	uint64_t still_idle = 0;
	uint64_t no_pfn = 0;
	int page_idle_fd;
	int pagemap_fd;

	/* If vCPUs are using an overlapping region, let vCPU 0 mark it idle. */
	if (overlap_memory_access && vcpu_idx)
		return;

	page_idle_fd = open("/sys/kernel/mm/page_idle/bitmap", O_RDWR);
	TEST_ASSERT(page_idle_fd > 0, "Failed to open page_idle.");

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	TEST_ASSERT(pagemap_fd > 0, "Failed to open pagemap.");

	for (page = 0; page < pages; page++) {
		uint64_t gva = base_gva + page * perf_test_args.guest_page_size;
		uint64_t pfn = lookup_pfn(pagemap_fd, vm, gva);

		if (!pfn) {
			no_pfn++;
			continue;
		}

		if (is_page_idle(page_idle_fd, pfn)) {
			still_idle++;
			continue;
		}

		mark_page_idle(page_idle_fd, pfn);
	}

	/*
	 * Assumption: Less than 1% of pages are going to be swapped out from
	 * under us during this test.
	 */
	TEST_ASSERT(no_pfn < pages / 100,
		    "vCPU %d: No PFN for %" PRIu64 " out of %" PRIu64 " pages.",
		    vcpu_idx, no_pfn, pages);

	/*
	 * Test that at least 90% of memory has been marked idle (the rest might
	 * not be marked idle because the pages have not yet made it to an LRU
	 * list or the translations are still cached in the TLB). 90% is
	 * arbitrary; high enough that we ensure most memory access went through
	 * access tracking but low enough as to not make the test too brittle
	 * over time and across architectures.
	 */
	TEST_ASSERT(still_idle < pages / 10,
		    "vCPU%d: Too many pages still idle (%"PRIu64 " out of %"
		    PRIu64 ").\n",
		    vcpu_idx, still_idle, pages);

	close(page_idle_fd);
	close(pagemap_fd);
}

static void assert_ucall(struct kvm_vcpu *vcpu, uint64_t expected_ucall)
{
	struct ucall uc;
	uint64_t actual_ucall = get_ucall(vcpu, &uc);

	TEST_ASSERT(expected_ucall == actual_ucall,
		    "Guest exited unexpectedly (expected ucall %" PRIu64
		    ", got %" PRIu64 ")",
		    expected_ucall, actual_ucall);
}

static bool spin_wait_for_next_iteration(int *current_iteration)
{
	int last_iteration = *current_iteration;

	do {
		if (READ_ONCE(done))
			return false;

		*current_iteration = READ_ONCE(iteration);
	} while (last_iteration == *current_iteration);

	return true;
}

static void vcpu_thread_main(struct perf_test_vcpu_args *vcpu_args)
{
	struct kvm_vcpu *vcpu = vcpu_args->vcpu;
	struct kvm_vm *vm = perf_test_args.vm;
	int vcpu_idx = vcpu_args->vcpu_idx;
	int current_iteration = 0;

	while (spin_wait_for_next_iteration(&current_iteration)) {
		switch (READ_ONCE(iteration_work)) {
		case ITERATION_ACCESS_MEMORY:
			vcpu_run(vcpu);
			assert_ucall(vcpu, UCALL_SYNC);
			break;
		case ITERATION_MARK_IDLE:
			mark_vcpu_memory_idle(vm, vcpu_args);
			break;
		};

		vcpu_last_completed_iteration[vcpu_idx] = current_iteration;
	}
}

static void spin_wait_for_vcpu(int vcpu_idx, int target_iteration)
{
	while (READ_ONCE(vcpu_last_completed_iteration[vcpu_idx]) !=
	       target_iteration) {
		continue;
	}
}

/* The type of memory accesses to perform in the VM. */
enum access_type {
	ACCESS_READ,
	ACCESS_WRITE,
};

static void run_iteration(struct kvm_vm *vm, int nr_vcpus, const char *description)
{
	struct timespec ts_start;
	struct timespec ts_elapsed;
	int next_iteration, i;

	/* Kick off the vCPUs by incrementing iteration. */
	next_iteration = ++iteration;

	clock_gettime(CLOCK_MONOTONIC, &ts_start);

	/* Wait for all vCPUs to finish the iteration. */
	for (i = 0; i < nr_vcpus; i++)
		spin_wait_for_vcpu(i, next_iteration);

	ts_elapsed = timespec_elapsed(ts_start);
	pr_info("%-30s: %ld.%09lds\n",
		description, ts_elapsed.tv_sec, ts_elapsed.tv_nsec);
}

static void access_memory(struct kvm_vm *vm, int nr_vcpus,
			  enum access_type access, const char *description)
{
	perf_test_set_wr_fract(vm, (access == ACCESS_READ) ? INT_MAX : 1);
	iteration_work = ITERATION_ACCESS_MEMORY;
	run_iteration(vm, nr_vcpus, description);
}

static void mark_memory_idle(struct kvm_vm *vm, int nr_vcpus)
{
	/*
	 * Even though this parallelizes the work across vCPUs, this is still a
	 * very slow operation because page_idle forces the test to mark one pfn
	 * at a time and the clear_young notifier serializes on the KVM MMU
	 * lock.
	 */
	pr_debug("Marking VM memory idle (slow)...\n");
	iteration_work = ITERATION_MARK_IDLE;
	run_iteration(vm, nr_vcpus, "Mark memory idle");
}

static void run_test(enum vm_guest_mode mode, void *arg)
{
	struct test_params *params = arg;
	struct kvm_vm *vm;
	int nr_vcpus = params->nr_vcpus;

	vm = perf_test_create_vm(mode, nr_vcpus, params->vcpu_memory_bytes, 1,
				 params->backing_src, !overlap_memory_access);

	perf_test_start_vcpu_threads(nr_vcpus, vcpu_thread_main);

	pr_info("\n");
	access_memory(vm, nr_vcpus, ACCESS_WRITE, "Populating memory");

	/* As a control, read and write to the populated memory first. */
	access_memory(vm, nr_vcpus, ACCESS_WRITE, "Writing to populated memory");
	access_memory(vm, nr_vcpus, ACCESS_READ, "Reading from populated memory");

	/* Repeat on memory that has been marked as idle. */
	mark_memory_idle(vm, nr_vcpus);
	access_memory(vm, nr_vcpus, ACCESS_WRITE, "Writing to idle memory");
	mark_memory_idle(vm, nr_vcpus);
	access_memory(vm, nr_vcpus, ACCESS_READ, "Reading from idle memory");

	/* Set done to signal the vCPU threads to exit */
	done = true;

	perf_test_join_vcpu_threads(nr_vcpus);
	perf_test_destroy_vm(vm);
}

static void help(char *name)
{
	puts("");
	printf("usage: %s [-h] [-m mode] [-b vcpu_bytes] [-v vcpus] [-o]  [-s mem_type]\n",
	       name);
	puts("");
	printf(" -h: Display this help message.");
	guest_modes_help();
	printf(" -b: specify the size of the memory region which should be\n"
	       "     dirtied by each vCPU. e.g. 10M or 3G.\n"
	       "     (default: 1G)\n");
	printf(" -v: specify the number of vCPUs to run.\n");
	printf(" -o: Overlap guest memory accesses instead of partitioning\n"
	       "     them into a separate region of memory for each vCPU.\n");
	backing_src_help("-s");
	puts("");
	exit(0);
}

int main(int argc, char *argv[])
{
	struct test_params params = {
		.backing_src = DEFAULT_VM_MEM_SRC,
		.vcpu_memory_bytes = DEFAULT_PER_VCPU_MEM_SIZE,
		.nr_vcpus = 1,
	};
	int page_idle_fd;
	int opt;

	guest_modes_append_default();

	while ((opt = getopt(argc, argv, "hm:b:v:os:")) != -1) {
		switch (opt) {
		case 'm':
			guest_modes_cmdline(optarg);
			break;
		case 'b':
			params.vcpu_memory_bytes = parse_size(optarg);
			break;
		case 'v':
			params.nr_vcpus = atoi(optarg);
			break;
		case 'o':
			overlap_memory_access = true;
			break;
		case 's':
			params.backing_src = parse_backing_src_type(optarg);
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	page_idle_fd = open("/sys/kernel/mm/page_idle/bitmap", O_RDWR);
	__TEST_REQUIRE(page_idle_fd >= 0,
		       "CONFIG_IDLE_PAGE_TRACKING is not enabled");
	close(page_idle_fd);

	for_each_guest_mode(run_test, &params);

	return 0;
}
