// SPDX-License-Identifier: GPL-2.0
/*
 * AArch64 code
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */

#define _GNU_SOURCE /* for program_invocation_name */

#include <linux/compiler.h>

#include "kvm_util.h"
#include "../kvm_util_internal.h"
#include "processor.h"

#define KVM_GUEST_PAGE_TABLE_MIN_PADDR		0x180000
#define DEFAULT_ARM64_GUEST_STACK_VADDR_MIN	0xac0000

static uint64_t page_align(struct kvm_vm *vm, uint64_t v)
{
	return (v + vm->page_size) & ~(vm->page_size - 1);
}

static uint64_t pgd_index(struct kvm_vm *vm, vm_vaddr_t gva)
{
	unsigned int shift = (vm->pgtable_levels - 1) * (vm->page_shift - 3) + vm->page_shift;
	uint64_t mask = (1UL << (vm->va_bits - shift)) - 1;

	return (gva >> shift) & mask;
}

static uint64_t pud_index(struct kvm_vm *vm, vm_vaddr_t gva)
{
	unsigned int shift = 2 * (vm->page_shift - 3) + vm->page_shift;
	uint64_t mask = (1UL << (vm->page_shift - 3)) - 1;

	TEST_ASSERT(vm->pgtable_levels == 4,
		"Mode %d does not have 4 page table levels", vm->mode);

	return (gva >> shift) & mask;
}

static uint64_t pmd_index(struct kvm_vm *vm, vm_vaddr_t gva)
{
	unsigned int shift = (vm->page_shift - 3) + vm->page_shift;
	uint64_t mask = (1UL << (vm->page_shift - 3)) - 1;

	TEST_ASSERT(vm->pgtable_levels >= 3,
		"Mode %d does not have >= 3 page table levels", vm->mode);

	return (gva >> shift) & mask;
}

static uint64_t pte_index(struct kvm_vm *vm, vm_vaddr_t gva)
{
	uint64_t mask = (1UL << (vm->page_shift - 3)) - 1;
	return (gva >> vm->page_shift) & mask;
}

static uint64_t pte_addr(struct kvm_vm *vm, uint64_t entry)
{
	uint64_t mask = ((1UL << (vm->va_bits - vm->page_shift)) - 1) << vm->page_shift;
	return entry & mask;
}

static uint64_t ptrs_per_pgd(struct kvm_vm *vm)
{
	unsigned int shift = (vm->pgtable_levels - 1) * (vm->page_shift - 3) + vm->page_shift;
	return 1 << (vm->va_bits - shift);
}

static uint64_t __maybe_unused ptrs_per_pte(struct kvm_vm *vm)
{
	return 1 << (vm->page_shift - 3);
}

void virt_pgd_alloc(struct kvm_vm *vm, uint32_t pgd_memslot)
{
	if (!vm->pgd_created) {
		vm_paddr_t paddr = vm_phy_pages_alloc(vm,
			page_align(vm, ptrs_per_pgd(vm) * 8) / vm->page_size,
			KVM_GUEST_PAGE_TABLE_MIN_PADDR, pgd_memslot);
		vm->pgd = paddr;
		vm->pgd_created = true;
	}
}

void _virt_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
		  uint32_t pgd_memslot, uint64_t flags)
{
	uint8_t attr_idx = flags & 7;
	uint64_t *ptep;

	TEST_ASSERT((vaddr % vm->page_size) == 0,
		"Virtual address not on page boundary,\n"
		"  vaddr: 0x%lx vm->page_size: 0x%x", vaddr, vm->page_size);
	TEST_ASSERT(sparsebit_is_set(vm->vpages_valid,
		(vaddr >> vm->page_shift)),
		"Invalid virtual address, vaddr: 0x%lx", vaddr);
	TEST_ASSERT((paddr % vm->page_size) == 0,
		"Physical address not on page boundary,\n"
		"  paddr: 0x%lx vm->page_size: 0x%x", paddr, vm->page_size);
	TEST_ASSERT((paddr >> vm->page_shift) <= vm->max_gfn,
		"Physical address beyond beyond maximum supported,\n"
		"  paddr: 0x%lx vm->max_gfn: 0x%lx vm->page_size: 0x%x",
		paddr, vm->max_gfn, vm->page_size);

	ptep = addr_gpa2hva(vm, vm->pgd) + pgd_index(vm, vaddr) * 8;
	if (!*ptep) {
		*ptep = vm_phy_page_alloc(vm, KVM_GUEST_PAGE_TABLE_MIN_PADDR, pgd_memslot);
		*ptep |= 3;
	}

	switch (vm->pgtable_levels) {
	case 4:
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) + pud_index(vm, vaddr) * 8;
		if (!*ptep) {
			*ptep = vm_phy_page_alloc(vm, KVM_GUEST_PAGE_TABLE_MIN_PADDR, pgd_memslot);
			*ptep |= 3;
		}
		/* fall through */
	case 3:
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) + pmd_index(vm, vaddr) * 8;
		if (!*ptep) {
			*ptep = vm_phy_page_alloc(vm, KVM_GUEST_PAGE_TABLE_MIN_PADDR, pgd_memslot);
			*ptep |= 3;
		}
		/* fall through */
	case 2:
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) + pte_index(vm, vaddr) * 8;
		break;
	default:
		TEST_ASSERT(false, "Page table levels must be 2, 3, or 4");
	}

	*ptep = paddr | 3;
	*ptep |= (attr_idx << 2) | (1 << 10) /* Access Flag */;
}

void virt_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
		 uint32_t pgd_memslot)
{
	uint64_t attr_idx = 4; /* NORMAL (See DEFAULT_MAIR_EL1) */

	_virt_pg_map(vm, vaddr, paddr, pgd_memslot, attr_idx);
}

vm_paddr_t addr_gva2gpa(struct kvm_vm *vm, vm_vaddr_t gva)
{
	uint64_t *ptep;

	if (!vm->pgd_created)
		goto unmapped_gva;

	ptep = addr_gpa2hva(vm, vm->pgd) + pgd_index(vm, gva) * 8;
	if (!ptep)
		goto unmapped_gva;

	switch (vm->pgtable_levels) {
	case 4:
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) + pud_index(vm, gva) * 8;
		if (!ptep)
			goto unmapped_gva;
		/* fall through */
	case 3:
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) + pmd_index(vm, gva) * 8;
		if (!ptep)
			goto unmapped_gva;
		/* fall through */
	case 2:
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) + pte_index(vm, gva) * 8;
		if (!ptep)
			goto unmapped_gva;
		break;
	default:
		TEST_ASSERT(false, "Page table levels must be 2, 3, or 4");
	}

	return pte_addr(vm, *ptep) + (gva & (vm->page_size - 1));

unmapped_gva:
	TEST_ASSERT(false, "No mapping for vm virtual address, "
		    "gva: 0x%lx", gva);
	exit(1);
}

static void pte_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent, uint64_t page, int level)
{
#ifdef DEBUG_VM
	static const char * const type[] = { "", "pud", "pmd", "pte" };
	uint64_t pte, *ptep;

	if (level == 4)
		return;

	for (pte = page; pte < page + ptrs_per_pte(vm) * 8; pte += 8) {
		ptep = addr_gpa2hva(vm, pte);
		if (!*ptep)
			continue;
		printf("%*s%s: %lx: %lx at %p\n", indent, "", type[level], pte, *ptep, ptep);
		pte_dump(stream, vm, indent + 1, pte_addr(vm, *ptep), level + 1);
	}
#endif
}

void virt_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent)
{
	int level = 4 - (vm->pgtable_levels - 1);
	uint64_t pgd, *ptep;

	if (!vm->pgd_created)
		return;

	for (pgd = vm->pgd; pgd < vm->pgd + ptrs_per_pgd(vm) * 8; pgd += 8) {
		ptep = addr_gpa2hva(vm, pgd);
		if (!*ptep)
			continue;
		printf("%*spgd: %lx: %lx at %p\n", indent, "", pgd, *ptep, ptep);
		pte_dump(stream, vm, indent + 1, pte_addr(vm, *ptep), level);
	}
}

struct kvm_vm *vm_create_default(uint32_t vcpuid, uint64_t extra_mem_pages,
				 void *guest_code)
{
	uint64_t ptrs_per_4k_pte = 512;
	uint64_t extra_pg_pages = (extra_mem_pages / ptrs_per_4k_pte) * 2;
	struct kvm_vm *vm;

	vm = vm_create(VM_MODE_P40V48_4K, DEFAULT_GUEST_PHY_PAGES + extra_pg_pages, O_RDWR);

	kvm_vm_elf_load(vm, program_invocation_name, 0, 0);
	vm_vcpu_add_default(vm, vcpuid, guest_code);

	return vm;
}

void vm_vcpu_add_default(struct kvm_vm *vm, uint32_t vcpuid, void *guest_code)
{
	size_t stack_size = vm->page_size == 4096 ?
					DEFAULT_STACK_PGS * vm->page_size :
					vm->page_size;
	uint64_t stack_vaddr = vm_vaddr_alloc(vm, stack_size,
					DEFAULT_ARM64_GUEST_STACK_VADDR_MIN, 0, 0);

	vm_vcpu_add(vm, vcpuid, 0, 0);

	set_reg(vm, vcpuid, ARM64_CORE_REG(sp_el1), stack_vaddr + stack_size);
	set_reg(vm, vcpuid, ARM64_CORE_REG(regs.pc), (uint64_t)guest_code);
}

void vcpu_setup(struct kvm_vm *vm, int vcpuid, int pgd_memslot, int gdt_memslot)
{
	struct kvm_vcpu_init init;
	uint64_t sctlr_el1, tcr_el1;

	memset(&init, 0, sizeof(init));
	init.target = KVM_ARM_TARGET_GENERIC_V8;
	vcpu_ioctl(vm, vcpuid, KVM_ARM_VCPU_INIT, &init);

	/*
	 * Enable FP/ASIMD to avoid trapping when accessing Q0-Q15
	 * registers, which the variable argument list macros do.
	 */
	set_reg(vm, vcpuid, ARM64_SYS_REG(CPACR_EL1), 3 << 20);

	get_reg(vm, vcpuid, ARM64_SYS_REG(SCTLR_EL1), &sctlr_el1);
	get_reg(vm, vcpuid, ARM64_SYS_REG(TCR_EL1), &tcr_el1);

	switch (vm->mode) {
	case VM_MODE_P52V48_4K:
		TEST_ASSERT(false, "AArch64 does not support 4K sized pages "
				   "with 52-bit physical address ranges");
	case VM_MODE_P52V48_64K:
		tcr_el1 |= 1ul << 14; /* TG0 = 64KB */
		tcr_el1 |= 6ul << 32; /* IPS = 52 bits */
		break;
	case VM_MODE_P48V48_4K:
		tcr_el1 |= 0ul << 14; /* TG0 = 4KB */
		tcr_el1 |= 5ul << 32; /* IPS = 48 bits */
		break;
	case VM_MODE_P48V48_64K:
		tcr_el1 |= 1ul << 14; /* TG0 = 64KB */
		tcr_el1 |= 5ul << 32; /* IPS = 48 bits */
		break;
	case VM_MODE_P40V48_4K:
		tcr_el1 |= 0ul << 14; /* TG0 = 4KB */
		tcr_el1 |= 2ul << 32; /* IPS = 40 bits */
		break;
	case VM_MODE_P40V48_64K:
		tcr_el1 |= 1ul << 14; /* TG0 = 64KB */
		tcr_el1 |= 2ul << 32; /* IPS = 40 bits */
		break;
	default:
		TEST_ASSERT(false, "Unknown guest mode, mode: 0x%x", vm->mode);
	}

	sctlr_el1 |= (1 << 0) | (1 << 2) | (1 << 12) /* M | C | I */;
	/* TCR_EL1 |= IRGN0:WBWA | ORGN0:WBWA | SH0:Inner-Shareable */;
	tcr_el1 |= (1 << 8) | (1 << 10) | (3 << 12);
	tcr_el1 |= (64 - vm->va_bits) /* T0SZ */;

	set_reg(vm, vcpuid, ARM64_SYS_REG(SCTLR_EL1), sctlr_el1);
	set_reg(vm, vcpuid, ARM64_SYS_REG(TCR_EL1), tcr_el1);
	set_reg(vm, vcpuid, ARM64_SYS_REG(MAIR_EL1), DEFAULT_MAIR_EL1);
	set_reg(vm, vcpuid, ARM64_SYS_REG(TTBR0_EL1), vm->pgd);
}

void vcpu_dump(FILE *stream, struct kvm_vm *vm, uint32_t vcpuid, uint8_t indent)
{
	uint64_t pstate, pc;

	get_reg(vm, vcpuid, ARM64_CORE_REG(regs.pstate), &pstate);
	get_reg(vm, vcpuid, ARM64_CORE_REG(regs.pc), &pc);

	fprintf(stream, "%*spstate: 0x%.16lx pc: 0x%.16lx\n",
		indent, "", pstate, pc);
}
