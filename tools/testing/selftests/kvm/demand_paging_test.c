// SPDX-License-Identifier: GPL-2.0
/*
 * KVM demand paging test
 * Adapted from dirty_log_test.c
 *
 * Copyright (C) 2018, Red Hat, Inc.
 * Copyright (C) 2019, Google, Inc.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <linux/userfaultfd.h>
#include <sys/syscall.h>

#include "kvm_util.h"
#include "test_util.h"
#include "memstress.h"
#include "guest_modes.h"
#include "ucall_common.h"
#include "userfaultfd_util.h"

#ifdef __NR_userfaultfd

static int nr_vcpus = 1;
static uint64_t guest_percpu_mem_size = DEFAULT_PER_VCPU_MEM_SIZE;

static size_t demand_paging_size;
static char *guest_data_prototype;

static void vcpu_worker(struct memstress_vcpu_args *vcpu_args)
{
	struct kvm_vcpu *vcpu = vcpu_args->vcpu;
	int vcpu_idx = vcpu_args->vcpu_idx;
	struct kvm_run *run = vcpu->run;
	struct timespec start;
	struct timespec ts_diff;
	int ret;

	clock_gettime(CLOCK_MONOTONIC, &start);

	/* Let the guest access its memory */
	ret = _vcpu_run(vcpu);
	TEST_ASSERT(ret == 0, "vcpu_run failed: %d", ret);
	if (get_ucall(vcpu, NULL) != UCALL_SYNC) {
		TEST_ASSERT(false,
			    "Invalid guest sync status: exit_reason=%s",
			    exit_reason_str(run->exit_reason));
	}

	ts_diff = timespec_elapsed(start);
	PER_VCPU_DEBUG("vCPU %d execution time: %ld.%.9lds\n", vcpu_idx,
		       ts_diff.tv_sec, ts_diff.tv_nsec);
}

static int handle_uffd_page_request(int uffd_mode, int uffd,
		struct uffd_msg *msg)
{
	pid_t tid = syscall(__NR_gettid);
	uint64_t addr = msg->arg.pagefault.address;
	struct timespec start;
	struct timespec ts_diff;
	int r;

	clock_gettime(CLOCK_MONOTONIC, &start);

	if (uffd_mode == UFFDIO_REGISTER_MODE_MISSING) {
		struct uffdio_copy copy;

		copy.src = (uint64_t)guest_data_prototype;
		copy.dst = addr;
		copy.len = demand_paging_size;
		copy.mode = 0;

		r = ioctl(uffd, UFFDIO_COPY, &copy);
		/*
		 * With multiple vCPU threads fault on a single page and there are
		 * multiple readers for the UFFD, at least one of the UFFDIO_COPYs
		 * will fail with EEXIST: handle that case without signaling an
		 * error.
		 *
		 * Note that this also suppress any EEXISTs occurring from,
		 * e.g., the first UFFDIO_COPY/CONTINUEs on a page. That never
		 * happens here, but a realistic VMM might potentially maintain
		 * some external state to correctly surface EEXISTs to userspace
		 * (or prevent duplicate COPY/CONTINUEs in the first place).
		 */
		if (r == -1 && errno != EEXIST) {
			pr_info("Failed UFFDIO_COPY in 0x%lx from thread %d, errno = %d\n",
				addr, tid, errno);
			return r;
		}
	} else if (uffd_mode == UFFDIO_REGISTER_MODE_MINOR) {
		struct uffdio_continue cont = {0};

		cont.range.start = addr;
		cont.range.len = demand_paging_size;

		r = ioctl(uffd, UFFDIO_CONTINUE, &cont);
		/*
		 * With multiple vCPU threads fault on a single page and there are
		 * multiple readers for the UFFD, at least one of the UFFDIO_COPYs
		 * will fail with EEXIST: handle that case without signaling an
		 * error.
		 *
		 * Note that this also suppress any EEXISTs occurring from,
		 * e.g., the first UFFDIO_COPY/CONTINUEs on a page. That never
		 * happens here, but a realistic VMM might potentially maintain
		 * some external state to correctly surface EEXISTs to userspace
		 * (or prevent duplicate COPY/CONTINUEs in the first place).
		 */
		if (r == -1 && errno != EEXIST) {
			pr_info("Failed UFFDIO_CONTINUE in 0x%lx, thread %d, errno = %d\n",
				addr, tid, errno);
			return r;
		}
	} else {
		TEST_FAIL("Invalid uffd mode %d", uffd_mode);
	}

	ts_diff = timespec_elapsed(start);

	PER_PAGE_DEBUG("UFFD page-in %d \t%ld ns\n", tid,
		       timespec_to_ns(ts_diff));
	PER_PAGE_DEBUG("Paged in %ld bytes at 0x%lx from thread %d\n",
		       demand_paging_size, addr, tid);

	return 0;
}

struct test_params {
	int uffd_mode;
	bool single_uffd;
	useconds_t uffd_delay;
	int readers_per_uffd;
	enum vm_mem_backing_src_type src_type;
	bool partition_vcpu_memory_access;
};

static void prefault_mem(void *alias, uint64_t len)
{
	size_t p;

	TEST_ASSERT(alias != NULL, "Alias required for minor faults");
	for (p = 0; p < (len / demand_paging_size); ++p) {
		memcpy(alias + (p * demand_paging_size),
		       guest_data_prototype, demand_paging_size);
	}
}

static void run_test(enum vm_guest_mode mode, void *arg)
{
	struct memstress_vcpu_args *vcpu_args;
	struct test_params *p = arg;
	struct uffd_desc **uffd_descs = NULL;
	uint64_t uffd_region_size;
	struct timespec start;
	struct timespec ts_diff;
	double vcpu_paging_rate;
	struct kvm_vm *vm;
	int i, num_uffds = 0;

	vm = memstress_create_vm(mode, nr_vcpus, guest_percpu_mem_size, 1,
				 p->src_type, p->partition_vcpu_memory_access);

	demand_paging_size = get_backing_src_pagesz(p->src_type);

	guest_data_prototype = malloc(demand_paging_size);
	TEST_ASSERT(guest_data_prototype,
		    "Failed to allocate buffer for guest data pattern");
	memset(guest_data_prototype, 0xAB, demand_paging_size);

	if (p->uffd_mode == UFFDIO_REGISTER_MODE_MINOR) {
		num_uffds = p->single_uffd ? 1 : nr_vcpus;
		for (i = 0; i < num_uffds; i++) {
			vcpu_args = &memstress_args.vcpu_args[i];
			prefault_mem(addr_gpa2alias(vm, vcpu_args->gpa),
				     vcpu_args->pages * memstress_args.guest_page_size);
		}
	}

	if (p->uffd_mode) {
		num_uffds = p->single_uffd ? 1 : nr_vcpus;
		uffd_region_size = nr_vcpus * guest_percpu_mem_size / num_uffds;

		uffd_descs = malloc(num_uffds * sizeof(struct uffd_desc *));
		TEST_ASSERT(uffd_descs, "Memory allocation failed");
		for (i = 0; i < num_uffds; i++) {
			struct memstress_vcpu_args *vcpu_args;
			void *vcpu_hva;

			vcpu_args = &memstress_args.vcpu_args[i];

			/* Cache the host addresses of the region */
			vcpu_hva = addr_gpa2hva(vm, vcpu_args->gpa);
			/*
			 * Set up user fault fd to handle demand paging
			 * requests.
			 */
			uffd_descs[i] = uffd_setup_demand_paging(
				p->uffd_mode, p->uffd_delay, vcpu_hva,
				uffd_region_size,
				p->readers_per_uffd,
				&handle_uffd_page_request);
		}
	}

	pr_info("Finished creating vCPUs and starting uffd threads\n");

	clock_gettime(CLOCK_MONOTONIC, &start);
	memstress_start_vcpu_threads(nr_vcpus, vcpu_worker);
	pr_info("Started all vCPUs\n");

	memstress_join_vcpu_threads(nr_vcpus);
	ts_diff = timespec_elapsed(start);
	pr_info("All vCPU threads joined\n");

	if (p->uffd_mode) {
		/* Tell the user fault fd handler threads to quit */
		for (i = 0; i < num_uffds; i++)
			uffd_stop_demand_paging(uffd_descs[i]);
	}

	pr_info("Total guest execution time:\t%ld.%.9lds\n",
		ts_diff.tv_sec, ts_diff.tv_nsec);

	vcpu_paging_rate = memstress_args.vcpu_args[0].pages /
			   ((double)ts_diff.tv_sec + (double)ts_diff.tv_nsec / NSEC_PER_SEC);
	pr_info("Per-vcpu demand paging rate:\t%f pgs/sec/vcpu\n",
		vcpu_paging_rate);
	pr_info("Overall demand paging rate:\t%f pgs/sec\n",
		vcpu_paging_rate * nr_vcpus);

	memstress_destroy_vm(vm);

	free(guest_data_prototype);
	if (p->uffd_mode)
		free(uffd_descs);
}

static void help(char *name)
{
	puts("");
	printf("usage: %s [-h] [-m vm_mode] [-u uffd_mode] [-a]\n"
		   "          [-d uffd_delay_usec] [-r readers_per_uffd] [-b memory]\n"
		   "          [-s type] [-v vcpus] [-c cpu_list] [-o]\n", name);
	guest_modes_help();
	printf(" -u: use userfaultfd to handle vCPU page faults. Mode is a\n"
	       "     UFFD registration mode: 'MISSING' or 'MINOR'.\n");
	kvm_print_vcpu_pinning_help();
	printf(" -a: Use a single userfaultfd for all of guest memory, instead of\n"
	       "     creating one for each region paged by a unique vCPU\n"
	       "     Set implicitly with -o, and no effect without -u.\n");
	printf(" -d: add a delay in usec to the User Fault\n"
	       "     FD handler to simulate demand paging\n"
	       "     overheads. Ignored without -u.\n");
	printf(" -r: Set the number of reader threads per uffd.\n");
	printf(" -b: specify the size of the memory region which should be\n"
	       "     demand paged by each vCPU. e.g. 10M or 3G.\n"
	       "     Default: 1G\n");
	backing_src_help("-s");
	printf(" -v: specify the number of vCPUs to run.\n");
	printf(" -o: Overlap guest memory accesses instead of partitioning\n"
	       "     them into a separate region of memory for each vCPU.\n");
	puts("");
	exit(0);
}

int main(int argc, char *argv[])
{
	int max_vcpus = kvm_check_cap(KVM_CAP_MAX_VCPUS);
	const char *cpulist = NULL;
	struct test_params p = {
		.src_type = DEFAULT_VM_MEM_SRC,
		.partition_vcpu_memory_access = true,
		.readers_per_uffd = 1,
		.single_uffd = false,
	};
	int opt;

	guest_modes_append_default();

	while ((opt = getopt(argc, argv, "ahom:u:d:b:s:v:c:r:")) != -1) {
		switch (opt) {
		case 'm':
			guest_modes_cmdline(optarg);
			break;
		case 'u':
			if (!strcmp("MISSING", optarg))
				p.uffd_mode = UFFDIO_REGISTER_MODE_MISSING;
			else if (!strcmp("MINOR", optarg))
				p.uffd_mode = UFFDIO_REGISTER_MODE_MINOR;
			TEST_ASSERT(p.uffd_mode, "UFFD mode must be 'MISSING' or 'MINOR'.");
			break;
		case 'a':
			p.single_uffd = true;
			break;
		case 'd':
			p.uffd_delay = strtoul(optarg, NULL, 0);
			TEST_ASSERT(p.uffd_delay >= 0, "A negative UFFD delay is not supported.");
			break;
		case 'b':
			guest_percpu_mem_size = parse_size(optarg);
			break;
		case 's':
			p.src_type = parse_backing_src_type(optarg);
			break;
		case 'v':
			nr_vcpus = atoi_positive("Number of vCPUs", optarg);
			TEST_ASSERT(nr_vcpus <= max_vcpus,
				    "Invalid number of vcpus, must be between 1 and %d", max_vcpus);
			break;
		case 'c':
			cpulist = optarg;
			break;
		case 'o':
			p.partition_vcpu_memory_access = false;
			p.single_uffd = true;
			break;
		case 'r':
			p.readers_per_uffd = atoi(optarg);
			TEST_ASSERT(p.readers_per_uffd >= 1,
				    "Invalid number of readers per uffd %d: must be >=1",
				    p.readers_per_uffd);
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	if (p.uffd_mode == UFFDIO_REGISTER_MODE_MINOR &&
	    !backing_src_is_shared(p.src_type)) {
		TEST_FAIL("userfaultfd MINOR mode requires shared memory; pick a different -s");
	}

	if (cpulist) {
		kvm_parse_vcpu_pinning(cpulist, memstress_args.vcpu_to_pcpu,
				       nr_vcpus);
		memstress_args.pin_vcpus = true;
	}

	for_each_guest_mode(run_test, &p);

	return 0;
}

#else /* __NR_userfaultfd */

#warning "missing __NR_userfaultfd definition"

int main(void)
{
	print_skip("__NR_userfaultfd must be present for userfaultfd test");
	return KSFT_SKIP;
}

#endif /* __NR_userfaultfd */
