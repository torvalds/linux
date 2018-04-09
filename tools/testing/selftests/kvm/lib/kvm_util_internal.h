/*
 * tools/testing/selftests/kvm/lib/kvm_util.c
 *
 * Copyright (C) 2018, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */

#ifndef KVM_UTIL_INTERNAL_H
#define KVM_UTIL_INTERNAL_H 1

#include "sparsebit.h"

#ifndef BITS_PER_BYTE
#define BITS_PER_BYTE           8
#endif

#ifndef BITS_PER_LONG
#define BITS_PER_LONG (BITS_PER_BYTE * sizeof(long))
#endif

#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_LONG)

/* Concrete definition of struct kvm_vm. */
struct userspace_mem_region {
	struct userspace_mem_region *next, *prev;
	struct kvm_userspace_memory_region region;
	struct sparsebit *unused_phy_pages;
	int fd;
	off_t offset;
	void *host_mem;
	void *mmap_start;
	size_t mmap_size;
};

struct vcpu {
	struct vcpu *next, *prev;
	uint32_t id;
	int fd;
	struct kvm_run *state;
};

struct kvm_vm {
	int mode;
	int fd;
	unsigned int page_size;
	unsigned int page_shift;
	uint64_t max_gfn;
	struct vcpu *vcpu_head;
	struct userspace_mem_region *userspace_mem_region_head;
	struct sparsebit *vpages_valid;
	struct sparsebit *vpages_mapped;
	bool pgd_created;
	vm_paddr_t pgd;
};

struct vcpu *vcpu_find(struct kvm_vm *vm,
	uint32_t vcpuid);
void vcpu_setup(struct kvm_vm *vm, int vcpuid);
void virt_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent);
void regs_dump(FILE *stream, struct kvm_regs *regs,
	uint8_t indent);
void sregs_dump(FILE *stream, struct kvm_sregs *sregs,
	uint8_t indent);

#endif
