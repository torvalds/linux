// SPDX-License-Identifier: GPL-2.0
/*
 * tools/testing/selftests/kvm/include/perf_test_util.h
 *
 * Copyright (C) 2020, Google LLC.
 */

#ifndef SELFTEST_KVM_PERF_TEST_UTIL_H
#define SELFTEST_KVM_PERF_TEST_UTIL_H

#include "kvm_util.h"
#include "processor.h"

#define MAX_VCPUS 512

#define PAGE_SHIFT_4K  12
#define PTES_PER_4K_PT 512

#define TEST_MEM_SLOT_INDEX		1

/* Default guest test virtual memory offset */
#define DEFAULT_GUEST_TEST_MEM		0xc0000000

#define DEFAULT_PER_VCPU_MEM_SIZE	(1 << 30) /* 1G */

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
static uint64_t guest_percpu_mem_size = DEFAULT_PER_VCPU_MEM_SIZE;

/* Number of VCPUs for the test */
static int nr_vcpus = 1;

struct vcpu_args {
	uint64_t gva;
	uint64_t pages;

	/* Only used by the host userspace part of the vCPU thread */
	int vcpu_id;
};

struct perf_test_args {
	struct kvm_vm *vm;
	uint64_t host_page_size;
	uint64_t guest_page_size;
	int wr_fract;

	struct vcpu_args vcpu_args[MAX_VCPUS];
};

static struct perf_test_args perf_test_args;

/*
 * Continuously write to the first 8 bytes of each page in the
 * specified region.
 */
static void guest_code(uint32_t vcpu_id)
{
	struct vcpu_args *vcpu_args = &perf_test_args.vcpu_args[vcpu_id];
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

static struct kvm_vm *create_vm(enum vm_guest_mode mode, int vcpus,
				uint64_t vcpu_memory_bytes)
{
	struct kvm_vm *vm;
	uint64_t pages = DEFAULT_GUEST_PHY_PAGES;
	uint64_t guest_num_pages;

	/* Account for a few pages per-vCPU for stacks */
	pages += DEFAULT_STACK_PGS * vcpus;

	/*
	 * Reserve twice the ammount of memory needed to map the test region and
	 * the page table / stacks region, at 4k, for page tables. Do the
	 * calculation with 4K page size: the smallest of all archs. (e.g., 64K
	 * page size guest will need even less memory for page tables).
	 */
	pages += (2 * pages) / PTES_PER_4K_PT;
	pages += ((2 * vcpus * vcpu_memory_bytes) >> PAGE_SHIFT_4K) /
		 PTES_PER_4K_PT;
	pages = vm_adjust_num_guest_pages(mode, pages);

	pr_info("Testing guest mode: %s\n", vm_guest_mode_string(mode));

	vm = vm_create(mode, pages, O_RDWR);
	kvm_vm_elf_load(vm, program_invocation_name, 0, 0);
#ifdef __x86_64__
	vm_create_irqchip(vm);
#endif

	perf_test_args.vm = vm;
	perf_test_args.guest_page_size = vm_get_page_size(vm);
	perf_test_args.host_page_size = getpagesize();

	TEST_ASSERT(vcpu_memory_bytes % perf_test_args.guest_page_size == 0,
		    "Guest memory size is not guest page size aligned.");

	guest_num_pages = (vcpus * vcpu_memory_bytes) /
			  perf_test_args.guest_page_size;
	guest_num_pages = vm_adjust_num_guest_pages(mode, guest_num_pages);

	/*
	 * If there should be more memory in the guest test region than there
	 * can be pages in the guest, it will definitely cause problems.
	 */
	TEST_ASSERT(guest_num_pages < vm_get_max_gfn(vm),
		    "Requested more guest memory than address space allows.\n"
		    "    guest pages: %lx max gfn: %x vcpus: %d wss: %lx]\n",
		    guest_num_pages, vm_get_max_gfn(vm), vcpus,
		    vcpu_memory_bytes);

	TEST_ASSERT(vcpu_memory_bytes % perf_test_args.host_page_size == 0,
		    "Guest memory size is not host page size aligned.");

	guest_test_phys_mem = (vm_get_max_gfn(vm) - guest_num_pages) *
			      perf_test_args.guest_page_size;
	guest_test_phys_mem &= ~(perf_test_args.host_page_size - 1);

#ifdef __s390x__
	/* Align to 1M (segment size) */
	guest_test_phys_mem &= ~((1 << 20) - 1);
#endif

	pr_info("guest physical test memory offset: 0x%lx\n", guest_test_phys_mem);

	/* Add an extra memory slot for testing */
	vm_userspace_mem_region_add(vm, VM_MEM_SRC_ANONYMOUS,
				    guest_test_phys_mem,
				    TEST_MEM_SLOT_INDEX,
				    guest_num_pages, 0);

	/* Do mapping for the demand paging memory slot */
	virt_map(vm, guest_test_virt_mem, guest_test_phys_mem, guest_num_pages, 0);

	ucall_init(vm, NULL);

	return vm;
}

static void add_vcpus(struct kvm_vm *vm, int vcpus, uint64_t vcpu_memory_bytes)
{
	vm_paddr_t vcpu_gpa;
	struct vcpu_args *vcpu_args;
	int vcpu_id;

	for (vcpu_id = 0; vcpu_id < vcpus; vcpu_id++) {
		vcpu_args = &perf_test_args.vcpu_args[vcpu_id];

		vm_vcpu_add_default(vm, vcpu_id, guest_code);

#ifdef __x86_64__
		vcpu_set_cpuid(vm, vcpu_id, kvm_get_supported_cpuid());
#endif

		vcpu_args->vcpu_id = vcpu_id;
		vcpu_args->gva = guest_test_virt_mem +
				 (vcpu_id * vcpu_memory_bytes);
		vcpu_args->pages = vcpu_memory_bytes /
				   perf_test_args.guest_page_size;

		vcpu_gpa = guest_test_phys_mem + (vcpu_id * vcpu_memory_bytes);
		pr_debug("Added VCPU %d with test mem gpa [%lx, %lx)\n",
			 vcpu_id, vcpu_gpa, vcpu_gpa + vcpu_memory_bytes);
	}
}

#endif /* SELFTEST_KVM_PERF_TEST_UTIL_H */
