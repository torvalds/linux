// SPDX-License-Identifier: GPL-2.0
/*
 * access_tracking_perf_test
 *
 * Copyright (C) 2021, Google, Inc.
 *
 * This test measures the performance effects of KVM's access tracking.
 * Access tracking is driven by the MMU notifiers test_young, clear_young, and
 * clear_flush_young. These notifiers do not have a direct userspace API,
 * however the clear_young notifier can be triggered either by
 *   1. marking a pages as idle in /sys/kernel/mm/page_idle/bitmap OR
 *   2. adding a new MGLRU generation using the lru_gen debugfs file.
 * This test leverages page_idle to enable access tracking on guest memory
 * unless MGLRU is enabled, in which case MGLRU is used.
 *
 * To measure performance this test runs a VM with a configurable number of
 * vCPUs that each touch every page in disjoint regions of memory. Performance
 * is measured in the time it takes all vCPUs to finish touching their
 * predefined region.
 *
 * Note that a deterministic correctness test of access tracking is not possible
 * by using page_idle or MGLRU aging as it exists today. This is for a few
 * reasons:
 *
 * 1. page_idle and MGLRU only issue clear_young notifiers, which lack a TLB flush.
 *    This means subsequent guest accesses are not guaranteed to see page table
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
 * overall workload. And if either of those conditions are not true (for example
 * in nesting, where TLB size is unlimited) this test will print a warning
 * rather than silently passing.
 */
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "kvm_util.h"
#include "test_util.h"
#include "memstress.h"
#include "guest_modes.h"
#include "processor.h"
#include "ucall_common.h"

#include "cgroup_util.h"
#include "lru_gen_util.h"

static const char *TEST_MEMCG_NAME = "access_tracking_perf_test";

/* Global variable used to synchronize all of the vCPU threads. */
static int iteration;

/* The cgroup memory controller root. Needed for lru_gen-based aging. */
char cgroup_root[PATH_MAX];

/* Defines what vCPU threads should do during a given iteration. */
static enum {
	/* Run the vCPU to access all its memory. */
	ITERATION_ACCESS_MEMORY,
	/* Mark the vCPU's memory idle in page_idle. */
	ITERATION_MARK_IDLE,
} iteration_work;

/* The iteration that was last completed by each vCPU. */
static int vcpu_last_completed_iteration[KVM_MAX_VCPUS];

/* Whether to overlap the regions of memory vCPUs access. */
static bool overlap_memory_access;

/*
 * If the test should only warn if there are too many idle pages (i.e., it is
 * expected).
 * -1: Not yet set.
 *  0: We do not expect too many idle pages, so FAIL if too many idle pages.
 *  1: Having too many idle pages is expected, so merely print a warning if
 *     too many idle pages are found.
 */
static int idle_pages_warn_only = -1;

/* Whether or not to use MGLRU instead of page_idle for access tracking */
static bool use_lru_gen;

/* Total number of pages to expect in the memcg after touching everything */
static long test_pages;

/* Last generation we found the pages in */
static int lru_gen_last_gen = -1;

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

static void too_many_idle_pages(long idle_pages, long total_pages, int vcpu_idx)
{
	char prefix[18] = {};

	if (vcpu_idx >= 0)
		snprintf(prefix, 18, "vCPU%d: ", vcpu_idx);

	TEST_ASSERT(idle_pages_warn_only,
		    "%sToo many pages still idle (%lu out of %lu)",
		    prefix, idle_pages, total_pages);

	printf("WARNING: %sToo many pages still idle (%lu out of %lu), "
	       "this will affect performance results.\n",
	       prefix, idle_pages, total_pages);
}

static void pageidle_mark_vcpu_memory_idle(struct kvm_vm *vm,
					   struct memstress_vcpu_args *vcpu_args)
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
		uint64_t gva = base_gva + page * memstress_args.guest_page_size;
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
	 * Check that at least 90% of memory has been marked idle (the rest
	 * might not be marked idle because the pages have not yet made it to an
	 * LRU list or the translations are still cached in the TLB). 90% is
	 * arbitrary; high enough that we ensure most memory access went through
	 * access tracking but low enough as to not make the test too brittle
	 * over time and across architectures.
	 */
	if (still_idle >= pages / 10)
		too_many_idle_pages(still_idle, pages,
				    overlap_memory_access ? -1 : vcpu_idx);

	close(page_idle_fd);
	close(pagemap_fd);
}

int find_generation(struct memcg_stats *stats, long total_pages)
{
	/*
	 * For finding the generation that contains our pages, use the same
	 * 90% threshold that page_idle uses.
	 */
	int gen = lru_gen_find_generation(stats, total_pages * 9 / 10);

	if (gen >= 0)
		return gen;

	if (!idle_pages_warn_only) {
		TEST_FAIL("Could not find a generation with 90%% of guest memory (%ld pages).",
			   total_pages * 9 / 10);
		return gen;
	}

	/*
	 * We couldn't find a generation with 90% of guest memory, which can
	 * happen if access tracking is unreliable. Simply look for a majority
	 * of pages.
	 */
	puts("WARNING: Couldn't find a generation with 90% of guest memory. "
	     "Performance results may not be accurate.");
	gen = lru_gen_find_generation(stats, total_pages / 2);
	TEST_ASSERT(gen >= 0,
		    "Could not find a generation with 50%% of guest memory (%ld pages).",
		    total_pages / 2);
	return gen;
}

static void lru_gen_mark_memory_idle(struct kvm_vm *vm)
{
	struct timespec ts_start;
	struct timespec ts_elapsed;
	struct memcg_stats stats;
	int new_gen;

	/* Make a new generation */
	clock_gettime(CLOCK_MONOTONIC, &ts_start);
	lru_gen_do_aging(&stats, TEST_MEMCG_NAME);
	ts_elapsed = timespec_elapsed(ts_start);

	/* Check the generation again */
	new_gen = find_generation(&stats, test_pages);

	/*
	 * This function should only be invoked with newly-accessed pages,
	 * so pages should always move to a newer generation.
	 */
	if (new_gen <= lru_gen_last_gen) {
		/* We did not move to a newer generation. */
		long idle_pages = lru_gen_sum_memcg_stats_for_gen(lru_gen_last_gen,
								  &stats);

		too_many_idle_pages(min_t(long, idle_pages, test_pages),
				    test_pages, -1);
	}
	pr_info("%-30s: %ld.%09lds\n",
		"Mark memory idle (lru_gen)", ts_elapsed.tv_sec,
		ts_elapsed.tv_nsec);
	lru_gen_last_gen = new_gen;
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
		if (READ_ONCE(memstress_args.stop_vcpus))
			return false;

		*current_iteration = READ_ONCE(iteration);
	} while (last_iteration == *current_iteration);

	return true;
}

static void vcpu_thread_main(struct memstress_vcpu_args *vcpu_args)
{
	struct kvm_vcpu *vcpu = vcpu_args->vcpu;
	struct kvm_vm *vm = memstress_args.vm;
	int vcpu_idx = vcpu_args->vcpu_idx;
	int current_iteration = 0;

	while (spin_wait_for_next_iteration(&current_iteration)) {
		switch (READ_ONCE(iteration_work)) {
		case ITERATION_ACCESS_MEMORY:
			vcpu_run(vcpu);
			assert_ucall(vcpu, UCALL_SYNC);
			break;
		case ITERATION_MARK_IDLE:
			pageidle_mark_vcpu_memory_idle(vm, vcpu_args);
			break;
		}

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
	memstress_set_write_percent(vm, (access == ACCESS_READ) ? 0 : 100);
	iteration_work = ITERATION_ACCESS_MEMORY;
	run_iteration(vm, nr_vcpus, description);
}

static void mark_memory_idle(struct kvm_vm *vm, int nr_vcpus)
{
	if (use_lru_gen)
		return lru_gen_mark_memory_idle(vm);

	/*
	 * Even though this parallelizes the work across vCPUs, this is still a
	 * very slow operation because page_idle forces the test to mark one pfn
	 * at a time and the clear_young notifier may serialize on the KVM MMU
	 * lock.
	 */
	pr_debug("Marking VM memory idle (slow)...\n");
	iteration_work = ITERATION_MARK_IDLE;
	run_iteration(vm, nr_vcpus, "Mark memory idle (page_idle)");
}

static void run_test(enum vm_guest_mode mode, void *arg)
{
	struct test_params *params = arg;
	struct kvm_vm *vm;
	int nr_vcpus = params->nr_vcpus;

	vm = memstress_create_vm(mode, nr_vcpus, params->vcpu_memory_bytes, 1,
				 params->backing_src, !overlap_memory_access);

	/*
	 * If guest_page_size is larger than the host's page size, the
	 * guest (memstress) will only fault in a subset of the host's pages.
	 */
	test_pages = params->nr_vcpus * params->vcpu_memory_bytes /
		      max(memstress_args.guest_page_size,
			  (uint64_t)getpagesize());

	memstress_start_vcpu_threads(nr_vcpus, vcpu_thread_main);

	pr_info("\n");
	access_memory(vm, nr_vcpus, ACCESS_WRITE, "Populating memory");

	if (use_lru_gen) {
		struct memcg_stats stats;

		/*
		 * Do a page table scan now. Following initial population, aging
		 * may not cause the pages to move to a newer generation. Do
		 * an aging pass now so that future aging passes always move
		 * pages to a newer generation.
		 */
		printf("Initial aging pass (lru_gen)\n");
		lru_gen_do_aging(&stats, TEST_MEMCG_NAME);
		TEST_ASSERT(lru_gen_sum_memcg_stats(&stats) >= test_pages,
			    "Not all pages accounted for (looking for %ld). "
			    "Was the memcg set up correctly?", test_pages);
		access_memory(vm, nr_vcpus, ACCESS_WRITE, "Re-populating memory");
		lru_gen_read_memcg_stats(&stats, TEST_MEMCG_NAME);
		lru_gen_last_gen = find_generation(&stats, test_pages);
	}

	/* As a control, read and write to the populated memory first. */
	access_memory(vm, nr_vcpus, ACCESS_WRITE, "Writing to populated memory");
	access_memory(vm, nr_vcpus, ACCESS_READ, "Reading from populated memory");

	/* Repeat on memory that has been marked as idle. */
	mark_memory_idle(vm, nr_vcpus);
	access_memory(vm, nr_vcpus, ACCESS_WRITE, "Writing to idle memory");
	mark_memory_idle(vm, nr_vcpus);
	access_memory(vm, nr_vcpus, ACCESS_READ, "Reading from idle memory");

	memstress_join_vcpu_threads(nr_vcpus);
	memstress_destroy_vm(vm);
}

static int access_tracking_unreliable(void)
{
#ifdef __x86_64__
	/*
	 * When running nested, the TLB size may be effectively unlimited (for
	 * example, this is the case when running on KVM L0), and KVM doesn't
	 * explicitly flush the TLB when aging SPTEs.  As a result, more pages
	 * are cached and the guest won't see the "idle" bit cleared.
	 */
	if (this_cpu_has(X86_FEATURE_HYPERVISOR)) {
		puts("Skipping idle page count sanity check, because the test is run nested");
		return 1;
	}
#endif
	/*
	 * When NUMA balancing is enabled, guest memory will be unmapped to get
	 * NUMA faults, dropping the Accessed bits.
	 */
	if (is_numa_balancing_enabled()) {
		puts("Skipping idle page count sanity check, because NUMA balancing is enabled");
		return 1;
	}
	return 0;
}

static int run_test_for_each_guest_mode(const char *cgroup, void *arg)
{
	for_each_guest_mode(run_test, arg);
	return 0;
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
	printf(" -w: Control whether the test warns or fails if more than 10%%\n"
	       "     of pages are still seen as idle/old after accessing guest\n"
	       "     memory.  >0 == warn only, 0 == fail, <0 == auto.  For auto\n"
	       "     mode, the test fails by default, but switches to warn only\n"
	       "     if NUMA balancing is enabled or the test detects it's running\n"
	       "     in a VM.\n");
	backing_src_help("-s");
	puts("");
	exit(0);
}

void destroy_cgroup(char *cg)
{
	printf("Destroying cgroup: %s\n", cg);
}

int main(int argc, char *argv[])
{
	struct test_params params = {
		.backing_src = DEFAULT_VM_MEM_SRC,
		.vcpu_memory_bytes = DEFAULT_PER_VCPU_MEM_SIZE,
		.nr_vcpus = 1,
	};
	char *new_cg = NULL;
	int page_idle_fd;
	int opt;

	guest_modes_append_default();

	while ((opt = getopt(argc, argv, "hm:b:v:os:w:")) != -1) {
		switch (opt) {
		case 'm':
			guest_modes_cmdline(optarg);
			break;
		case 'b':
			params.vcpu_memory_bytes = parse_size(optarg);
			break;
		case 'v':
			params.nr_vcpus = atoi_positive("Number of vCPUs", optarg);
			break;
		case 'o':
			overlap_memory_access = true;
			break;
		case 's':
			params.backing_src = parse_backing_src_type(optarg);
			break;
		case 'w':
			idle_pages_warn_only =
				atoi_non_negative("Idle pages warning",
						  optarg);
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	if (idle_pages_warn_only == -1)
		idle_pages_warn_only = access_tracking_unreliable();

	if (lru_gen_usable()) {
		bool cg_created = true;
		int ret;

		puts("Using lru_gen for aging");
		use_lru_gen = true;

		if (cg_find_controller_root(cgroup_root, sizeof(cgroup_root), "memory"))
			ksft_exit_skip("Cannot find memory cgroup controller\n");

		new_cg = cg_name(cgroup_root, TEST_MEMCG_NAME);
		printf("Creating cgroup: %s\n", new_cg);
		if (cg_create(new_cg)) {
			if (errno == EEXIST) {
				printf("Found existing cgroup");
				cg_created = false;
			} else {
				ksft_exit_skip("could not create new cgroup: %s\n", new_cg);
			}
		}

		/*
		 * This will fork off a new process to run the test within
		 * a new memcg, so we need to properly propagate the return
		 * value up.
		 */
		ret = cg_run(new_cg, &run_test_for_each_guest_mode, &params);
		if (cg_created)
			cg_destroy(new_cg);
		if (ret < 0)
			TEST_FAIL("child did not spawn or was abnormally killed");
		if (ret)
			return ret;
	} else {
		page_idle_fd = __open_path_or_exit("/sys/kernel/mm/page_idle/bitmap", O_RDWR,
						   "Is CONFIG_IDLE_PAGE_TRACKING enabled?");
		close(page_idle_fd);

		puts("Using page_idle for aging");
		run_test_for_each_guest_mode(NULL, &params);
	}

	return 0;
}
