/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/lib/kvm_util_internal.h
 *
 * Copyright (C) 2018, Google LLC.
 */

#ifndef SELFTEST_KVM_UTIL_INTERNAL_H
#define SELFTEST_KVM_UTIL_INTERNAL_H

#include "sparsebit.h"

#define KVM_DEV_PATH		"/dev/kvm"

#ifndef BITS_PER_BYTE
#define BITS_PER_BYTE		8
#endif

#ifndef BITS_PER_LONG
#define BITS_PER_LONG		(BITS_PER_BYTE * sizeof(long))
#endif

#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#define BITS_TO_LONGS(nr)	DIV_ROUND_UP(nr, BITS_PER_LONG)

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
	unsigned long type;
	int kvm_fd;
	int fd;
	unsigned int pgtable_levels;
	unsigned int page_size;
	unsigned int page_shift;
	unsigned int pa_bits;
	unsigned int va_bits;
	uint64_t max_gfn;
	struct vcpu *vcpu_head;
	struct userspace_mem_region *userspace_mem_region_head;
	struct sparsebit *vpages_valid;
	struct sparsebit *vpages_mapped;
	bool has_irqchip;
	bool pgd_created;
	vm_paddr_t pgd;
	vm_vaddr_t gdt;
	vm_vaddr_t tss;
};

struct vcpu *vcpu_find(struct kvm_vm *vm, uint32_t vcpuid);
void vcpu_setup(struct kvm_vm *vm, int vcpuid, int pgd_memslot,
		int gdt_memslot);
void virt_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent);
void regs_dump(FILE *stream, struct kvm_regs *regs, uint8_t indent);
void sregs_dump(FILE *stream, struct kvm_sregs *sregs, uint8_t indent);

#endif /* SELFTEST_KVM_UTIL_INTERNAL_H */
