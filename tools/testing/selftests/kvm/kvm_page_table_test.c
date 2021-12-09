// SPDX-License-Identifier: GPL-2.0
/*
 * KVM page table test
 *
 * Copyright (C) 2021, Huawei, Inc.
 *
 * Make sure that THP has been enabled or enough HUGETLB pages with specific
 * page size have been pre-allocated on your system, if you are planning to
 * use hugepages to back the guest memory for testing.
 */

#define _GNU_SOURCE /* for program_invocation_name */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "guest_modes.h"

#define TEST_MEM_SLOT_INDEX             1

/* Default size(1GB) of the memory for testing */
#define DEFAULT_TEST_MEM_SIZE		(1 << 30)

/* Default guest test virtual memory offset */
#define DEFAULT_GUEST_TEST_MEM		0xc0000000

/* Different guest memory accessing stages */
enum test_stage {
	KVM_BEFORE_MAPPINGS,
	KVM_CREATE_MAPPINGS,
	KVM_UPDATE_MAPPINGS,
	KVM_ADJUST_MAPPINGS,
	NUM_TEST_STAGES,
};

static const char * const test_stage_string[] = {
	"KVM_BEFORE_MAPPINGS",
	"KVM_CREATE_MAPPINGS",
	"KVM_UPDATE_MAPPINGS",
	"KVM_ADJUST_MAPPINGS",
};

struct vcpu_args {
	int vcpu_id;
	bool vcpu_write;
};

struct test_args {
	struct kvm_vm *vm;
	uint64_t guest_test_virt_mem;
	uint64_t host_page_size;
	uint64_t host_num_pages;
	uint64_t large_page_size;
	uint64_t large_num_pages;
	uint64_t host_pages_per_lpage;
	enum vm_mem_backing_src_type src_type;
	struct vcpu_args vcpu_args[KVM_MAX_VCPUS];
};

/*
 * Guest variables. Use addr_gva2hva() if these variables need
 * to be changed in host.
 */
static enum test_stage guest_test_stage;

/* Host variables */
static uint32_t nr_vcpus = 1;
static struct test_args test_args;
static enum test_stage *current_stage;
static bool host_quit;

/* Whether the test stage is updated, or completed */
static sem_t test_stage_updated;
static sem_t test_stage_completed;

/*
 * Guest physical memory offset of the testing memory slot.
 * This will be set to the topmost valid physical address minus
 * the test memory size.
 */
static uint64_t guest_test_phys_mem;

/*
 * Guest virtual memory offset of the testing memory slot.
 * Must not conflict with identity mapped test code.
 */
static uint64_t guest_test_virt_mem = DEFAULT_GUEST_TEST_MEM;

static void guest_code(int vcpu_id)
{
	struct test_args *p = &test_args;
	struct vcpu_args *vcpu_args = &p->vcpu_args[vcpu_id];
	enum test_stage *current_stage = &guest_test_stage;
	uint64_t addr;
	int i, j;

	/* Make sure vCPU args data structure is not corrupt */
	GUEST_ASSERT(vcpu_args->vcpu_id == vcpu_id);

	while (true) {
		addr = p->guest_test_virt_mem;

		switch (READ_ONCE(*current_stage)) {
		/*
		 * All vCPU threads will be started in this stage,
		 * where guest code of each vCPU will do nothing.
		 */
		case KVM_BEFORE_MAPPINGS:
			break;

		/*
		 * Before dirty logging, vCPUs concurrently access the first
		 * 8 bytes of each page (host page/large page) within the same
		 * memory region with different accessing types (read/write).
		 * Then KVM will create normal page mappings or huge block
		 * mappings for them.
		 */
		case KVM_CREATE_MAPPINGS:
			for (i = 0; i < p->large_num_pages; i++) {
				if (vcpu_args->vcpu_write)
					*(uint64_t *)addr = 0x0123456789ABCDEF;
				else
					READ_ONCE(*(uint64_t *)addr);

				addr += p->large_page_size;
			}
			break;

		/*
		 * During dirty logging, KVM will only update attributes of the
		 * normal page mappings from RO to RW if memory backing src type
		 * is anonymous. In other cases, KVM will split the huge block
		 * mappings into normal page mappings if memory backing src type
		 * is THP or HUGETLB.
		 */
		case KVM_UPDATE_MAPPINGS:
			if (p->src_type == VM_MEM_SRC_ANONYMOUS) {
				for (i = 0; i < p->host_num_pages; i++) {
					*(uint64_t *)addr = 0x0123456789ABCDEF;
					addr += p->host_page_size;
				}
				break;
			}

			for (i = 0; i < p->large_num_pages; i++) {
				/*
				 * Write to the first host page in each large
				 * page region, and triger break of large pages.
				 */
				*(uint64_t *)addr = 0x0123456789ABCDEF;

				/*
				 * Access the middle host pages in each large
				 * page region. Since dirty logging is enabled,
				 * this will create new mappings at the smallest
				 * granularity.
				 */
				addr += p->large_page_size / 2;
				for (j = 0; j < p->host_pages_per_lpage / 2; j++) {
					READ_ONCE(*(uint64_t *)addr);
					addr += p->host_page_size;
				}
			}
			break;

		/*
		 * After dirty logging is stopped, vCPUs concurrently read
		 * from every single host page. Then KVM will coalesce the
		 * split page mappings back to block mappings. And a TLB
		 * conflict abort could occur here if TLB entries of the
		 * page mappings are not fully invalidated.
		 */
		case KVM_ADJUST_MAPPINGS:
			for (i = 0; i < p->host_num_pages; i++) {
				READ_ONCE(*(uint64_t *)addr);
				addr += p->host_page_size;
			}
			break;

		default:
			GUEST_ASSERT(0);
		}

		GUEST_SYNC(1);
	}
}

static void *vcpu_worker(void *data)
{
	int ret;
	struct vcpu_args *vcpu_args = data;
	struct kvm_vm *vm = test_args.vm;
	int vcpu_id = vcpu_args->vcpu_id;
	struct kvm_run *run;
	struct timespec start;
	struct timespec ts_diff;
	enum test_stage stage;

	vcpu_args_set(vm, vcpu_id, 1, vcpu_id);
	run = vcpu_state(vm, vcpu_id);

	while (!READ_ONCE(host_quit)) {
		ret = sem_wait(&test_stage_updated);
		TEST_ASSERT(ret == 0, "Error in sem_wait");

		if (READ_ONCE(host_quit))
			return NULL;

		clock_gettime(CLOCK_MONOTONIC_RAW, &start);
		ret = _vcpu_run(vm, vcpu_id);
		ts_diff = timespec_elapsed(start);

		TEST_ASSERT(ret == 0, "vcpu_run failed: %d\n", ret);
		TEST_ASSERT(get_ucall(vm, vcpu_id, NULL) == UCALL_SYNC,
			    "Invalid guest sync status: exit_reason=%s\n",
			    exit_reason_str(run->exit_reason));

		pr_debug("Got sync event from vCPU %d\n", vcpu_id);
		stage = READ_ONCE(*current_stage);

		/*
		 * Here we can know the execution time of every
		 * single vcpu running in different test stages.
		 */
		pr_debug("vCPU %d has completed stage %s\n"
			 "execution time is: %ld.%.9lds\n\n",
			 vcpu_id, test_stage_string[stage],
			 ts_diff.tv_sec, ts_diff.tv_nsec);

		ret = sem_post(&test_stage_completed);
		TEST_ASSERT(ret == 0, "Error in sem_post");
	}

	return NULL;
}

struct test_params {
	uint64_t phys_offset;
	uint64_t test_mem_size;
	enum vm_mem_backing_src_type src_type;
};

static struct kvm_vm *pre_init_before_test(enum vm_guest_mode mode, void *arg)
{
	int ret;
	struct test_params *p = arg;
	struct vcpu_args *vcpu_args;
	enum vm_mem_backing_src_type src_type = p->src_type;
	uint64_t large_page_size = get_backing_src_pagesz(src_type);
	uint64_t guest_page_size = vm_guest_mode_params[mode].page_size;
	uint64_t host_page_size = getpagesize();
	uint64_t test_mem_size = p->test_mem_size;
	uint64_t guest_num_pages;
	uint64_t alignment;
	void *host_test_mem;
	struct kvm_vm *vm;
	int vcpu_id;

	/* Align up the test memory size */
	alignment = max(large_page_size, guest_page_size);
	test_mem_size = (test_mem_size + alignment - 1) & ~(alignment - 1);

	/* Create a VM with enough guest pages */
	guest_num_pages = test_mem_size / guest_page_size;
	vm = vm_create_with_vcpus(mode, nr_vcpus, DEFAULT_GUEST_PHY_PAGES,
				  guest_num_pages, 0, guest_code, NULL);

	/* Align down GPA of the testing memslot */
	if (!p->phys_offset)
		guest_test_phys_mem = (vm_get_max_gfn(vm) - guest_num_pages) *
				       guest_page_size;
	else
		guest_test_phys_mem = p->phys_offset;
#ifdef __s390x__
	alignment = max(0x100000, alignment);
#endif
	guest_test_phys_mem &= ~(alignment - 1);

	/* Set up the shared data structure test_args */
	test_args.vm = vm;
	test_args.guest_test_virt_mem = guest_test_virt_mem;
	test_args.host_page_size = host_page_size;
	test_args.host_num_pages = test_mem_size / host_page_size;
	test_args.large_page_size = large_page_size;
	test_args.large_num_pages = test_mem_size / large_page_size;
	test_args.host_pages_per_lpage = large_page_size / host_page_size;
	test_args.src_type = src_type;

	for (vcpu_id = 0; vcpu_id < KVM_MAX_VCPUS; vcpu_id++) {
		vcpu_args = &test_args.vcpu_args[vcpu_id];
		vcpu_args->vcpu_id = vcpu_id;
		vcpu_args->vcpu_write = !(vcpu_id % 2);
	}

	/* Add an extra memory slot with specified backing src type */
	vm_userspace_mem_region_add(vm, src_type, guest_test_phys_mem,
				    TEST_MEM_SLOT_INDEX, guest_num_pages, 0);

	/* Do mapping(GVA->GPA) for the testing memory slot */
	virt_map(vm, guest_test_virt_mem, guest_test_phys_mem, guest_num_pages);

	/* Cache the HVA pointer of the region */
	host_test_mem = addr_gpa2hva(vm, (vm_paddr_t)guest_test_phys_mem);

	/* Export shared structure test_args to guest */
	ucall_init(vm, NULL);
	sync_global_to_guest(vm, test_args);

	ret = sem_init(&test_stage_updated, 0, 0);
	TEST_ASSERT(ret == 0, "Error in sem_init");

	ret = sem_init(&test_stage_completed, 0, 0);
	TEST_ASSERT(ret == 0, "Error in sem_init");

	current_stage = addr_gva2hva(vm, (vm_vaddr_t)(&guest_test_stage));
	*current_stage = NUM_TEST_STAGES;

	pr_info("Testing guest mode: %s\n", vm_guest_mode_string(mode));
	pr_info("Testing memory backing src type: %s\n",
		vm_mem_backing_src_alias(src_type)->name);
	pr_info("Testing memory backing src granularity: 0x%lx\n",
		large_page_size);
	pr_info("Testing memory size(aligned): 0x%lx\n", test_mem_size);
	pr_info("Guest physical test memory offset: 0x%lx\n",
		guest_test_phys_mem);
	pr_info("Host  virtual  test memory offset: 0x%lx\n",
		(uint64_t)host_test_mem);
	pr_info("Number of testing vCPUs: %d\n", nr_vcpus);

	return vm;
}

static void vcpus_complete_new_stage(enum test_stage stage)
{
	int ret;
	int vcpus;

	/* Wake up all the vcpus to run new test stage */
	for (vcpus = 0; vcpus < nr_vcpus; vcpus++) {
		ret = sem_post(&test_stage_updated);
		TEST_ASSERT(ret == 0, "Error in sem_post");
	}
	pr_debug("All vcpus have been notified to continue\n");

	/* Wait for all the vcpus to complete new test stage */
	for (vcpus = 0; vcpus < nr_vcpus; vcpus++) {
		ret = sem_wait(&test_stage_completed);
		TEST_ASSERT(ret == 0, "Error in sem_wait");

		pr_debug("%d vcpus have completed stage %s\n",
			 vcpus + 1, test_stage_string[stage]);
	}

	pr_debug("All vcpus have completed stage %s\n",
		 test_stage_string[stage]);
}

static void run_test(enum vm_guest_mode mode, void *arg)
{
	int ret;
	pthread_t *vcpu_threads;
	struct kvm_vm *vm;
	int vcpu_id;
	struct timespec start;
	struct timespec ts_diff;

	/* Create VM with vCPUs and make some pre-initialization */
	vm = pre_init_before_test(mode, arg);

	vcpu_threads = malloc(nr_vcpus * sizeof(*vcpu_threads));
	TEST_ASSERT(vcpu_threads, "Memory allocation failed");

	host_quit = false;
	*current_stage = KVM_BEFORE_MAPPINGS;

	for (vcpu_id = 0; vcpu_id < nr_vcpus; vcpu_id++) {
		pthread_create(&vcpu_threads[vcpu_id], NULL, vcpu_worker,
			       &test_args.vcpu_args[vcpu_id]);
	}

	vcpus_complete_new_stage(*current_stage);
	pr_info("Started all vCPUs successfully\n");

	/* Test the stage of KVM creating mappings */
	*current_stage = KVM_CREATE_MAPPINGS;

	clock_gettime(CLOCK_MONOTONIC_RAW, &start);
	vcpus_complete_new_stage(*current_stage);
	ts_diff = timespec_elapsed(start);

	pr_info("KVM_CREATE_MAPPINGS: total execution time: %ld.%.9lds\n\n",
		ts_diff.tv_sec, ts_diff.tv_nsec);

	/* Test the stage of KVM updating mappings */
	vm_mem_region_set_flags(vm, TEST_MEM_SLOT_INDEX,
				KVM_MEM_LOG_DIRTY_PAGES);

	*current_stage = KVM_UPDATE_MAPPINGS;

	clock_gettime(CLOCK_MONOTONIC_RAW, &start);
	vcpus_complete_new_stage(*current_stage);
	ts_diff = timespec_elapsed(start);

	pr_info("KVM_UPDATE_MAPPINGS: total execution time: %ld.%.9lds\n\n",
		ts_diff.tv_sec, ts_diff.tv_nsec);

	/* Test the stage of KVM adjusting mappings */
	vm_mem_region_set_flags(vm, TEST_MEM_SLOT_INDEX, 0);

	*current_stage = KVM_ADJUST_MAPPINGS;

	clock_gettime(CLOCK_MONOTONIC_RAW, &start);
	vcpus_complete_new_stage(*current_stage);
	ts_diff = timespec_elapsed(start);

	pr_info("KVM_ADJUST_MAPPINGS: total execution time: %ld.%.9lds\n\n",
		ts_diff.tv_sec, ts_diff.tv_nsec);

	/* Tell the vcpu thread to quit */
	host_quit = true;
	for (vcpu_id = 0; vcpu_id < nr_vcpus; vcpu_id++) {
		ret = sem_post(&test_stage_updated);
		TEST_ASSERT(ret == 0, "Error in sem_post");
	}

	for (vcpu_id = 0; vcpu_id < nr_vcpus; vcpu_id++)
		pthread_join(vcpu_threads[vcpu_id], NULL);

	ret = sem_destroy(&test_stage_updated);
	TEST_ASSERT(ret == 0, "Error in sem_destroy");

	ret = sem_destroy(&test_stage_completed);
	TEST_ASSERT(ret == 0, "Error in sem_destroy");

	free(vcpu_threads);
	ucall_uninit(vm);
	kvm_vm_free(vm);
}

static void help(char *name)
{
	puts("");
	printf("usage: %s [-h] [-p offset] [-m mode] "
	       "[-b mem-size] [-v vcpus] [-s mem-type]\n", name);
	puts("");
	printf(" -p: specify guest physical test memory offset\n"
	       "     Warning: a low offset can conflict with the loaded test code.\n");
	guest_modes_help();
	printf(" -b: specify size of the memory region for testing. e.g. 10M or 3G.\n"
	       "     (default: 1G)\n");
	printf(" -v: specify the number of vCPUs to run\n"
	       "     (default: 1)\n");
	backing_src_help("-s");
	puts("");
}

int main(int argc, char *argv[])
{
	int max_vcpus = kvm_check_cap(KVM_CAP_MAX_VCPUS);
	struct test_params p = {
		.test_mem_size = DEFAULT_TEST_MEM_SIZE,
		.src_type = DEFAULT_VM_MEM_SRC,
	};
	int opt;

	guest_modes_append_default();

	while ((opt = getopt(argc, argv, "hp:m:b:v:s:")) != -1) {
		switch (opt) {
		case 'p':
			p.phys_offset = strtoull(optarg, NULL, 0);
			break;
		case 'm':
			guest_modes_cmdline(optarg);
			break;
		case 'b':
			p.test_mem_size = parse_size(optarg);
			break;
		case 'v':
			nr_vcpus = atoi(optarg);
			TEST_ASSERT(nr_vcpus > 0 && nr_vcpus <= max_vcpus,
				    "Invalid number of vcpus, must be between 1 and %d", max_vcpus);
			break;
		case 's':
			p.src_type = parse_backing_src_type(optarg);
			break;
		case 'h':
		default:
			help(argv[0]);
			exit(0);
		}
	}

	for_each_guest_mode(run_test, &p);

	return 0;
}
