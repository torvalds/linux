// SPDX-License-Identifier: GPL-2.0
/*
 * KVM dirty page logging test
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>

#include "test_util.h"
#include "kvm_util.h"

#define  DEBUG                 printf

#define  VCPU_ID                        1
/* The memory slot index to track dirty pages */
#define  TEST_MEM_SLOT_INDEX            1
/*
 * GPA offset of the testing memory slot. Must be bigger than the
 * default vm mem slot, which is DEFAULT_GUEST_PHY_PAGES.
 */
#define  TEST_MEM_OFFSET                (1ULL << 30) /* 1G */
/* Size of the testing memory slot */
#define  TEST_MEM_PAGES                 (1ULL << 18) /* 1G for 4K pages */
/* How many pages to dirty for each guest loop */
#define  TEST_PAGES_PER_LOOP            1024
/* How many host loops to run (one KVM_GET_DIRTY_LOG for each loop) */
#define  TEST_HOST_LOOP_N               32
/* Interval for each host loop (ms) */
#define  TEST_HOST_LOOP_INTERVAL        10

/*
 * Guest variables.  We use these variables to share data between host
 * and guest.  There are two copies of the variables, one in host memory
 * (which is unused) and one in guest memory.  When the host wants to
 * access these variables, it needs to call addr_gva2hva() to access the
 * guest copy.
 */
uint64_t guest_random_array[TEST_PAGES_PER_LOOP];
uint64_t guest_iteration;
uint64_t guest_page_size;

/*
 * Writes to the first byte of a random page within the testing memory
 * region continuously.
 */
void guest_code(void)
{
	int i = 0;
	uint64_t volatile *array = guest_random_array;
	uint64_t volatile *guest_addr;

	while (true) {
		for (i = 0; i < TEST_PAGES_PER_LOOP; i++) {
			/*
			 * Write to the first 8 bytes of a random page
			 * on the testing memory region.
			 */
			guest_addr = (uint64_t *)
			    (TEST_MEM_OFFSET +
			     (array[i] % TEST_MEM_PAGES) * guest_page_size);
			*guest_addr = guest_iteration;
		}
		/* Tell the host that we need more random numbers */
		GUEST_SYNC(1);
	}
}

/*
 * Host variables.  These variables should only be used by the host
 * rather than the guest.
 */
bool host_quit;

/* Points to the test VM memory region on which we track dirty logs */
void *host_test_mem;

/* For statistics only */
uint64_t host_dirty_count;
uint64_t host_clear_count;
uint64_t host_track_next_count;

/*
 * We use this bitmap to track some pages that should have its dirty
 * bit set in the _next_ iteration.  For example, if we detected the
 * page value changed to current iteration but at the same time the
 * page bit is cleared in the latest bitmap, then the system must
 * report that write in the next get dirty log call.
 */
unsigned long *host_bmap_track;

void generate_random_array(uint64_t *guest_array, uint64_t size)
{
	uint64_t i;

	for (i = 0; i < size; i++) {
		guest_array[i] = random();
	}
}

void *vcpu_worker(void *data)
{
	int ret;
	uint64_t loops, *guest_array, pages_count = 0;
	struct kvm_vm *vm = data;
	struct kvm_run *run;
	struct guest_args args;

	run = vcpu_state(vm, VCPU_ID);

	/* Retrieve the guest random array pointer and cache it */
	guest_array = addr_gva2hva(vm, (vm_vaddr_t)guest_random_array);

	DEBUG("VCPU starts\n");

	generate_random_array(guest_array, TEST_PAGES_PER_LOOP);

	while (!READ_ONCE(host_quit)) {
		/* Let the guest to dirty these random pages */
		ret = _vcpu_run(vm, VCPU_ID);
		guest_args_read(vm, VCPU_ID, &args);
		if (run->exit_reason == KVM_EXIT_IO &&
		    args.port == GUEST_PORT_SYNC) {
			pages_count += TEST_PAGES_PER_LOOP;
			generate_random_array(guest_array, TEST_PAGES_PER_LOOP);
		} else {
			TEST_ASSERT(false,
				    "Invalid guest sync status: "
				    "exit_reason=%s\n",
				    exit_reason_str(run->exit_reason));
		}
	}

	DEBUG("VCPU exits, dirtied %"PRIu64" pages\n", pages_count);

	return NULL;
}

void vm_dirty_log_verify(unsigned long *bmap, uint64_t iteration)
{
	uint64_t page;
	uint64_t volatile *value_ptr;

	for (page = 0; page < TEST_MEM_PAGES; page++) {
		value_ptr = host_test_mem + page * getpagesize();

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

void help(char *name)
{
	puts("");
	printf("usage: %s [-i iterations] [-I interval] [-h]\n", name);
	puts("");
	printf(" -i: specify iteration counts (default: %"PRIu64")\n",
	       TEST_HOST_LOOP_N);
	printf(" -I: specify interval in ms (default: %"PRIu64" ms)\n",
	       TEST_HOST_LOOP_INTERVAL);
	puts("");
	exit(0);
}

int main(int argc, char *argv[])
{
	pthread_t vcpu_thread;
	struct kvm_vm *vm;
	uint64_t volatile *psize, *iteration;
	unsigned long *bmap, iterations = TEST_HOST_LOOP_N,
	    interval = TEST_HOST_LOOP_INTERVAL;
	int opt;

	while ((opt = getopt(argc, argv, "hi:I:")) != -1) {
		switch (opt) {
		case 'i':
			iterations = strtol(optarg, NULL, 10);
			break;
		case 'I':
			interval = strtol(optarg, NULL, 10);
			break;
		case 'h':
		default:
			help(argv[0]);
			break;
		}
	}

	TEST_ASSERT(iterations > 2, "Iteration must be bigger than zero\n");
	TEST_ASSERT(interval > 0, "Interval must be bigger than zero");

	DEBUG("Test iterations: %"PRIu64", interval: %"PRIu64" (ms)\n",
	      iterations, interval);

	srandom(time(0));

	bmap = bitmap_alloc(TEST_MEM_PAGES);
	host_bmap_track = bitmap_alloc(TEST_MEM_PAGES);

	vm = vm_create_default(VCPU_ID, TEST_MEM_PAGES, guest_code);

	/* Add an extra memory slot for testing dirty logging */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    TEST_MEM_OFFSET,
				    TEST_MEM_SLOT_INDEX,
				    TEST_MEM_PAGES,
				    KVM_MEM_LOG_DIRTY_PAGES);
	/* Cache the HVA pointer of the region */
	host_test_mem = addr_gpa2hva(vm, (vm_paddr_t)TEST_MEM_OFFSET);

	/* Do 1:1 mapping for the dirty track memory slot */
	virt_map(vm, TEST_MEM_OFFSET, TEST_MEM_OFFSET,
		 TEST_MEM_PAGES * getpagesize(), 0);

	vcpu_set_cpuid(vm, VCPU_ID, kvm_get_supported_cpuid());

	/* Tell the guest about the page size on the system */
	psize = addr_gva2hva(vm, (vm_vaddr_t)&guest_page_size);
	*psize = getpagesize();

	/* Start the iterations */
	iteration = addr_gva2hva(vm, (vm_vaddr_t)&guest_iteration);
	*iteration = 1;

	/* Start dirtying pages */
	pthread_create(&vcpu_thread, NULL, vcpu_worker, vm);

	while (*iteration < iterations) {
		/* Give the vcpu thread some time to dirty some pages */
		usleep(interval * 1000);
		kvm_vm_get_dirty_log(vm, TEST_MEM_SLOT_INDEX, bmap);
		vm_dirty_log_verify(bmap, *iteration);
		(*iteration)++;
	}

	/* Tell the vcpu thread to quit */
	host_quit = true;
	pthread_join(vcpu_thread, NULL);

	DEBUG("Total bits checked: dirty (%"PRIu64"), clear (%"PRIu64"), "
	      "track_next (%"PRIu64")\n", host_dirty_count, host_clear_count,
	      host_track_next_count);

	free(bmap);
	free(host_bmap_track);
	kvm_vm_free(vm);

	return 0;
}
