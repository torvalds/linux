// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020, Google LLC.
 */
#define _GNU_SOURCE

#include <inttypes.h>
#include <linux/bitmap.h>

#include "kvm_util.h"
#include "memstress.h"
#include "processor.h"

struct memstress_args memstress_args;

/*
 * Guest virtual memory offset of the testing memory slot.
 * Must not conflict with identity mapped test code.
 */
static uint64_t guest_test_virt_mem = DEFAULT_GUEST_TEST_MEM;

struct vcpu_thread {
	/* The index of the vCPU. */
	int vcpu_idx;

	/* The pthread backing the vCPU. */
	pthread_t thread;

	/* Set to true once the vCPU thread is up and running. */
	bool running;
};

/* The vCPU threads involved in this test. */
static struct vcpu_thread vcpu_threads[KVM_MAX_VCPUS];

/* The function run by each vCPU thread, as provided by the test. */
static void (*vcpu_thread_fn)(struct memstress_vcpu_args *);

/* Set to true once all vCPU threads are up and running. */
static bool all_vcpu_threads_running;

static struct kvm_vcpu *vcpus[KVM_MAX_VCPUS];

/*
 * Continuously write to the first 8 bytes of each page in the
 * specified region.
 */
void memstress_guest_code(uint32_t vcpu_idx)
{
	struct memstress_args *args = &memstress_args;
	struct memstress_vcpu_args *vcpu_args = &args->vcpu_args[vcpu_idx];
	struct guest_random_state rand_state;
	uint64_t gva;
	uint64_t pages;
	uint64_t addr;
	uint64_t page;
	int i;

	rand_state = new_guest_random_state(args->random_seed + vcpu_idx);

	gva = vcpu_args->gva;
	pages = vcpu_args->pages;

	/* Make sure vCPU args data structure is not corrupt. */
	GUEST_ASSERT(vcpu_args->vcpu_idx == vcpu_idx);

	while (true) {
		for (i = 0; i < sizeof(memstress_args); i += args->guest_page_size)
			(void) *((volatile char *)args + i);

		for (i = 0; i < pages; i++) {
			if (args->random_access)
				page = guest_random_u32(&rand_state) % pages;
			else
				page = i;

			addr = gva + (page * args->guest_page_size);

			if (guest_random_u32(&rand_state) % 100 < args->write_percent)
				*(uint64_t *)addr = 0x0123456789ABCDEF;
			else
				READ_ONCE(*(uint64_t *)addr);
		}

		GUEST_SYNC(1);
	}
}

void memstress_setup_vcpus(struct kvm_vm *vm, int nr_vcpus,
			   struct kvm_vcpu *vcpus[],
			   uint64_t vcpu_memory_bytes,
			   bool partition_vcpu_memory_access)
{
	struct memstress_args *args = &memstress_args;
	struct memstress_vcpu_args *vcpu_args;
	int i;

	for (i = 0; i < nr_vcpus; i++) {
		vcpu_args = &args->vcpu_args[i];

		vcpu_args->vcpu = vcpus[i];
		vcpu_args->vcpu_idx = i;

		if (partition_vcpu_memory_access) {
			vcpu_args->gva = guest_test_virt_mem +
					 (i * vcpu_memory_bytes);
			vcpu_args->pages = vcpu_memory_bytes /
					   args->guest_page_size;
			vcpu_args->gpa = args->gpa + (i * vcpu_memory_bytes);
		} else {
			vcpu_args->gva = guest_test_virt_mem;
			vcpu_args->pages = (nr_vcpus * vcpu_memory_bytes) /
					   args->guest_page_size;
			vcpu_args->gpa = args->gpa;
		}

		vcpu_args_set(vcpus[i], 1, i);

		pr_debug("Added VCPU %d with test mem gpa [%lx, %lx)\n",
			 i, vcpu_args->gpa, vcpu_args->gpa +
			 (vcpu_args->pages * args->guest_page_size));
	}
}

struct kvm_vm *memstress_create_vm(enum vm_guest_mode mode, int nr_vcpus,
				   uint64_t vcpu_memory_bytes, int slots,
				   enum vm_mem_backing_src_type backing_src,
				   bool partition_vcpu_memory_access)
{
	struct memstress_args *args = &memstress_args;
	struct kvm_vm *vm;
	uint64_t guest_num_pages, slot0_pages = 0;
	uint64_t backing_src_pagesz = get_backing_src_pagesz(backing_src);
	uint64_t region_end_gfn;
	int i;

	pr_info("Testing guest mode: %s\n", vm_guest_mode_string(mode));

	/* By default vCPUs will write to memory. */
	args->write_percent = 100;

	/*
	 * Snapshot the non-huge page size.  This is used by the guest code to
	 * access/dirty pages at the logging granularity.
	 */
	args->guest_page_size = vm_guest_mode_params[mode].page_size;

	guest_num_pages = vm_adjust_num_guest_pages(mode,
				(nr_vcpus * vcpu_memory_bytes) / args->guest_page_size);

	TEST_ASSERT(vcpu_memory_bytes % getpagesize() == 0,
		    "Guest memory size is not host page size aligned.");
	TEST_ASSERT(vcpu_memory_bytes % args->guest_page_size == 0,
		    "Guest memory size is not guest page size aligned.");
	TEST_ASSERT(guest_num_pages % slots == 0,
		    "Guest memory cannot be evenly divided into %d slots.",
		    slots);

	/*
	 * If using nested, allocate extra pages for the nested page tables and
	 * in-memory data structures.
	 */
	if (args->nested)
		slot0_pages += memstress_nested_pages(nr_vcpus);

	/*
	 * Pass guest_num_pages to populate the page tables for test memory.
	 * The memory is also added to memslot 0, but that's a benign side
	 * effect as KVM allows aliasing HVAs in meslots.
	 */
	vm = __vm_create_with_vcpus(VM_SHAPE(mode), nr_vcpus,
				    slot0_pages + guest_num_pages,
				    memstress_guest_code, vcpus);

	args->vm = vm;

	/* Put the test region at the top guest physical memory. */
	region_end_gfn = vm->max_gfn + 1;

#ifdef __x86_64__
	/*
	 * When running vCPUs in L2, restrict the test region to 48 bits to
	 * avoid needing 5-level page tables to identity map L2.
	 */
	if (args->nested)
		region_end_gfn = min(region_end_gfn, (1UL << 48) / args->guest_page_size);
#endif
	/*
	 * If there should be more memory in the guest test region than there
	 * can be pages in the guest, it will definitely cause problems.
	 */
	TEST_ASSERT(guest_num_pages < region_end_gfn,
		    "Requested more guest memory than address space allows.\n"
		    "    guest pages: %" PRIx64 " max gfn: %" PRIx64
		    " nr_vcpus: %d wss: %" PRIx64 "]\n",
		    guest_num_pages, region_end_gfn - 1, nr_vcpus, vcpu_memory_bytes);

	args->gpa = (region_end_gfn - guest_num_pages - 1) * args->guest_page_size;
	args->gpa = align_down(args->gpa, backing_src_pagesz);
#ifdef __s390x__
	/* Align to 1M (segment size) */
	args->gpa = align_down(args->gpa, 1 << 20);
#endif
	args->size = guest_num_pages * args->guest_page_size;
	pr_info("guest physical test memory: [0x%lx, 0x%lx)\n",
		args->gpa, args->gpa + args->size);

	/* Add extra memory slots for testing */
	for (i = 0; i < slots; i++) {
		uint64_t region_pages = guest_num_pages / slots;
		vm_paddr_t region_start = args->gpa + region_pages * args->guest_page_size * i;

		vm_userspace_mem_region_add(vm, backing_src, region_start,
					    MEMSTRESS_MEM_SLOT_INDEX + i,
					    region_pages, 0);
	}

	/* Do mapping for the demand paging memory slot */
	virt_map(vm, guest_test_virt_mem, args->gpa, guest_num_pages);

	memstress_setup_vcpus(vm, nr_vcpus, vcpus, vcpu_memory_bytes,
			      partition_vcpu_memory_access);

	if (args->nested) {
		pr_info("Configuring vCPUs to run in L2 (nested).\n");
		memstress_setup_nested(vm, nr_vcpus, vcpus);
	}

	/* Export the shared variables to the guest. */
	sync_global_to_guest(vm, memstress_args);

	return vm;
}

void memstress_destroy_vm(struct kvm_vm *vm)
{
	kvm_vm_free(vm);
}

void memstress_set_write_percent(struct kvm_vm *vm, uint32_t write_percent)
{
	memstress_args.write_percent = write_percent;
	sync_global_to_guest(vm, memstress_args.write_percent);
}

void memstress_set_random_seed(struct kvm_vm *vm, uint32_t random_seed)
{
	memstress_args.random_seed = random_seed;
	sync_global_to_guest(vm, memstress_args.random_seed);
}

void memstress_set_random_access(struct kvm_vm *vm, bool random_access)
{
	memstress_args.random_access = random_access;
	sync_global_to_guest(vm, memstress_args.random_access);
}

uint64_t __weak memstress_nested_pages(int nr_vcpus)
{
	return 0;
}

void __weak memstress_setup_nested(struct kvm_vm *vm, int nr_vcpus, struct kvm_vcpu **vcpus)
{
	pr_info("%s() not support on this architecture, skipping.\n", __func__);
	exit(KSFT_SKIP);
}

static void *vcpu_thread_main(void *data)
{
	struct vcpu_thread *vcpu = data;
	int vcpu_idx = vcpu->vcpu_idx;

	if (memstress_args.pin_vcpus)
		kvm_pin_this_task_to_pcpu(memstress_args.vcpu_to_pcpu[vcpu_idx]);

	WRITE_ONCE(vcpu->running, true);

	/*
	 * Wait for all vCPU threads to be up and running before calling the test-
	 * provided vCPU thread function. This prevents thread creation (which
	 * requires taking the mmap_sem in write mode) from interfering with the
	 * guest faulting in its memory.
	 */
	while (!READ_ONCE(all_vcpu_threads_running))
		;

	vcpu_thread_fn(&memstress_args.vcpu_args[vcpu_idx]);

	return NULL;
}

void memstress_start_vcpu_threads(int nr_vcpus,
				  void (*vcpu_fn)(struct memstress_vcpu_args *))
{
	int i;

	vcpu_thread_fn = vcpu_fn;
	WRITE_ONCE(all_vcpu_threads_running, false);
	WRITE_ONCE(memstress_args.stop_vcpus, false);

	for (i = 0; i < nr_vcpus; i++) {
		struct vcpu_thread *vcpu = &vcpu_threads[i];

		vcpu->vcpu_idx = i;
		WRITE_ONCE(vcpu->running, false);

		pthread_create(&vcpu->thread, NULL, vcpu_thread_main, vcpu);
	}

	for (i = 0; i < nr_vcpus; i++) {
		while (!READ_ONCE(vcpu_threads[i].running))
			;
	}

	WRITE_ONCE(all_vcpu_threads_running, true);
}

void memstress_join_vcpu_threads(int nr_vcpus)
{
	int i;

	WRITE_ONCE(memstress_args.stop_vcpus, true);

	for (i = 0; i < nr_vcpus; i++)
		pthread_join(vcpu_threads[i].thread, NULL);
}

static void toggle_dirty_logging(struct kvm_vm *vm, int slots, bool enable)
{
	int i;

	for (i = 0; i < slots; i++) {
		int slot = MEMSTRESS_MEM_SLOT_INDEX + i;
		int flags = enable ? KVM_MEM_LOG_DIRTY_PAGES : 0;

		vm_mem_region_set_flags(vm, slot, flags);
	}
}

void memstress_enable_dirty_logging(struct kvm_vm *vm, int slots)
{
	toggle_dirty_logging(vm, slots, true);
}

void memstress_disable_dirty_logging(struct kvm_vm *vm, int slots)
{
	toggle_dirty_logging(vm, slots, false);
}

void memstress_get_dirty_log(struct kvm_vm *vm, unsigned long *bitmaps[], int slots)
{
	int i;

	for (i = 0; i < slots; i++) {
		int slot = MEMSTRESS_MEM_SLOT_INDEX + i;

		kvm_vm_get_dirty_log(vm, slot, bitmaps[i]);
	}
}

void memstress_clear_dirty_log(struct kvm_vm *vm, unsigned long *bitmaps[],
			       int slots, uint64_t pages_per_slot)
{
	int i;

	for (i = 0; i < slots; i++) {
		int slot = MEMSTRESS_MEM_SLOT_INDEX + i;

		kvm_vm_clear_dirty_log(vm, slot, bitmaps[i], 0, pages_per_slot);
	}
}

unsigned long **memstress_alloc_bitmaps(int slots, uint64_t pages_per_slot)
{
	unsigned long **bitmaps;
	int i;

	bitmaps = malloc(slots * sizeof(bitmaps[0]));
	TEST_ASSERT(bitmaps, "Failed to allocate bitmaps array.");

	for (i = 0; i < slots; i++) {
		bitmaps[i] = bitmap_zalloc(pages_per_slot);
		TEST_ASSERT(bitmaps[i], "Failed to allocate slot bitmap.");
	}

	return bitmaps;
}

void memstress_free_bitmaps(unsigned long *bitmaps[], int slots)
{
	int i;

	for (i = 0; i < slots; i++)
		free(bitmaps[i]);

	free(bitmaps);
}
