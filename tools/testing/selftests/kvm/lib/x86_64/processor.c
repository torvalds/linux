// SPDX-License-Identifier: GPL-2.0-only
/*
 * tools/testing/selftests/kvm/lib/x86_64/processor.c
 *
 * Copyright (C) 2018, Google LLC.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "../kvm_util_internal.h"
#include "processor.h"

#ifndef NUM_INTERRUPTS
#define NUM_INTERRUPTS 256
#endif

#define DEFAULT_CODE_SELECTOR 0x8
#define DEFAULT_DATA_SELECTOR 0x10

vm_vaddr_t exception_handlers;

/* Virtual translation table structure declarations */
struct pageUpperEntry {
	uint64_t present:1;
	uint64_t writable:1;
	uint64_t user:1;
	uint64_t write_through:1;
	uint64_t cache_disable:1;
	uint64_t accessed:1;
	uint64_t ignored_06:1;
	uint64_t page_size:1;
	uint64_t ignored_11_08:4;
	uint64_t pfn:40;
	uint64_t ignored_62_52:11;
	uint64_t execute_disable:1;
};

struct pageTableEntry {
	uint64_t present:1;
	uint64_t writable:1;
	uint64_t user:1;
	uint64_t write_through:1;
	uint64_t cache_disable:1;
	uint64_t accessed:1;
	uint64_t dirty:1;
	uint64_t reserved_07:1;
	uint64_t global:1;
	uint64_t ignored_11_09:3;
	uint64_t pfn:40;
	uint64_t ignored_62_52:11;
	uint64_t execute_disable:1;
};

void regs_dump(FILE *stream, struct kvm_regs *regs,
	       uint8_t indent)
{
	fprintf(stream, "%*srax: 0x%.16llx rbx: 0x%.16llx "
		"rcx: 0x%.16llx rdx: 0x%.16llx\n",
		indent, "",
		regs->rax, regs->rbx, regs->rcx, regs->rdx);
	fprintf(stream, "%*srsi: 0x%.16llx rdi: 0x%.16llx "
		"rsp: 0x%.16llx rbp: 0x%.16llx\n",
		indent, "",
		regs->rsi, regs->rdi, regs->rsp, regs->rbp);
	fprintf(stream, "%*sr8:  0x%.16llx r9:  0x%.16llx "
		"r10: 0x%.16llx r11: 0x%.16llx\n",
		indent, "",
		regs->r8, regs->r9, regs->r10, regs->r11);
	fprintf(stream, "%*sr12: 0x%.16llx r13: 0x%.16llx "
		"r14: 0x%.16llx r15: 0x%.16llx\n",
		indent, "",
		regs->r12, regs->r13, regs->r14, regs->r15);
	fprintf(stream, "%*srip: 0x%.16llx rfl: 0x%.16llx\n",
		indent, "",
		regs->rip, regs->rflags);
}

/*
 * Segment Dump
 *
 * Input Args:
 *   stream  - Output FILE stream
 *   segment - KVM segment
 *   indent  - Left margin indent amount
 *
 * Output Args: None
 *
 * Return: None
 *
 * Dumps the state of the KVM segment given by @segment, to the FILE stream
 * given by @stream.
 */
static void segment_dump(FILE *stream, struct kvm_segment *segment,
			 uint8_t indent)
{
	fprintf(stream, "%*sbase: 0x%.16llx limit: 0x%.8x "
		"selector: 0x%.4x type: 0x%.2x\n",
		indent, "", segment->base, segment->limit,
		segment->selector, segment->type);
	fprintf(stream, "%*spresent: 0x%.2x dpl: 0x%.2x "
		"db: 0x%.2x s: 0x%.2x l: 0x%.2x\n",
		indent, "", segment->present, segment->dpl,
		segment->db, segment->s, segment->l);
	fprintf(stream, "%*sg: 0x%.2x avl: 0x%.2x "
		"unusable: 0x%.2x padding: 0x%.2x\n",
		indent, "", segment->g, segment->avl,
		segment->unusable, segment->padding);
}

/*
 * dtable Dump
 *
 * Input Args:
 *   stream - Output FILE stream
 *   dtable - KVM dtable
 *   indent - Left margin indent amount
 *
 * Output Args: None
 *
 * Return: None
 *
 * Dumps the state of the KVM dtable given by @dtable, to the FILE stream
 * given by @stream.
 */
static void dtable_dump(FILE *stream, struct kvm_dtable *dtable,
			uint8_t indent)
{
	fprintf(stream, "%*sbase: 0x%.16llx limit: 0x%.4x "
		"padding: 0x%.4x 0x%.4x 0x%.4x\n",
		indent, "", dtable->base, dtable->limit,
		dtable->padding[0], dtable->padding[1], dtable->padding[2]);
}

void sregs_dump(FILE *stream, struct kvm_sregs *sregs,
		uint8_t indent)
{
	unsigned int i;

	fprintf(stream, "%*scs:\n", indent, "");
	segment_dump(stream, &sregs->cs, indent + 2);
	fprintf(stream, "%*sds:\n", indent, "");
	segment_dump(stream, &sregs->ds, indent + 2);
	fprintf(stream, "%*ses:\n", indent, "");
	segment_dump(stream, &sregs->es, indent + 2);
	fprintf(stream, "%*sfs:\n", indent, "");
	segment_dump(stream, &sregs->fs, indent + 2);
	fprintf(stream, "%*sgs:\n", indent, "");
	segment_dump(stream, &sregs->gs, indent + 2);
	fprintf(stream, "%*sss:\n", indent, "");
	segment_dump(stream, &sregs->ss, indent + 2);
	fprintf(stream, "%*str:\n", indent, "");
	segment_dump(stream, &sregs->tr, indent + 2);
	fprintf(stream, "%*sldt:\n", indent, "");
	segment_dump(stream, &sregs->ldt, indent + 2);

	fprintf(stream, "%*sgdt:\n", indent, "");
	dtable_dump(stream, &sregs->gdt, indent + 2);
	fprintf(stream, "%*sidt:\n", indent, "");
	dtable_dump(stream, &sregs->idt, indent + 2);

	fprintf(stream, "%*scr0: 0x%.16llx cr2: 0x%.16llx "
		"cr3: 0x%.16llx cr4: 0x%.16llx\n",
		indent, "",
		sregs->cr0, sregs->cr2, sregs->cr3, sregs->cr4);
	fprintf(stream, "%*scr8: 0x%.16llx efer: 0x%.16llx "
		"apic_base: 0x%.16llx\n",
		indent, "",
		sregs->cr8, sregs->efer, sregs->apic_base);

	fprintf(stream, "%*sinterrupt_bitmap:\n", indent, "");
	for (i = 0; i < (KVM_NR_INTERRUPTS + 63) / 64; i++) {
		fprintf(stream, "%*s%.16llx\n", indent + 2, "",
			sregs->interrupt_bitmap[i]);
	}
}

void virt_pgd_alloc(struct kvm_vm *vm)
{
	TEST_ASSERT(vm->mode == VM_MODE_PXXV48_4K, "Attempt to use "
		"unknown or unsupported guest mode, mode: 0x%x", vm->mode);

	/* If needed, create page map l4 table. */
	if (!vm->pgd_created) {
		vm->pgd = vm_alloc_page_table(vm);
		vm->pgd_created = true;
	}
}

static void *virt_get_pte(struct kvm_vm *vm, uint64_t pt_pfn, uint64_t vaddr,
			  int level)
{
	uint64_t *page_table = addr_gpa2hva(vm, pt_pfn << vm->page_shift);
	int index = vaddr >> (vm->page_shift + level * 9) & 0x1ffu;

	return &page_table[index];
}

static struct pageUpperEntry *virt_create_upper_pte(struct kvm_vm *vm,
						    uint64_t pt_pfn,
						    uint64_t vaddr,
						    uint64_t paddr,
						    int level,
						    enum x86_page_size page_size)
{
	struct pageUpperEntry *pte = virt_get_pte(vm, pt_pfn, vaddr, level);

	if (!pte->present) {
		pte->writable = true;
		pte->present = true;
		pte->page_size = (level == page_size);
		if (pte->page_size)
			pte->pfn = paddr >> vm->page_shift;
		else
			pte->pfn = vm_alloc_page_table(vm) >> vm->page_shift;
	} else {
		/*
		 * Entry already present.  Assert that the caller doesn't want
		 * a hugepage at this level, and that there isn't a hugepage at
		 * this level.
		 */
		TEST_ASSERT(level != page_size,
			    "Cannot create hugepage at level: %u, vaddr: 0x%lx\n",
			    page_size, vaddr);
		TEST_ASSERT(!pte->page_size,
			    "Cannot create page table at level: %u, vaddr: 0x%lx\n",
			    level, vaddr);
	}
	return pte;
}

void __virt_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
		   enum x86_page_size page_size)
{
	const uint64_t pg_size = 1ull << ((page_size * 9) + 12);
	struct pageUpperEntry *pml4e, *pdpe, *pde;
	struct pageTableEntry *pte;

	TEST_ASSERT(vm->mode == VM_MODE_PXXV48_4K,
		    "Unknown or unsupported guest mode, mode: 0x%x", vm->mode);

	TEST_ASSERT((vaddr % pg_size) == 0,
		    "Virtual address not aligned,\n"
		    "vaddr: 0x%lx page size: 0x%lx", vaddr, pg_size);
	TEST_ASSERT(sparsebit_is_set(vm->vpages_valid, (vaddr >> vm->page_shift)),
		    "Invalid virtual address, vaddr: 0x%lx", vaddr);
	TEST_ASSERT((paddr % pg_size) == 0,
		    "Physical address not aligned,\n"
		    "  paddr: 0x%lx page size: 0x%lx", paddr, pg_size);
	TEST_ASSERT((paddr >> vm->page_shift) <= vm->max_gfn,
		    "Physical address beyond maximum supported,\n"
		    "  paddr: 0x%lx vm->max_gfn: 0x%lx vm->page_size: 0x%x",
		    paddr, vm->max_gfn, vm->page_size);

	/*
	 * Allocate upper level page tables, if not already present.  Return
	 * early if a hugepage was created.
	 */
	pml4e = virt_create_upper_pte(vm, vm->pgd >> vm->page_shift,
				      vaddr, paddr, 3, page_size);
	if (pml4e->page_size)
		return;

	pdpe = virt_create_upper_pte(vm, pml4e->pfn, vaddr, paddr, 2, page_size);
	if (pdpe->page_size)
		return;

	pde = virt_create_upper_pte(vm, pdpe->pfn, vaddr, paddr, 1, page_size);
	if (pde->page_size)
		return;

	/* Fill in page table entry. */
	pte = virt_get_pte(vm, pde->pfn, vaddr, 0);
	TEST_ASSERT(!pte->present,
		    "PTE already present for 4k page at vaddr: 0x%lx\n", vaddr);
	pte->pfn = paddr >> vm->page_shift;
	pte->writable = true;
	pte->present = 1;
}

void virt_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr)
{
	__virt_pg_map(vm, vaddr, paddr, X86_PAGE_SIZE_4K);
}

static struct pageTableEntry *_vm_get_page_table_entry(struct kvm_vm *vm, int vcpuid,
						       uint64_t vaddr)
{
	uint16_t index[4];
	struct pageUpperEntry *pml4e, *pdpe, *pde;
	struct pageTableEntry *pte;
	struct kvm_cpuid_entry2 *entry;
	struct kvm_sregs sregs;
	int max_phy_addr;
	/* Set the bottom 52 bits. */
	uint64_t rsvd_mask = 0x000fffffffffffff;

	entry = kvm_get_supported_cpuid_index(0x80000008, 0);
	max_phy_addr = entry->eax & 0x000000ff;
	/* Clear the bottom bits of the reserved mask. */
	rsvd_mask = (rsvd_mask >> max_phy_addr) << max_phy_addr;

	/*
	 * SDM vol 3, fig 4-11 "Formats of CR3 and Paging-Structure Entries
	 * with 4-Level Paging and 5-Level Paging".
	 * If IA32_EFER.NXE = 0 and the P flag of a paging-structure entry is 1,
	 * the XD flag (bit 63) is reserved.
	 */
	vcpu_sregs_get(vm, vcpuid, &sregs);
	if ((sregs.efer & EFER_NX) == 0) {
		rsvd_mask |= (1ull << 63);
	}

	TEST_ASSERT(vm->mode == VM_MODE_PXXV48_4K, "Attempt to use "
		"unknown or unsupported guest mode, mode: 0x%x", vm->mode);
	TEST_ASSERT(sparsebit_is_set(vm->vpages_valid,
		(vaddr >> vm->page_shift)),
		"Invalid virtual address, vaddr: 0x%lx",
		vaddr);
	/*
	 * Based on the mode check above there are 48 bits in the vaddr, so
	 * shift 16 to sign extend the last bit (bit-47),
	 */
	TEST_ASSERT(vaddr == (((int64_t)vaddr << 16) >> 16),
		"Canonical check failed.  The virtual address is invalid.");

	index[0] = (vaddr >> 12) & 0x1ffu;
	index[1] = (vaddr >> 21) & 0x1ffu;
	index[2] = (vaddr >> 30) & 0x1ffu;
	index[3] = (vaddr >> 39) & 0x1ffu;

	pml4e = addr_gpa2hva(vm, vm->pgd);
	TEST_ASSERT(pml4e[index[3]].present,
		"Expected pml4e to be present for gva: 0x%08lx", vaddr);
	TEST_ASSERT((*(uint64_t*)(&pml4e[index[3]]) &
		(rsvd_mask | (1ull << 7))) == 0,
		"Unexpected reserved bits set.");

	pdpe = addr_gpa2hva(vm, pml4e[index[3]].pfn * vm->page_size);
	TEST_ASSERT(pdpe[index[2]].present,
		"Expected pdpe to be present for gva: 0x%08lx", vaddr);
	TEST_ASSERT(pdpe[index[2]].page_size == 0,
		"Expected pdpe to map a pde not a 1-GByte page.");
	TEST_ASSERT((*(uint64_t*)(&pdpe[index[2]]) & rsvd_mask) == 0,
		"Unexpected reserved bits set.");

	pde = addr_gpa2hva(vm, pdpe[index[2]].pfn * vm->page_size);
	TEST_ASSERT(pde[index[1]].present,
		"Expected pde to be present for gva: 0x%08lx", vaddr);
	TEST_ASSERT(pde[index[1]].page_size == 0,
		"Expected pde to map a pte not a 2-MByte page.");
	TEST_ASSERT((*(uint64_t*)(&pde[index[1]]) & rsvd_mask) == 0,
		"Unexpected reserved bits set.");

	pte = addr_gpa2hva(vm, pde[index[1]].pfn * vm->page_size);
	TEST_ASSERT(pte[index[0]].present,
		"Expected pte to be present for gva: 0x%08lx", vaddr);

	return &pte[index[0]];
}

uint64_t vm_get_page_table_entry(struct kvm_vm *vm, int vcpuid, uint64_t vaddr)
{
	struct pageTableEntry *pte = _vm_get_page_table_entry(vm, vcpuid, vaddr);

	return *(uint64_t *)pte;
}

void vm_set_page_table_entry(struct kvm_vm *vm, int vcpuid, uint64_t vaddr,
			     uint64_t pte)
{
	struct pageTableEntry *new_pte = _vm_get_page_table_entry(vm, vcpuid,
								  vaddr);

	*(uint64_t *)new_pte = pte;
}

void virt_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent)
{
	struct pageUpperEntry *pml4e, *pml4e_start;
	struct pageUpperEntry *pdpe, *pdpe_start;
	struct pageUpperEntry *pde, *pde_start;
	struct pageTableEntry *pte, *pte_start;

	if (!vm->pgd_created)
		return;

	fprintf(stream, "%*s                                          "
		"                no\n", indent, "");
	fprintf(stream, "%*s      index hvaddr         gpaddr         "
		"addr         w exec dirty\n",
		indent, "");
	pml4e_start = (struct pageUpperEntry *) addr_gpa2hva(vm, vm->pgd);
	for (uint16_t n1 = 0; n1 <= 0x1ffu; n1++) {
		pml4e = &pml4e_start[n1];
		if (!pml4e->present)
			continue;
		fprintf(stream, "%*spml4e 0x%-3zx %p 0x%-12lx 0x%-10lx %u "
			" %u\n",
			indent, "",
			pml4e - pml4e_start, pml4e,
			addr_hva2gpa(vm, pml4e), (uint64_t) pml4e->pfn,
			pml4e->writable, pml4e->execute_disable);

		pdpe_start = addr_gpa2hva(vm, pml4e->pfn * vm->page_size);
		for (uint16_t n2 = 0; n2 <= 0x1ffu; n2++) {
			pdpe = &pdpe_start[n2];
			if (!pdpe->present)
				continue;
			fprintf(stream, "%*spdpe  0x%-3zx %p 0x%-12lx 0x%-10lx "
				"%u  %u\n",
				indent, "",
				pdpe - pdpe_start, pdpe,
				addr_hva2gpa(vm, pdpe),
				(uint64_t) pdpe->pfn, pdpe->writable,
				pdpe->execute_disable);

			pde_start = addr_gpa2hva(vm, pdpe->pfn * vm->page_size);
			for (uint16_t n3 = 0; n3 <= 0x1ffu; n3++) {
				pde = &pde_start[n3];
				if (!pde->present)
					continue;
				fprintf(stream, "%*spde   0x%-3zx %p "
					"0x%-12lx 0x%-10lx %u  %u\n",
					indent, "", pde - pde_start, pde,
					addr_hva2gpa(vm, pde),
					(uint64_t) pde->pfn, pde->writable,
					pde->execute_disable);

				pte_start = addr_gpa2hva(vm, pde->pfn * vm->page_size);
				for (uint16_t n4 = 0; n4 <= 0x1ffu; n4++) {
					pte = &pte_start[n4];
					if (!pte->present)
						continue;
					fprintf(stream, "%*spte   0x%-3zx %p "
						"0x%-12lx 0x%-10lx %u  %u "
						"    %u    0x%-10lx\n",
						indent, "",
						pte - pte_start, pte,
						addr_hva2gpa(vm, pte),
						(uint64_t) pte->pfn,
						pte->writable,
						pte->execute_disable,
						pte->dirty,
						((uint64_t) n1 << 27)
							| ((uint64_t) n2 << 18)
							| ((uint64_t) n3 << 9)
							| ((uint64_t) n4));
				}
			}
		}
	}
}

/*
 * Set Unusable Segment
 *
 * Input Args: None
 *
 * Output Args:
 *   segp - Pointer to segment register
 *
 * Return: None
 *
 * Sets the segment register pointed to by @segp to an unusable state.
 */
static void kvm_seg_set_unusable(struct kvm_segment *segp)
{
	memset(segp, 0, sizeof(*segp));
	segp->unusable = true;
}

static void kvm_seg_fill_gdt_64bit(struct kvm_vm *vm, struct kvm_segment *segp)
{
	void *gdt = addr_gva2hva(vm, vm->gdt);
	struct desc64 *desc = gdt + (segp->selector >> 3) * 8;

	desc->limit0 = segp->limit & 0xFFFF;
	desc->base0 = segp->base & 0xFFFF;
	desc->base1 = segp->base >> 16;
	desc->type = segp->type;
	desc->s = segp->s;
	desc->dpl = segp->dpl;
	desc->p = segp->present;
	desc->limit1 = segp->limit >> 16;
	desc->avl = segp->avl;
	desc->l = segp->l;
	desc->db = segp->db;
	desc->g = segp->g;
	desc->base2 = segp->base >> 24;
	if (!segp->s)
		desc->base3 = segp->base >> 32;
}


/*
 * Set Long Mode Flat Kernel Code Segment
 *
 * Input Args:
 *   vm - VM whose GDT is being filled, or NULL to only write segp
 *   selector - selector value
 *
 * Output Args:
 *   segp - Pointer to KVM segment
 *
 * Return: None
 *
 * Sets up the KVM segment pointed to by @segp, to be a code segment
 * with the selector value given by @selector.
 */
static void kvm_seg_set_kernel_code_64bit(struct kvm_vm *vm, uint16_t selector,
	struct kvm_segment *segp)
{
	memset(segp, 0, sizeof(*segp));
	segp->selector = selector;
	segp->limit = 0xFFFFFFFFu;
	segp->s = 0x1; /* kTypeCodeData */
	segp->type = 0x08 | 0x01 | 0x02; /* kFlagCode | kFlagCodeAccessed
					  * | kFlagCodeReadable
					  */
	segp->g = true;
	segp->l = true;
	segp->present = 1;
	if (vm)
		kvm_seg_fill_gdt_64bit(vm, segp);
}

/*
 * Set Long Mode Flat Kernel Data Segment
 *
 * Input Args:
 *   vm - VM whose GDT is being filled, or NULL to only write segp
 *   selector - selector value
 *
 * Output Args:
 *   segp - Pointer to KVM segment
 *
 * Return: None
 *
 * Sets up the KVM segment pointed to by @segp, to be a data segment
 * with the selector value given by @selector.
 */
static void kvm_seg_set_kernel_data_64bit(struct kvm_vm *vm, uint16_t selector,
	struct kvm_segment *segp)
{
	memset(segp, 0, sizeof(*segp));
	segp->selector = selector;
	segp->limit = 0xFFFFFFFFu;
	segp->s = 0x1; /* kTypeCodeData */
	segp->type = 0x00 | 0x01 | 0x02; /* kFlagData | kFlagDataAccessed
					  * | kFlagDataWritable
					  */
	segp->g = true;
	segp->present = true;
	if (vm)
		kvm_seg_fill_gdt_64bit(vm, segp);
}

vm_paddr_t addr_gva2gpa(struct kvm_vm *vm, vm_vaddr_t gva)
{
	uint16_t index[4];
	struct pageUpperEntry *pml4e, *pdpe, *pde;
	struct pageTableEntry *pte;

	TEST_ASSERT(vm->mode == VM_MODE_PXXV48_4K, "Attempt to use "
		"unknown or unsupported guest mode, mode: 0x%x", vm->mode);

	index[0] = (gva >> 12) & 0x1ffu;
	index[1] = (gva >> 21) & 0x1ffu;
	index[2] = (gva >> 30) & 0x1ffu;
	index[3] = (gva >> 39) & 0x1ffu;

	if (!vm->pgd_created)
		goto unmapped_gva;
	pml4e = addr_gpa2hva(vm, vm->pgd);
	if (!pml4e[index[3]].present)
		goto unmapped_gva;

	pdpe = addr_gpa2hva(vm, pml4e[index[3]].pfn * vm->page_size);
	if (!pdpe[index[2]].present)
		goto unmapped_gva;

	pde = addr_gpa2hva(vm, pdpe[index[2]].pfn * vm->page_size);
	if (!pde[index[1]].present)
		goto unmapped_gva;

	pte = addr_gpa2hva(vm, pde[index[1]].pfn * vm->page_size);
	if (!pte[index[0]].present)
		goto unmapped_gva;

	return (pte[index[0]].pfn * vm->page_size) + (gva & 0xfffu);

unmapped_gva:
	TEST_FAIL("No mapping for vm virtual address, gva: 0x%lx", gva);
	exit(EXIT_FAILURE);
}

static void kvm_setup_gdt(struct kvm_vm *vm, struct kvm_dtable *dt)
{
	if (!vm->gdt)
		vm->gdt = vm_vaddr_alloc_page(vm);

	dt->base = vm->gdt;
	dt->limit = getpagesize();
}

static void kvm_setup_tss_64bit(struct kvm_vm *vm, struct kvm_segment *segp,
				int selector)
{
	if (!vm->tss)
		vm->tss = vm_vaddr_alloc_page(vm);

	memset(segp, 0, sizeof(*segp));
	segp->base = vm->tss;
	segp->limit = 0x67;
	segp->selector = selector;
	segp->type = 0xb;
	segp->present = 1;
	kvm_seg_fill_gdt_64bit(vm, segp);
}

static void vcpu_setup(struct kvm_vm *vm, int vcpuid)
{
	struct kvm_sregs sregs;

	/* Set mode specific system register values. */
	vcpu_sregs_get(vm, vcpuid, &sregs);

	sregs.idt.limit = 0;

	kvm_setup_gdt(vm, &sregs.gdt);

	switch (vm->mode) {
	case VM_MODE_PXXV48_4K:
		sregs.cr0 = X86_CR0_PE | X86_CR0_NE | X86_CR0_PG;
		sregs.cr4 |= X86_CR4_PAE | X86_CR4_OSFXSR;
		sregs.efer |= (EFER_LME | EFER_LMA | EFER_NX);

		kvm_seg_set_unusable(&sregs.ldt);
		kvm_seg_set_kernel_code_64bit(vm, DEFAULT_CODE_SELECTOR, &sregs.cs);
		kvm_seg_set_kernel_data_64bit(vm, DEFAULT_DATA_SELECTOR, &sregs.ds);
		kvm_seg_set_kernel_data_64bit(vm, DEFAULT_DATA_SELECTOR, &sregs.es);
		kvm_setup_tss_64bit(vm, &sregs.tr, 0x18);
		break;

	default:
		TEST_FAIL("Unknown guest mode, mode: 0x%x", vm->mode);
	}

	sregs.cr3 = vm->pgd;
	vcpu_sregs_set(vm, vcpuid, &sregs);
}

void vm_vcpu_add_default(struct kvm_vm *vm, uint32_t vcpuid, void *guest_code)
{
	struct kvm_mp_state mp_state;
	struct kvm_regs regs;
	vm_vaddr_t stack_vaddr;
	stack_vaddr = vm_vaddr_alloc(vm, DEFAULT_STACK_PGS * getpagesize(),
				     DEFAULT_GUEST_STACK_VADDR_MIN);

	/* Create VCPU */
	vm_vcpu_add(vm, vcpuid);
	vcpu_set_cpuid(vm, vcpuid, kvm_get_supported_cpuid());
	vcpu_setup(vm, vcpuid);

	/* Setup guest general purpose registers */
	vcpu_regs_get(vm, vcpuid, &regs);
	regs.rflags = regs.rflags | 0x2;
	regs.rsp = stack_vaddr + (DEFAULT_STACK_PGS * getpagesize());
	regs.rip = (unsigned long) guest_code;
	vcpu_regs_set(vm, vcpuid, &regs);

	/* Setup the MP state */
	mp_state.mp_state = 0;
	vcpu_set_mp_state(vm, vcpuid, &mp_state);
}

/*
 * Allocate an instance of struct kvm_cpuid2
 *
 * Input Args: None
 *
 * Output Args: None
 *
 * Return: A pointer to the allocated struct. The caller is responsible
 * for freeing this struct.
 *
 * Since kvm_cpuid2 uses a 0-length array to allow a the size of the
 * array to be decided at allocation time, allocation is slightly
 * complicated. This function uses a reasonable default length for
 * the array and performs the appropriate allocation.
 */
static struct kvm_cpuid2 *allocate_kvm_cpuid2(void)
{
	struct kvm_cpuid2 *cpuid;
	int nent = 100;
	size_t size;

	size = sizeof(*cpuid);
	size += nent * sizeof(struct kvm_cpuid_entry2);
	cpuid = malloc(size);
	if (!cpuid) {
		perror("malloc");
		abort();
	}

	cpuid->nent = nent;

	return cpuid;
}

/*
 * KVM Supported CPUID Get
 *
 * Input Args: None
 *
 * Output Args:
 *
 * Return: The supported KVM CPUID
 *
 * Get the guest CPUID supported by KVM.
 */
struct kvm_cpuid2 *kvm_get_supported_cpuid(void)
{
	static struct kvm_cpuid2 *cpuid;
	int ret;
	int kvm_fd;

	if (cpuid)
		return cpuid;

	cpuid = allocate_kvm_cpuid2();
	kvm_fd = open_kvm_dev_path_or_exit();

	ret = ioctl(kvm_fd, KVM_GET_SUPPORTED_CPUID, cpuid);
	TEST_ASSERT(ret == 0, "KVM_GET_SUPPORTED_CPUID failed %d %d\n",
		    ret, errno);

	close(kvm_fd);
	return cpuid;
}

/*
 * KVM Get MSR
 *
 * Input Args:
 *   msr_index - Index of MSR
 *
 * Output Args: None
 *
 * Return: On success, value of the MSR. On failure a TEST_ASSERT is produced.
 *
 * Get value of MSR for VCPU.
 */
uint64_t kvm_get_feature_msr(uint64_t msr_index)
{
	struct {
		struct kvm_msrs header;
		struct kvm_msr_entry entry;
	} buffer = {};
	int r, kvm_fd;

	buffer.header.nmsrs = 1;
	buffer.entry.index = msr_index;
	kvm_fd = open_kvm_dev_path_or_exit();

	r = ioctl(kvm_fd, KVM_GET_MSRS, &buffer.header);
	TEST_ASSERT(r == 1, "KVM_GET_MSRS IOCTL failed,\n"
		"  rc: %i errno: %i", r, errno);

	close(kvm_fd);
	return buffer.entry.data;
}

/*
 * VM VCPU CPUID Set
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU id
 *
 * Output Args: None
 *
 * Return: KVM CPUID (KVM_GET_CPUID2)
 *
 * Set the VCPU's CPUID.
 */
struct kvm_cpuid2 *vcpu_get_cpuid(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	struct kvm_cpuid2 *cpuid;
	int max_ent;
	int rc = -1;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	cpuid = allocate_kvm_cpuid2();
	max_ent = cpuid->nent;

	for (cpuid->nent = 1; cpuid->nent <= max_ent; cpuid->nent++) {
		rc = ioctl(vcpu->fd, KVM_GET_CPUID2, cpuid);
		if (!rc)
			break;

		TEST_ASSERT(rc == -1 && errno == E2BIG,
			    "KVM_GET_CPUID2 should either succeed or give E2BIG: %d %d",
			    rc, errno);
	}

	TEST_ASSERT(rc == 0, "KVM_GET_CPUID2 failed, rc: %i errno: %i",
		    rc, errno);

	return cpuid;
}



/*
 * Locate a cpuid entry.
 *
 * Input Args:
 *   function: The function of the cpuid entry to find.
 *   index: The index of the cpuid entry.
 *
 * Output Args: None
 *
 * Return: A pointer to the cpuid entry. Never returns NULL.
 */
struct kvm_cpuid_entry2 *
kvm_get_supported_cpuid_index(uint32_t function, uint32_t index)
{
	struct kvm_cpuid2 *cpuid;
	struct kvm_cpuid_entry2 *entry = NULL;
	int i;

	cpuid = kvm_get_supported_cpuid();
	for (i = 0; i < cpuid->nent; i++) {
		if (cpuid->entries[i].function == function &&
		    cpuid->entries[i].index == index) {
			entry = &cpuid->entries[i];
			break;
		}
	}

	TEST_ASSERT(entry, "Guest CPUID entry not found: (EAX=%x, ECX=%x).",
		    function, index);
	return entry;
}

/*
 * VM VCPU CPUID Set
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU id
 *   cpuid - The CPUID values to set.
 *
 * Output Args: None
 *
 * Return: void
 *
 * Set the VCPU's CPUID.
 */
void vcpu_set_cpuid(struct kvm_vm *vm,
		uint32_t vcpuid, struct kvm_cpuid2 *cpuid)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int rc;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);

	rc = ioctl(vcpu->fd, KVM_SET_CPUID2, cpuid);
	TEST_ASSERT(rc == 0, "KVM_SET_CPUID2 failed, rc: %i errno: %i",
		    rc, errno);

}

/*
 * VCPU Get MSR
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *   msr_index - Index of MSR
 *
 * Output Args: None
 *
 * Return: On success, value of the MSR. On failure a TEST_ASSERT is produced.
 *
 * Get value of MSR for VCPU.
 */
uint64_t vcpu_get_msr(struct kvm_vm *vm, uint32_t vcpuid, uint64_t msr_index)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	struct {
		struct kvm_msrs header;
		struct kvm_msr_entry entry;
	} buffer = {};
	int r;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);
	buffer.header.nmsrs = 1;
	buffer.entry.index = msr_index;
	r = ioctl(vcpu->fd, KVM_GET_MSRS, &buffer.header);
	TEST_ASSERT(r == 1, "KVM_GET_MSRS IOCTL failed,\n"
		"  rc: %i errno: %i", r, errno);

	return buffer.entry.data;
}

/*
 * _VCPU Set MSR
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *   msr_index - Index of MSR
 *   msr_value - New value of MSR
 *
 * Output Args: None
 *
 * Return: The result of KVM_SET_MSRS.
 *
 * Sets the value of an MSR for the given VCPU.
 */
int _vcpu_set_msr(struct kvm_vm *vm, uint32_t vcpuid, uint64_t msr_index,
		  uint64_t msr_value)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	struct {
		struct kvm_msrs header;
		struct kvm_msr_entry entry;
	} buffer = {};
	int r;

	TEST_ASSERT(vcpu != NULL, "vcpu not found, vcpuid: %u", vcpuid);
	memset(&buffer, 0, sizeof(buffer));
	buffer.header.nmsrs = 1;
	buffer.entry.index = msr_index;
	buffer.entry.data = msr_value;
	r = ioctl(vcpu->fd, KVM_SET_MSRS, &buffer.header);
	return r;
}

/*
 * VCPU Set MSR
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vcpuid - VCPU ID
 *   msr_index - Index of MSR
 *   msr_value - New value of MSR
 *
 * Output Args: None
 *
 * Return: On success, nothing. On failure a TEST_ASSERT is produced.
 *
 * Set value of MSR for VCPU.
 */
void vcpu_set_msr(struct kvm_vm *vm, uint32_t vcpuid, uint64_t msr_index,
	uint64_t msr_value)
{
	int r;

	r = _vcpu_set_msr(vm, vcpuid, msr_index, msr_value);
	TEST_ASSERT(r == 1, "KVM_SET_MSRS IOCTL failed,\n"
		"  rc: %i errno: %i", r, errno);
}

void vcpu_args_set(struct kvm_vm *vm, uint32_t vcpuid, unsigned int num, ...)
{
	va_list ap;
	struct kvm_regs regs;

	TEST_ASSERT(num >= 1 && num <= 6, "Unsupported number of args,\n"
		    "  num: %u\n",
		    num);

	va_start(ap, num);
	vcpu_regs_get(vm, vcpuid, &regs);

	if (num >= 1)
		regs.rdi = va_arg(ap, uint64_t);

	if (num >= 2)
		regs.rsi = va_arg(ap, uint64_t);

	if (num >= 3)
		regs.rdx = va_arg(ap, uint64_t);

	if (num >= 4)
		regs.rcx = va_arg(ap, uint64_t);

	if (num >= 5)
		regs.r8 = va_arg(ap, uint64_t);

	if (num >= 6)
		regs.r9 = va_arg(ap, uint64_t);

	vcpu_regs_set(vm, vcpuid, &regs);
	va_end(ap);
}

void vcpu_dump(FILE *stream, struct kvm_vm *vm, uint32_t vcpuid, uint8_t indent)
{
	struct kvm_regs regs;
	struct kvm_sregs sregs;

	fprintf(stream, "%*scpuid: %u\n", indent, "", vcpuid);

	fprintf(stream, "%*sregs:\n", indent + 2, "");
	vcpu_regs_get(vm, vcpuid, &regs);
	regs_dump(stream, &regs, indent + 4);

	fprintf(stream, "%*ssregs:\n", indent + 2, "");
	vcpu_sregs_get(vm, vcpuid, &sregs);
	sregs_dump(stream, &sregs, indent + 4);
}

struct kvm_x86_state {
	struct kvm_vcpu_events events;
	struct kvm_mp_state mp_state;
	struct kvm_regs regs;
	struct kvm_xsave xsave;
	struct kvm_xcrs xcrs;
	struct kvm_sregs sregs;
	struct kvm_debugregs debugregs;
	union {
		struct kvm_nested_state nested;
		char nested_[16384];
	};
	struct kvm_msrs msrs;
};

static int kvm_get_num_msrs_fd(int kvm_fd)
{
	struct kvm_msr_list nmsrs;
	int r;

	nmsrs.nmsrs = 0;
	r = ioctl(kvm_fd, KVM_GET_MSR_INDEX_LIST, &nmsrs);
	TEST_ASSERT(r == -1 && errno == E2BIG, "Unexpected result from KVM_GET_MSR_INDEX_LIST probe, r: %i",
		r);

	return nmsrs.nmsrs;
}

static int kvm_get_num_msrs(struct kvm_vm *vm)
{
	return kvm_get_num_msrs_fd(vm->kvm_fd);
}

struct kvm_msr_list *kvm_get_msr_index_list(void)
{
	struct kvm_msr_list *list;
	int nmsrs, r, kvm_fd;

	kvm_fd = open_kvm_dev_path_or_exit();

	nmsrs = kvm_get_num_msrs_fd(kvm_fd);
	list = malloc(sizeof(*list) + nmsrs * sizeof(list->indices[0]));
	list->nmsrs = nmsrs;
	r = ioctl(kvm_fd, KVM_GET_MSR_INDEX_LIST, list);
	close(kvm_fd);

	TEST_ASSERT(r == 0, "Unexpected result from KVM_GET_MSR_INDEX_LIST, r: %i",
		r);

	return list;
}

struct kvm_x86_state *vcpu_save_state(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	struct kvm_msr_list *list;
	struct kvm_x86_state *state;
	int nmsrs, r, i;
	static int nested_size = -1;

	if (nested_size == -1) {
		nested_size = kvm_check_cap(KVM_CAP_NESTED_STATE);
		TEST_ASSERT(nested_size <= sizeof(state->nested_),
			    "Nested state size too big, %i > %zi",
			    nested_size, sizeof(state->nested_));
	}

	/*
	 * When KVM exits to userspace with KVM_EXIT_IO, KVM guarantees
	 * guest state is consistent only after userspace re-enters the
	 * kernel with KVM_RUN.  Complete IO prior to migrating state
	 * to a new VM.
	 */
	vcpu_run_complete_io(vm, vcpuid);

	nmsrs = kvm_get_num_msrs(vm);
	list = malloc(sizeof(*list) + nmsrs * sizeof(list->indices[0]));
	list->nmsrs = nmsrs;
	r = ioctl(vm->kvm_fd, KVM_GET_MSR_INDEX_LIST, list);
        TEST_ASSERT(r == 0, "Unexpected result from KVM_GET_MSR_INDEX_LIST, r: %i",
                r);

	state = malloc(sizeof(*state) + nmsrs * sizeof(state->msrs.entries[0]));
	r = ioctl(vcpu->fd, KVM_GET_VCPU_EVENTS, &state->events);
        TEST_ASSERT(r == 0, "Unexpected result from KVM_GET_VCPU_EVENTS, r: %i",
                r);

	r = ioctl(vcpu->fd, KVM_GET_MP_STATE, &state->mp_state);
        TEST_ASSERT(r == 0, "Unexpected result from KVM_GET_MP_STATE, r: %i",
                r);

	r = ioctl(vcpu->fd, KVM_GET_REGS, &state->regs);
        TEST_ASSERT(r == 0, "Unexpected result from KVM_GET_REGS, r: %i",
                r);

	r = ioctl(vcpu->fd, KVM_GET_XSAVE, &state->xsave);
        TEST_ASSERT(r == 0, "Unexpected result from KVM_GET_XSAVE, r: %i",
                r);

	if (kvm_check_cap(KVM_CAP_XCRS)) {
		r = ioctl(vcpu->fd, KVM_GET_XCRS, &state->xcrs);
		TEST_ASSERT(r == 0, "Unexpected result from KVM_GET_XCRS, r: %i",
			    r);
	}

	r = ioctl(vcpu->fd, KVM_GET_SREGS, &state->sregs);
        TEST_ASSERT(r == 0, "Unexpected result from KVM_GET_SREGS, r: %i",
                r);

	if (nested_size) {
		state->nested.size = sizeof(state->nested_);
		r = ioctl(vcpu->fd, KVM_GET_NESTED_STATE, &state->nested);
		TEST_ASSERT(r == 0, "Unexpected result from KVM_GET_NESTED_STATE, r: %i",
			r);
		TEST_ASSERT(state->nested.size <= nested_size,
			"Nested state size too big, %i (KVM_CHECK_CAP gave %i)",
			state->nested.size, nested_size);
	} else
		state->nested.size = 0;

	state->msrs.nmsrs = nmsrs;
	for (i = 0; i < nmsrs; i++)
		state->msrs.entries[i].index = list->indices[i];
	r = ioctl(vcpu->fd, KVM_GET_MSRS, &state->msrs);
        TEST_ASSERT(r == nmsrs, "Unexpected result from KVM_GET_MSRS, r: %i (failed MSR was 0x%x)",
                r, r == nmsrs ? -1 : list->indices[r]);

	r = ioctl(vcpu->fd, KVM_GET_DEBUGREGS, &state->debugregs);
        TEST_ASSERT(r == 0, "Unexpected result from KVM_GET_DEBUGREGS, r: %i",
                r);

	free(list);
	return state;
}

void vcpu_load_state(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_x86_state *state)
{
	struct vcpu *vcpu = vcpu_find(vm, vcpuid);
	int r;

	r = ioctl(vcpu->fd, KVM_SET_XSAVE, &state->xsave);
        TEST_ASSERT(r == 0, "Unexpected result from KVM_SET_XSAVE, r: %i",
                r);

	if (kvm_check_cap(KVM_CAP_XCRS)) {
		r = ioctl(vcpu->fd, KVM_SET_XCRS, &state->xcrs);
		TEST_ASSERT(r == 0, "Unexpected result from KVM_SET_XCRS, r: %i",
			    r);
	}

	r = ioctl(vcpu->fd, KVM_SET_SREGS, &state->sregs);
        TEST_ASSERT(r == 0, "Unexpected result from KVM_SET_SREGS, r: %i",
                r);

	r = ioctl(vcpu->fd, KVM_SET_MSRS, &state->msrs);
        TEST_ASSERT(r == state->msrs.nmsrs, "Unexpected result from KVM_SET_MSRS, r: %i (failed at %x)",
                r, r == state->msrs.nmsrs ? -1 : state->msrs.entries[r].index);

	r = ioctl(vcpu->fd, KVM_SET_VCPU_EVENTS, &state->events);
        TEST_ASSERT(r == 0, "Unexpected result from KVM_SET_VCPU_EVENTS, r: %i",
                r);

	r = ioctl(vcpu->fd, KVM_SET_MP_STATE, &state->mp_state);
        TEST_ASSERT(r == 0, "Unexpected result from KVM_SET_MP_STATE, r: %i",
                r);

	r = ioctl(vcpu->fd, KVM_SET_DEBUGREGS, &state->debugregs);
        TEST_ASSERT(r == 0, "Unexpected result from KVM_SET_DEBUGREGS, r: %i",
                r);

	r = ioctl(vcpu->fd, KVM_SET_REGS, &state->regs);
        TEST_ASSERT(r == 0, "Unexpected result from KVM_SET_REGS, r: %i",
                r);

	if (state->nested.size) {
		r = ioctl(vcpu->fd, KVM_SET_NESTED_STATE, &state->nested);
		TEST_ASSERT(r == 0, "Unexpected result from KVM_SET_NESTED_STATE, r: %i",
			r);
	}
}

bool is_intel_cpu(void)
{
	int eax, ebx, ecx, edx;
	const uint32_t *chunk;
	const int leaf = 0;

	__asm__ __volatile__(
		"cpuid"
		: /* output */ "=a"(eax), "=b"(ebx),
		  "=c"(ecx), "=d"(edx)
		: /* input */ "0"(leaf), "2"(0));

	chunk = (const uint32_t *)("GenuineIntel");
	return (ebx == chunk[0] && edx == chunk[1] && ecx == chunk[2]);
}

uint32_t kvm_get_cpuid_max_basic(void)
{
	return kvm_get_supported_cpuid_entry(0)->eax;
}

uint32_t kvm_get_cpuid_max_extended(void)
{
	return kvm_get_supported_cpuid_entry(0x80000000)->eax;
}

void kvm_get_cpu_address_width(unsigned int *pa_bits, unsigned int *va_bits)
{
	struct kvm_cpuid_entry2 *entry;
	bool pae;

	/* SDM 4.1.4 */
	if (kvm_get_cpuid_max_extended() < 0x80000008) {
		pae = kvm_get_supported_cpuid_entry(1)->edx & (1 << 6);
		*pa_bits = pae ? 36 : 32;
		*va_bits = 32;
	} else {
		entry = kvm_get_supported_cpuid_entry(0x80000008);
		*pa_bits = entry->eax & 0xff;
		*va_bits = (entry->eax >> 8) & 0xff;
	}
}

struct idt_entry {
	uint16_t offset0;
	uint16_t selector;
	uint16_t ist : 3;
	uint16_t : 5;
	uint16_t type : 4;
	uint16_t : 1;
	uint16_t dpl : 2;
	uint16_t p : 1;
	uint16_t offset1;
	uint32_t offset2; uint32_t reserved;
};

static void set_idt_entry(struct kvm_vm *vm, int vector, unsigned long addr,
			  int dpl, unsigned short selector)
{
	struct idt_entry *base =
		(struct idt_entry *)addr_gva2hva(vm, vm->idt);
	struct idt_entry *e = &base[vector];

	memset(e, 0, sizeof(*e));
	e->offset0 = addr;
	e->selector = selector;
	e->ist = 0;
	e->type = 14;
	e->dpl = dpl;
	e->p = 1;
	e->offset1 = addr >> 16;
	e->offset2 = addr >> 32;
}

void kvm_exit_unexpected_vector(uint32_t value)
{
	ucall(UCALL_UNHANDLED, 1, value);
}

void route_exception(struct ex_regs *regs)
{
	typedef void(*handler)(struct ex_regs *);
	handler *handlers = (handler *)exception_handlers;

	if (handlers && handlers[regs->vector]) {
		handlers[regs->vector](regs);
		return;
	}

	kvm_exit_unexpected_vector(regs->vector);
}

void vm_init_descriptor_tables(struct kvm_vm *vm)
{
	extern void *idt_handlers;
	int i;

	vm->idt = vm_vaddr_alloc_page(vm);
	vm->handlers = vm_vaddr_alloc_page(vm);
	/* Handlers have the same address in both address spaces.*/
	for (i = 0; i < NUM_INTERRUPTS; i++)
		set_idt_entry(vm, i, (unsigned long)(&idt_handlers)[i], 0,
			DEFAULT_CODE_SELECTOR);
}

void vcpu_init_descriptor_tables(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct kvm_sregs sregs;

	vcpu_sregs_get(vm, vcpuid, &sregs);
	sregs.idt.base = vm->idt;
	sregs.idt.limit = NUM_INTERRUPTS * sizeof(struct idt_entry) - 1;
	sregs.gdt.base = vm->gdt;
	sregs.gdt.limit = getpagesize() - 1;
	kvm_seg_set_kernel_data_64bit(NULL, DEFAULT_DATA_SELECTOR, &sregs.gs);
	vcpu_sregs_set(vm, vcpuid, &sregs);
	*(vm_vaddr_t *)addr_gva2hva(vm, (vm_vaddr_t)(&exception_handlers)) = vm->handlers;
}

void vm_install_exception_handler(struct kvm_vm *vm, int vector,
			       void (*handler)(struct ex_regs *))
{
	vm_vaddr_t *handlers = (vm_vaddr_t *)addr_gva2hva(vm, vm->handlers);

	handlers[vector] = (vm_vaddr_t)handler;
}

void assert_on_unhandled_exception(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct ucall uc;

	if (get_ucall(vm, vcpuid, &uc) == UCALL_UNHANDLED) {
		uint64_t vector = uc.args[0];

		TEST_FAIL("Unexpected vectored event in guest (vector:0x%lx)",
			  vector);
	}
}

bool set_cpuid(struct kvm_cpuid2 *cpuid,
	       struct kvm_cpuid_entry2 *ent)
{
	int i;

	for (i = 0; i < cpuid->nent; i++) {
		struct kvm_cpuid_entry2 *cur = &cpuid->entries[i];

		if (cur->function != ent->function || cur->index != ent->index)
			continue;

		memcpy(cur, ent, sizeof(struct kvm_cpuid_entry2));
		return true;
	}

	return false;
}

uint64_t kvm_hypercall(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
		       uint64_t a3)
{
	uint64_t r;

	asm volatile("vmcall"
		     : "=a"(r)
		     : "b"(a0), "c"(a1), "d"(a2), "S"(a3));
	return r;
}

struct kvm_cpuid2 *kvm_get_supported_hv_cpuid(void)
{
	static struct kvm_cpuid2 *cpuid;
	int ret;
	int kvm_fd;

	if (cpuid)
		return cpuid;

	cpuid = allocate_kvm_cpuid2();
	kvm_fd = open_kvm_dev_path_or_exit();

	ret = ioctl(kvm_fd, KVM_GET_SUPPORTED_HV_CPUID, cpuid);
	TEST_ASSERT(ret == 0, "KVM_GET_SUPPORTED_HV_CPUID failed %d %d\n",
		    ret, errno);

	close(kvm_fd);
	return cpuid;
}

void vcpu_set_hv_cpuid(struct kvm_vm *vm, uint32_t vcpuid)
{
	static struct kvm_cpuid2 *cpuid_full;
	struct kvm_cpuid2 *cpuid_sys, *cpuid_hv;
	int i, nent = 0;

	if (!cpuid_full) {
		cpuid_sys = kvm_get_supported_cpuid();
		cpuid_hv = kvm_get_supported_hv_cpuid();

		cpuid_full = malloc(sizeof(*cpuid_full) +
				    (cpuid_sys->nent + cpuid_hv->nent) *
				    sizeof(struct kvm_cpuid_entry2));
		if (!cpuid_full) {
			perror("malloc");
			abort();
		}

		/* Need to skip KVM CPUID leaves 0x400000xx */
		for (i = 0; i < cpuid_sys->nent; i++) {
			if (cpuid_sys->entries[i].function >= 0x40000000 &&
			    cpuid_sys->entries[i].function < 0x40000100)
				continue;
			cpuid_full->entries[nent] = cpuid_sys->entries[i];
			nent++;
		}

		memcpy(&cpuid_full->entries[nent], cpuid_hv->entries,
		       cpuid_hv->nent * sizeof(struct kvm_cpuid_entry2));
		cpuid_full->nent = nent + cpuid_hv->nent;
	}

	vcpu_set_cpuid(vm, vcpuid, cpuid_full);
}

struct kvm_cpuid2 *vcpu_get_supported_hv_cpuid(struct kvm_vm *vm, uint32_t vcpuid)
{
	static struct kvm_cpuid2 *cpuid;

	cpuid = allocate_kvm_cpuid2();

	vcpu_ioctl(vm, vcpuid, KVM_GET_SUPPORTED_HV_CPUID, cpuid);

	return cpuid;
}
