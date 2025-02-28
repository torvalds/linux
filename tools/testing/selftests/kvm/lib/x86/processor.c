// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018, Google LLC.
 */

#include "linux/bitmap.h"
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "sev.h"

#ifndef NUM_INTERRUPTS
#define NUM_INTERRUPTS 256
#endif

#define KERNEL_CS	0x8
#define KERNEL_DS	0x10
#define KERNEL_TSS	0x18

vm_vaddr_t exception_handlers;
bool host_cpu_is_amd;
bool host_cpu_is_intel;
bool is_forced_emulation_enabled;
uint64_t guest_tsc_khz;

static void regs_dump(FILE *stream, struct kvm_regs *regs, uint8_t indent)
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

static void dtable_dump(FILE *stream, struct kvm_dtable *dtable,
			uint8_t indent)
{
	fprintf(stream, "%*sbase: 0x%.16llx limit: 0x%.4x "
		"padding: 0x%.4x 0x%.4x 0x%.4x\n",
		indent, "", dtable->base, dtable->limit,
		dtable->padding[0], dtable->padding[1], dtable->padding[2]);
}

static void sregs_dump(FILE *stream, struct kvm_sregs *sregs, uint8_t indent)
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

bool kvm_is_tdp_enabled(void)
{
	if (host_cpu_is_intel)
		return get_kvm_intel_param_bool("ept");
	else
		return get_kvm_amd_param_bool("npt");
}

void virt_arch_pgd_alloc(struct kvm_vm *vm)
{
	TEST_ASSERT(vm->mode == VM_MODE_PXXV48_4K, "Attempt to use "
		"unknown or unsupported guest mode, mode: 0x%x", vm->mode);

	/* If needed, create page map l4 table. */
	if (!vm->pgd_created) {
		vm->pgd = vm_alloc_page_table(vm);
		vm->pgd_created = true;
	}
}

static void *virt_get_pte(struct kvm_vm *vm, uint64_t *parent_pte,
			  uint64_t vaddr, int level)
{
	uint64_t pt_gpa = PTE_GET_PA(*parent_pte);
	uint64_t *page_table = addr_gpa2hva(vm, pt_gpa);
	int index = (vaddr >> PG_LEVEL_SHIFT(level)) & 0x1ffu;

	TEST_ASSERT((*parent_pte & PTE_PRESENT_MASK) || parent_pte == &vm->pgd,
		    "Parent PTE (level %d) not PRESENT for gva: 0x%08lx",
		    level + 1, vaddr);

	return &page_table[index];
}

static uint64_t *virt_create_upper_pte(struct kvm_vm *vm,
				       uint64_t *parent_pte,
				       uint64_t vaddr,
				       uint64_t paddr,
				       int current_level,
				       int target_level)
{
	uint64_t *pte = virt_get_pte(vm, parent_pte, vaddr, current_level);

	paddr = vm_untag_gpa(vm, paddr);

	if (!(*pte & PTE_PRESENT_MASK)) {
		*pte = PTE_PRESENT_MASK | PTE_WRITABLE_MASK;
		if (current_level == target_level)
			*pte |= PTE_LARGE_MASK | (paddr & PHYSICAL_PAGE_MASK);
		else
			*pte |= vm_alloc_page_table(vm) & PHYSICAL_PAGE_MASK;
	} else {
		/*
		 * Entry already present.  Assert that the caller doesn't want
		 * a hugepage at this level, and that there isn't a hugepage at
		 * this level.
		 */
		TEST_ASSERT(current_level != target_level,
			    "Cannot create hugepage at level: %u, vaddr: 0x%lx",
			    current_level, vaddr);
		TEST_ASSERT(!(*pte & PTE_LARGE_MASK),
			    "Cannot create page table at level: %u, vaddr: 0x%lx",
			    current_level, vaddr);
	}
	return pte;
}

void __virt_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr, int level)
{
	const uint64_t pg_size = PG_LEVEL_SIZE(level);
	uint64_t *pml4e, *pdpe, *pde;
	uint64_t *pte;

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
	TEST_ASSERT(vm_untag_gpa(vm, paddr) == paddr,
		    "Unexpected bits in paddr: %lx", paddr);

	/*
	 * Allocate upper level page tables, if not already present.  Return
	 * early if a hugepage was created.
	 */
	pml4e = virt_create_upper_pte(vm, &vm->pgd, vaddr, paddr, PG_LEVEL_512G, level);
	if (*pml4e & PTE_LARGE_MASK)
		return;

	pdpe = virt_create_upper_pte(vm, pml4e, vaddr, paddr, PG_LEVEL_1G, level);
	if (*pdpe & PTE_LARGE_MASK)
		return;

	pde = virt_create_upper_pte(vm, pdpe, vaddr, paddr, PG_LEVEL_2M, level);
	if (*pde & PTE_LARGE_MASK)
		return;

	/* Fill in page table entry. */
	pte = virt_get_pte(vm, pde, vaddr, PG_LEVEL_4K);
	TEST_ASSERT(!(*pte & PTE_PRESENT_MASK),
		    "PTE already present for 4k page at vaddr: 0x%lx", vaddr);
	*pte = PTE_PRESENT_MASK | PTE_WRITABLE_MASK | (paddr & PHYSICAL_PAGE_MASK);

	/*
	 * Neither SEV nor TDX supports shared page tables, so only the final
	 * leaf PTE needs manually set the C/S-bit.
	 */
	if (vm_is_gpa_protected(vm, paddr))
		*pte |= vm->arch.c_bit;
	else
		*pte |= vm->arch.s_bit;
}

void virt_arch_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr)
{
	__virt_pg_map(vm, vaddr, paddr, PG_LEVEL_4K);
}

void virt_map_level(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
		    uint64_t nr_bytes, int level)
{
	uint64_t pg_size = PG_LEVEL_SIZE(level);
	uint64_t nr_pages = nr_bytes / pg_size;
	int i;

	TEST_ASSERT(nr_bytes % pg_size == 0,
		    "Region size not aligned: nr_bytes: 0x%lx, page size: 0x%lx",
		    nr_bytes, pg_size);

	for (i = 0; i < nr_pages; i++) {
		__virt_pg_map(vm, vaddr, paddr, level);

		vaddr += pg_size;
		paddr += pg_size;
	}
}

static bool vm_is_target_pte(uint64_t *pte, int *level, int current_level)
{
	if (*pte & PTE_LARGE_MASK) {
		TEST_ASSERT(*level == PG_LEVEL_NONE ||
			    *level == current_level,
			    "Unexpected hugepage at level %d", current_level);
		*level = current_level;
	}

	return *level == current_level;
}

uint64_t *__vm_get_page_table_entry(struct kvm_vm *vm, uint64_t vaddr,
				    int *level)
{
	uint64_t *pml4e, *pdpe, *pde;

	TEST_ASSERT(!vm->arch.is_pt_protected,
		    "Walking page tables of protected guests is impossible");

	TEST_ASSERT(*level >= PG_LEVEL_NONE && *level < PG_LEVEL_NUM,
		    "Invalid PG_LEVEL_* '%d'", *level);

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

	pml4e = virt_get_pte(vm, &vm->pgd, vaddr, PG_LEVEL_512G);
	if (vm_is_target_pte(pml4e, level, PG_LEVEL_512G))
		return pml4e;

	pdpe = virt_get_pte(vm, pml4e, vaddr, PG_LEVEL_1G);
	if (vm_is_target_pte(pdpe, level, PG_LEVEL_1G))
		return pdpe;

	pde = virt_get_pte(vm, pdpe, vaddr, PG_LEVEL_2M);
	if (vm_is_target_pte(pde, level, PG_LEVEL_2M))
		return pde;

	return virt_get_pte(vm, pde, vaddr, PG_LEVEL_4K);
}

uint64_t *vm_get_page_table_entry(struct kvm_vm *vm, uint64_t vaddr)
{
	int level = PG_LEVEL_4K;

	return __vm_get_page_table_entry(vm, vaddr, &level);
}

void virt_arch_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent)
{
	uint64_t *pml4e, *pml4e_start;
	uint64_t *pdpe, *pdpe_start;
	uint64_t *pde, *pde_start;
	uint64_t *pte, *pte_start;

	if (!vm->pgd_created)
		return;

	fprintf(stream, "%*s                                          "
		"                no\n", indent, "");
	fprintf(stream, "%*s      index hvaddr         gpaddr         "
		"addr         w exec dirty\n",
		indent, "");
	pml4e_start = (uint64_t *) addr_gpa2hva(vm, vm->pgd);
	for (uint16_t n1 = 0; n1 <= 0x1ffu; n1++) {
		pml4e = &pml4e_start[n1];
		if (!(*pml4e & PTE_PRESENT_MASK))
			continue;
		fprintf(stream, "%*spml4e 0x%-3zx %p 0x%-12lx 0x%-10llx %u "
			" %u\n",
			indent, "",
			pml4e - pml4e_start, pml4e,
			addr_hva2gpa(vm, pml4e), PTE_GET_PFN(*pml4e),
			!!(*pml4e & PTE_WRITABLE_MASK), !!(*pml4e & PTE_NX_MASK));

		pdpe_start = addr_gpa2hva(vm, *pml4e & PHYSICAL_PAGE_MASK);
		for (uint16_t n2 = 0; n2 <= 0x1ffu; n2++) {
			pdpe = &pdpe_start[n2];
			if (!(*pdpe & PTE_PRESENT_MASK))
				continue;
			fprintf(stream, "%*spdpe  0x%-3zx %p 0x%-12lx 0x%-10llx "
				"%u  %u\n",
				indent, "",
				pdpe - pdpe_start, pdpe,
				addr_hva2gpa(vm, pdpe),
				PTE_GET_PFN(*pdpe), !!(*pdpe & PTE_WRITABLE_MASK),
				!!(*pdpe & PTE_NX_MASK));

			pde_start = addr_gpa2hva(vm, *pdpe & PHYSICAL_PAGE_MASK);
			for (uint16_t n3 = 0; n3 <= 0x1ffu; n3++) {
				pde = &pde_start[n3];
				if (!(*pde & PTE_PRESENT_MASK))
					continue;
				fprintf(stream, "%*spde   0x%-3zx %p "
					"0x%-12lx 0x%-10llx %u  %u\n",
					indent, "", pde - pde_start, pde,
					addr_hva2gpa(vm, pde),
					PTE_GET_PFN(*pde), !!(*pde & PTE_WRITABLE_MASK),
					!!(*pde & PTE_NX_MASK));

				pte_start = addr_gpa2hva(vm, *pde & PHYSICAL_PAGE_MASK);
				for (uint16_t n4 = 0; n4 <= 0x1ffu; n4++) {
					pte = &pte_start[n4];
					if (!(*pte & PTE_PRESENT_MASK))
						continue;
					fprintf(stream, "%*spte   0x%-3zx %p "
						"0x%-12lx 0x%-10llx %u  %u "
						"    %u    0x%-10lx\n",
						indent, "",
						pte - pte_start, pte,
						addr_hva2gpa(vm, pte),
						PTE_GET_PFN(*pte),
						!!(*pte & PTE_WRITABLE_MASK),
						!!(*pte & PTE_NX_MASK),
						!!(*pte & PTE_DIRTY_MASK),
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
	void *gdt = addr_gva2hva(vm, vm->arch.gdt);
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

static void kvm_seg_set_kernel_code_64bit(struct kvm_segment *segp)
{
	memset(segp, 0, sizeof(*segp));
	segp->selector = KERNEL_CS;
	segp->limit = 0xFFFFFFFFu;
	segp->s = 0x1; /* kTypeCodeData */
	segp->type = 0x08 | 0x01 | 0x02; /* kFlagCode | kFlagCodeAccessed
					  * | kFlagCodeReadable
					  */
	segp->g = true;
	segp->l = true;
	segp->present = 1;
}

static void kvm_seg_set_kernel_data_64bit(struct kvm_segment *segp)
{
	memset(segp, 0, sizeof(*segp));
	segp->selector = KERNEL_DS;
	segp->limit = 0xFFFFFFFFu;
	segp->s = 0x1; /* kTypeCodeData */
	segp->type = 0x00 | 0x01 | 0x02; /* kFlagData | kFlagDataAccessed
					  * | kFlagDataWritable
					  */
	segp->g = true;
	segp->present = true;
}

vm_paddr_t addr_arch_gva2gpa(struct kvm_vm *vm, vm_vaddr_t gva)
{
	int level = PG_LEVEL_NONE;
	uint64_t *pte = __vm_get_page_table_entry(vm, gva, &level);

	TEST_ASSERT(*pte & PTE_PRESENT_MASK,
		    "Leaf PTE not PRESENT for gva: 0x%08lx", gva);

	/*
	 * No need for a hugepage mask on the PTE, x86-64 requires the "unused"
	 * address bits to be zero.
	 */
	return vm_untag_gpa(vm, PTE_GET_PA(*pte)) | (gva & ~HUGEPAGE_MASK(level));
}

static void kvm_seg_set_tss_64bit(vm_vaddr_t base, struct kvm_segment *segp)
{
	memset(segp, 0, sizeof(*segp));
	segp->base = base;
	segp->limit = 0x67;
	segp->selector = KERNEL_TSS;
	segp->type = 0xb;
	segp->present = 1;
}

static void vcpu_init_sregs(struct kvm_vm *vm, struct kvm_vcpu *vcpu)
{
	struct kvm_sregs sregs;

	TEST_ASSERT_EQ(vm->mode, VM_MODE_PXXV48_4K);

	/* Set mode specific system register values. */
	vcpu_sregs_get(vcpu, &sregs);

	sregs.idt.base = vm->arch.idt;
	sregs.idt.limit = NUM_INTERRUPTS * sizeof(struct idt_entry) - 1;
	sregs.gdt.base = vm->arch.gdt;
	sregs.gdt.limit = getpagesize() - 1;

	sregs.cr0 = X86_CR0_PE | X86_CR0_NE | X86_CR0_PG;
	sregs.cr4 |= X86_CR4_PAE | X86_CR4_OSFXSR;
	if (kvm_cpu_has(X86_FEATURE_XSAVE))
		sregs.cr4 |= X86_CR4_OSXSAVE;
	sregs.efer |= (EFER_LME | EFER_LMA | EFER_NX);

	kvm_seg_set_unusable(&sregs.ldt);
	kvm_seg_set_kernel_code_64bit(&sregs.cs);
	kvm_seg_set_kernel_data_64bit(&sregs.ds);
	kvm_seg_set_kernel_data_64bit(&sregs.es);
	kvm_seg_set_kernel_data_64bit(&sregs.gs);
	kvm_seg_set_tss_64bit(vm->arch.tss, &sregs.tr);

	sregs.cr3 = vm->pgd;
	vcpu_sregs_set(vcpu, &sregs);
}

static void vcpu_init_xcrs(struct kvm_vm *vm, struct kvm_vcpu *vcpu)
{
	struct kvm_xcrs xcrs = {
		.nr_xcrs = 1,
		.xcrs[0].xcr = 0,
		.xcrs[0].value = kvm_cpu_supported_xcr0(),
	};

	if (!kvm_cpu_has(X86_FEATURE_XSAVE))
		return;

	vcpu_xcrs_set(vcpu, &xcrs);
}

static void set_idt_entry(struct kvm_vm *vm, int vector, unsigned long addr,
			  int dpl, unsigned short selector)
{
	struct idt_entry *base =
		(struct idt_entry *)addr_gva2hva(vm, vm->arch.idt);
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

static bool kvm_fixup_exception(struct ex_regs *regs)
{
	if (regs->r9 != KVM_EXCEPTION_MAGIC || regs->rip != regs->r10)
		return false;

	if (regs->vector == DE_VECTOR)
		return false;

	regs->rip = regs->r11;
	regs->r9 = regs->vector;
	regs->r10 = regs->error_code;
	return true;
}

void route_exception(struct ex_regs *regs)
{
	typedef void(*handler)(struct ex_regs *);
	handler *handlers = (handler *)exception_handlers;

	if (handlers && handlers[regs->vector]) {
		handlers[regs->vector](regs);
		return;
	}

	if (kvm_fixup_exception(regs))
		return;

	GUEST_FAIL("Unhandled exception '0x%lx' at guest RIP '0x%lx'",
		   regs->vector, regs->rip);
}

static void vm_init_descriptor_tables(struct kvm_vm *vm)
{
	extern void *idt_handlers;
	struct kvm_segment seg;
	int i;

	vm->arch.gdt = __vm_vaddr_alloc_page(vm, MEM_REGION_DATA);
	vm->arch.idt = __vm_vaddr_alloc_page(vm, MEM_REGION_DATA);
	vm->handlers = __vm_vaddr_alloc_page(vm, MEM_REGION_DATA);
	vm->arch.tss = __vm_vaddr_alloc_page(vm, MEM_REGION_DATA);

	/* Handlers have the same address in both address spaces.*/
	for (i = 0; i < NUM_INTERRUPTS; i++)
		set_idt_entry(vm, i, (unsigned long)(&idt_handlers)[i], 0, KERNEL_CS);

	*(vm_vaddr_t *)addr_gva2hva(vm, (vm_vaddr_t)(&exception_handlers)) = vm->handlers;

	kvm_seg_set_kernel_code_64bit(&seg);
	kvm_seg_fill_gdt_64bit(vm, &seg);

	kvm_seg_set_kernel_data_64bit(&seg);
	kvm_seg_fill_gdt_64bit(vm, &seg);

	kvm_seg_set_tss_64bit(vm->arch.tss, &seg);
	kvm_seg_fill_gdt_64bit(vm, &seg);
}

void vm_install_exception_handler(struct kvm_vm *vm, int vector,
			       void (*handler)(struct ex_regs *))
{
	vm_vaddr_t *handlers = (vm_vaddr_t *)addr_gva2hva(vm, vm->handlers);

	handlers[vector] = (vm_vaddr_t)handler;
}

void assert_on_unhandled_exception(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

	if (get_ucall(vcpu, &uc) == UCALL_ABORT)
		REPORT_GUEST_ASSERT(uc);
}

void kvm_arch_vm_post_create(struct kvm_vm *vm)
{
	int r;

	TEST_ASSERT(kvm_has_cap(KVM_CAP_GET_TSC_KHZ),
		    "Require KVM_GET_TSC_KHZ to provide udelay() to guest.");

	vm_create_irqchip(vm);
	vm_init_descriptor_tables(vm);

	sync_global_to_guest(vm, host_cpu_is_intel);
	sync_global_to_guest(vm, host_cpu_is_amd);
	sync_global_to_guest(vm, is_forced_emulation_enabled);

	if (vm->type == KVM_X86_SEV_VM || vm->type == KVM_X86_SEV_ES_VM) {
		struct kvm_sev_init init = { 0 };

		vm_sev_ioctl(vm, KVM_SEV_INIT2, &init);
	}

	r = __vm_ioctl(vm, KVM_GET_TSC_KHZ, NULL);
	TEST_ASSERT(r > 0, "KVM_GET_TSC_KHZ did not provide a valid TSC frequency.");
	guest_tsc_khz = r;
	sync_global_to_guest(vm, guest_tsc_khz);
}

void vcpu_arch_set_entry_point(struct kvm_vcpu *vcpu, void *guest_code)
{
	struct kvm_regs regs;

	vcpu_regs_get(vcpu, &regs);
	regs.rip = (unsigned long) guest_code;
	vcpu_regs_set(vcpu, &regs);
}

struct kvm_vcpu *vm_arch_vcpu_add(struct kvm_vm *vm, uint32_t vcpu_id)
{
	struct kvm_mp_state mp_state;
	struct kvm_regs regs;
	vm_vaddr_t stack_vaddr;
	struct kvm_vcpu *vcpu;

	stack_vaddr = __vm_vaddr_alloc(vm, DEFAULT_STACK_PGS * getpagesize(),
				       DEFAULT_GUEST_STACK_VADDR_MIN,
				       MEM_REGION_DATA);

	stack_vaddr += DEFAULT_STACK_PGS * getpagesize();

	/*
	 * Align stack to match calling sequence requirements in section "The
	 * Stack Frame" of the System V ABI AMD64 Architecture Processor
	 * Supplement, which requires the value (%rsp + 8) to be a multiple of
	 * 16 when control is transferred to the function entry point.
	 *
	 * If this code is ever used to launch a vCPU with 32-bit entry point it
	 * may need to subtract 4 bytes instead of 8 bytes.
	 */
	TEST_ASSERT(IS_ALIGNED(stack_vaddr, PAGE_SIZE),
		    "__vm_vaddr_alloc() did not provide a page-aligned address");
	stack_vaddr -= 8;

	vcpu = __vm_vcpu_add(vm, vcpu_id);
	vcpu_init_cpuid(vcpu, kvm_get_supported_cpuid());
	vcpu_init_sregs(vm, vcpu);
	vcpu_init_xcrs(vm, vcpu);

	/* Setup guest general purpose registers */
	vcpu_regs_get(vcpu, &regs);
	regs.rflags = regs.rflags | 0x2;
	regs.rsp = stack_vaddr;
	vcpu_regs_set(vcpu, &regs);

	/* Setup the MP state */
	mp_state.mp_state = 0;
	vcpu_mp_state_set(vcpu, &mp_state);

	/*
	 * Refresh CPUID after setting SREGS and XCR0, so that KVM's "runtime"
	 * updates to guest CPUID, e.g. for OSXSAVE and XSAVE state size, are
	 * reflected into selftests' vCPU CPUID cache, i.e. so that the cache
	 * is consistent with vCPU state.
	 */
	vcpu_get_cpuid(vcpu);
	return vcpu;
}

struct kvm_vcpu *vm_arch_vcpu_recreate(struct kvm_vm *vm, uint32_t vcpu_id)
{
	struct kvm_vcpu *vcpu = __vm_vcpu_add(vm, vcpu_id);

	vcpu_init_cpuid(vcpu, kvm_get_supported_cpuid());

	return vcpu;
}

void vcpu_arch_free(struct kvm_vcpu *vcpu)
{
	if (vcpu->cpuid)
		free(vcpu->cpuid);
}

/* Do not use kvm_supported_cpuid directly except for validity checks. */
static void *kvm_supported_cpuid;

const struct kvm_cpuid2 *kvm_get_supported_cpuid(void)
{
	int kvm_fd;

	if (kvm_supported_cpuid)
		return kvm_supported_cpuid;

	kvm_supported_cpuid = allocate_kvm_cpuid2(MAX_NR_CPUID_ENTRIES);
	kvm_fd = open_kvm_dev_path_or_exit();

	kvm_ioctl(kvm_fd, KVM_GET_SUPPORTED_CPUID,
		  (struct kvm_cpuid2 *)kvm_supported_cpuid);

	close(kvm_fd);
	return kvm_supported_cpuid;
}

static uint32_t __kvm_cpu_has(const struct kvm_cpuid2 *cpuid,
			      uint32_t function, uint32_t index,
			      uint8_t reg, uint8_t lo, uint8_t hi)
{
	const struct kvm_cpuid_entry2 *entry;
	int i;

	for (i = 0; i < cpuid->nent; i++) {
		entry = &cpuid->entries[i];

		/*
		 * The output registers in kvm_cpuid_entry2 are in alphabetical
		 * order, but kvm_x86_cpu_feature matches that mess, so yay
		 * pointer shenanigans!
		 */
		if (entry->function == function && entry->index == index)
			return ((&entry->eax)[reg] & GENMASK(hi, lo)) >> lo;
	}

	return 0;
}

bool kvm_cpuid_has(const struct kvm_cpuid2 *cpuid,
		   struct kvm_x86_cpu_feature feature)
{
	return __kvm_cpu_has(cpuid, feature.function, feature.index,
			     feature.reg, feature.bit, feature.bit);
}

uint32_t kvm_cpuid_property(const struct kvm_cpuid2 *cpuid,
			    struct kvm_x86_cpu_property property)
{
	return __kvm_cpu_has(cpuid, property.function, property.index,
			     property.reg, property.lo_bit, property.hi_bit);
}

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

	r = __kvm_ioctl(kvm_fd, KVM_GET_MSRS, &buffer.header);
	TEST_ASSERT(r == 1, KVM_IOCTL_ERROR(KVM_GET_MSRS, r));

	close(kvm_fd);
	return buffer.entry.data;
}

void __vm_xsave_require_permission(uint64_t xfeature, const char *name)
{
	int kvm_fd;
	u64 bitmask;
	long rc;
	struct kvm_device_attr attr = {
		.group = 0,
		.attr = KVM_X86_XCOMP_GUEST_SUPP,
		.addr = (unsigned long) &bitmask,
	};

	TEST_ASSERT(!kvm_supported_cpuid,
		    "kvm_get_supported_cpuid() cannot be used before ARCH_REQ_XCOMP_GUEST_PERM");

	TEST_ASSERT(is_power_of_2(xfeature),
		    "Dynamic XFeatures must be enabled one at a time");

	kvm_fd = open_kvm_dev_path_or_exit();
	rc = __kvm_ioctl(kvm_fd, KVM_GET_DEVICE_ATTR, &attr);
	close(kvm_fd);

	if (rc == -1 && (errno == ENXIO || errno == EINVAL))
		__TEST_REQUIRE(0, "KVM_X86_XCOMP_GUEST_SUPP not supported");

	TEST_ASSERT(rc == 0, "KVM_GET_DEVICE_ATTR(0, KVM_X86_XCOMP_GUEST_SUPP) error: %ld", rc);

	__TEST_REQUIRE(bitmask & xfeature,
		       "Required XSAVE feature '%s' not supported", name);

	TEST_REQUIRE(!syscall(SYS_arch_prctl, ARCH_REQ_XCOMP_GUEST_PERM, ilog2(xfeature)));

	rc = syscall(SYS_arch_prctl, ARCH_GET_XCOMP_GUEST_PERM, &bitmask);
	TEST_ASSERT(rc == 0, "prctl(ARCH_GET_XCOMP_GUEST_PERM) error: %ld", rc);
	TEST_ASSERT(bitmask & xfeature,
		    "'%s' (0x%lx) not permitted after prctl(ARCH_REQ_XCOMP_GUEST_PERM) permitted=0x%lx",
		    name, xfeature, bitmask);
}

void vcpu_init_cpuid(struct kvm_vcpu *vcpu, const struct kvm_cpuid2 *cpuid)
{
	TEST_ASSERT(cpuid != vcpu->cpuid, "@cpuid can't be the vCPU's CPUID");

	/* Allow overriding the default CPUID. */
	if (vcpu->cpuid && vcpu->cpuid->nent < cpuid->nent) {
		free(vcpu->cpuid);
		vcpu->cpuid = NULL;
	}

	if (!vcpu->cpuid)
		vcpu->cpuid = allocate_kvm_cpuid2(cpuid->nent);

	memcpy(vcpu->cpuid, cpuid, kvm_cpuid2_size(cpuid->nent));
	vcpu_set_cpuid(vcpu);
}

void vcpu_set_cpuid_property(struct kvm_vcpu *vcpu,
			     struct kvm_x86_cpu_property property,
			     uint32_t value)
{
	struct kvm_cpuid_entry2 *entry;

	entry = __vcpu_get_cpuid_entry(vcpu, property.function, property.index);

	(&entry->eax)[property.reg] &= ~GENMASK(property.hi_bit, property.lo_bit);
	(&entry->eax)[property.reg] |= value << property.lo_bit;

	vcpu_set_cpuid(vcpu);

	/* Sanity check that @value doesn't exceed the bounds in any way. */
	TEST_ASSERT_EQ(kvm_cpuid_property(vcpu->cpuid, property), value);
}

void vcpu_clear_cpuid_entry(struct kvm_vcpu *vcpu, uint32_t function)
{
	struct kvm_cpuid_entry2 *entry = vcpu_get_cpuid_entry(vcpu, function);

	entry->eax = 0;
	entry->ebx = 0;
	entry->ecx = 0;
	entry->edx = 0;
	vcpu_set_cpuid(vcpu);
}

void vcpu_set_or_clear_cpuid_feature(struct kvm_vcpu *vcpu,
				     struct kvm_x86_cpu_feature feature,
				     bool set)
{
	struct kvm_cpuid_entry2 *entry;
	u32 *reg;

	entry = __vcpu_get_cpuid_entry(vcpu, feature.function, feature.index);
	reg = (&entry->eax) + feature.reg;

	if (set)
		*reg |= BIT(feature.bit);
	else
		*reg &= ~BIT(feature.bit);

	vcpu_set_cpuid(vcpu);
}

uint64_t vcpu_get_msr(struct kvm_vcpu *vcpu, uint64_t msr_index)
{
	struct {
		struct kvm_msrs header;
		struct kvm_msr_entry entry;
	} buffer = {};

	buffer.header.nmsrs = 1;
	buffer.entry.index = msr_index;

	vcpu_msrs_get(vcpu, &buffer.header);

	return buffer.entry.data;
}

int _vcpu_set_msr(struct kvm_vcpu *vcpu, uint64_t msr_index, uint64_t msr_value)
{
	struct {
		struct kvm_msrs header;
		struct kvm_msr_entry entry;
	} buffer = {};

	memset(&buffer, 0, sizeof(buffer));
	buffer.header.nmsrs = 1;
	buffer.entry.index = msr_index;
	buffer.entry.data = msr_value;

	return __vcpu_ioctl(vcpu, KVM_SET_MSRS, &buffer.header);
}

void vcpu_args_set(struct kvm_vcpu *vcpu, unsigned int num, ...)
{
	va_list ap;
	struct kvm_regs regs;

	TEST_ASSERT(num >= 1 && num <= 6, "Unsupported number of args,\n"
		    "  num: %u",
		    num);

	va_start(ap, num);
	vcpu_regs_get(vcpu, &regs);

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

	vcpu_regs_set(vcpu, &regs);
	va_end(ap);
}

void vcpu_arch_dump(FILE *stream, struct kvm_vcpu *vcpu, uint8_t indent)
{
	struct kvm_regs regs;
	struct kvm_sregs sregs;

	fprintf(stream, "%*svCPU ID: %u\n", indent, "", vcpu->id);

	fprintf(stream, "%*sregs:\n", indent + 2, "");
	vcpu_regs_get(vcpu, &regs);
	regs_dump(stream, &regs, indent + 4);

	fprintf(stream, "%*ssregs:\n", indent + 2, "");
	vcpu_sregs_get(vcpu, &sregs);
	sregs_dump(stream, &sregs, indent + 4);
}

static struct kvm_msr_list *__kvm_get_msr_index_list(bool feature_msrs)
{
	struct kvm_msr_list *list;
	struct kvm_msr_list nmsrs;
	int kvm_fd, r;

	kvm_fd = open_kvm_dev_path_or_exit();

	nmsrs.nmsrs = 0;
	if (!feature_msrs)
		r = __kvm_ioctl(kvm_fd, KVM_GET_MSR_INDEX_LIST, &nmsrs);
	else
		r = __kvm_ioctl(kvm_fd, KVM_GET_MSR_FEATURE_INDEX_LIST, &nmsrs);

	TEST_ASSERT(r == -1 && errno == E2BIG,
		    "Expected -E2BIG, got rc: %i errno: %i (%s)",
		    r, errno, strerror(errno));

	list = malloc(sizeof(*list) + nmsrs.nmsrs * sizeof(list->indices[0]));
	TEST_ASSERT(list, "-ENOMEM when allocating MSR index list");
	list->nmsrs = nmsrs.nmsrs;

	if (!feature_msrs)
		kvm_ioctl(kvm_fd, KVM_GET_MSR_INDEX_LIST, list);
	else
		kvm_ioctl(kvm_fd, KVM_GET_MSR_FEATURE_INDEX_LIST, list);
	close(kvm_fd);

	TEST_ASSERT(list->nmsrs == nmsrs.nmsrs,
		    "Number of MSRs in list changed, was %d, now %d",
		    nmsrs.nmsrs, list->nmsrs);
	return list;
}

const struct kvm_msr_list *kvm_get_msr_index_list(void)
{
	static const struct kvm_msr_list *list;

	if (!list)
		list = __kvm_get_msr_index_list(false);
	return list;
}


const struct kvm_msr_list *kvm_get_feature_msr_index_list(void)
{
	static const struct kvm_msr_list *list;

	if (!list)
		list = __kvm_get_msr_index_list(true);
	return list;
}

bool kvm_msr_is_in_save_restore_list(uint32_t msr_index)
{
	const struct kvm_msr_list *list = kvm_get_msr_index_list();
	int i;

	for (i = 0; i < list->nmsrs; ++i) {
		if (list->indices[i] == msr_index)
			return true;
	}

	return false;
}

static void vcpu_save_xsave_state(struct kvm_vcpu *vcpu,
				  struct kvm_x86_state *state)
{
	int size = vm_check_cap(vcpu->vm, KVM_CAP_XSAVE2);

	if (size) {
		state->xsave = malloc(size);
		vcpu_xsave2_get(vcpu, state->xsave);
	} else {
		state->xsave = malloc(sizeof(struct kvm_xsave));
		vcpu_xsave_get(vcpu, state->xsave);
	}
}

struct kvm_x86_state *vcpu_save_state(struct kvm_vcpu *vcpu)
{
	const struct kvm_msr_list *msr_list = kvm_get_msr_index_list();
	struct kvm_x86_state *state;
	int i;

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
	vcpu_run_complete_io(vcpu);

	state = malloc(sizeof(*state) + msr_list->nmsrs * sizeof(state->msrs.entries[0]));
	TEST_ASSERT(state, "-ENOMEM when allocating kvm state");

	vcpu_events_get(vcpu, &state->events);
	vcpu_mp_state_get(vcpu, &state->mp_state);
	vcpu_regs_get(vcpu, &state->regs);
	vcpu_save_xsave_state(vcpu, state);

	if (kvm_has_cap(KVM_CAP_XCRS))
		vcpu_xcrs_get(vcpu, &state->xcrs);

	vcpu_sregs_get(vcpu, &state->sregs);

	if (nested_size) {
		state->nested.size = sizeof(state->nested_);

		vcpu_nested_state_get(vcpu, &state->nested);
		TEST_ASSERT(state->nested.size <= nested_size,
			    "Nested state size too big, %i (KVM_CHECK_CAP gave %i)",
			    state->nested.size, nested_size);
	} else {
		state->nested.size = 0;
	}

	state->msrs.nmsrs = msr_list->nmsrs;
	for (i = 0; i < msr_list->nmsrs; i++)
		state->msrs.entries[i].index = msr_list->indices[i];
	vcpu_msrs_get(vcpu, &state->msrs);

	vcpu_debugregs_get(vcpu, &state->debugregs);

	return state;
}

void vcpu_load_state(struct kvm_vcpu *vcpu, struct kvm_x86_state *state)
{
	vcpu_sregs_set(vcpu, &state->sregs);
	vcpu_msrs_set(vcpu, &state->msrs);

	if (kvm_has_cap(KVM_CAP_XCRS))
		vcpu_xcrs_set(vcpu, &state->xcrs);

	vcpu_xsave_set(vcpu,  state->xsave);
	vcpu_events_set(vcpu, &state->events);
	vcpu_mp_state_set(vcpu, &state->mp_state);
	vcpu_debugregs_set(vcpu, &state->debugregs);
	vcpu_regs_set(vcpu, &state->regs);

	if (state->nested.size)
		vcpu_nested_state_set(vcpu, &state->nested);
}

void kvm_x86_state_cleanup(struct kvm_x86_state *state)
{
	free(state->xsave);
	free(state);
}

void kvm_get_cpu_address_width(unsigned int *pa_bits, unsigned int *va_bits)
{
	if (!kvm_cpu_has_p(X86_PROPERTY_MAX_PHY_ADDR)) {
		*pa_bits = kvm_cpu_has(X86_FEATURE_PAE) ? 36 : 32;
		*va_bits = 32;
	} else {
		*pa_bits = kvm_cpu_property(X86_PROPERTY_MAX_PHY_ADDR);
		*va_bits = kvm_cpu_property(X86_PROPERTY_MAX_VIRT_ADDR);
	}
}

void kvm_init_vm_address_properties(struct kvm_vm *vm)
{
	if (vm->type == KVM_X86_SEV_VM || vm->type == KVM_X86_SEV_ES_VM) {
		vm->arch.sev_fd = open_sev_dev_path_or_exit();
		vm->arch.c_bit = BIT_ULL(this_cpu_property(X86_PROPERTY_SEV_C_BIT));
		vm->gpa_tag_mask = vm->arch.c_bit;
	} else {
		vm->arch.sev_fd = -1;
	}
}

const struct kvm_cpuid_entry2 *get_cpuid_entry(const struct kvm_cpuid2 *cpuid,
					       uint32_t function, uint32_t index)
{
	int i;

	for (i = 0; i < cpuid->nent; i++) {
		if (cpuid->entries[i].function == function &&
		    cpuid->entries[i].index == index)
			return &cpuid->entries[i];
	}

	TEST_FAIL("CPUID function 0x%x index 0x%x not found ", function, index);

	return NULL;
}

#define X86_HYPERCALL(inputs...)					\
({									\
	uint64_t r;							\
									\
	asm volatile("test %[use_vmmcall], %[use_vmmcall]\n\t"		\
		     "jnz 1f\n\t"					\
		     "vmcall\n\t"					\
		     "jmp 2f\n\t"					\
		     "1: vmmcall\n\t"					\
		     "2:"						\
		     : "=a"(r)						\
		     : [use_vmmcall] "r" (host_cpu_is_amd), inputs);	\
									\
	r;								\
})

uint64_t kvm_hypercall(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
		       uint64_t a3)
{
	return X86_HYPERCALL("a"(nr), "b"(a0), "c"(a1), "d"(a2), "S"(a3));
}

uint64_t __xen_hypercall(uint64_t nr, uint64_t a0, void *a1)
{
	return X86_HYPERCALL("a"(nr), "D"(a0), "S"(a1));
}

void xen_hypercall(uint64_t nr, uint64_t a0, void *a1)
{
	GUEST_ASSERT(!__xen_hypercall(nr, a0, a1));
}

unsigned long vm_compute_max_gfn(struct kvm_vm *vm)
{
	const unsigned long num_ht_pages = 12 << (30 - vm->page_shift); /* 12 GiB */
	unsigned long ht_gfn, max_gfn, max_pfn;
	uint8_t maxphyaddr, guest_maxphyaddr;

	/*
	 * Use "guest MAXPHYADDR" from KVM if it's available.  Guest MAXPHYADDR
	 * enumerates the max _mappable_ GPA, which can be less than the raw
	 * MAXPHYADDR, e.g. if MAXPHYADDR=52, KVM is using TDP, and the CPU
	 * doesn't support 5-level TDP.
	 */
	guest_maxphyaddr = kvm_cpu_property(X86_PROPERTY_GUEST_MAX_PHY_ADDR);
	guest_maxphyaddr = guest_maxphyaddr ?: vm->pa_bits;
	TEST_ASSERT(guest_maxphyaddr <= vm->pa_bits,
		    "Guest MAXPHYADDR should never be greater than raw MAXPHYADDR");

	max_gfn = (1ULL << (guest_maxphyaddr - vm->page_shift)) - 1;

	/* Avoid reserved HyperTransport region on AMD processors.  */
	if (!host_cpu_is_amd)
		return max_gfn;

	/* On parts with <40 physical address bits, the area is fully hidden */
	if (vm->pa_bits < 40)
		return max_gfn;

	/* Before family 17h, the HyperTransport area is just below 1T.  */
	ht_gfn = (1 << 28) - num_ht_pages;
	if (this_cpu_family() < 0x17)
		goto done;

	/*
	 * Otherwise it's at the top of the physical address space, possibly
	 * reduced due to SME by bits 11:6 of CPUID[0x8000001f].EBX.  Use
	 * the old conservative value if MAXPHYADDR is not enumerated.
	 */
	if (!this_cpu_has_p(X86_PROPERTY_MAX_PHY_ADDR))
		goto done;

	maxphyaddr = this_cpu_property(X86_PROPERTY_MAX_PHY_ADDR);
	max_pfn = (1ULL << (maxphyaddr - vm->page_shift)) - 1;

	if (this_cpu_has_p(X86_PROPERTY_PHYS_ADDR_REDUCTION))
		max_pfn >>= this_cpu_property(X86_PROPERTY_PHYS_ADDR_REDUCTION);

	ht_gfn = max_pfn - num_ht_pages;
done:
	return min(max_gfn, ht_gfn - 1);
}

/* Returns true if kvm_intel was loaded with unrestricted_guest=1. */
bool vm_is_unrestricted_guest(struct kvm_vm *vm)
{
	/* Ensure that a KVM vendor-specific module is loaded. */
	if (vm == NULL)
		close(open_kvm_dev_path_or_exit());

	return get_kvm_intel_param_bool("unrestricted_guest");
}

void kvm_selftest_arch_init(void)
{
	host_cpu_is_intel = this_cpu_is_intel();
	host_cpu_is_amd = this_cpu_is_amd();
	is_forced_emulation_enabled = kvm_is_forced_emulation_enabled();
}

bool sys_clocksource_is_based_on_tsc(void)
{
	char *clk_name = sys_get_cur_clocksource();
	bool ret = !strcmp(clk_name, "tsc\n") ||
		   !strcmp(clk_name, "hyperv_clocksource_tsc_page\n");

	free(clk_name);

	return ret;
}
