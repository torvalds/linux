// SPDX-License-Identifier: GPL-2.0
/*
 * KVM dirty page logging performance test
 *
 * Based on dirty_log_test.c
 *
 * Copyright (C) 2018, Red Hat, Inc.
 * Copyright (C) 2020, Google, Inc.
 */

#define _GNU_SOURCE /* for program_invocation_name */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>

#include "kvm_util.h"
#include "perf_test_util.h"
#include "processor.h"
#include "test_util.h"

/* How many host loops to run by default (one KVM_GET_DIRTY_LOG for each loop)*/
#define TEST_HOST_LOOP_N		2UL

/* Host variables */
static bool host_quit;
static uint64_t iteration;
static uint64_t vcpu_last_completed_iteration[MAX_VCPUS];

static void *vcpu_worker(void *data)
{
	int ret;
	struct kvm_vm *vm = perf_test_args.vm;
	uint64_t pages_count = 0;
	struct kvm_run *run;
	struct timespec start;
	struct timespec ts_diff;
	struct timespec total = (struct timespec){0};
	struct timespec avg;
	struct vcpu_args *vcpu_args = (struct vcpu_args *)data;
	int vcpu_id = vcpu_args->vcpu_id;

	vcpu_args_set(vm, vcpu_id, 1, vcpu_id);
	run = vcpu_state(vm, vcpu_id);

	while (!READ_ONCE(host_quit)) {
		uint64_t current_iteration = READ_ONCE(iteration);

		clock_gettime(CLOCK_MONOTONIC, &start);
		ret = _vcpu_run(vm, vcpu_id);
		ts_diff = timespec_diff_now(start);

		TEST_ASSERT(ret == 0, "vcpu_run failed: %d\n", ret);
		TEST_ASSERT(get_ucall(vm, vcpu_id, NULL) == UCALL_SYNC,
			    "Invalid guest sync status: exit_reason=%s\n",
			    exit_reason_str(run->exit_reason));

		pr_debug("Got sync event from vCPU %d\n", vcpu_id);
		vcpu_last_completed_iteration[vcpu_id] = current_iteration;
		pr_debug("vCPU %d updated last completed iteration to %lu\n",
			 vcpu_id, vcpu_last_completed_iteration[vcpu_id]);

		if (current_iteration) {
			pages_count += vcpu_args->pages;
			total = timespec_add(total, ts_diff);
			pr_debug("vCPU %d iteration %lu dirty memory time: %ld.%.9lds\n",
				vcpu_id, current_iteration, ts_diff.tv_sec,
				ts_diff.tv_nsec);
		} else {
			pr_debug("vCPU %d iteration %lu populate memory time: %ld.%.9lds\n",
				vcpu_id, current_iteration, ts_diff.tv_sec,
				ts_diff.tv_nsec);
		}

		while (current_iteration == READ_ONCE(iteration) &&
		       !READ_ONCE(host_quit)) {}
	}

	avg = timespec_div(total, vcpu_last_completed_iteration[vcpu_id]);
	pr_debug("\nvCPU %d dirtied 0x%lx pages over %lu iterations in %ld.%.9lds. (Avg %ld.%.9lds/iteration)\n",
		vcpu_id, pages_count, vcpu_last_completed_iteration[vcpu_id],
		total.tv_sec, total.tv_nsec, avg.tv_sec, avg.tv_nsec);

	return NULL;
}

#ifdef USE_CLEAR_DIRTY_LOG
static u64 dirty_log_manual_caps;
#endif

static void run_test(enum vm_guest_mode mode, unsigned long iterations,
		     uint64_t phys_offset, int wr_fract)
{
	pthread_t *vcpu_threads;
	struct kvm_vm *vm;
	unsigned long *bmap;
	uint64_t guest_num_pages;
	uint64_t host_num_pages;
	int vcpu_id;
	struct timespec start;
	struct timespec ts_diff;
	struct timespec get_dirty_log_total = (struct timespec){0};
	struct timespec vcpu_dirty_total = (struct timespec){0};
	struct timespec avg;
#ifdef USE_CLEAR_DIRTY_LOG
	struct kvm_enable_cap cap = {};
	struct timespec clear_dirty_log_total = (struct timespec){0};
#endif

	vm = create_vm(mode, nr_vcpus, guest_percpu_mem_size);

	perf_test_args.wr_fract = wr_fract;

	guest_num_pages = (nr_vcpus * guest_percpu_mem_size) >> vm_get_page_shift(vm);
	guest_num_pages = vm_adjust_num_guest_pages(mode, guest_num_pages);
	host_num_pages = vm_num_host_pages(mode, guest_num_pages);
	bmap = bitmap_alloc(host_num_pages);

#ifdef USE_CLEAR_DIRTY_LOG
	cap.cap = KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2;
	cap.args[0] = dirty_log_manual_caps;
	vm_enable_cap(vm, &cap);
#endif

	vcpu_threads = malloc(nr_vcpus * sizeof(*vcpu_threads));
	TEST_ASSERT(vcpu_threads, "Memory allocation failed");

	add_vcpus(vm, nr_vcpus, guest_percpu_mem_size);

	sync_global_to_guest(vm, perf_test_args);

	/* Start the iterations */
	iteration = 0;
	host_quit = false;

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (vcpu_id = 0; vcpu_id < nr_vcpus; vcpu_id++) {
		pthread_create(&vcpu_threads[vcpu_id], NULL, vcpu_worker,
			       &perf_test_args.vcpu_args[vcpu_id]);
	}

	/* Allow the vCPU to populate memory */
	pr_debug("Starting iteration %lu - Populating\n", iteration);
	while (READ_ONCE(vcpu_last_completed_iteration[vcpu_id]) != iteration)
		pr_debug("Waiting for vcpu_last_completed_iteration == %lu\n",
			iteration);

	ts_diff = timespec_diff_now(start);
	pr_info("Populate memory time: %ld.%.9lds\n",
		ts_diff.tv_sec, ts_diff.tv_nsec);

	/* Enable dirty logging */
	clock_gettime(CLOCK_MONOTONIC, &start);
	vm_mem_region_set_flags(vm, TEST_MEM_SLOT_INDEX,
				KVM_MEM_LOG_DIRTY_PAGES);
	ts_diff = timespec_diff_now(start);
	pr_info("Enabling dirty logging time: %ld.%.9lds\n\n",
		ts_diff.tv_sec, ts_diff.tv_nsec);

	while (iteration < iterations) {
		/*
		 * Incrementing the iteration number will start the vCPUs
		 * dirtying memory again.
		 */
		clock_gettime(CLOCK_MONOTONIC, &start);
		iteration++;

		pr_debug("Starting iteration %lu\n", iteration);
		for (vcpu_id = 0; vcpu_id < nr_vcpus; vcpu_id++) {
			while (READ_ONCE(vcpu_last_completed_iteration[vcpu_id]) != iteration)
				pr_debug("Waiting for vCPU %d vcpu_last_completed_iteration == %lu\n",
					 vcpu_id, iteration);
		}

		ts_diff = timespec_diff_now(start);
		vcpu_dirty_total = timespec_add(vcpu_dirty_total, ts_diff);
		pr_info("Iteration %lu dirty memory time: %ld.%.9lds\n",
			iteration, ts_diff.tv_sec, ts_diff.tv_nsec);

		clock_gettime(CLOCK_MONOTONIC, &start);
		kvm_vm_get_dirty_log(vm, TEST_MEM_SLOT_INDEX, bmap);

		ts_diff = timespec_diff_now(start);
		get_dirty_log_total = timespec_add(get_dirty_log_total,
						   ts_diff);
		pr_info("Iteration %lu get dirty log time: %ld.%.9lds\n",
			iteration, ts_diff.tv_sec, ts_diff.tv_nsec);

#ifdef USE_CLEAR_DIRTY_LOG
		clock_gettime(CLOCK_MONOTONIC, &start);
		kvm_vm_clear_dirty_log(vm, TEST_MEM_SLOT_INDEX, bmap, 0,
				       host_num_pages);

		ts_diff = timespec_diff_now(start);
		clear_dirty_log_total = timespec_add(clear_dirty_log_total,
						     ts_diff);
		pr_info("Iteration %lu clear dirty log time: %ld.%.9lds\n",
			iteration, ts_diff.tv_sec, ts_diff.tv_nsec);
#endif
	}

	/* Tell the vcpu thread to quit */
	host_quit = true;
	for (vcpu_id = 0; vcpu_id < nr_vcpus; vcpu_id++)
		pthread_join(vcpu_threads[vcpu_id], NULL);

	/* Disable dirty logging */
	clock_gettime(CLOCK_MONOTONIC, &start);
	vm_mem_region_set_flags(vm, TEST_MEM_SLOT_INDEX, 0);
	ts_diff = timespec_diff_now(start);
	pr_info("Disabling dirty logging time: %ld.%.9lds\n",
		ts_diff.tv_sec, ts_diff.tv_nsec);

	avg = timespec_div(get_dirty_log_total, iterations);
	pr_info("Get dirty log over %lu iterations took %ld.%.9lds. (Avg %ld.%.9lds/iteration)\n",
		iterations, get_dirty_log_total.tv_sec,
		get_dirty_log_total.tv_nsec, avg.tv_sec, avg.tv_nsec);

#ifdef USE_CLEAR_DIRTY_LOG
	avg = timespec_div(clear_dirty_log_total, iterations);
	pr_info("Clear dirty log over %lu iterations took %ld.%.9lds. (Avg %ld.%.9lds/iteration)\n",
		iterations, clear_dirty_log_total.tv_sec,
		clear_dirty_log_total.tv_nsec, avg.tv_sec, avg.tv_nsec);
#endif

	free(bmap);
	free(vcpu_threads);
	ucall_uninit(vm);
	kvm_vm_free(vm);
}

struct guest_mode {
	bool supported;
	bool enabled;
};
static struct guest_mode guest_modes[NUM_VM_MODES];

#define guest_mode_init(mode, supported, enabled) ({ \
	guest_modes[mode] = (struct guest_mode){ supported, enabled }; \
})

static void help(char *name)
{
	int i;

	puts("");
	printf("usage: %s [-h] [-i iterations] [-p offset] "
	       "[-m mode] [-b vcpu bytes] [-v vcpus]\n", name);
	puts("");
	printf(" -i: specify iteration counts (default: %"PRIu64")\n",
	       TEST_HOST_LOOP_N);
	printf(" -p: specify guest physical test memory offset\n"
	       "     Warning: a low offset can conflict with the loaded test code.\n");
	printf(" -m: specify the guest mode ID to test "
	       "(default: test all supported modes)\n"
	       "     This option may be used multiple times.\n"
	       "     Guest mode IDs:\n");
	for (i = 0; i < NUM_VM_MODES; ++i) {
		printf("         %d:    %s%s\n", i, vm_guest_mode_string(i),
		       guest_modes[i].supported ? " (supported)" : "");
	}
	printf(" -b: specify the size of the memory region which should be\n"
	       "     dirtied by each vCPU. e.g. 10M or 3G.\n"
	       "     (default: 1G)\n");
	printf(" -f: specify the fraction of pages which should be written to\n"
	       "     as opposed to simply read, in the form\n"
	       "     1/<fraction of pages to write>.\n"
	       "     (default: 1 i.e. all pages are written to.)\n");
	printf(" -v: specify the number of vCPUs to run.\n");
	puts("");
	exit(0);
}

int main(int argc, char *argv[])
{
	unsigned long iterations = TEST_HOST_LOOP_N;
	bool mode_selected = false;
	uint64_t phys_offset = 0;
	unsigned int mode;
	int opt, i;
	int wr_fract = 1;

#ifdef USE_CLEAR_DIRTY_LOG
	dirty_log_manual_caps =
		kvm_check_cap(KVM_CAP_MANUAL_DIRTY_LOG_PROTECT2);
	if (!dirty_log_manual_caps) {
		print_skip("KVM_CLEAR_DIRTY_LOG not available");
		exit(KSFT_SKIP);
	}
	dirty_log_manual_caps &= (KVM_DIRTY_LOG_MANUAL_PROTECT_ENABLE |
				  KVM_DIRTY_LOG_INITIALLY_SET);
#endif

#ifdef __x86_64__
	guest_mode_init(VM_MODE_PXXV48_4K, true, true);
#endif
#ifdef __aarch64__
	guest_mode_init(VM_MODE_P40V48_4K, true, true);
	guest_mode_init(VM_MODE_P40V48_64K, true, true);

	{
		unsigned int limit = kvm_check_cap(KVM_CAP_ARM_VM_IPA_SIZE);

		if (limit >= 52)
			guest_mode_init(VM_MODE_P52V48_64K, true, true);
		if (limit >= 48) {
			guest_mode_init(VM_MODE_P48V48_4K, true, true);
			guest_mode_init(VM_MODE_P48V48_64K, true, true);
		}
	}
#endif
#ifdef __s390x__
	guest_mode_init(VM_MODE_P40V48_4K, true, true);
#endif

	while ((opt = getopt(argc, argv, "hi:p:m:b:f:v:")) != -1) {
		switch (opt) {
		case 'i':
			iterations = strtol(optarg, NULL, 10);
			break;
		case 'p':
			phys_offset = strtoull(optarg, NULL, 0);
			break;
		case 'm':
			if (!mode_selected) {
				for (i = 0; i < NUM_VM_MODES; ++i)
					guest_modes[i].enabled = false;
				mode_selected = true;
			}
			mode = strtoul(optarg, NULL, 10);
			TEST_ASSERT(mode < NUM_VM_MODES,
				    "Guest mode ID %d too big", mode);
			guest_modes[mode].enabled = true;
			break;
		case 'b':
			guest_percpu_mem_size = parse_size(optarg);
			break;
		case 'f':
			wr_fract = atoi(optarg);
			TEST_ASSERT(wr_fract >= 1,
				    "Write fraction cannot be less than one");
			break;
		case 'v':
			nr_vcpus = atoi(optarg);
			TEST_ASSERT(nr_vcpus > 0,
				    "Must have a positive number of vCPUs");
			TEST_ASSERT(nr_vcpus <= MAX_VCPUS,
				    "This test does not currently support\n"
				    "more than %d vCPUs.", MAX_VCPUS);
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	TEST_ASSERT(iterations >= 2, "The test should have at least two iterations");

	pr_info("Test iterations: %"PRIu64"\n",	iterations);

	for (i = 0; i < NUM_VM_MODES; ++i) {
		if (!guest_modes[i].enabled)
			continue;
		TEST_ASSERT(guest_modes[i].supported,
			    "Guest mode ID %d (%s) not supported.",
			    i, vm_guest_mode_string(i));
		run_test(i, iterations, phys_offset, wr_fract);
	}

	return 0;
}
