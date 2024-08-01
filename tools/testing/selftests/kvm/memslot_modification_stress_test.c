// SPDX-License-Identifier: GPL-2.0
/*
 * KVM memslot modification stress test
 * Adapted from demand_paging_test.c
 *
 * Copyright (C) 2018, Red Hat, Inc.
 * Copyright (C) 2020, Google, Inc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <asm/unistd.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/userfaultfd.h>

#include "memstress.h"
#include "processor.h"
#include "test_util.h"
#include "guest_modes.h"

#define DUMMY_MEMSLOT_INDEX 7

#define DEFAULT_MEMSLOT_MODIFICATION_ITERATIONS 10


static int nr_vcpus = 1;
static uint64_t guest_percpu_mem_size = DEFAULT_PER_VCPU_MEM_SIZE;

static void vcpu_worker(struct memstress_vcpu_args *vcpu_args)
{
	struct kvm_vcpu *vcpu = vcpu_args->vcpu;
	struct kvm_run *run;
	int ret;

	run = vcpu->run;

	/* Let the guest access its memory until a stop signal is received */
	while (!READ_ONCE(memstress_args.stop_vcpus)) {
		ret = _vcpu_run(vcpu);
		TEST_ASSERT(ret == 0, "vcpu_run failed: %d", ret);

		if (get_ucall(vcpu, NULL) == UCALL_SYNC)
			continue;

		TEST_ASSERT(false,
			    "Invalid guest sync status: exit_reason=%s\n",
			    exit_reason_str(run->exit_reason));
	}
}

static void add_remove_memslot(struct kvm_vm *vm, useconds_t delay,
			       uint64_t nr_modifications)
{
	uint64_t pages = max_t(int, vm->page_size, getpagesize()) / vm->page_size;
	uint64_t gpa;
	int i;

	/*
	 * Add the dummy memslot just below the memstress memslot, which is
	 * at the top of the guest physical address space.
	 */
	gpa = memstress_args.gpa - pages * vm->page_size;

	for (i = 0; i < nr_modifications; i++) {
		usleep(delay);
		vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS, gpa,
					    DUMMY_MEMSLOT_INDEX, pages, 0);

		vm_mem_region_delete(vm, DUMMY_MEMSLOT_INDEX);
	}
}

struct test_params {
	useconds_t delay;
	uint64_t nr_iterations;
	bool partition_vcpu_memory_access;
};

static void run_test(enum vm_guest_mode mode, void *arg)
{
	struct test_params *p = arg;
	struct kvm_vm *vm;

	vm = memstress_create_vm(mode, nr_vcpus, guest_percpu_mem_size, 1,
				 VM_MEM_SRC_ANONYMOUS,
				 p->partition_vcpu_memory_access);

	pr_info("Finished creating vCPUs\n");

	memstress_start_vcpu_threads(nr_vcpus, vcpu_worker);

	pr_info("Started all vCPUs\n");

	add_remove_memslot(vm, p->delay, p->nr_iterations);

	memstress_join_vcpu_threads(nr_vcpus);
	pr_info("All vCPU threads joined\n");

	memstress_destroy_vm(vm);
}

static void help(char *name)
{
	puts("");
	printf("usage: %s [-h] [-m mode] [-d delay_usec]\n"
	       "          [-b memory] [-v vcpus] [-o] [-i iterations]\n", name);
	guest_modes_help();
	printf(" -d: add a delay between each iteration of adding and\n"
	       "     deleting a memslot in usec.\n");
	printf(" -b: specify the size of the memory region which should be\n"
	       "     accessed by each vCPU. e.g. 10M or 3G.\n"
	       "     Default: 1G\n");
	printf(" -v: specify the number of vCPUs to run.\n");
	printf(" -o: Overlap guest memory accesses instead of partitioning\n"
	       "     them into a separate region of memory for each vCPU.\n");
	printf(" -i: specify the number of iterations of adding and removing\n"
	       "     a memslot.\n"
	       "     Default: %d\n", DEFAULT_MEMSLOT_MODIFICATION_ITERATIONS);
	puts("");
	exit(0);
}

int main(int argc, char *argv[])
{
	int max_vcpus = kvm_check_cap(KVM_CAP_MAX_VCPUS);
	int opt;
	struct test_params p = {
		.delay = 0,
		.nr_iterations = DEFAULT_MEMSLOT_MODIFICATION_ITERATIONS,
		.partition_vcpu_memory_access = true
	};

	guest_modes_append_default();

	while ((opt = getopt(argc, argv, "hm:d:b:v:oi:")) != -1) {
		switch (opt) {
		case 'm':
			guest_modes_cmdline(optarg);
			break;
		case 'd':
			p.delay = atoi_non_negative("Delay", optarg);
			break;
		case 'b':
			guest_percpu_mem_size = parse_size(optarg);
			break;
		case 'v':
			nr_vcpus = atoi_positive("Number of vCPUs", optarg);
			TEST_ASSERT(nr_vcpus <= max_vcpus,
				    "Invalid number of vcpus, must be between 1 and %d",
				    max_vcpus);
			break;
		case 'o':
			p.partition_vcpu_memory_access = false;
			break;
		case 'i':
			p.nr_iterations = atoi_positive("Number of iterations", optarg);
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	for_each_guest_mode(run_test, &p);

	return 0;
}
