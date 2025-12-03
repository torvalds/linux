// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM selftest s390x library code - CPU-related functions (page tables...)
 *
 * Copyright (C) 2019, Red Hat, Inc.
 */

#include "processor.h"
#include "kvm_util.h"

#define PAGES_PER_REGION 4

void virt_arch_pgd_alloc(struct kvm_vm *vm)
{
	vm_paddr_t paddr;

	TEST_ASSERT(vm->page_size == PAGE_SIZE, "Unsupported page size: 0x%x",
		    vm->page_size);

	if (vm->pgd_created)
		return;

	paddr = vm_phy_pages_alloc(vm, PAGES_PER_REGION,
				   KVM_GUEST_PAGE_TABLE_MIN_PADDR,
				   vm->memslots[MEM_REGION_PT]);
	memset(addr_gpa2hva(vm, paddr), 0xff, PAGES_PER_REGION * vm->page_size);

	vm->pgd = paddr;
	vm->pgd_created = true;
}

/*
 * Allocate 4 pages for a region/segment table (ri < 4), or one page for
 * a page table (ri == 4). Returns a suitable region/segment table entry
 * which points to the freshly allocated pages.
 */
static uint64_t virt_alloc_region(struct kvm_vm *vm, int ri)
{
	uint64_t taddr;

	taddr = vm_phy_pages_alloc(vm,  ri < 4 ? PAGES_PER_REGION : 1,
				   KVM_GUEST_PAGE_TABLE_MIN_PADDR, 0);
	memset(addr_gpa2hva(vm, taddr), 0xff, PAGES_PER_REGION * vm->page_size);

	return (taddr & REGION_ENTRY_ORIGIN)
		| (((4 - ri) << 2) & REGION_ENTRY_TYPE)
		| ((ri < 4 ? (PAGES_PER_REGION - 1) : 0) & REGION_ENTRY_LENGTH);
}

void virt_arch_pg_map(struct kvm_vm *vm, uint64_t gva, uint64_t gpa)
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
			entry[idx] = virt_alloc_region(vm, ri);
		entry = addr_gpa2hva(vm, entry[idx] & REGION_ENTRY_ORIGIN);
	}

	/* Fill in page table entry */
	idx = (gva >> PAGE_SHIFT) & 0x0ffu;		/* page index */
	if (!(entry[idx] & PAGE_INVALID))
		fprintf(stderr,
			"WARNING: PTE for gpa=0x%"PRIx64" already set!\n", gpa);
	entry[idx] = gpa;
}

vm_paddr_t addr_arch_gva2gpa(struct kvm_vm *vm, vm_vaddr_t gva)
{
	int ri, idx;
	uint64_t *entry;

	TEST_ASSERT(vm->page_size == PAGE_SIZE, "Unsupported page size: 0x%x",
		    vm->page_size);

	entry = addr_gpa2hva(vm, vm->pgd);
	for (ri = 1; ri <= 4; ri++) {
		idx = (gva >> (64 - 11 * ri)) & 0x7ffu;
		TEST_ASSERT(!(entry[idx] & REGION_ENTRY_INVALID),
			    "No region mapping for vm virtual address 0x%lx",
			    gva);
		entry = addr_gpa2hva(vm, entry[idx] & REGION_ENTRY_ORIGIN);
	}

	idx = (gva >> PAGE_SHIFT) & 0x0ffu;		/* page index */

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

void virt_arch_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent)
{
	if (!vm->pgd_created)
		return;

	virt_dump_region(stream, vm, indent, vm->pgd);
}

void vcpu_arch_set_entry_point(struct kvm_vcpu *vcpu, void *guest_code)
{
	vcpu->run->psw_addr = (uintptr_t)guest_code;
}

struct kvm_vcpu *vm_arch_vcpu_add(struct kvm_vm *vm, uint32_t vcpu_id)
{
	size_t stack_size =  DEFAULT_STACK_PGS * getpagesize();
	uint64_t stack_vaddr;
	struct kvm_regs regs;
	struct kvm_sregs sregs;
	struct kvm_vcpu *vcpu;

	TEST_ASSERT(vm->page_size == PAGE_SIZE, "Unsupported page size: 0x%x",
		    vm->page_size);

	stack_vaddr = __vm_vaddr_alloc(vm, stack_size,
				       DEFAULT_GUEST_STACK_VADDR_MIN,
				       MEM_REGION_DATA);

	vcpu = __vm_vcpu_add(vm, vcpu_id);

	/* Setup guest registers */
	vcpu_regs_get(vcpu, &regs);
	regs.gprs[15] = stack_vaddr + (DEFAULT_STACK_PGS * getpagesize()) - 160;
	vcpu_regs_set(vcpu, &regs);

	vcpu_sregs_get(vcpu, &sregs);
	sregs.crs[0] |= 0x00040000;		/* Enable floating point regs */
	sregs.crs[1] = vm->pgd | 0xf;		/* Primary region table */
	vcpu_sregs_set(vcpu, &sregs);

	vcpu->run->psw_mask = 0x0400000180000000ULL;  /* DAT enabled + 64 bit mode */

	return vcpu;
}

void vcpu_args_set(struct kvm_vcpu *vcpu, unsigned int num, ...)
{
	va_list ap;
	struct kvm_regs regs;
	int i;

	TEST_ASSERT(num >= 1 && num <= 5, "Unsupported number of args,\n"
		    "  num: %u",
		    num);

	va_start(ap, num);
	vcpu_regs_get(vcpu, &regs);

	for (i = 0; i < num; i++)
		regs.gprs[i + 2] = va_arg(ap, uint64_t);

	vcpu_regs_set(vcpu, &regs);
	va_end(ap);
}

void vcpu_arch_dump(FILE *stream, struct kvm_vcpu *vcpu, uint8_t indent)
{
	fprintf(stream, "%*spstate: psw: 0x%.16llx:0x%.16llx\n",
		indent, "", vcpu->run->psw_mask, vcpu->run->psw_addr);
}

void assert_on_unhandled_exception(struct kvm_vcpu *vcpu)
{
}

bool kvm_arch_has_default_irqchip(void)
{
	return true;
}
