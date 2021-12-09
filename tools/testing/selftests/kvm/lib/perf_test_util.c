// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020, Google LLC.
 */
#include <inttypes.h>

#include "kvm_util.h"
#include "perf_test_util.h"
#include "processor.h"

struct perf_test_args perf_test_args;

uint64_t guest_test_phys_mem;

/*
 * Guest virtual memory offset of the testing memory slot.
 * Must not conflict with identity mapped test code.
 */
static uint64_t guest_test_virt_mem = DEFAULT_GUEST_TEST_MEM;

/*
 * Continuously write to the first 8 bytes of each page in the
 * specified region.
 */
static void guest_code(uint32_t vcpu_id)
{
	struct perf_test_vcpu_args *vcpu_args = &perf_test_args.vcpu_args[vcpu_id];
	uint64_t gva;
	uint64_t pages;
	int i;

	/* Make sure vCPU args data structure is not corrupt. */
	GUEST_ASSERT(vcpu_args->vcpu_id == vcpu_id);

	gva = vcpu_args->gva;
	pages = vcpu_args->pages;

	while (true) {
		for (i = 0; i < pages; i++) {
			uint64_t addr = gva + (i * perf_test_args.guest_page_size);

			if (i % perf_test_args.wr_fract == 0)
				*(uint64_t *)addr = 0x0123456789ABCDEF;
			else
				READ_ONCE(*(uint64_t *)addr);
		}

		GUEST_SYNC(1);
	}
}

struct kvm_vm *perf_test_create_vm(enum vm_guest_mode mode, int vcpus,
				   uint64_t vcpu_memory_bytes, int slots,
				   enum vm_mem_backing_src_type backing_src)
{
	struct kvm_vm *vm;
	uint64_t guest_num_pages;
	int i;

	pr_info("Testing guest mode: %s\n", vm_guest_mode_string(mode));

	perf_test_args.host_page_size = getpagesize();
	perf_test_args.guest_page_size = vm_guest_mode_params[mode].page_size;

	guest_num_pages = vm_adjust_num_guest_pages(mode,
				(vcpus * vcpu_memory_bytes) / perf_test_args.guest_page_size);

	TEST_ASSERT(vcpu_memory_bytes % perf_test_args.host_page_size == 0,
		    "Guest memory size is not host page size aligned.");
	TEST_ASSERT(vcpu_memory_bytes % perf_test_args.guest_page_size == 0,
		    "Guest memory size is not guest page size aligned.");
	TEST_ASSERT(guest_num_pages % slots == 0,
		    "Guest memory cannot be evenly divided into %d slots.",
		    slots);

	vm = vm_create_with_vcpus(mode, vcpus, DEFAULT_GUEST_PHY_PAGES,
				  (vcpus * vcpu_memory_bytes) / perf_test_args.guest_page_size,
				  0, guest_code, NULL);

	perf_test_args.vm = vm;

	/*
	 * If there should be more memory in the guest test region than there
	 * can be pages in the guest, it will definitely cause problems.
	 */
	TEST_ASSERT(guest_num_pages < vm_get_max_gfn(vm),
		    "Requested more guest memory than address space allows.\n"
		    "    guest pages: %" PRIx64 " max gfn: %" PRIx64
		    " vcpus: %d wss: %" PRIx64 "]\n",
		    guest_num_pages, vm_get_max_gfn(vm), vcpus,
		    vcpu_memory_bytes);

	guest_test_phys_mem = (vm_get_max_gfn(vm) - guest_num_pages) *
			      perf_test_args.guest_page_size;
	guest_test_phys_mem &= ~(perf_test_args.host_page_size - 1);
#ifdef __s390x__
	/* Align to 1M (segment size) */
	guest_test_phys_mem &= ~((1 << 20) - 1);
#endif
	pr_info("guest physical test memory offset: 0x%lx\n", guest_test_phys_mem);

	/* Add extra memory slots for testing */
	for (i = 0; i < slots; i++) {
		uint64_t region_pages = guest_num_pages / slots;
		vm_paddr_t region_start = guest_test_phys_mem +
			region_pages * perf_test_args.guest_page_size * i;

		vm_userspace_mem_region_add(vm, backing_src, region_start,
					    PERF_TEST_MEM_SLOT_INDEX + i,
					    region_pages, 0);
	}

	/* Do mapping for the demand paging memory slot */
	virt_map(vm, guest_test_virt_mem, guest_test_phys_mem, guest_num_pages);

	ucall_init(vm, NULL);

	return vm;
}

void perf_test_destroy_vm(struct kvm_vm *vm)
{
	ucall_uninit(vm);
	kvm_vm_free(vm);
}

void perf_test_setup_vcpus(struct kvm_vm *vm, int vcpus,
			   uint64_t vcpu_memory_bytes,
			   bool partition_vcpu_memory_access)
{
	vm_paddr_t vcpu_gpa;
	struct perf_test_vcpu_args *vcpu_args;
	int vcpu_id;

	for (vcpu_id = 0; vcpu_id < vcpus; vcpu_id++) {
		vcpu_args = &perf_test_args.vcpu_args[vcpu_id];

		vcpu_args->vcpu_id = vcpu_id;
		if (partition_vcpu_memory_access) {
			vcpu_args->gva = guest_test_virt_mem +
					 (vcpu_id * vcpu_memory_bytes);
			vcpu_args->pages = vcpu_memory_bytes /
					   perf_test_args.guest_page_size;
			vcpu_gpa = guest_test_phys_mem +
				   (vcpu_id * vcpu_memory_bytes);
		} else {
			vcpu_args->gva = guest_test_virt_mem;
			vcpu_args->pages = (vcpus * vcpu_memory_bytes) /
					   perf_test_args.guest_page_size;
			vcpu_gpa = guest_test_phys_mem;
		}

		vcpu_args_set(vm, vcpu_id, 1, vcpu_id);

		pr_debug("Added VCPU %d with test mem gpa [%lx, %lx)\n",
			 vcpu_id, vcpu_gpa, vcpu_gpa +
			 (vcpu_args->pages * perf_test_args.guest_page_size));
	}
}
