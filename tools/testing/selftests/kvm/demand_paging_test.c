// SPDX-License-Identifier: GPL-2.0
/*
 * KVM demand paging test
 * Adapted from dirty_log_test.c
 *
 * Copyright (C) 2018, Red Hat, Inc.
 * Copyright (C) 2019, Google, Inc.
 */

#define _GNU_SOURCE /* for program_invocation_name */

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

#include "perf_test_util.h"
#include "processor.h"
#include "test_util.h"

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

static char *guest_data_prototype;

static void *vcpu_worker(void *data)
{
	int ret;
	struct vcpu_args *vcpu_args = (struct vcpu_args *)data;
	int vcpu_id = vcpu_args->vcpu_id;
	struct kvm_vm *vm = perf_test_args.vm;
	struct kvm_run *run;
	struct timespec start;
	struct timespec ts_diff;

	vcpu_args_set(vm, vcpu_id, 1, vcpu_id);
	run = vcpu_state(vm, vcpu_id);

	clock_gettime(CLOCK_MONOTONIC, &start);

	/* Let the guest access its memory */
	ret = _vcpu_run(vm, vcpu_id);
	TEST_ASSERT(ret == 0, "vcpu_run failed: %d\n", ret);
	if (get_ucall(vm, vcpu_id, NULL) != UCALL_SYNC) {
		TEST_ASSERT(false,
			    "Invalid guest sync status: exit_reason=%s\n",
			    exit_reason_str(run->exit_reason));
	}

	ts_diff = timespec_diff_now(start);
	PER_VCPU_DEBUG("vCPU %d execution time: %ld.%.9lds\n", vcpu_id,
		       ts_diff.tv_sec, ts_diff.tv_nsec);

	return NULL;
}

static int handle_uffd_page_request(int uffd, uint64_t addr)
{
	pid_t tid;
	struct timespec start;
	struct timespec ts_diff;
	struct uffdio_copy copy;
	int r;

	tid = syscall(__NR_gettid);

	copy.src = (uint64_t)guest_data_prototype;
	copy.dst = addr;
	copy.len = perf_test_args.host_page_size;
	copy.mode = 0;

	clock_gettime(CLOCK_MONOTONIC, &start);

	r = ioctl(uffd, UFFDIO_COPY, &copy);
	if (r == -1) {
		pr_info("Failed Paged in 0x%lx from thread %d with errno: %d\n",
			addr, tid, errno);
		return r;
	}

	ts_diff = timespec_diff_now(start);

	PER_PAGE_DEBUG("UFFDIO_COPY %d \t%ld ns\n", tid,
		       timespec_to_ns(ts_diff));
	PER_PAGE_DEBUG("Paged in %ld bytes at 0x%lx from thread %d\n",
		       perf_test_args.host_page_size, addr, tid);

	return 0;
}

bool quit_uffd_thread;

struct uffd_handler_args {
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

		if (!pollfd[0].revents & POLLIN)
			continue;

		r = read(uffd, &msg, sizeof(msg));
		if (r == -1) {
			if (errno == EAGAIN)
				continue;
			pr_info("Read of uffd gor errno %d", errno);
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
		r = handle_uffd_page_request(uffd, addr);
		if (r < 0)
			return NULL;
		pages++;
	}

	ts_diff = timespec_diff_now(start);
	PER_VCPU_DEBUG("userfaulted %ld pages over %ld.%.9lds. (%f/sec)\n",
		       pages, ts_diff.tv_sec, ts_diff.tv_nsec,
		       pages / ((double)ts_diff.tv_sec + (double)ts_diff.tv_nsec / 100000000.0));

	return NULL;
}

static int setup_demand_paging(struct kvm_vm *vm,
			       pthread_t *uffd_handler_thread, int pipefd,
			       useconds_t uffd_delay,
			       struct uffd_handler_args *uffd_args,
			       void *hva, uint64_t len)
{
	int uffd;
	struct uffdio_api uffdio_api;
	struct uffdio_register uffdio_register;

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd == -1) {
		pr_info("uffd creation failed\n");
		return -1;
	}

	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
		pr_info("ioctl uffdio_api failed\n");
		return -1;
	}

	uffdio_register.range.start = (uint64_t)hva;
	uffdio_register.range.len = len;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
		pr_info("ioctl uffdio_register failed\n");
		return -1;
	}

	if ((uffdio_register.ioctls & UFFD_API_RANGE_IOCTLS) !=
			UFFD_API_RANGE_IOCTLS) {
		pr_info("unexpected userfaultfd ioctl set\n");
		return -1;
	}

	uffd_args->uffd = uffd;
	uffd_args->pipefd = pipefd;
	uffd_args->delay = uffd_delay;
	pthread_create(uffd_handler_thread, NULL, uffd_handler_thread_fn,
		       uffd_args);

	PER_VCPU_DEBUG("Created uffd thread for HVA range [%p, %p)\n",
		       hva, hva + len);

	return 0;
}

static void run_test(enum vm_guest_mode mode, bool use_uffd,
		     useconds_t uffd_delay)
{
	pthread_t *vcpu_threads;
	pthread_t *uffd_handler_threads = NULL;
	struct uffd_handler_args *uffd_args = NULL;
	struct timespec start;
	struct timespec ts_diff;
	int *pipefds = NULL;
	struct kvm_vm *vm;
	int vcpu_id;
	int r;

	vm = create_vm(mode, nr_vcpus, guest_percpu_mem_size);

	perf_test_args.wr_fract = 1;

	guest_data_prototype = malloc(perf_test_args.host_page_size);
	TEST_ASSERT(guest_data_prototype,
		    "Failed to allocate buffer for guest data pattern");
	memset(guest_data_prototype, 0xAB, perf_test_args.host_page_size);

	vcpu_threads = malloc(nr_vcpus * sizeof(*vcpu_threads));
	TEST_ASSERT(vcpu_threads, "Memory allocation failed");

	add_vcpus(vm, nr_vcpus, guest_percpu_mem_size);

	if (use_uffd) {
		uffd_handler_threads =
			malloc(nr_vcpus * sizeof(*uffd_handler_threads));
		TEST_ASSERT(uffd_handler_threads, "Memory allocation failed");

		uffd_args = malloc(nr_vcpus * sizeof(*uffd_args));
		TEST_ASSERT(uffd_args, "Memory allocation failed");

		pipefds = malloc(sizeof(int) * nr_vcpus * 2);
		TEST_ASSERT(pipefds, "Unable to allocate memory for pipefd");

		for (vcpu_id = 0; vcpu_id < nr_vcpus; vcpu_id++) {
			vm_paddr_t vcpu_gpa;
			void *vcpu_hva;

			vcpu_gpa = guest_test_phys_mem + (vcpu_id * guest_percpu_mem_size);
			PER_VCPU_DEBUG("Added VCPU %d with test mem gpa [%lx, %lx)\n",
				       vcpu_id, vcpu_gpa, vcpu_gpa + guest_percpu_mem_size);

			/* Cache the HVA pointer of the region */
			vcpu_hva = addr_gpa2hva(vm, vcpu_gpa);

			/*
			 * Set up user fault fd to handle demand paging
			 * requests.
			 */
			r = pipe2(&pipefds[vcpu_id * 2],
				  O_CLOEXEC | O_NONBLOCK);
			TEST_ASSERT(!r, "Failed to set up pipefd");

			r = setup_demand_paging(vm,
						&uffd_handler_threads[vcpu_id],
						pipefds[vcpu_id * 2],
						uffd_delay, &uffd_args[vcpu_id],
						vcpu_hva, guest_percpu_mem_size);
			if (r < 0)
				exit(-r);
		}
	}

	/* Export the shared variables to the guest */
	sync_global_to_guest(vm, perf_test_args);

	pr_info("Finished creating vCPUs and starting uffd threads\n");

	clock_gettime(CLOCK_MONOTONIC, &start);

	for (vcpu_id = 0; vcpu_id < nr_vcpus; vcpu_id++) {
		pthread_create(&vcpu_threads[vcpu_id], NULL, vcpu_worker,
			       &perf_test_args.vcpu_args[vcpu_id]);
	}

	pr_info("Started all vCPUs\n");

	/* Wait for the vcpu threads to quit */
	for (vcpu_id = 0; vcpu_id < nr_vcpus; vcpu_id++) {
		pthread_join(vcpu_threads[vcpu_id], NULL);
		PER_VCPU_DEBUG("Joined thread for vCPU %d\n", vcpu_id);
	}

	ts_diff = timespec_diff_now(start);

	pr_info("All vCPU threads joined\n");

	if (use_uffd) {
		char c;

		/* Tell the user fault fd handler threads to quit */
		for (vcpu_id = 0; vcpu_id < nr_vcpus; vcpu_id++) {
			r = write(pipefds[vcpu_id * 2 + 1], &c, 1);
			TEST_ASSERT(r == 1, "Unable to write to pipefd");

			pthread_join(uffd_handler_threads[vcpu_id], NULL);
		}
	}

	pr_info("Total guest execution time: %ld.%.9lds\n",
		ts_diff.tv_sec, ts_diff.tv_nsec);
	pr_info("Overall demand paging rate: %f pgs/sec\n",
		perf_test_args.vcpu_args[0].pages * nr_vcpus /
		((double)ts_diff.tv_sec + (double)ts_diff.tv_nsec / 100000000.0));

	ucall_uninit(vm);
	kvm_vm_free(vm);

	free(guest_data_prototype);
	free(vcpu_threads);
	if (use_uffd) {
		free(uffd_handler_threads);
		free(uffd_args);
		free(pipefds);
	}
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
	printf("usage: %s [-h] [-m mode] [-u] [-d uffd_delay_usec]\n"
	       "          [-b memory] [-v vcpus]\n", name);
	printf(" -m: specify the guest mode ID to test\n"
	       "     (default: test all supported modes)\n"
	       "     This option may be used multiple times.\n"
	       "     Guest mode IDs:\n");
	for (i = 0; i < NUM_VM_MODES; ++i) {
		printf("         %d:    %s%s\n", i, vm_guest_mode_string(i),
		       guest_modes[i].supported ? " (supported)" : "");
	}
	printf(" -u: use User Fault FD to handle vCPU page\n"
	       "     faults.\n");
	printf(" -d: add a delay in usec to the User Fault\n"
	       "     FD handler to simulate demand paging\n"
	       "     overheads. Ignored without -u.\n");
	printf(" -b: specify the size of the memory region which should be\n"
	       "     demand paged by each vCPU. e.g. 10M or 3G.\n"
	       "     Default: 1G\n");
	printf(" -v: specify the number of vCPUs to run.\n");
	puts("");
	exit(0);
}

int main(int argc, char *argv[])
{
	int max_vcpus = kvm_check_cap(KVM_CAP_MAX_VCPUS);
	bool mode_selected = false;
	unsigned int mode;
	int opt, i;
	bool use_uffd = false;
	useconds_t uffd_delay = 0;

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

	while ((opt = getopt(argc, argv, "hm:ud:b:v:")) != -1) {
		switch (opt) {
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
		case 'u':
			use_uffd = true;
			break;
		case 'd':
			uffd_delay = strtoul(optarg, NULL, 0);
			TEST_ASSERT(uffd_delay >= 0,
				    "A negative UFFD delay is not supported.");
			break;
		case 'b':
			guest_percpu_mem_size = parse_size(optarg);
			break;
		case 'v':
			nr_vcpus = atoi(optarg);
			TEST_ASSERT(nr_vcpus > 0 && nr_vcpus <= max_vcpus,
				    "Invalid number of vcpus, must be between 1 and %d", max_vcpus);
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	for (i = 0; i < NUM_VM_MODES; ++i) {
		if (!guest_modes[i].enabled)
			continue;
		TEST_ASSERT(guest_modes[i].supported,
			    "Guest mode ID %d (%s) not supported.",
			    i, vm_guest_mode_string(i));
		run_test(i, use_uffd, uffd_delay);
	}

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
