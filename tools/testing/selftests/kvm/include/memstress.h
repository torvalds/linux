// SPDX-License-Identifier: GPL-2.0
/*
 * tools/testing/selftests/kvm/include/memstress.h
 *
 * Copyright (C) 2020, Google LLC.
 */

#ifndef SELFTEST_KVM_MEMSTRESS_H
#define SELFTEST_KVM_MEMSTRESS_H

#include <pthread.h>

#include "kvm_util.h"

/* Default guest test virtual memory offset */
#define DEFAULT_GUEST_TEST_MEM		0xc0000000

#define DEFAULT_PER_VCPU_MEM_SIZE	(1 << 30) /* 1G */

#define MEMSTRESS_MEM_SLOT_INDEX	1

struct memstress_vcpu_args {
	gpa_t gpa;
	gva_t gva;
	u64 pages;

	/* Only used by the host userspace part of the vCPU thread */
	struct kvm_vcpu *vcpu;
	int vcpu_idx;
};

struct memstress_args {
	struct kvm_vm *vm;
	/* The starting address and size of the guest test region. */
	gpa_t gpa;
	u64 size;
	u64 guest_page_size;
	u32 random_seed;
	u32 write_percent;

	/* Run vCPUs in L2 instead of L1, if the architecture supports it. */
	bool nested;
	/* Randomize which pages are accessed by the guest. */
	bool random_access;
	/* True if all vCPUs are pinned to pCPUs */
	bool pin_vcpus;
	/* The vCPU=>pCPU pinning map. Only valid if pin_vcpus is true. */
	u32 vcpu_to_pcpu[KVM_MAX_VCPUS];

 	/* Test is done, stop running vCPUs. */
 	bool stop_vcpus;

	struct memstress_vcpu_args vcpu_args[KVM_MAX_VCPUS];
};

extern struct memstress_args memstress_args;

struct kvm_vm *memstress_create_vm(enum vm_guest_mode mode, int nr_vcpus,
				   u64 vcpu_memory_bytes, int slots,
				   enum vm_mem_backing_src_type backing_src,
				   bool partition_vcpu_memory_access);
void memstress_destroy_vm(struct kvm_vm *vm);

void memstress_set_write_percent(struct kvm_vm *vm, u32 write_percent);
void memstress_set_random_access(struct kvm_vm *vm, bool random_access);

void memstress_start_vcpu_threads(int vcpus, void (*vcpu_fn)(struct memstress_vcpu_args *));
void memstress_join_vcpu_threads(int vcpus);
void memstress_guest_code(u32 vcpu_id);

u64 memstress_nested_pages(int nr_vcpus);
void memstress_setup_nested(struct kvm_vm *vm, int nr_vcpus, struct kvm_vcpu *vcpus[]);

void memstress_enable_dirty_logging(struct kvm_vm *vm, int slots);
void memstress_disable_dirty_logging(struct kvm_vm *vm, int slots);
void memstress_get_dirty_log(struct kvm_vm *vm, unsigned long *bitmaps[], int slots);
void memstress_clear_dirty_log(struct kvm_vm *vm, unsigned long *bitmaps[],
			       int slots, u64 pages_per_slot);
unsigned long **memstress_alloc_bitmaps(int slots, u64 pages_per_slot);
void memstress_free_bitmaps(unsigned long *bitmaps[], int slots);

#endif /* SELFTEST_KVM_MEMSTRESS_H */
