/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/lib/kvm_util_internal.h
 *
 * Copyright (C) 2018, Google LLC.
 */

#ifndef SELFTEST_KVM_UTIL_INTERNAL_H
#define SELFTEST_KVM_UTIL_INTERNAL_H

#include "sparsebit.h"

struct userspace_mem_region {
	struct kvm_userspace_memory_region region;
	struct sparsebit *unused_phy_pages;
	int fd;
	off_t offset;
	void *host_mem;
	void *mmap_start;
	size_t mmap_size;
	struct list_head list;
};

struct vcpu {
	struct list_head list;
	uint32_t id;
	int fd;
	struct kvm_run *state;
	struct kvm_dirty_gfn *dirty_gfns;
	uint32_t fetch_index;
	uint32_t dirty_gfns_count;
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
	struct list_head vcpus;
	struct list_head userspace_mem_regions;
	struct sparsebit *vpages_valid;
	struct sparsebit *vpages_mapped;
	bool has_irqchip;
	bool pgd_created;
	vm_paddr_t pgd;
	vm_vaddr_t gdt;
	vm_vaddr_t tss;
	vm_vaddr_t idt;
	vm_vaddr_t handlers;
	uint32_t dirty_ring_size;
};

struct vcpu *vcpu_find(struct kvm_vm *vm, uint32_t vcpuid);

/*
 * Virtual Translation Tables Dump
 *
 * Input Args:
 *   stream - Output FILE stream
 *   vm     - Virtual Machine
 *   indent - Left margin indent amount
 *
 * Output Args: None
 *
 * Return: None
 *
 * Dumps to the FILE stream given by @stream, the contents of all the
 * virtual translation tables for the VM given by @vm.
 */
void virt_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent);

/*
 * Register Dump
 *
 * Input Args:
 *   stream - Output FILE stream
 *   regs   - Registers
 *   indent - Left margin indent amount
 *
 * Output Args: None
 *
 * Return: None
 *
 * Dumps the state of the registers given by @regs, to the FILE stream
 * given by @stream.
 */
void regs_dump(FILE *stream, struct kvm_regs *regs, uint8_t indent);

/*
 * System Register Dump
 *
 * Input Args:
 *   stream - Output FILE stream
 *   sregs  - System registers
 *   indent - Left margin indent amount
 *
 * Output Args: None
 *
 * Return: None
 *
 * Dumps the state of the system registers given by @sregs, to the FILE stream
 * given by @stream.
 */
void sregs_dump(FILE *stream, struct kvm_sregs *sregs, uint8_t indent);

struct userspace_mem_region *
memslot2region(struct kvm_vm *vm, uint32_t memslot);

#endif /* SELFTEST_KVM_UTIL_INTERNAL_H */
