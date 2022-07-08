// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020, Google LLC.
 */
#include <inttypes.h>

#include "kvm_util.h"
#include "perf_test_util.h"
#include "processor.h"

struct perf_test_args perf_test_args;

/*
 * Guest virtual memory offset of the testing memory slot.
 * Must not conflict with identity mapped test code.
 */
static uint64_t guest_test_virt_mem = DEFAULT_GUEST_TEST_MEM;

struct vcpu_thread {
	/* The id of the vCPU. */
	int vcpu_id;

	/* The pthread backing the vCPU. */
	pthread_t thread;

	/* Set to true once the vCPU thread is up and running. */
	bool running;
};

/* The vCPU threads involved in this test. */
static struct vcpu_thread vcpu_threads[KVM_MAX_VCPUS];

/* The function run by each vCPU thread, as provided by the test. */
static void (*vcpu_thread_fn)(struct perf_test_vcpu_args *);

/* Set to true once all vCPU threads are up and running. */
static bool all_vcpu_threads_running;

/*
 * Continuously write to the first 8 bytes of each page in the
 * specified region.
 */
void perf_test_guest_code(uint32_t vcpu_id)
{
	struct perf_test_args *pta = &perf_test_args;
	struct perf_test_vcpu_args *vcpu_args = &pta->vcpu_args[vcpu_id];
	uint64_t gva;
	uint64_t pages;
	int i;

	/* Make sure vCPU args data structure is not corrupt. */
	GUEST_ASSERT(vcpu_args->vcpu_id == vcpu_id);

	gva = vcpu_args->gva;
	pages = vcpu_args->pages;

	while (true) {
		for (i = 0; i < pages; i++) {
			uint64_t addr = gva + (i * pta->guest_page_size);

			if (i % pta->wr_fract == 0)
				*(uint64_t *)addr = 0x0123456789ABCDEF;
			else
				READ_ONCE(*(uint64_t *)addr);
		}

		GUEST_SYNC(1);
	}
}

void perf_test_setup_vcpus(struct kvm_vm *vm, int vcpus,
			   uint64_t vcpu_memory_bytes,
			   bool partition_vcpu_memory_access)
{
	struct perf_test_args *pta = &perf_test_args;
	struct perf_test_vcpu_args *vcpu_args;
	int vcpu_id;

	for (vcpu_id = 0; vcpu_id < vcpus; vcpu_id++) {
		vcpu_args = &pta->vcpu_args[vcpu_id];

		vcpu_args->vcpu_id = vcpu_id;
		if (partition_vcpu_memory_access) {
			vcpu_args->gva = guest_test_virt_mem +
					 (vcpu_id * vcpu_memory_bytes);
			vcpu_args->pages = vcpu_memory_bytes /
					   pta->guest_page_size;
			vcpu_args->gpa = pta->gpa + (vcpu_id * vcpu_memory_bytes);
		} else {
			vcpu_args->gva = guest_test_virt_mem;
			vcpu_args->pages = (vcpus * vcpu_memory_bytes) /
					   pta->guest_page_size;
			vcpu_args->gpa = pta->gpa;
		}

		vcpu_args_set(vm, vcpu_id, 1, vcpu_id);

		pr_debug("Added VCPU %d with test mem gpa [%lx, %lx)\n",
			 vcpu_id, vcpu_args->gpa, vcpu_args->gpa +
			 (vcpu_args->pages * pta->guest_page_size));
	}
}

struct kvm_vm *perf_test_create_vm(enum vm_guest_mode mode, int vcpus,
				   uint64_t vcpu_memory_bytes, int slots,
				   enum vm_mem_backing_src_type backing_src,
				   bool partition_vcpu_memory_access)
{
	struct perf_test_args *pta = &perf_test_args;
	struct kvm_vm *vm;
	uint64_t guest_num_pages, slot0_pages = DEFAULT_GUEST_PHY_PAGES;
	uint64_t backing_src_pagesz = get_backing_src_pagesz(backing_src);
	uint64_t region_end_gfn;
	int i;

	pr_info("Testing guest mode: %s\n", vm_guest_mode_string(mode));

	/* By default vCPUs will write to memory. */
	pta->wr_fract = 1;

	/*
	 * Snapshot the non-huge page size.  This is used by the guest code to
	 * access/dirty pages at the logging granularity.
	 */
	pta->guest_page_size = vm_guest_mode_params[mode].page_size;

	guest_num_pages = vm_adjust_num_guest_pages(mode,
				(vcpus * vcpu_memory_bytes) / pta->guest_page_size);

	TEST_ASSERT(vcpu_memory_bytes % getpagesize() == 0,
		    "Guest memory size is not host page size aligned.");
	TEST_ASSERT(vcpu_memory_bytes % pta->guest_page_size == 0,
		    "Guest memory size is not guest page size aligned.");
	TEST_ASSERT(guest_num_pages % slots == 0,
		    "Guest memory cannot be evenly divided into %d slots.",
		    slots);

	/*
	 * If using nested, allocate extra pages for the nested page tables and
	 * in-memory data structures.
	 */
	if (pta->nested)
		slot0_pages += perf_test_nested_pages(vcpus);

	/*
	 * Pass guest_num_pages to populate the page tables for test memory.
	 * The memory is also added to memslot 0, but that's a benign side
	 * effect as KVM allows aliasing HVAs in meslots.
	 */
	vm = vm_create_with_vcpus(mode, vcpus, slot0_pages, guest_num_pages, 0,
				  perf_test_guest_code, NULL);

	pta->vm = vm;

	/* Put the test region at the top guest physical memory. */
	region_end_gfn = vm_get_max_gfn(vm) + 1;

#ifdef __x86_64__
	/*
	 * When running vCPUs in L2, restrict the test region to 48 bits to
	 * avoid needing 5-level page tables to identity map L2.
	 */
	if (pta->nested)
		region_end_gfn = min(region_end_gfn, (1UL << 48) / pta->guest_page_size);
#endif
	/*
	 * If there should be more memory in the guest test region than there
	 * can be pages in the guest, it will definitely cause problems.
	 */
	TEST_ASSERT(guest_num_pages < region_end_gfn,
		    "Requested more guest memory than address space allows.\n"
		    "    guest pages: %" PRIx64 " max gfn: %" PRIx64
		    " vcpus: %d wss: %" PRIx64 "]\n",
		    guest_num_pages, region_end_gfn - 1, vcpus,
		    vcpu_memory_bytes);

	pta->gpa = (region_end_gfn - guest_num_pages) * pta->guest_page_size;
	pta->gpa = align_down(pta->gpa, backing_src_pagesz);
#ifdef __s390x__
	/* Align to 1M (segment size) */
	pta->gpa = align_down(pta->gpa, 1 << 20);
#endif
	pta->size = guest_num_pages * pta->guest_page_size;
	pr_info("guest physical test memory: [0x%lx, 0x%lx)\n",
		pta->gpa, pta->gpa + pta->size);

	/* Add extra memory slots for testing */
	for (i = 0; i < slots; i++) {
		uint64_t region_pages = guest_num_pages / slots;
		vm_paddr_t region_start = pta->gpa + region_pages * pta->guest_page_size * i;

		vm_userspace_mem_region_add(vm, backing_src, region_start,
					    PERF_TEST_MEM_SLOT_INDEX + i,
					    region_pages, 0);
	}

	/* Do mapping for the demand paging memory slot */
	virt_map(vm, guest_test_virt_mem, pta->gpa, guest_num_pages);

	perf_test_setup_vcpus(vm, vcpus, vcpu_memory_bytes, partition_vcpu_memory_access);

	if (pta->nested) {
		pr_info("Configuring vCPUs to run in L2 (nested).\n");
		perf_test_setup_nested(vm, vcpus);
	}

	ucall_init(vm, NULL);

	/* Export the shared variables to the guest. */
	sync_global_to_guest(vm, perf_test_args);

	return vm;
}

void perf_test_destroy_vm(struct kvm_vm *vm)
{
	ucall_uninit(vm);
	kvm_vm_free(vm);
}

void perf_test_set_wr_fract(struct kvm_vm *vm, int wr_fract)
{
	perf_test_args.wr_fract = wr_fract;
	sync_global_to_guest(vm, perf_test_args);
}

uint64_t __weak perf_test_nested_pages(int nr_vcpus)
{
	return 0;
}

void __weak perf_test_setup_nested(struct kvm_vm *vm, int nr_vcpus)
{
	pr_info("%s() not support on this architecture, skipping.\n", __func__);
	exit(KSFT_SKIP);
}

static void *vcpu_thread_main(void *data)
{
	struct vcpu_thread *vcpu = data;

	WRITE_ONCE(vcpu->running, true);

	/*
	 * Wait for all vCPU threads to be up and running before calling the test-
	 * provided vCPU thread function. This prevents thread creation (which
	 * requires taking the mmap_sem in write mode) from interfering with the
	 * guest faulting in its memory.
	 */
	while (!READ_ONCE(all_vcpu_threads_running))
		;

	vcpu_thread_fn(&perf_test_args.vcpu_args[vcpu->vcpu_id]);

	return NULL;
}

void perf_test_start_vcpu_threads(int vcpus, void (*vcpu_fn)(struct perf_test_vcpu_args *))
{
	int vcpu_id;

	vcpu_thread_fn = vcpu_fn;
	WRITE_ONCE(all_vcpu_threads_running, false);

	for (vcpu_id = 0; vcpu_id < vcpus; vcpu_id++) {
		struct vcpu_thread *vcpu = &vcpu_threads[vcpu_id];

		vcpu->vcpu_id = vcpu_id;
		WRITE_ONCE(vcpu->running, false);

		pthread_create(&vcpu->thread, NULL, vcpu_thread_main, vcpu);
	}

	for (vcpu_id = 0; vcpu_id < vcpus; vcpu_id++) {
		while (!READ_ONCE(vcpu_threads[vcpu_id].running))
			;
	}

	WRITE_ONCE(all_vcpu_threads_running, true);
}

void perf_test_join_vcpu_threads(int vcpus)
{
	int vcpu_id;

	for (vcpu_id = 0; vcpu_id < vcpus; vcpu_id++)
		pthread_join(vcpu_threads[vcpu_id].thread, NULL);
}
