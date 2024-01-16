// SPDX-License-Identifier: GPL-2.0
/*
 * KVM demand paging test
 * Adapted from dirty_log_test.c
 *
 * Copyright (C) 2018, Red Hat, Inc.
 * Copyright (C) 2019, Google, Inc.
 */

#define _GNU_SOURCE /* for pipe2 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include <linux/userfaultfd.h>
#include <sys/syscall.h>

#include "kvm_util.h"
#include "test_util.h"
#include "perf_test_util.h"
#include "guest_modes.h"

#ifdef __NR_userfaultfd

#ifdef PRINT_PER_PAGE_UPDATES
#define PER_PAGE_DEBUG(...) printf(__VA_ARGS__)
#else
#define PER_PAGE_DEBUG(...) _no_printf(__VA_ARGS__)
#endif

#ifdef PRINT_PER_VCPU_UPDATES
#define PER_VCPU_DEBUG(...) printf(__VA_ARGS__)
#else
#define PER_VCPU_DEBUG(...) _no_printf(__VA_ARGS__)
#endif

static int nr_vcpus = 1;
static uint64_t guest_percpu_mem_size = DEFAULT_PER_VCPU_MEM_SIZE;
static size_t demand_paging_size;
static char *guest_data_prototype;

static void vcpu_worker(struct perf_test_vcpu_args *vcpu_args)
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
	TEST_ASSERT(ret == 0, "vcpu_run failed: %d\n", ret);
	if (get_ucall(vcpu, NULL) != UCALL_SYNC) {
		TEST_ASSERT(false,
			    "Invalid guest sync status: exit_reason=%s\n",
			    exit_reason_str(run->exit_reason));
	}

	ts_diff = timespec_elapsed(start);
	PER_VCPU_DEBUG("vCPU %d execution time: %ld.%.9lds\n", vcpu_idx,
		       ts_diff.tv_sec, ts_diff.tv_nsec);
}

static int handle_uffd_page_request(int uffd_mode, int uffd, uint64_t addr)
{
	pid_t tid = syscall(__NR_gettid);
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
		if (r == -1) {
			pr_info("Failed UFFDIO_COPY in 0x%lx from thread %d with errno: %d\n",
				addr, tid, errno);
			return r;
		}
	} else if (uffd_mode == UFFDIO_REGISTER_MODE_MINOR) {
		struct uffdio_continue cont = {0};

		cont.range.start = addr;
		cont.range.len = demand_paging_size;

		r = ioctl(uffd, UFFDIO_CONTINUE, &cont);
		if (r == -1) {
			pr_info("Failed UFFDIO_CONTINUE in 0x%lx from thread %d with errno: %d\n",
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

bool quit_uffd_thread;

struct uffd_handler_args {
	int uffd_mode;
	int uffd;
	int pipefd;
	useconds_t delay;
};

static void *uffd_handler_thread_fn(void *arg)
{
	struct uffd_handler_args *uffd_args = (struct uffd_handler_args *)arg;
	int uffd = uffd_args->uffd;
	int pipefd = uffd_args->pipefd;
	useconds_t delay = uffd_args->delay;
	int64_t pages = 0;
	struct timespec start;
	struct timespec ts_diff;

	clock_gettime(CLOCK_MONOTONIC, &start);
	while (!quit_uffd_thread) {
		struct uffd_msg msg;
		struct pollfd pollfd[2];
		char tmp_chr;
		int r;
		uint64_t addr;

		pollfd[0].fd = uffd;
		pollfd[0].events = POLLIN;
		pollfd[1].fd = pipefd;
		pollfd[1].events = POLLIN;

		r = poll(pollfd, 2, -1);
		switch (r) {
		case -1:
			pr_info("poll err");
			continue;
		case 0:
			continue;
		case 1:
			break;
		default:
			pr_info("Polling uffd returned %d", r);
			return NULL;
		}

		if (pollfd[0].revents & POLLERR) {
			pr_info("uffd revents has POLLERR");
			return NULL;
		}

		if (pollfd[1].revents & POLLIN) {
			r = read(pollfd[1].fd, &tmp_chr, 1);
			TEST_ASSERT(r == 1,
				    "Error reading pipefd in UFFD thread\n");
			return NULL;
		}

		if (!(pollfd[0].revents & POLLIN))
			continue;

		r = read(uffd, &msg, sizeof(msg));
		if (r == -1) {
			if (errno == EAGAIN)
				continue;
			pr_info("Read of uffd got errno %d\n", errno);
			return NULL;
		}

		if (r != sizeof(msg)) {
			pr_info("Read on uffd returned unexpected size: %d bytes", r);
			return NULL;
		}

		if (!(msg.event & UFFD_EVENT_PAGEFAULT))
			continue;

		if (delay)
			usleep(delay);
		addr =  msg.arg.pagefault.address;
		r = handle_uffd_page_request(uffd_args->uffd_mode, uffd, addr);
		if (r < 0)
			return NULL;
		pages++;
	}

	ts_diff = timespec_elapsed(start);
	PER_VCPU_DEBUG("userfaulted %ld pages over %ld.%.9lds. (%f/sec)\n",
		       pages, ts_diff.tv_sec, ts_diff.tv_nsec,
		       pages / ((double)ts_diff.tv_sec + (double)ts_diff.tv_nsec / 100000000.0));

	return NULL;
}

static void setup_demand_paging(struct kvm_vm *vm,
				pthread_t *uffd_handler_thread, int pipefd,
				int uffd_mode, useconds_t uffd_delay,
				struct uffd_handler_args *uffd_args,
				void *hva, void *alias, uint64_t len)
{
	bool is_minor = (uffd_mode == UFFDIO_REGISTER_MODE_MINOR);
	int uffd;
	struct uffdio_api uffdio_api;
	struct uffdio_register uffdio_register;
	uint64_t expected_ioctls = ((uint64_t) 1) << _UFFDIO_COPY;
	int ret;

	PER_PAGE_DEBUG("Userfaultfd %s mode, faults resolved with %s\n",
		       is_minor ? "MINOR" : "MISSING",
		       is_minor ? "UFFDIO_CONINUE" : "UFFDIO_COPY");

	/* In order to get minor faults, prefault via the alias. */
	if (is_minor) {
		size_t p;

		expected_ioctls = ((uint64_t) 1) << _UFFDIO_CONTINUE;

		TEST_ASSERT(alias != NULL, "Alias required for minor faults");
		for (p = 0; p < (len / demand_paging_size); ++p) {
			memcpy(alias + (p * demand_paging_size),
			       guest_data_prototype, demand_paging_size);
		}
	}

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	TEST_ASSERT(uffd >= 0, __KVM_SYSCALL_ERROR("userfaultfd()", uffd));

	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	ret = ioctl(uffd, UFFDIO_API, &uffdio_api);
	TEST_ASSERT(ret != -1, __KVM_SYSCALL_ERROR("UFFDIO_API", ret));

	uffdio_register.range.start = (uint64_t)hva;
	uffdio_register.range.len = len;
	uffdio_register.mode = uffd_mode;
	ret = ioctl(uffd, UFFDIO_REGISTER, &uffdio_register);
	TEST_ASSERT(ret != -1, __KVM_SYSCALL_ERROR("UFFDIO_REGISTER", ret));
	TEST_ASSERT((uffdio_register.ioctls & expected_ioctls) ==
		    expected_ioctls, "missing userfaultfd ioctls");

	uffd_args->uffd_mode = uffd_mode;
	uffd_args->uffd = uffd;
	uffd_args->pipefd = pipefd;
	uffd_args->delay = uffd_delay;
	pthread_create(uffd_handler_thread, NULL, uffd_handler_thread_fn,
		       uffd_args);

	PER_VCPU_DEBUG("Created uffd thread for HVA range [%p, %p)\n",
		       hva, hva + len);
}

struct test_params {
	int uffd_mode;
	useconds_t uffd_delay;
	enum vm_mem_backing_src_type src_type;
	bool partition_vcpu_memory_access;
};

static void run_test(enum vm_guest_mode mode, void *arg)
{
	struct test_params *p = arg;
	pthread_t *uffd_handler_threads = NULL;
	struct uffd_handler_args *uffd_args = NULL;
	struct timespec start;
	struct timespec ts_diff;
	int *pipefds = NULL;
	struct kvm_vm *vm;
	int r, i;

	vm = perf_test_create_vm(mode, nr_vcpus, guest_percpu_mem_size, 1,
				 p->src_type, p->partition_vcpu_memory_access);

	demand_paging_size = get_backing_src_pagesz(p->src_type);

	guest_data_prototype = malloc(demand_paging_size);
	TEST_ASSERT(guest_data_prototype,
		    "Failed to allocate buffer for guest data pattern");
	memset(guest_data_prototype, 0xAB, demand_paging_size);

	if (p->uffd_mode) {
		uffd_handler_threads =
			malloc(nr_vcpus * sizeof(*uffd_handler_threads));
		TEST_ASSERT(uffd_handler_threads, "Memory allocation failed");

		uffd_args = malloc(nr_vcpus * sizeof(*uffd_args));
		TEST_ASSERT(uffd_args, "Memory allocation failed");

		pipefds = malloc(sizeof(int) * nr_vcpus * 2);
		TEST_ASSERT(pipefds, "Unable to allocate memory for pipefd");

		for (i = 0; i < nr_vcpus; i++) {
			struct perf_test_vcpu_args *vcpu_args;
			void *vcpu_hva;
			void *vcpu_alias;

			vcpu_args = &perf_test_args.vcpu_args[i];

			/* Cache the host addresses of the region */
			vcpu_hva = addr_gpa2hva(vm, vcpu_args->gpa);
			vcpu_alias = addr_gpa2alias(vm, vcpu_args->gpa);

			/*
			 * Set up user fault fd to handle demand paging
			 * requests.
			 */
			r = pipe2(&pipefds[i * 2],
				  O_CLOEXEC | O_NONBLOCK);
			TEST_ASSERT(!r, "Failed to set up pipefd");

			setup_demand_paging(vm, &uffd_handler_threads[i],
					    pipefds[i * 2], p->uffd_mode,
					    p->uffd_delay, &uffd_args[i],
					    vcpu_hva, vcpu_alias,
					    vcpu_args->pages * perf_test_args.guest_page_size);
		}
	}

	pr_info("Finished creating vCPUs and starting uffd threads\n");

	clock_gettime(CLOCK_MONOTONIC, &start);
	perf_test_start_vcpu_threads(nr_vcpus, vcpu_worker);
	pr_info("Started all vCPUs\n");

	perf_test_join_vcpu_threads(nr_vcpus);
	ts_diff = timespec_elapsed(start);
	pr_info("All vCPU threads joined\n");

	if (p->uffd_mode) {
		char c;

		/* Tell the user fault fd handler threads to quit */
		for (i = 0; i < nr_vcpus; i++) {
			r = write(pipefds[i * 2 + 1], &c, 1);
			TEST_ASSERT(r == 1, "Unable to write to pipefd");

			pthread_join(uffd_handler_threads[i], NULL);
		}
	}

	pr_info("Total guest execution time: %ld.%.9lds\n",
		ts_diff.tv_sec, ts_diff.tv_nsec);
	pr_info("Overall demand paging rate: %f pgs/sec\n",
		perf_test_args.vcpu_args[0].pages * nr_vcpus /
		((double)ts_diff.tv_sec + (double)ts_diff.tv_nsec / 100000000.0));

	perf_test_destroy_vm(vm);

	free(guest_data_prototype);
	if (p->uffd_mode) {
		free(uffd_handler_threads);
		free(uffd_args);
		free(pipefds);
	}
}

static void help(char *name)
{
	puts("");
	printf("usage: %s [-h] [-m vm_mode] [-u uffd_mode] [-d uffd_delay_usec]\n"
	       "          [-b memory] [-s type] [-v vcpus] [-o]\n", name);
	guest_modes_help();
	printf(" -u: use userfaultfd to handle vCPU page faults. Mode is a\n"
	       "     UFFD registration mode: 'MISSING' or 'MINOR'.\n");
	printf(" -d: add a delay in usec to the User Fault\n"
	       "     FD handler to simulate demand paging\n"
	       "     overheads. Ignored without -u.\n");
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
	struct test_params p = {
		.src_type = DEFAULT_VM_MEM_SRC,
		.partition_vcpu_memory_access = true,
	};
	int opt;

	guest_modes_append_default();

	while ((opt = getopt(argc, argv, "hm:u:d:b:s:v:o")) != -1) {
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
			nr_vcpus = atoi(optarg);
			TEST_ASSERT(nr_vcpus > 0 && nr_vcpus <= max_vcpus,
				    "Invalid number of vcpus, must be between 1 and %d", max_vcpus);
			break;
		case 'o':
			p.partition_vcpu_memory_access = false;
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
