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
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

#define VCPU_ID				1

/* The memory slot index demand page */
#define TEST_MEM_SLOT_INDEX		1

/* Default guest test virtual memory offset */
#define DEFAULT_GUEST_TEST_MEM		0xc0000000

/*
 * Guest/Host shared variables. Ensure addr_gva2hva() and/or
 * sync_global_to/from_guest() are used when accessing from
 * the host. READ/WRITE_ONCE() should also be used with anything
 * that may change.
 */
static uint64_t host_page_size;
static uint64_t guest_page_size;
static uint64_t guest_num_pages;

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

#define GUEST_MEM_SHIFT 30 /* 1G */
#define PAGE_SHIFT_4K  12

static void run_test(enum vm_guest_mode mode)
{
	pthread_t vcpu_thread;
	struct kvm_vm *vm;

	/*
	 * We reserve page table for 2 times of extra dirty mem which
	 * will definitely cover the original (1G+) test range.  Here
	 * we do the calculation with 4K page size which is the
	 * smallest so the page number will be enough for all archs
	 * (e.g., 64K page size guest will need even less memory for
	 * page tables).
	 */
	vm = create_vm(mode, VCPU_ID,
		       2ul << (GUEST_MEM_SHIFT - PAGE_SHIFT_4K),
		       guest_code);

	guest_page_size = vm_get_page_size(vm);
	/*
	 * A little more than 1G of guest page sized pages.  Cover the
	 * case where the size is not aligned to 64 pages.
	 */
	guest_num_pages = (1ul << (GUEST_MEM_SHIFT -
				   vm_get_page_shift(vm))) + 16;
#ifdef __s390x__
	/* Round up to multiple of 1M (segment size) */
	guest_num_pages = (guest_num_pages + 0xff) & ~0xffUL;
#endif

	host_page_size = getpagesize();
	host_num_pages = (guest_num_pages * guest_page_size) / host_page_size +
			 !!((guest_num_pages * guest_page_size) %
			    host_page_size);

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

	ucall_uninit(vm);
	kvm_vm_free(vm);
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
	printf("usage: %s [-h] [-m mode]\n", name);
	printf(" -m: specify the guest mode ID to test\n"
	       "     (default: test all supported modes)\n"
	       "     This option may be used multiple times.\n"
	       "     Guest mode IDs:\n");
	for (i = 0; i < NUM_VM_MODES; ++i) {
		printf("         %d:    %s%s\n", i, vm_guest_mode_string(i),
		       guest_modes[i].supported ? " (supported)" : "");
	}
	puts("");
	exit(0);
}

int main(int argc, char *argv[])
{
	bool mode_selected = false;
	unsigned int mode;
	int opt, i;

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

	while ((opt = getopt(argc, argv, "hm:")) != -1) {
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
		run_test(i);
	}

	return 0;
}
