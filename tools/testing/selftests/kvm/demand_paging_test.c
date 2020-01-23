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

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#ifdef __NR_userfaultfd
#define VCPU_ID				1

/* The memory slot index demand page */
#define TEST_MEM_SLOT_INDEX		1

/* Default guest test virtual memory offset */
#define DEFAULT_GUEST_TEST_MEM		0xc0000000

#define DEFAULT_GUEST_TEST_MEM_SIZE (1 << 30) /* 1G */

/*
 * Guest/Host shared variables. Ensure addr_gva2hva() and/or
 * sync_global_to/from_guest() are used when accessing from
 * the host. READ/WRITE_ONCE() should also be used with anything
 * that may change.
 */
static uint64_t host_page_size;
static uint64_t guest_page_size;
static uint64_t guest_num_pages;

static char *guest_data_prototype;

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

/*
 * Continuously write to the first 8 bytes of each page in the demand paging
 * memory region.
 */
static void guest_code(void)
{
	int i;

	for (i = 0; i < guest_num_pages; i++) {
		uint64_t addr = guest_test_virt_mem;

		addr += i * guest_page_size;
		addr &= ~(host_page_size - 1);
		*(uint64_t *)addr = 0x0123456789ABCDEF;
	}

	GUEST_SYNC(1);
}

/* Points to the test VM memory region on which we are doing demand paging */
static void *host_test_mem;
static uint64_t host_num_pages;

static void *vcpu_worker(void *data)
{
	int ret;
	struct kvm_vm *vm = data;
	struct kvm_run *run;

	run = vcpu_state(vm, VCPU_ID);

	/* Let the guest access its memory */
	ret = _vcpu_run(vm, VCPU_ID);
	TEST_ASSERT(ret == 0, "vcpu_run failed: %d\n", ret);
	if (get_ucall(vm, VCPU_ID, NULL) != UCALL_SYNC) {
		TEST_ASSERT(false,
			    "Invalid guest sync status: exit_reason=%s\n",
			    exit_reason_str(run->exit_reason));
	}

	return NULL;
}

static struct kvm_vm *create_vm(enum vm_guest_mode mode, uint32_t vcpuid,
				uint64_t extra_mem_pages, void *guest_code)
{
	struct kvm_vm *vm;
	uint64_t extra_pg_pages = extra_mem_pages / 512 * 2;

	vm = _vm_create(mode, DEFAULT_GUEST_PHY_PAGES + extra_pg_pages, O_RDWR);
	kvm_vm_elf_load(vm, program_invocation_name, 0, 0);
#ifdef __x86_64__
	vm_create_irqchip(vm);
#endif
	vm_vcpu_add_default(vm, vcpuid, guest_code);
	return vm;
}

static int handle_uffd_page_request(int uffd, uint64_t addr)
{
	pid_t tid;
	struct uffdio_copy copy;
	int r;

	tid = syscall(__NR_gettid);

	copy.src = (uint64_t)guest_data_prototype;
	copy.dst = addr;
	copy.len = host_page_size;
	copy.mode = 0;

	r = ioctl(uffd, UFFDIO_COPY, &copy);
	if (r == -1) {
		DEBUG("Failed Paged in 0x%lx from thread %d with errno: %d\n",
		      addr, tid, errno);
		return r;
	}

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
			DEBUG("poll err");
			continue;
		case 0:
			continue;
		case 1:
			break;
		default:
			DEBUG("Polling uffd returned %d", r);
			return NULL;
		}

		if (pollfd[0].revents & POLLERR) {
			DEBUG("uffd revents has POLLERR");
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
			DEBUG("Read of uffd gor errno %d", errno);
			return NULL;
		}

		if (r != sizeof(msg)) {
			DEBUG("Read on uffd returned unexpected size: %d bytes",
			      r);
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

	return NULL;
}

static int setup_demand_paging(struct kvm_vm *vm,
			       pthread_t *uffd_handler_thread, int pipefd,
			       useconds_t uffd_delay)
{
	int uffd;
	struct uffdio_api uffdio_api;
	struct uffdio_register uffdio_register;
	struct uffd_handler_args uffd_args;

	guest_data_prototype = malloc(host_page_size);
	TEST_ASSERT(guest_data_prototype,
		    "Failed to allocate buffer for guest data pattern");
	memset(guest_data_prototype, 0xAB, host_page_size);

	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd == -1) {
		DEBUG("uffd creation failed\n");
		return -1;
	}

	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1) {
		DEBUG("ioctl uffdio_api failed\n");
		return -1;
	}

	uffdio_register.range.start = (uint64_t)host_test_mem;
	uffdio_register.range.len = host_num_pages * host_page_size;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1) {
		DEBUG("ioctl uffdio_register failed\n");
		return -1;
	}

	if ((uffdio_register.ioctls & UFFD_API_RANGE_IOCTLS) !=
			UFFD_API_RANGE_IOCTLS) {
		DEBUG("unexpected userfaultfd ioctl set\n");
		return -1;
	}

	uffd_args.uffd = uffd;
	uffd_args.pipefd = pipefd;
	uffd_args.delay = uffd_delay;
	pthread_create(uffd_handler_thread, NULL, uffd_handler_thread_fn,
		       &uffd_args);

	return 0;
}

#define PAGE_SHIFT_4K  12

static void run_test(enum vm_guest_mode mode, bool use_uffd,
		     useconds_t uffd_delay, uint64_t guest_memory_bytes)
{
	pthread_t vcpu_thread;
	pthread_t uffd_handler_thread;
	int pipefd[2];
	struct kvm_vm *vm;
	int r;

	/*
	 * We reserve page table for twice the ammount of memory we intend
	 * to use in the test region for demand paging. Here we do the
	 * calculation with 4K page size which is the smallest so the page
	 * number will be enough for all archs. (e.g., 64K page size guest
	 * will need even less memory for page tables).
	 */
	vm = create_vm(mode, VCPU_ID,
		       (2 * guest_memory_bytes) >> PAGE_SHIFT_4K,
		       guest_code);

	guest_page_size = vm_get_page_size(vm);

	TEST_ASSERT(guest_memory_bytes % guest_page_size == 0,
		    "Guest memory size is not guest page size aligned.");

	guest_num_pages = guest_memory_bytes / guest_page_size;

#ifdef __s390x__
	/* Round up to multiple of 1M (segment size) */
	guest_num_pages = (guest_num_pages + 0xff) & ~0xffUL;
#endif
	/*
	 * If there should be more memory in the guest test region than there
	 * can be pages in the guest, it will definitely cause problems.
	 */
	TEST_ASSERT(guest_num_pages < vm_get_max_gfn(vm),
		    "Requested more guest memory than address space allows.\n"
		    "    guest pages: %lx max gfn: %lx\n",
		    guest_num_pages, vm_get_max_gfn(vm));

	host_page_size = getpagesize();
	TEST_ASSERT(guest_memory_bytes % host_page_size == 0,
		    "Guest memory size is not host page size aligned.");
	host_num_pages = guest_memory_bytes / host_page_size;

	guest_test_phys_mem = (vm_get_max_gfn(vm) - guest_num_pages) *
			      guest_page_size;
	guest_test_phys_mem &= ~(host_page_size - 1);

#ifdef __s390x__
	/* Align to 1M (segment size) */
	guest_test_phys_mem &= ~((1 << 20) - 1);
#endif

	DEBUG("guest physical test memory offset: 0x%lx\n",
	      guest_test_phys_mem);


	/* Add an extra memory slot for testing demand paging */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    guest_test_phys_mem,
				    TEST_MEM_SLOT_INDEX,
				    guest_num_pages, 0);

	/* Do mapping for the demand paging memory slot */
	virt_map(vm, guest_test_virt_mem, guest_test_phys_mem,
		 guest_num_pages * guest_page_size, 0);

	/* Cache the HVA pointer of the region */
	host_test_mem = addr_gpa2hva(vm, (vm_paddr_t)guest_test_phys_mem);

	if (use_uffd) {
		/* Set up user fault fd to handle demand paging requests. */
		r = pipe2(pipefd, O_CLOEXEC | O_NONBLOCK);
		TEST_ASSERT(!r, "Failed to set up pipefd");

		r = setup_demand_paging(vm, &uffd_handler_thread, pipefd[0],
					uffd_delay);
		if (r < 0)
			exit(-r);
	}

#ifdef __x86_64__
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());
#endif
#ifdef __aarch64__
	ucall_init(vm, NULL);
#endif

	/* Export the shared variables to the guest */
	sync_global_to_guest(vm, host_page_size);
	sync_global_to_guest(vm, guest_page_size);
	sync_global_to_guest(vm, guest_test_virt_mem);
	sync_global_to_guest(vm, guest_num_pages);

	pthread_create(&vcpu_thread, NULL, vcpu_worker, vm);

	/* Wait for the vcpu thread to quit */
	pthread_join(vcpu_thread, NULL);

	if (use_uffd) {
		char c;

		/* Tell the user fault fd handler thread to quit */
		r = write(pipefd[1], &c, 1);
		TEST_ASSERT(r == 1, "Unable to write to pipefd");

		pthread_join(uffd_handler_thread, NULL);
	}

	ucall_uninit(vm);
	kvm_vm_free(vm);

	free(guest_data_prototype);
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
	       "          [-b memory]\n", name);
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
	       "     demand paged. e.g. 10M or 3G. Default: 1G\n");
	puts("");
	exit(0);
}

int main(int argc, char *argv[])
{
	bool mode_selected = false;
	uint64_t guest_memory_bytes = DEFAULT_GUEST_TEST_MEM_SIZE;
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

	while ((opt = getopt(argc, argv, "hm:ud:b:")) != -1) {
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
			guest_memory_bytes = parse_size(optarg);
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
		run_test(i, use_uffd, uffd_delay, guest_memory_bytes);
	}

	return 0;
}

#else /* __NR_userfaultfd */

#warning "missing __NR_userfaultfd definition"

int main(void)
{
        printf("skip: Skipping userfaultfd test (missing __NR_userfaultfd)\n");
        return KSFT_SKIP;
}

#endif /* __NR_userfaultfd */
