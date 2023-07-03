// SPDX-License-Identifier: GPL-2.0
/*
 * KVM dirty logging page splitting test
 *
 * Based on dirty_log_perf.c
 *
 * Copyright (C) 2018, Red Hat, Inc.
 * Copyright (C) 2023, Google, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <linux/bitmap.h>

#include "kvm_util.h"
#include "test_util.h"
#include "memstress.h"
#include "guest_modes.h"

#define VCPUS		2
#define SLOTS		2
#define ITERATIONS	2

static uint64_t guest_percpu_mem_size = DEFAULT_PER_VCPU_MEM_SIZE;

static enum vm_mem_backing_src_type backing_src = VM_MEM_SRC_ANONYMOUS_HUGETLB;

static u64 dirty_log_manual_caps;
static bool host_quit;
static int iteration;
static int vcpu_last_completed_iteration[KVM_MAX_VCPUS];

struct kvm_page_stats {
	uint64_t pages_4k;
	uint64_t pages_2m;
	uint64_t pages_1g;
	uint64_t hugepages;
};

static void get_page_stats(struct kvm_vm *vm, struct kvm_page_stats *stats, const char *stage)
{
	stats->pages_4k = vm_get_stat(vm, "pages_4k");
	stats->pages_2m = vm_get_stat(vm, "pages_2m");
	stats->pages_1g = vm_get_stat(vm, "pages_1g");
	stats->hugepages = stats->pages_2m + stats->pages_1g;

	pr_debug("\nPage stats after %s: 4K: %ld 2M: %ld 1G: %ld huge: %ld\n",
		 stage, stats->pages_4k, stats->pages_2m, stats->pages_1g,
		 stats->hugepages);
}

static void run_vcpu_iteration(struct kvm_vm *vm)
{
	int i;

	iteration++;
	for (i = 0; i < VCPUS; i++) {
		while (READ_ONCE(vcpu_last_completed_iteration[i]) !=
		       iteration)
			;
	}
}

static void vcpu_worker(struct memstress_vcpu_args *vcpu_args)
{
	struct kvm_vcpu *vcpu = vcpu_args->vcpu;
	int vcpu_idx = vcpu_args->vcpu_idx;

	while (!READ_ONCE(host_quit)) {
		int current_iteration = READ_ONCE(iteration);

		vcpu_run(vcpu);

		ASSERT_EQ(get_ucall(vcpu, NULL), UCALL_SYNC);

		vcpu_last_completed_iteration[vcpu_idx] = current_iteration;

		/* Wait for the start of the next iteration to be signaled. */
		while (current_iteration == READ_ONCE(iteration) &&
		       READ_ONCE(iteration) >= 0 &&
		       !READ_ONCE(host_quit))
			;
	}
}

static void run_test(enum vm_guest_mode mode, void *unused)
{
	struct kvm_vm *vm;
	unsigned long **bitmaps;
	uint64_t guest_num_pages;
	uint64_t host_num_pages;
	uint64_t pages_per_slot;
	int i;
	uint64_t total_4k_pages;
	struct kvm_page_stats stats_populated;
	struct kvm_page_stats stats_dirty_logging_enabled;
	struct kvm_page_stats stats_dirty_pass[ITERATIONS];
	struct kvm_page_stats stats_clear_pass[ITERATIONS];
	struct kvm_page_stats stats_dirty_logging_disabled;
	struct kvm_page_stats stats_repopulated;

	vm = memstress_create_vm(mode, VCPUS, guest_percpu_mem_size,
				 SLOTS, backing_src, false);

	guest_num_pages = (VCPUS * guest_percpu_mem_size) >> vm->page_shift;
	guest_num_pages = vm_adjust_num_guest_pages(mode, guest_num_pages);
	host_num_pages = vm_num_host_pages(mode, guest_num_pages);
	pages_per_slot = host_num_pages / SLOTS;

	bitmaps = memstress_alloc_bitmaps(SLOTS, pages_per_slot);

	if (dirty_log_manual_caps)
		vm_enable_cap(vm, KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2,
			      dirty_log_manual_caps);

	/* Start the iterations */
	iteration = -1;
	host_quit = false;

	for (i = 0; i < VCPUS; i++)
		vcpu_last_completed_iteration[i] = -1;

	memstress_start_vcpu_threads(VCPUS, vcpu_worker);

	run_vcpu_iteration(vm);
	get_page_stats(vm, &stats_populated, "populating memory");

	/* Enable dirty logging */
	memstress_enable_dirty_logging(vm, SLOTS);

	get_page_stats(vm, &stats_dirty_logging_enabled, "enabling dirty logging");

	while (iteration < ITERATIONS) {
		run_vcpu_iteration(vm);
		get_page_stats(vm, &stats_dirty_pass[iteration - 1],
			       "dirtying memory");

		memstress_get_dirty_log(vm, bitmaps, SLOTS);

		if (dirty_log_manual_caps) {
			memstress_clear_dirty_log(vm, bitmaps, SLOTS, pages_per_slot);

			get_page_stats(vm, &stats_clear_pass[iteration - 1], "clearing dirty log");
		}
	}

	/* Disable dirty logging */
	memstress_disable_dirty_logging(vm, SLOTS);

	get_page_stats(vm, &stats_dirty_logging_disabled, "disabling dirty logging");

	/* Run vCPUs again to fault pages back in. */
	run_vcpu_iteration(vm);
	get_page_stats(vm, &stats_repopulated, "repopulating memory");

	/*
	 * Tell the vCPU threads to quit.  No need to manually check that vCPUs
	 * have stopped running after disabling dirty logging, the join will
	 * wait for them to exit.
	 */
	host_quit = true;
	memstress_join_vcpu_threads(VCPUS);

	memstress_free_bitmaps(bitmaps, SLOTS);
	memstress_destroy_vm(vm);

	/* Make assertions about the page counts. */
	total_4k_pages = stats_populated.pages_4k;
	total_4k_pages += stats_populated.pages_2m * 512;
	total_4k_pages += stats_populated.pages_1g * 512 * 512;

	/*
	 * Check that all huge pages were split. Since large pages can only
	 * exist in the data slot, and the vCPUs should have dirtied all pages
	 * in the data slot, there should be no huge pages left after splitting.
	 * Splitting happens at dirty log enable time without
	 * KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2 and after the first clear pass
	 * with that capability.
	 */
	if (dirty_log_manual_caps) {
		ASSERT_EQ(stats_clear_pass[0].hugepages, 0);
		ASSERT_EQ(stats_clear_pass[0].pages_4k, total_4k_pages);
		ASSERT_EQ(stats_dirty_logging_enabled.hugepages, stats_populated.hugepages);
	} else {
		ASSERT_EQ(stats_dirty_logging_enabled.hugepages, 0);
		ASSERT_EQ(stats_dirty_logging_enabled.pages_4k, total_4k_pages);
	}

	/*
	 * Once dirty logging is disabled and the vCPUs have touched all their
	 * memory again, the page counts should be the same as they were
	 * right after initial population of memory.
	 */
	ASSERT_EQ(stats_populated.pages_4k, stats_repopulated.pages_4k);
	ASSERT_EQ(stats_populated.pages_2m, stats_repopulated.pages_2m);
	ASSERT_EQ(stats_populated.pages_1g, stats_repopulated.pages_1g);
}

static void help(char *name)
{
	puts("");
	printf("usage: %s [-h] [-b vcpu bytes] [-s mem type]\n",
	       name);
	puts("");
	printf(" -b: specify the size of the memory region which should be\n"
	       "     dirtied by each vCPU. e.g. 10M or 3G.\n"
	       "     (default: 1G)\n");
	backing_src_help("-s");
	puts("");
}

int main(int argc, char *argv[])
{
	int opt;

	TEST_REQUIRE(get_kvm_param_bool("eager_page_split"));
	TEST_REQUIRE(get_kvm_param_bool("tdp_mmu"));

	while ((opt = getopt(argc, argv, "b:hs:")) != -1) {
		switch (opt) {
		case 'b':
			guest_percpu_mem_size = parse_size(optarg);
			break;
		case 'h':
			help(argv[0]);
			exit(0);
		case 's':
			backing_src = parse_backing_src_type(optarg);
			break;
		default:
			help(argv[0]);
			exit(1);
		}
	}

	if (!is_backing_src_hugetlb(backing_src)) {
		pr_info("This test will only work reliably with HugeTLB memory. "
			"It can work with THP, but that is best effort.\n");
	}

	guest_modes_append_default();

	dirty_log_manual_caps = 0;
	for_each_guest_mode(run_test, NULL);

	dirty_log_manual_caps =
		kvm_check_cap(KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2);

	if (dirty_log_manual_caps) {
		dirty_log_manual_caps &= (KVM_DIRTY_LOG_MANUAL_PROTECT_ENABLE |
					  KVM_DIRTY_LOG_INITIALLY_SET);
		for_each_guest_mode(run_test, NULL);
	} else {
		pr_info("Skipping testing with MANUAL_PROTECT as it is not supported");
	}

	return 0;
}
