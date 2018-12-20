// SPDX-License-Identifier: GPL-2.0
/*
 * KVM dirty page logging test
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */

#define _GNU_SOURCE /* for program_invocation_name */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#define DEBUG printf

#define VCPU_ID				1

/* The memory slot index to track dirty pages */
#define TEST_MEM_SLOT_INDEX		1

/* Default guest test memory offset, 1G */
#define DEFAULT_GUEST_TEST_MEM		0x40000000

/* How many pages to dirty for each guest loop */
#define TEST_PAGES_PER_LOOP		1024

/* How many host loops to run (one KVM_GET_DIRTY_LOG for each loop) */
#define TEST_HOST_LOOP_N		32UL

/* Interval for each host loop (ms) */
#define TEST_HOST_LOOP_INTERVAL		10UL

/*
 * Guest/Host shared variables. Ensure addr_gva2hva() and/or
 * sync_global_to/from_guest() are used when accessing from
 * the host. READ/WRITE_ONCE() should also be used with anything
 * that may change.
 */
static uint64_t host_page_size;
static uint64_t guest_page_size;
static uint64_t guest_num_pages;
static uint64_t random_array[TEST_PAGES_PER_LOOP];
static uint64_t iteration;

/*
 * GPA offset of the testing memory slot. Must be bigger than
 * DEFAULT_GUEST_PHY_PAGES.
 */
static uint64_t guest_test_mem = DEFAULT_GUEST_TEST_MEM;

/*
 * Continuously write to the first 8 bytes of a random pages within
 * the testing memory region.
 */
static void guest_code(void)
{
	int i;

	while (true) {
		for (i = 0; i < TEST_PAGES_PER_LOOP; i++) {
			uint64_t addr = guest_test_mem;
			addr += (READ_ONCE(random_array[i]) % guest_num_pages)
				* guest_page_size;
			addr &= ~(host_page_size - 1);
			*(uint64_t *)addr = READ_ONCE(iteration);
		}

		/* Tell the host that we need more random numbers */
		GUEST_SYNC(1);
	}
}

/* Host variables */
static bool host_quit;

/* Points to the test VM memory region on which we track dirty logs */
static void *host_test_mem;
static uint64_t host_num_pages;

/* For statistics only */
static uint64_t host_dirty_count;
static uint64_t host_clear_count;
static uint64_t host_track_next_count;

/*
 * We use this bitmap to track some pages that should have its dirty
 * bit set in the _next_ iteration.  For example, if we detected the
 * page value changed to current iteration but at the same time the
 * page bit is cleared in the latest bitmap, then the system must
 * report that write in the next get dirty log call.
 */
static unsigned long *host_bmap_track;

static void generate_random_array(uint64_t *guest_array, uint64_t size)
{
	uint64_t i;

	for (i = 0; i < size; i++)
		guest_array[i] = random();
}

static void *vcpu_worker(void *data)
{
	int ret;
	struct kvm_vm *vm = data;
	uint64_t *guest_array;
	uint64_t pages_count = 0;
	struct kvm_run *run;
	struct ucall uc;

	run = vcpu_state(vm, VCPU_ID);

	guest_array = addr_gva2hva(vm, (vm_vaddr_t)random_array);
	generate_random_array(guest_array, TEST_PAGES_PER_LOOP);

	while (!READ_ONCE(host_quit)) {
		/* Let the guest dirty the random pages */
		ret = _vcpu_run(vm, VCPU_ID);
		if (get_ucall(vm, VCPU_ID, &uc) == UCALL_SYNC) {
			pages_count += TEST_PAGES_PER_LOOP;
			generate_random_array(guest_array, TEST_PAGES_PER_LOOP);
		} else {
			TEST_ASSERT(false,
				    "Invalid guest sync status: "
				    "exit_reason=%s\n",
				    exit_reason_str(run->exit_reason));
		}
	}

	DEBUG("Dirtied %"PRIu64" pages\n", pages_count);

	return NULL;
}

static void vm_dirty_log_verify(unsigned long *bmap)
{
	uint64_t page;
	uint64_t *value_ptr;
	uint64_t step = host_page_size >= guest_page_size ? 1 :
				guest_page_size / host_page_size;

	for (page = 0; page < host_num_pages; page += step) {
		value_ptr = host_test_mem + page * host_page_size;

		/* If this is a special page that we were tracking... */
		if (test_and_clear_bit(page, host_bmap_track)) {
			host_track_next_count++;
			TEST_ASSERT(test_bit(page, bmap),
				    "Page %"PRIu64" should have its dirty bit "
				    "set in this iteration but it is missing",
				    page);
		}

		if (test_bit(page, bmap)) {
			host_dirty_count++;
			/*
			 * If the bit is set, the value written onto
			 * the corresponding page should be either the
			 * previous iteration number or the current one.
			 */
			TEST_ASSERT(*value_ptr == iteration ||
				    *value_ptr == iteration - 1,
				    "Set page %"PRIu64" value %"PRIu64
				    " incorrect (iteration=%"PRIu64")",
				    page, *value_ptr, iteration);
		} else {
			host_clear_count++;
			/*
			 * If cleared, the value written can be any
			 * value smaller or equals to the iteration
			 * number.  Note that the value can be exactly
			 * (iteration-1) if that write can happen
			 * like this:
			 *
			 * (1) increase loop count to "iteration-1"
			 * (2) write to page P happens (with value
			 *     "iteration-1")
			 * (3) get dirty log for "iteration-1"; we'll
			 *     see that page P bit is set (dirtied),
			 *     and not set the bit in host_bmap_track
			 * (4) increase loop count to "iteration"
			 *     (which is current iteration)
			 * (5) get dirty log for current iteration,
			 *     we'll see that page P is cleared, with
			 *     value "iteration-1".
			 */
			TEST_ASSERT(*value_ptr <= iteration,
				    "Clear page %"PRIu64" value %"PRIu64
				    " incorrect (iteration=%"PRIu64")",
				    page, *value_ptr, iteration);
			if (*value_ptr == iteration) {
				/*
				 * This page is _just_ modified; it
				 * should report its dirtyness in the
				 * next run
				 */
				set_bit(page, host_bmap_track);
			}
		}
	}
}

static struct kvm_vm *create_vm(enum vm_guest_mode mode, uint32_t vcpuid,
				uint64_t extra_mem_pages, void *guest_code)
{
	struct kvm_vm *vm;
	uint64_t extra_pg_pages = extra_mem_pages / 512 * 2;

	vm = vm_create(mode, DEFAULT_GUEST_PHY_PAGES + extra_pg_pages, O_RDWR);
	kvm_vm_elf_load(vm, program_invocation_name, 0, 0);
#ifdef __x86_64__
	vm_create_irqchip(vm);
#endif
	vm_vcpu_add_default(vm, vcpuid, guest_code);
	return vm;
}

static void run_test(enum vm_guest_mode mode, unsigned long iterations,
		     unsigned long interval, bool top_offset)
{
	unsigned int guest_pa_bits, guest_page_shift;
	pthread_t vcpu_thread;
	struct kvm_vm *vm;
	uint64_t max_gfn;
	unsigned long *bmap;

	switch (mode) {
	case VM_MODE_P52V48_4K:
		guest_pa_bits = 52;
		guest_page_shift = 12;
		break;
	case VM_MODE_P52V48_64K:
		guest_pa_bits = 52;
		guest_page_shift = 16;
		break;
	case VM_MODE_P40V48_4K:
		guest_pa_bits = 40;
		guest_page_shift = 12;
		break;
	case VM_MODE_P40V48_64K:
		guest_pa_bits = 40;
		guest_page_shift = 16;
		break;
	default:
		TEST_ASSERT(false, "Unknown guest mode, mode: 0x%x", mode);
	}

	DEBUG("Testing guest mode: %s\n", vm_guest_mode_string(mode));

	max_gfn = (1ul << (guest_pa_bits - guest_page_shift)) - 1;
	guest_page_size = (1ul << guest_page_shift);
	/* 1G of guest page sized pages */
	guest_num_pages = (1ul << (30 - guest_page_shift));
	host_page_size = getpagesize();
	host_num_pages = (guest_num_pages * guest_page_size) / host_page_size +
			 !!((guest_num_pages * guest_page_size) % host_page_size);

	if (top_offset) {
		guest_test_mem = (max_gfn - guest_num_pages) * guest_page_size;
		guest_test_mem &= ~(host_page_size - 1);
	}

	DEBUG("guest test mem offset: 0x%lx\n", guest_test_mem);

	bmap = bitmap_alloc(host_num_pages);
	host_bmap_track = bitmap_alloc(host_num_pages);

	vm = create_vm(mode, VCPU_ID, guest_num_pages, guest_code);

	/* Add an extra memory slot for testing dirty logging */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    guest_test_mem,
				    TEST_MEM_SLOT_INDEX,
				    guest_num_pages,
				    KVM_MEM_LOG_DIRTY_PAGES);

	/* Do 1:1 mapping for the dirty track memory slot */
	virt_map(vm, guest_test_mem, guest_test_mem,
		 guest_num_pages * guest_page_size, 0);

	/* Cache the HVA pointer of the region */
	host_test_mem = addr_gpa2hva(vm, (vm_paddr_t)guest_test_mem);

#ifdef __x86_64__
	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());
#endif
#ifdef __aarch64__
	ucall_init(vm, UCALL_MMIO, NULL);
#endif

	/* Export the shared variables to the guest */
	sync_global_to_guest(vm, host_page_size);
	sync_global_to_guest(vm, guest_page_size);
	sync_global_to_guest(vm, guest_test_mem);
	sync_global_to_guest(vm, guest_num_pages);

	/* Start the iterations */
	iteration = 1;
	sync_global_to_guest(vm, iteration);
	host_quit = false;
	host_dirty_count = 0;
	host_clear_count = 0;
	host_track_next_count = 0;

	pthread_create(&vcpu_thread, NULL, vcpu_worker, vm);

	while (iteration < iterations) {
		/* Give the vcpu thread some time to dirty some pages */
		usleep(interval * 1000);
		kvm_vm_get_dirty_log(vm, TEST_MEM_SLOT_INDEX, bmap);
		vm_dirty_log_verify(bmap);
		iteration++;
		sync_global_to_guest(vm, iteration);
	}

	/* Tell the vcpu thread to quit */
	host_quit = true;
	pthread_join(vcpu_thread, NULL);

	DEBUG("Total bits checked: dirty (%"PRIu64"), clear (%"PRIu64"), "
	      "track_next (%"PRIu64")\n", host_dirty_count, host_clear_count,
	      host_track_next_count);

	free(bmap);
	free(host_bmap_track);
	ucall_uninit(vm);
	kvm_vm_free(vm);
}

static struct vm_guest_modes {
	enum vm_guest_mode mode;
	bool supported;
	bool enabled;
} vm_guest_modes[NUM_VM_MODES] = {
#if defined(__x86_64__)
	{ VM_MODE_P52V48_4K,	1, 1, },
	{ VM_MODE_P52V48_64K,	0, 0, },
	{ VM_MODE_P40V48_4K,	0, 0, },
	{ VM_MODE_P40V48_64K,	0, 0, },
#elif defined(__aarch64__)
	{ VM_MODE_P52V48_4K,	0, 0, },
	{ VM_MODE_P52V48_64K,	0, 0, },
	{ VM_MODE_P40V48_4K,	1, 1, },
	{ VM_MODE_P40V48_64K,	1, 1, },
#endif
};

static void help(char *name)
{
	int i;

	puts("");
	printf("usage: %s [-h] [-i iterations] [-I interval] "
	       "[-o offset] [-t] [-m mode]\n", name);
	puts("");
	printf(" -i: specify iteration counts (default: %"PRIu64")\n",
	       TEST_HOST_LOOP_N);
	printf(" -I: specify interval in ms (default: %"PRIu64" ms)\n",
	       TEST_HOST_LOOP_INTERVAL);
	printf(" -o: guest test memory offset (default: 0x%lx)\n",
	       DEFAULT_GUEST_TEST_MEM);
	printf(" -t: map guest test memory at the top of the allowed "
	       "physical address range\n");
	printf(" -m: specify the guest mode ID to test "
	       "(default: test all supported modes)\n"
	       "     This option may be used multiple times.\n"
	       "     Guest mode IDs:\n");
	for (i = 0; i < NUM_VM_MODES; ++i) {
		printf("         %d:    %s%s\n",
		       vm_guest_modes[i].mode,
		       vm_guest_mode_string(vm_guest_modes[i].mode),
		       vm_guest_modes[i].supported ? " (supported)" : "");
	}
	puts("");
	exit(0);
}

int main(int argc, char *argv[])
{
	unsigned long iterations = TEST_HOST_LOOP_N;
	unsigned long interval = TEST_HOST_LOOP_INTERVAL;
	bool mode_selected = false;
	bool top_offset = false;
	unsigned int mode;
	int opt, i;

	while ((opt = getopt(argc, argv, "hi:I:o:tm:")) != -1) {
		switch (opt) {
		case 'i':
			iterations = strtol(optarg, NULL, 10);
			break;
		case 'I':
			interval = strtol(optarg, NULL, 10);
			break;
		case 'o':
			guest_test_mem = strtoull(optarg, NULL, 0);
			break;
		case 't':
			top_offset = true;
			break;
		case 'm':
			if (!mode_selected) {
				for (i = 0; i < NUM_VM_MODES; ++i)
					vm_guest_modes[i].enabled = 0;
				mode_selected = true;
			}
			mode = strtoul(optarg, NULL, 10);
			TEST_ASSERT(mode < NUM_VM_MODES,
				    "Guest mode ID %d too big", mode);
			vm_guest_modes[mode].enabled = 1;
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	TEST_ASSERT(iterations > 2, "Iterations must be greater than two");
	TEST_ASSERT(interval > 0, "Interval must be greater than zero");
	TEST_ASSERT(!top_offset || guest_test_mem == DEFAULT_GUEST_TEST_MEM,
		    "Cannot use both -o [offset] and -t at the same time");

	DEBUG("Test iterations: %"PRIu64", interval: %"PRIu64" (ms)\n",
	      iterations, interval);

	srandom(time(0));

	for (i = 0; i < NUM_VM_MODES; ++i) {
		if (!vm_guest_modes[i].enabled)
			continue;
		TEST_ASSERT(vm_guest_modes[i].supported,
			    "Guest mode ID %d (%s) not supported.",
			    vm_guest_modes[i].mode,
			    vm_guest_mode_string(vm_guest_modes[i].mode));
		run_test(vm_guest_modes[i].mode, iterations, interval, top_offset);
	}

	return 0;
}
