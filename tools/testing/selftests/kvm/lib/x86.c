/*
 * tools/testing/selftests/kvm/lib/x86.c
 *
 * Copyright (C) 2018, Google LLC.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */

#define _GNU_SOURCE /* for program_invocation_name */

#include "test_util.h"
#include "kvm_util.h"
#include "kvm_util_internal.h"
#include "x86.h"

/* Minimum physical address used for virtual translation tables. */
#define KVM_GUEST_PAGE_TABLE_MIN_PADDR 0x180000

/* Virtual translation table structure declarations */
struct pageMapL4Entry {
	uint64_t present:1;
	uint64_t writable:1;
	uint64_t user:1;
	uint64_t write_through:1;
	uint64_t cache_disable:1;
	uint64_t accessed:1;
	uint64_t ignored_06:1;
	uint64_t page_size:1;
	uint64_t ignored_11_08:4;
	uint64_t address:40;
	uint64_t ignored_62_52:11;
	uint64_t execute_disable:1;
};

struct pageDirectoryPointerEntry {
	uint64_t present:1;
	uint64_t writable:1;
	uint64_t user:1;
	uint64_t write_through:1;
	uint64_t cache_disable:1;
	uint64_t accessed:1;
	uint64_t ignored_06:1;
	uint64_t page_size:1;
	uint64_t ignored_11_08:4;
	uint64_t address:40;
	uint64_t ignored_62_52:11;
	uint64_t execute_disable:1;
};

struct pageDirectoryEntry {
	uint64_t present:1;
	uint64_t writable:1;
	uint64_t user:1;
	uint64_t write_through:1;
	uint64_t cache_disable:1;
	uint64_t accessed:1;
	uint64_t ignored_06:1;
	uint64_t page_size:1;
	uint64_t ignored_11_08:4;
	uint64_t address:40;
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
	uint64_t address:40;
	uint64_t ignored_62_52:11;
	uint64_t execute_disable:1;
};

/* Register Dump
 *
 * Input Args:
 *   indent - Left margin indent amount
 *   regs - register
 *
 * Output Args:
 *   stream - Output FILE stream
 *
 * Return: None
 *
 * Dumps the state of the registers given by regs, to the FILE stream
 * given by steam.
 */
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

/* Segment Dump
 *
 * Input Args:
 *   indent - Left margin indent amount
 *   segment - KVM segment
 *
 * Output Args:
 *   stream - Output FILE stream
 *
 * Return: None
 *
 * Dumps the state of the KVM segment given by segment, to the FILE stream
 * given by steam.
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

/* dtable Dump
 *
 * Input Args:
 *   indent - Left margin indent amount
 *   dtable - KVM dtable
 *
 * Output Args:
 *   stream - Output FILE stream
 *
 * Return: None
 *
 * Dumps the state of the KVM dtable given by dtable, to the FILE stream
 * given by steam.
 */
static void dtable_dump(FILE *stream, struct kvm_dtable *dtable,
			uint8_t indent)
{
	fprintf(stream, "%*sbase: 0x%.16llx limit: 0x%.4x "
		"padding: 0x%.4x 0x%.4x 0x%.4x\n",
		indent, "", dtable->base, dtable->limit,
		dtable->padding[0], dtable->padding[1], dtable->padding[2]);
}

/* System Register Dump
 *
 * Input Args:
 *   indent - Left margin indent amount
 *   sregs - System registers
 *
 * Output Args:
 *   stream - Output FILE stream
 *
 * Return: None
 *
 * Dumps the state of the system registers given by sregs, to the FILE stream
 * given by steam.
 */
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

void virt_pgd_alloc(struct kvm_vm *vm, uint32_t pgd_memslot)
{
	int rc;

	TEST_ASSERT(vm->mode == VM_MODE_FLAT48PG, "Attempt to use "
		"unknown or unsupported guest mode, mode: 0x%x", vm->mode);

	/* If needed, create page map l4 table. */
	if (!vm->pgd_created) {
		vm_paddr_t paddr = vm_phy_page_alloc(vm,
			KVM_GUEST_PAGE_TABLE_MIN_PADDR, pgd_memslot);
		vm->pgd = paddr;

		/* Set pointer to pgd tables in all the VCPUs that
		 * have already been created.  Future VCPUs will have
		 * the value set as each one is created.
		 */
		for (struct vcpu *vcpu = vm->vcpu_head; vcpu;
			vcpu = vcpu->next) {
			struct kvm_sregs sregs;

			/* Obtain the current system register settings */
			vcpu_sregs_get(vm, vcpu->id, &sregs);

			/* Set and store the pointer to the start of the
			 * pgd tables.
			 */
			sregs.cr3 = vm->pgd;
			vcpu_sregs_set(vm, vcpu->id, &sregs);
		}

		vm->pgd_created = true;
	}
}

/* VM Virtual Page Map
 *
 * Input Args:
 *   vm - Virtual Machine
 *   vaddr - VM Virtual Address
 *   paddr - VM Physical Address
 *   pgd_memslot - Memory region slot for new virtual translation tables
 *
 * Output Args: None
 *
 * Return: None
 *
 * Within the VM given by vm, creates a virtual translation for the page
 * starting at vaddr to the page starting at paddr.
 */
void virt_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
	uint32_t pgd_memslot)
{
	uint16_t index[4];
	struct pageMapL4Entry *pml4e;

	TEST_ASSERT(vm->mode == VM_MODE_FLAT48PG, "Attempt to use "
		"unknown or unsupported guest mode, mode: 0x%x", vm->mode);

	TEST_ASSERT((vaddr % vm->page_size) == 0,
		"Virtual address not on page boundary,\n"
		"  vaddr: 0x%lx vm->page_size: 0x%x",
		vaddr, vm->page_size);
	TEST_ASSERT(sparsebit_is_set(vm->vpages_valid,
		(vaddr >> vm->page_shift)),
		"Invalid virtual address, vaddr: 0x%lx",
		vaddr);
	TEST_ASSERT((paddr % vm->page_size) == 0,
		"Physical address not on page boundary,\n"
		"  paddr: 0x%lx vm->page_size: 0x%x",
		paddr, vm->page_size);
	TEST_ASSERT((paddr >> vm->page_shift) <= vm->max_gfn,
		"Physical address beyond beyond maximum supported,\n"
		"  paddr: 0x%lx vm->max_gfn: 0x%lx vm->page_size: 0x%x",
		paddr, vm->max_gfn, vm->page_size);

	index[0] = (vaddr >> 12) & 0x1ffu;
	index[1] = (vaddr >> 21) & 0x1ffu;
	index[2] = (vaddr >> 30) & 0x1ffu;
	index[3] = (vaddr >> 39) & 0x1ffu;

	/* Allocate page directory pointer table if not present. */
	pml4e = addr_gpa2hva(vm, vm->pgd);
	if (!pml4e[index[3]].present) {
		pml4e[index[3]].address = vm_phy_page_alloc(vm,
			KVM_GUEST_PAGE_TABLE_MIN_PADDR, pgd_memslot)
			>> vm->page_shift;
		pml4e[index[3]].writable = true;
		pml4e[index[3]].present = true;
	}

	/* Allocate page directory table if not present. */
	struct pageDirectoryPointerEntry *pdpe;
	pdpe = addr_gpa2hva(vm, pml4e[index[3]].address * vm->page_size);
	if (!pdpe[index[2]].present) {
		pdpe[index[2]].address = vm_phy_page_alloc(vm,
			KVM_GUEST_PAGE_TABLE_MIN_PADDR, pgd_memslot)
			>> vm->page_shift;
		pdpe[index[2]].writable = true;
		pdpe[index[2]].present = true;
	}

	/* Allocate page table if not present. */
	struct pageDirectoryEntry *pde;
	pde = addr_gpa2hva(vm, pdpe[index[2]].address * vm->page_size);
	if (!pde[index[1]].present) {
		pde[index[1]].address = vm_phy_page_alloc(vm,
			KVM_GUEST_PAGE_TABLE_MIN_PADDR, pgd_memslot)
			>> vm->page_shift;
		pde[index[1]].writable = true;
		pde[index[1]].present = true;
	}

	/* Fill in page table entry. */
	struct pageTableEntry *pte;
	pte = addr_gpa2hva(vm, pde[index[1]].address * vm->page_size);
	pte[index[0]].address = paddr >> vm->page_shift;
	pte[index[0]].writable = true;
	pte[index[0]].present = 1;
}

/* Virtual Translation Tables Dump
 *
 * Input Args:
 *   vm - Virtual Machine
 *   indent - Left margin indent amount
 *
 * Output Args:
 *   stream - Output FILE stream
 *
 * Return: None
 *
 * Dumps to the FILE stream given by stream, the contents of all the
 * virtual translation tables for the VM given by vm.
 */
void virt_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent)
{
	struct pageMapL4Entry *pml4e, *pml4e_start;
	struct pageDirectoryPointerEntry *pdpe, *pdpe_start;
	struct pageDirectoryEntry *pde, *pde_start;
	struct pageTableEntry *pte, *pte_start;

	if (!vm->pgd_created)
		return;

	fprintf(stream, "%*s                                          "
		"                no\n", indent, "");
	fprintf(stream, "%*s      index hvaddr         gpaddr         "
		"addr         w exec dirty\n",
		indent, "");
	pml4e_start = (struct pageMapL4Entry *) addr_gpa2hva(vm,
		vm->pgd);
	for (uint16_t n1 = 0; n1 <= 0x1ffu; n1++) {
		pml4e = &pml4e_start[n1];
		if (!pml4e->present)
			continue;
		fprintf(stream, "%*spml4e 0x%-3zx %p 0x%-12lx 0x%-10lx %u "
			" %u\n",
			indent, "",
			pml4e - pml4e_start, pml4e,
			addr_hva2gpa(vm, pml4e), (uint64_t) pml4e->address,
			pml4e->writable, pml4e->execute_disable);

		pdpe_start = addr_gpa2hva(vm, pml4e->address
			* vm->page_size);
		for (uint16_t n2 = 0; n2 <= 0x1ffu; n2++) {
			pdpe = &pdpe_start[n2];
			if (!pdpe->present)
				continue;
			fprintf(stream, "%*spdpe  0x%-3zx %p 0x%-12lx 0x%-10lx "
				"%u  %u\n",
				indent, "",
				pdpe - pdpe_start, pdpe,
				addr_hva2gpa(vm, pdpe),
				(uint64_t) pdpe->address, pdpe->writable,
				pdpe->execute_disable);

			pde_start = addr_gpa2hva(vm,
				pdpe->address * vm->page_size);
			for (uint16_t n3 = 0; n3 <= 0x1ffu; n3++) {
				pde = &pde_start[n3];
				if (!pde->present)
					continue;
				fprintf(stream, "%*spde   0x%-3zx %p "
					"0x%-12lx 0x%-10lx %u  %u\n",
					indent, "", pde - pde_start, pde,
					addr_hva2gpa(vm, pde),
					(uint64_t) pde->address, pde->writable,
					pde->execute_disable);

				pte_start = addr_gpa2hva(vm,
					pde->address * vm->page_size);
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
						(uint64_t) pte->address,
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

/* Set Unusable Segment
 *
 * Input Args: None
 *
 * Output Args:
 *   segp - Pointer to segment register
 *
 * Return: None
 *
 * Sets the segment register pointed to by segp to an unusable state.
 */
static void kvm_seg_set_unusable(struct kvm_segment *segp)
{
	memset(segp, 0, sizeof(*segp));
	segp->unusable = true;
}

/* Set Long Mode Flat Kernel Code Segment
 *
 * Input Args:
 *   selector - selector value
 *
 * Output Args:
 *   segp - Pointer to KVM segment
 *
 * Return: None
 *
 * Sets up the KVM segment pointed to by segp, to be a code segment
 * with the selector value given by selector.
 */
static void kvm_seg_set_kernel_code_64bit(uint16_t selector,
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
}

/* Set Long Mode Flat Kernel Data Segment
 *
 * Input Args:
 *   selector - selector value
 *
 * Output Args:
 *   segp - Pointer to KVM segment
 *
 * Return: None
 *
 * Sets up the KVM segment pointed to by segp, to be a data segment
 * with the selector value given by selector.
 */
static void kvm_seg_set_kernel_data_64bit(uint16_t selector,
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
}

/* Address Guest Virtual to Guest Physical
 *
 * Input Args:
 *   vm - Virtual Machine
 *   gpa - VM virtual address
 *
 * Output Args: None
 *
 * Return:
 *   Equivalent VM physical address
 *
 * Translates the VM virtual address given by gva to a VM physical
 * address and then locates the memory region containing the VM
 * physical address, within the VM given by vm.  When found, the host
 * virtual address providing the memory to the vm physical address is returned.
 * A TEST_ASSERT failure occurs if no region containing translated
 * VM virtual address exists.
 */
vm_paddr_t addr_gva2gpa(struct kvm_vm *vm, vm_vaddr_t gva)
{
	uint16_t index[4];
	struct pageMapL4Entry *pml4e;
	struct pageDirectoryPointerEntry *pdpe;
	struct pageDirectoryEntry *pde;
	struct pageTableEntry *pte;
	void *hva;

	TEST_ASSERT(vm->mode == VM_MODE_FLAT48PG, "Attempt to use "
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

	pdpe = addr_gpa2hva(vm, pml4e[index[3]].address * vm->page_size);
	if (!pdpe[index[2]].present)
		goto unmapped_gva;

	pde = addr_gpa2hva(vm, pdpe[index[2]].address * vm->page_size);
	if (!pde[index[1]].present)
		goto unmapped_gva;

	pte = addr_gpa2hva(vm, pde[index[1]].address * vm->page_size);
	if (!pte[index[0]].present)
		goto unmapped_gva;

	return (pte[index[0]].address * vm->page_size) + (gva & 0xfffu);

unmapped_gva:
	TEST_ASSERT(false, "No mapping for vm virtual address, "
		    "gva: 0x%lx", gva);
}

void vcpu_setup(struct kvm_vm *vm, int vcpuid)
{
	struct kvm_sregs sregs;

	/* Set mode specific system register values. */
	vcpu_sregs_get(vm, vcpuid, &sregs);

	switch (vm->mode) {
	case VM_MODE_FLAT48PG:
		sregs.cr0 = X86_CR0_PE | X86_CR0_NE | X86_CR0_PG;
		sregs.cr4 |= X86_CR4_PAE;
		sregs.efer |= (EFER_LME | EFER_LMA | EFER_NX);

		kvm_seg_set_unusable(&sregs.ldt);
		kvm_seg_set_kernel_code_64bit(0x8, &sregs.cs);
		kvm_seg_set_kernel_data_64bit(0x10, &sregs.ds);
		kvm_seg_set_kernel_data_64bit(0x10, &sregs.es);
		break;

	default:
		TEST_ASSERT(false, "Unknown guest mode, mode: 0x%x", vm->mode);
	}
	vcpu_sregs_set(vm, vcpuid, &sregs);

	/* If virtual translation table have been setup, set system register
	 * to point to the tables.  It's okay if they haven't been setup yet,
	 * in that the code that sets up the virtual translation tables, will
	 * go back through any VCPUs that have already been created and set
	 * their values.
	 */
	if (vm->pgd_created) {
		struct kvm_sregs sregs;

		vcpu_sregs_get(vm, vcpuid, &sregs);

		sregs.cr3 = vm->pgd;
		vcpu_sregs_set(vm, vcpuid, &sregs);
	}
}
/* Adds a vCPU with reasonable defaults (i.e., a stack)
 *
 * Input Args:
 *   vcpuid - The id of the VCPU to add to the VM.
 *   guest_code - The vCPU's entry point
 */
void vm_vcpu_add_default(struct kvm_vm *vm, uint32_t vcpuid, void *guest_code)
{
	struct kvm_mp_state mp_state;
	struct kvm_regs regs;
	vm_vaddr_t stack_vaddr;
	stack_vaddr = vm_vaddr_alloc(vm, DEFAULT_STACK_PGS * getpagesize(),
				     DEFAULT_GUEST_STACK_VADDR_MIN, 0, 0);

	/* Create VCPU */
	vm_vcpu_add(vm, vcpuid);

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

/* VM VCPU CPUID Set
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
/* Create a VM with reasonable defaults
 *
 * Input Args:
 *   vcpuid - The id of the single VCPU to add to the VM.
 *   guest_code - The vCPU's entry point
 *
 * Output Args: None
 *
 * Return:
 *   Pointer to opaque structure that describes the created VM.
 */
struct kvm_vm *vm_create_default(uint32_t vcpuid, void *guest_code)
{
	struct kvm_vm *vm;

	/* Create VM */
	vm = vm_create(VM_MODE_FLAT48PG, DEFAULT_GUEST_PHY_PAGES, O_RDWR);

	/* Setup guest code */
	kvm_vm_elf_load(vm, program_invocation_name, 0, 0);

	/* Setup IRQ Chip */
	vm_create_irqchip(vm);

	/* Add the first vCPU. */
	vm_vcpu_add_default(vm, vcpuid, guest_code);

	return vm;
}
