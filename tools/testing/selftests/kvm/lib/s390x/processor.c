// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM selftest s390x library code - CPU-related functions (page tables...)
 *
 * Copyright (C) 2019, Red Hat, Inc.
 */

#define _GNU_SOURCE /* for program_invocation_name */

#include "processor.h"
#include "kvm_util.h"
#include "../kvm_util_internal.h"

#define KVM_GUEST_PAGE_TABLE_MIN_PADDR		0x180000

#define PAGES_PER_REGION 4

void virt_pgd_alloc(struct kvm_vm *vm, uint32_t memslot)
{
	vm_paddr_t paddr;

	TEST_ASSERT(vm->page_size == 4096, "Unsupported page size: 0x%x",
		    vm->page_size);

	if (vm->pgd_created)
		return;

	paddr = vm_phy_pages_alloc(vm, PAGES_PER_REGION,
				   KVM_GUEST_PAGE_TABLE_MIN_PADDR, memslot);
	memset(addr_gpa2hva(vm, paddr), 0xff, PAGES_PER_REGION * vm->page_size);

	vm->pgd = paddr;
	vm->pgd_created = true;
}

/*
 * Allocate 4 pages for a region/segment table (ri < 4), or one page for
 * a page table (ri == 4). Returns a suitable region/segment table entry
 * which points to the freshly allocated pages.
 */
static uint64_t virt_alloc_region(struct kvm_vm *vm, int ri, uint32_t memslot)
{
	uint64_t taddr;

	taddr = vm_phy_pages_alloc(vm,  ri < 4 ? PAGES_PER_REGION : 1,
				   KVM_GUEST_PAGE_TABLE_MIN_PADDR, memslot);
	memset(addr_gpa2hva(vm, taddr), 0xff, PAGES_PER_REGION * vm->page_size);

	return (taddr & REGION_ENTRY_ORIGIN)
		| (((4 - ri) << 2) & REGION_ENTRY_TYPE)
		| ((ri < 4 ? (PAGES_PER_REGION - 1) : 0) & REGION_ENTRY_LENGTH);
}

void virt_pg_map(struct kvm_vm *vm, uint64_t gva, uint64_t gpa,
		 uint32_t memslot)
{
	int ri, idx;
	uint64_t *entry;

	TEST_ASSERT((gva % vm->page_size) == 0,
		"Virtual address not on page boundary,\n"
		"  vaddr: 0x%lx vm->page_size: 0x%x",
		gva, vm->page_size);
	TEST_ASSERT(sparsebit_is_set(vm->vpages_valid,
		(gva >> vm->page_shift)),
		"Invalid virtual address, vaddr: 0x%lx",
		gva);
	TEST_ASSERT((gpa % vm->page_size) == 0,
		"Physical address not on page boundary,\n"
		"  paddr: 0x%lx vm->page_size: 0x%x",
		gva, vm->page_size);
	TEST_ASSERT((gpa >> vm->page_shift) <= vm->max_gfn,
		"Physical address beyond beyond maximum supported,\n"
		"  paddr: 0x%lx vm->max_gfn: 0x%lx vm->page_size: 0x%x",
		gva, vm->max_gfn, vm->page_size);

	/* Walk through region and segment tables */
	entry = addr_gpa2hva(vm, vm->pgd);
	for (ri = 1; ri <= 4; ri++) {
		idx = (gva >> (64 - 11 * ri)) & 0x7ffu;
		if (entry[idx] & REGION_ENTRY_INVALID)
			entry[idx] = virt_alloc_region(vm, ri, memslot);
		entry = addr_gpa2hva(vm, entry[idx] & REGION_ENTRY_ORIGIN);
	}

	/* Fill in page table entry */
	idx = (gva >> 12) & 0x0ffu;		/* page index */
	if (!(entry[idx] & PAGE_INVALID))
		fprintf(stderr,
			"WARNING: PTE for gpa=0x%"PRIx64" already set!\n", gpa);
	entry[idx] = gpa;
}

vm_paddr_t addr_gva2gpa(struct kvm_vm *vm, vm_vaddr_t gva)
{
	int ri, idx;
	uint64_t *entry;

	TEST_ASSERT(vm->page_size == 4096, "Unsupported page size: 0x%x",
		    vm->page_size);

	entry = addr_gpa2hva(vm, vm->pgd);
	for (ri = 1; ri <= 4; ri++) {
		idx = (gva >> (64 - 11 * ri)) & 0x7ffu;
		TEST_ASSERT(!(entry[idx] & REGION_ENTRY_INVALID),
			    "No region mapping for vm virtual address 0x%lx",
			    gva);
		entry = addr_gpa2hva(vm, entry[idx] & REGION_ENTRY_ORIGIN);
	}

	idx = (gva >> 12) & 0x0ffu;		/* page index */

	TEST_ASSERT(!(entry[idx] & PAGE_INVALID),
		    "No page mapping for vm virtual address 0x%lx", gva);

	return (entry[idx] & ~0xffful) + (gva & 0xffful);
}

static void virt_dump_ptes(FILE *stream, struct kvm_vm *vm, uint8_t indent,
			   uint64_t ptea_start)
{
	uint64_t *pte, ptea;

	for (ptea = ptea_start; ptea < ptea_start + 0x100 * 8; ptea += 8) {
		pte = addr_gpa2hva(vm, ptea);
		if (*pte & PAGE_INVALID)
			continue;
		fprintf(stream, "%*spte @ 0x%lx: 0x%016lx\n",
			indent, "", ptea, *pte);
	}
}

static void virt_dump_region(FILE *stream, struct kvm_vm *vm, uint8_t indent,
			     uint64_t reg_tab_addr)
{
	uint64_t addr, *entry;

	for (addr = reg_tab_addr; addr < reg_tab_addr + 0x400 * 8; addr += 8) {
		entry = addr_gpa2hva(vm, addr);
		if (*entry & REGION_ENTRY_INVALID)
			continue;
		fprintf(stream, "%*srt%lde @ 0x%lx: 0x%016lx\n",
			indent, "", 4 - ((*entry & REGION_ENTRY_TYPE) >> 2),
			addr, *entry);
		if (*entry & REGION_ENTRY_TYPE) {
			virt_dump_region(stream, vm, indent + 2,
					 *entry & REGION_ENTRY_ORIGIN);
		} else {
			virt_dump_ptes(stream, vm, indent + 2,
				       *entry & REGION_ENTRY_ORIGIN);
		}
	}
}

void virt_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent)
{
	if (!vm->pgd_created)
		return;

	virt_dump_region(stream, vm, indent, vm->pgd);
}

struct kvm_vm *vm_create_default(uint32_t vcpuid, uint64_t extra_mem_pages,
				 void *guest_code)
{
	/*
	 * The additional amount of pages required for the page tables is:
	 * 1 * n / 256 + 4 * (n / 256) / 2048 + 4 * (n / 256) / 2048^2 + ...
	 * which is definitely smaller than (n / 256) * 2.
	 */
	uint64_t extra_pg_pages = extra_mem_pages / 256 * 2;
	struct kvm_vm *vm;

	vm = vm_create(VM_MODE_DEFAULT,
		       DEFAULT_GUEST_PHY_PAGES + extra_pg_pages, O_RDWR);

	kvm_vm_elf_load(vm, program_invocation_name, 0, 0);
	vm_vcpu_add_default(vm, vcpuid, guest_code);

	return vm;
}

void vm_vcpu_add_default(struct kvm_vm *vm, uint32_t vcpuid, void *guest_code)
{
	size_t stack_size =  DEFAULT_STACK_PGS * getpagesize();
	uint64_t stack_vaddr;
	struct kvm_regs regs;
	struct kvm_sregs sregs;
	struct kvm_run *run;

	TEST_ASSERT(vm->page_size == 4096, "Unsupported page size: 0x%x",
		    vm->page_size);

	stack_vaddr = vm_vaddr_alloc(vm, stack_size,
				     DEFAULT_GUEST_STACK_VADDR_MIN, 0, 0);

	vm_vcpu_add(vm, vcpuid);

	/* Setup guest registers */
	vcpu_regs_get(vm, vcpuid, &regs);
	regs.gprs[15] = stack_vaddr + (DEFAULT_STACK_PGS * getpagesize()) - 160;
	vcpu_regs_set(vm, vcpuid, &regs);

	vcpu_sregs_get(vm, vcpuid, &sregs);
	sregs.crs[0] |= 0x00040000;		/* Enable floating point regs */
	sregs.crs[1] = vm->pgd | 0xf;		/* Primary region table */
	vcpu_sregs_set(vm, vcpuid, &sregs);

	run = vcpu_state(vm, vcpuid);
	run->psw_mask = 0x0400000180000000ULL;  /* DAT enabled + 64 bit mode */
	run->psw_addr = (uintptr_t)guest_code;
}

void vcpu_args_set(struct kvm_vm *vm, uint32_t vcpuid, unsigned int num, ...)
{
	va_list ap;
	struct kvm_regs regs;
	int i;

	TEST_ASSERT(num >= 1 && num <= 5, "Unsupported number of args,\n"
		    "  num: %u\n",
		    num);

	va_start(ap, num);
	vcpu_regs_get(vm, vcpuid, &regs);

	for (i = 0; i < num; i++)
		regs.gprs[i + 2] = va_arg(ap, uint64_t);

	vcpu_regs_set(vm, vcpuid, &regs);
	va_end(ap);
}

void vcpu_dump(FILE *stream, struct kvm_vm *vm, uint32_t vcpuid, uint8_t indent)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);

	if (!vcpu)
		return;

	fprintf(stream, "%*spstate: psw: 0x%.16llx:0x%.16llx\n",
		indent, "", vcpu->state->psw_mask, vcpu->state->psw_addr);
}

void assert_on_unhandled_exception(struct kvm_vm *vm, uint32_t vcpuid)
{
}
