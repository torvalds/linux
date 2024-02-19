// SPDX-License-Identifier: GPL-2.0
/*
 * RISC-V code
 *
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */

#include <linux/compiler.h>
#include <assert.h>

#include "kvm_util.h"
#include "processor.h"

#define DEFAULT_RISCV_GUEST_STACK_VADDR_MIN	0xac0000

static uint64_t page_align(struct kvm_vm *vm, uint64_t v)
{
	return (v + vm->page_size) & ~(vm->page_size - 1);
}

static uint64_t pte_addr(struct kvm_vm *vm, uint64_t entry)
{
	return ((entry & PGTBL_PTE_ADDR_MASK) >> PGTBL_PTE_ADDR_SHIFT) <<
		PGTBL_PAGE_SIZE_SHIFT;
}

static uint64_t ptrs_per_pte(struct kvm_vm *vm)
{
	return PGTBL_PAGE_SIZE / sizeof(uint64_t);
}

static uint64_t pte_index_mask[] = {
	PGTBL_L0_INDEX_MASK,
	PGTBL_L1_INDEX_MASK,
	PGTBL_L2_INDEX_MASK,
	PGTBL_L3_INDEX_MASK,
};

static uint32_t pte_index_shift[] = {
	PGTBL_L0_INDEX_SHIFT,
	PGTBL_L1_INDEX_SHIFT,
	PGTBL_L2_INDEX_SHIFT,
	PGTBL_L3_INDEX_SHIFT,
};

static uint64_t pte_index(struct kvm_vm *vm, vm_vaddr_t gva, int level)
{
	TEST_ASSERT(level > -1,
		"Negative page table level (%d) not possible", level);
	TEST_ASSERT(level < vm->pgtable_levels,
		"Invalid page table level (%d)", level);

	return (gva & pte_index_mask[level]) >> pte_index_shift[level];
}

void virt_arch_pgd_alloc(struct kvm_vm *vm)
{
	size_t nr_pages = page_align(vm, ptrs_per_pte(vm) * 8) / vm->page_size;

	if (vm->pgd_created)
		return;

	vm->pgd = vm_phy_pages_alloc(vm, nr_pages,
				     KVM_GUEST_PAGE_TABLE_MIN_PADDR,
				     vm->memslots[MEM_REGION_PT]);
	vm->pgd_created = true;
}

void virt_arch_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr)
{
	uint64_t *ptep, next_ppn;
	int level = vm->pgtable_levels - 1;

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
		"Physical address beyond maximum supported,\n"
		"  paddr: 0x%lx vm->max_gfn: 0x%lx vm->page_size: 0x%x",
		paddr, vm->max_gfn, vm->page_size);

	ptep = addr_gpa2hva(vm, vm->pgd) + pte_index(vm, vaddr, level) * 8;
	if (!*ptep) {
		next_ppn = vm_alloc_page_table(vm) >> PGTBL_PAGE_SIZE_SHIFT;
		*ptep = (next_ppn << PGTBL_PTE_ADDR_SHIFT) |
			PGTBL_PTE_VALID_MASK;
	}
	level--;

	while (level > -1) {
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) +
		       pte_index(vm, vaddr, level) * 8;
		if (!*ptep && level > 0) {
			next_ppn = vm_alloc_page_table(vm) >>
				   PGTBL_PAGE_SIZE_SHIFT;
			*ptep = (next_ppn << PGTBL_PTE_ADDR_SHIFT) |
				PGTBL_PTE_VALID_MASK;
		}
		level--;
	}

	paddr = paddr >> PGTBL_PAGE_SIZE_SHIFT;
	*ptep = (paddr << PGTBL_PTE_ADDR_SHIFT) |
		PGTBL_PTE_PERM_MASK | PGTBL_PTE_VALID_MASK;
}

vm_paddr_t addr_arch_gva2gpa(struct kvm_vm *vm, vm_vaddr_t gva)
{
	uint64_t *ptep;
	int level = vm->pgtable_levels - 1;

	if (!vm->pgd_created)
		goto unmapped_gva;

	ptep = addr_gpa2hva(vm, vm->pgd) + pte_index(vm, gva, level) * 8;
	if (!ptep)
		goto unmapped_gva;
	level--;

	while (level > -1) {
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) +
		       pte_index(vm, gva, level) * 8;
		if (!ptep)
			goto unmapped_gva;
		level--;
	}

	return pte_addr(vm, *ptep) + (gva & (vm->page_size - 1));

unmapped_gva:
	TEST_FAIL("No mapping for vm virtual address gva: 0x%lx level: %d",
		  gva, level);
	exit(1);
}

static void pte_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent,
		     uint64_t page, int level)
{
#ifdef DEBUG
	static const char *const type[] = { "pte", "pmd", "pud", "p4d"};
	uint64_t pte, *ptep;

	if (level < 0)
		return;

	for (pte = page; pte < page + ptrs_per_pte(vm) * 8; pte += 8) {
		ptep = addr_gpa2hva(vm, pte);
		if (!*ptep)
			continue;
		fprintf(stream, "%*s%s: %lx: %lx at %p\n", indent, "",
			type[level], pte, *ptep, ptep);
		pte_dump(stream, vm, indent + 1,
			 pte_addr(vm, *ptep), level - 1);
	}
#endif
}

void virt_arch_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent)
{
	int level = vm->pgtable_levels - 1;
	uint64_t pgd, *ptep;

	if (!vm->pgd_created)
		return;

	for (pgd = vm->pgd; pgd < vm->pgd + ptrs_per_pte(vm) * 8; pgd += 8) {
		ptep = addr_gpa2hva(vm, pgd);
		if (!*ptep)
			continue;
		fprintf(stream, "%*spgd: %lx: %lx at %p\n", indent, "",
			pgd, *ptep, ptep);
		pte_dump(stream, vm, indent + 1,
			 pte_addr(vm, *ptep), level - 1);
	}
}

void riscv_vcpu_mmu_setup(struct kvm_vcpu *vcpu)
{
	struct kvm_vm *vm = vcpu->vm;
	unsigned long satp;

	/*
	 * The RISC-V Sv48 MMU mode supports 56-bit physical address
	 * for 48-bit virtual address with 4KB last level page size.
	 */
	switch (vm->mode) {
	case VM_MODE_P52V48_4K:
	case VM_MODE_P48V48_4K:
	case VM_MODE_P40V48_4K:
		break;
	default:
		TEST_FAIL("Unknown guest mode, mode: 0x%x", vm->mode);
	}

	satp = (vm->pgd >> PGTBL_PAGE_SIZE_SHIFT) & SATP_PPN;
	satp |= SATP_MODE_48;

	vcpu_set_reg(vcpu, RISCV_GENERAL_CSR_REG(satp), satp);
}

void vcpu_arch_dump(FILE *stream, struct kvm_vcpu *vcpu, uint8_t indent)
{
	struct kvm_riscv_core core;

	vcpu_get_reg(vcpu, RISCV_CORE_REG(mode), &core.mode);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.pc), &core.regs.pc);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.ra), &core.regs.ra);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.sp), &core.regs.sp);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.gp), &core.regs.gp);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.tp), &core.regs.tp);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.t0), &core.regs.t0);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.t1), &core.regs.t1);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.t2), &core.regs.t2);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.s0), &core.regs.s0);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.s1), &core.regs.s1);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.a0), &core.regs.a0);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.a1), &core.regs.a1);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.a2), &core.regs.a2);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.a3), &core.regs.a3);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.a4), &core.regs.a4);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.a5), &core.regs.a5);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.a6), &core.regs.a6);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.a7), &core.regs.a7);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.s2), &core.regs.s2);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.s3), &core.regs.s3);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.s4), &core.regs.s4);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.s5), &core.regs.s5);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.s6), &core.regs.s6);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.s7), &core.regs.s7);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.s8), &core.regs.s8);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.s9), &core.regs.s9);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.s10), &core.regs.s10);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.s11), &core.regs.s11);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.t3), &core.regs.t3);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.t4), &core.regs.t4);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.t5), &core.regs.t5);
	vcpu_get_reg(vcpu, RISCV_CORE_REG(regs.t6), &core.regs.t6);

	fprintf(stream,
		" MODE:  0x%lx\n", core.mode);
	fprintf(stream,
		" PC: 0x%016lx   RA: 0x%016lx SP: 0x%016lx GP: 0x%016lx\n",
		core.regs.pc, core.regs.ra, core.regs.sp, core.regs.gp);
	fprintf(stream,
		" TP: 0x%016lx   T0: 0x%016lx T1: 0x%016lx T2: 0x%016lx\n",
		core.regs.tp, core.regs.t0, core.regs.t1, core.regs.t2);
	fprintf(stream,
		" S0: 0x%016lx   S1: 0x%016lx A0: 0x%016lx A1: 0x%016lx\n",
		core.regs.s0, core.regs.s1, core.regs.a0, core.regs.a1);
	fprintf(stream,
		" A2: 0x%016lx   A3: 0x%016lx A4: 0x%016lx A5: 0x%016lx\n",
		core.regs.a2, core.regs.a3, core.regs.a4, core.regs.a5);
	fprintf(stream,
		" A6: 0x%016lx   A7: 0x%016lx S2: 0x%016lx S3: 0x%016lx\n",
		core.regs.a6, core.regs.a7, core.regs.s2, core.regs.s3);
	fprintf(stream,
		" S4: 0x%016lx   S5: 0x%016lx S6: 0x%016lx S7: 0x%016lx\n",
		core.regs.s4, core.regs.s5, core.regs.s6, core.regs.s7);
	fprintf(stream,
		" S8: 0x%016lx   S9: 0x%016lx S10: 0x%016lx S11: 0x%016lx\n",
		core.regs.s8, core.regs.s9, core.regs.s10, core.regs.s11);
	fprintf(stream,
		" T3: 0x%016lx   T4: 0x%016lx T5: 0x%016lx T6: 0x%016lx\n",
		core.regs.t3, core.regs.t4, core.regs.t5, core.regs.t6);
}

static void __aligned(16) guest_unexp_trap(void)
{
	sbi_ecall(KVM_RISCV_SELFTESTS_SBI_EXT,
		  KVM_RISCV_SELFTESTS_SBI_UNEXP,
		  0, 0, 0, 0, 0, 0);
}

struct kvm_vcpu *vm_arch_vcpu_add(struct kvm_vm *vm, uint32_t vcpu_id,
				  void *guest_code)
{
	int r;
	size_t stack_size;
	unsigned long stack_vaddr;
	unsigned long current_gp = 0;
	struct kvm_mp_state mps;
	struct kvm_vcpu *vcpu;

	stack_size = vm->page_size == 4096 ? DEFAULT_STACK_PGS * vm->page_size :
					     vm->page_size;
	stack_vaddr = __vm_vaddr_alloc(vm, stack_size,
				       DEFAULT_RISCV_GUEST_STACK_VADDR_MIN,
				       MEM_REGION_DATA);

	vcpu = __vm_vcpu_add(vm, vcpu_id);
	riscv_vcpu_mmu_setup(vcpu);

	/*
	 * With SBI HSM support in KVM RISC-V, all secondary VCPUs are
	 * powered-off by default so we ensure that all secondary VCPUs
	 * are powered-on using KVM_SET_MP_STATE ioctl().
	 */
	mps.mp_state = KVM_MP_STATE_RUNNABLE;
	r = __vcpu_ioctl(vcpu, KVM_SET_MP_STATE, &mps);
	TEST_ASSERT(!r, "IOCTL KVM_SET_MP_STATE failed (error %d)", r);

	/* Setup global pointer of guest to be same as the host */
	asm volatile (
		"add %0, gp, zero" : "=r" (current_gp) : : "memory");
	vcpu_set_reg(vcpu, RISCV_CORE_REG(regs.gp), current_gp);

	/* Setup stack pointer and program counter of guest */
	vcpu_set_reg(vcpu, RISCV_CORE_REG(regs.sp), stack_vaddr + stack_size);
	vcpu_set_reg(vcpu, RISCV_CORE_REG(regs.pc), (unsigned long)guest_code);

	/* Setup default exception vector of guest */
	vcpu_set_reg(vcpu, RISCV_GENERAL_CSR_REG(stvec), (unsigned long)guest_unexp_trap);

	return vcpu;
}

void vcpu_args_set(struct kvm_vcpu *vcpu, unsigned int num, ...)
{
	va_list ap;
	uint64_t id = RISCV_CORE_REG(regs.a0);
	int i;

	TEST_ASSERT(num >= 1 && num <= 8, "Unsupported number of args,\n"
		    "  num: %u", num);

	va_start(ap, num);

	for (i = 0; i < num; i++) {
		switch (i) {
		case 0:
			id = RISCV_CORE_REG(regs.a0);
			break;
		case 1:
			id = RISCV_CORE_REG(regs.a1);
			break;
		case 2:
			id = RISCV_CORE_REG(regs.a2);
			break;
		case 3:
			id = RISCV_CORE_REG(regs.a3);
			break;
		case 4:
			id = RISCV_CORE_REG(regs.a4);
			break;
		case 5:
			id = RISCV_CORE_REG(regs.a5);
			break;
		case 6:
			id = RISCV_CORE_REG(regs.a6);
			break;
		case 7:
			id = RISCV_CORE_REG(regs.a7);
			break;
		}
		vcpu_set_reg(vcpu, id, va_arg(ap, uint64_t));
	}

	va_end(ap);
}

void assert_on_unhandled_exception(struct kvm_vcpu *vcpu)
{
}

struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
			unsigned long arg1, unsigned long arg2,
			unsigned long arg3, unsigned long arg4,
			unsigned long arg5)
{
	register uintptr_t a0 asm ("a0") = (uintptr_t)(arg0);
	register uintptr_t a1 asm ("a1") = (uintptr_t)(arg1);
	register uintptr_t a2 asm ("a2") = (uintptr_t)(arg2);
	register uintptr_t a3 asm ("a3") = (uintptr_t)(arg3);
	register uintptr_t a4 asm ("a4") = (uintptr_t)(arg4);
	register uintptr_t a5 asm ("a5") = (uintptr_t)(arg5);
	register uintptr_t a6 asm ("a6") = (uintptr_t)(fid);
	register uintptr_t a7 asm ("a7") = (uintptr_t)(ext);
	struct sbiret ret;

	asm volatile (
		"ecall"
		: "+r" (a0), "+r" (a1)
		: "r" (a2), "r" (a3), "r" (a4), "r" (a5), "r" (a6), "r" (a7)
		: "memory");
	ret.error = a0;
	ret.value = a1;

	return ret;
}

bool guest_sbi_probe_extension(int extid, long *out_val)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_PROBE_EXT, extid,
			0, 0, 0, 0, 0);

	__GUEST_ASSERT(!ret.error || ret.error == SBI_ERR_NOT_SUPPORTED,
		       "ret.error=%ld, ret.value=%ld\n", ret.error, ret.value);

	if (ret.error == SBI_ERR_NOT_SUPPORTED)
		return false;

	if (out_val)
		*out_val = ret.value;

	return true;
}
