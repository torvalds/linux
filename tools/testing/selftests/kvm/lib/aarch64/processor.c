// SPDX-License-Identifier: GPL-2.0
/*
 * AArch64 code
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */

#include <linux/compiler.h>
#include <assert.h>

#include "kvm_util.h"
#include "../kvm_util_internal.h"
#include "processor.h"

#define DEFAULT_ARM64_GUEST_STACK_VADDR_MIN	0xac0000

static vm_vaddr_t exception_handlers;

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

void virt_pgd_alloc(struct kvm_vm *vm)
{
	if (!vm->pgd_created) {
		vm_paddr_t paddr = vm_phy_pages_alloc(vm,
			page_align(vm, ptrs_per_pgd(vm) * 8) / vm->page_size,
			KVM_GUEST_PAGE_TABLE_MIN_PADDR, 0);
		vm->pgd = paddr;
		vm->pgd_created = true;
	}
}

static void _virt_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr,
			 uint64_t flags)
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
	if (!*ptep)
		*ptep = vm_alloc_page_table(vm) | 3;

	switch (vm->pgtable_levels) {
	case 4:
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) + pud_index(vm, vaddr) * 8;
		if (!*ptep)
			*ptep = vm_alloc_page_table(vm) | 3;
		/* fall through */
	case 3:
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) + pmd_index(vm, vaddr) * 8;
		if (!*ptep)
			*ptep = vm_alloc_page_table(vm) | 3;
		/* fall through */
	case 2:
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) + pte_index(vm, vaddr) * 8;
		break;
	default:
		TEST_FAIL("Page table levels must be 2, 3, or 4");
	}

	*ptep = paddr | 3;
	*ptep |= (attr_idx << 2) | (1 << 10) /* Access Flag */;
}

void virt_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr)
{
	uint64_t attr_idx = 4; /* NORMAL (See DEFAULT_MAIR_EL1) */

	_virt_pg_map(vm, vaddr, paddr, attr_idx);
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
		TEST_FAIL("Page table levels must be 2, 3, or 4");
	}

	return pte_addr(vm, *ptep) + (gva & (vm->page_size - 1));

unmapped_gva:
	TEST_FAIL("No mapping for vm virtual address, gva: 0x%lx", gva);
	exit(1);
}

static void pte_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent, uint64_t page, int level)
{
#ifdef DEBUG
	static const char * const type[] = { "", "pud", "pmd", "pte" };
	uint64_t pte, *ptep;

	if (level == 4)
		return;

	for (pte = page; pte < page + ptrs_per_pte(vm) * 8; pte += 8) {
		ptep = addr_gpa2hva(vm, pte);
		if (!*ptep)
			continue;
		fprintf(stream, "%*s%s: %lx: %lx at %p\n", indent, "", type[level], pte, *ptep, ptep);
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
		fprintf(stream, "%*spgd: %lx: %lx at %p\n", indent, "", pgd, *ptep, ptep);
		pte_dump(stream, vm, indent + 1, pte_addr(vm, *ptep), level);
	}
}

void aarch64_vcpu_setup(struct kvm_vm *vm, uint32_t vcpuid, struct kvm_vcpu_init *init)
{
	struct kvm_vcpu_init default_init = { .target = -1, };
	uint64_t sctlr_el1, tcr_el1;

	if (!init)
		init = &default_init;

	if (init->target == -1) {
		struct kvm_vcpu_init preferred;
		vm_ioctl(vm, KVM_ARM_PREFERRED_TARGET, &preferred);
		init->target = preferred.target;
	}

	vcpu_ioctl(vm, vcpuid, KVM_ARM_VCPU_INIT, init);

	/*
	 * Enable FP/ASIMD to avoid trapping when accessing Q0-Q15
	 * registers, which the variable argument list macros do.
	 */
	set_reg(vm, vcpuid, KVM_ARM64_SYS_REG(SYS_CPACR_EL1), 3 << 20);

	get_reg(vm, vcpuid, KVM_ARM64_SYS_REG(SYS_SCTLR_EL1), &sctlr_el1);
	get_reg(vm, vcpuid, KVM_ARM64_SYS_REG(SYS_TCR_EL1), &tcr_el1);

	switch (vm->mode) {
	case VM_MODE_P52V48_4K:
		TEST_FAIL("AArch64 does not support 4K sized pages "
			  "with 52-bit physical address ranges");
	case VM_MODE_PXXV48_4K:
		TEST_FAIL("AArch64 does not support 4K sized pages "
			  "with ANY-bit physical address ranges");
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
		TEST_FAIL("Unknown guest mode, mode: 0x%x", vm->mode);
	}

	sctlr_el1 |= (1 << 0) | (1 << 2) | (1 << 12) /* M | C | I */;
	/* TCR_EL1 |= IRGN0:WBWA | ORGN0:WBWA | SH0:Inner-Shareable */;
	tcr_el1 |= (1 << 8) | (1 << 10) | (3 << 12);
	tcr_el1 |= (64 - vm->va_bits) /* T0SZ */;

	set_reg(vm, vcpuid, KVM_ARM64_SYS_REG(SYS_SCTLR_EL1), sctlr_el1);
	set_reg(vm, vcpuid, KVM_ARM64_SYS_REG(SYS_TCR_EL1), tcr_el1);
	set_reg(vm, vcpuid, KVM_ARM64_SYS_REG(SYS_MAIR_EL1), DEFAULT_MAIR_EL1);
	set_reg(vm, vcpuid, KVM_ARM64_SYS_REG(SYS_TTBR0_EL1), vm->pgd);
	set_reg(vm, vcpuid, KVM_ARM64_SYS_REG(SYS_TPIDR_EL1), vcpuid);
}

void vcpu_dump(FILE *stream, struct kvm_vm *vm, uint32_t vcpuid, uint8_t indent)
{
	uint64_t pstate, pc;

	get_reg(vm, vcpuid, ARM64_CORE_REG(regs.pstate), &pstate);
	get_reg(vm, vcpuid, ARM64_CORE_REG(regs.pc), &pc);

	fprintf(stream, "%*spstate: 0x%.16lx pc: 0x%.16lx\n",
		indent, "", pstate, pc);
}

void aarch64_vcpu_add_default(struct kvm_vm *vm, uint32_t vcpuid,
			      struct kvm_vcpu_init *init, void *guest_code)
{
	size_t stack_size = vm->page_size == 4096 ?
					DEFAULT_STACK_PGS * vm->page_size :
					vm->page_size;
	uint64_t stack_vaddr = vm_vaddr_alloc(vm, stack_size,
					      DEFAULT_ARM64_GUEST_STACK_VADDR_MIN);

	vm_vcpu_add(vm, vcpuid);
	aarch64_vcpu_setup(vm, vcpuid, init);

	set_reg(vm, vcpuid, ARM64_CORE_REG(sp_el1), stack_vaddr + stack_size);
	set_reg(vm, vcpuid, ARM64_CORE_REG(regs.pc), (uint64_t)guest_code);
}

void vm_vcpu_add_default(struct kvm_vm *vm, uint32_t vcpuid, void *guest_code)
{
	aarch64_vcpu_add_default(vm, vcpuid, NULL, guest_code);
}

void vcpu_args_set(struct kvm_vm *vm, uint32_t vcpuid, unsigned int num, ...)
{
	va_list ap;
	int i;

	TEST_ASSERT(num >= 1 && num <= 8, "Unsupported number of args,\n"
		    "  num: %u\n", num);

	va_start(ap, num);

	for (i = 0; i < num; i++) {
		set_reg(vm, vcpuid, ARM64_CORE_REG(regs.regs[i]),
			va_arg(ap, uint64_t));
	}

	va_end(ap);
}

void kvm_exit_unexpected_exception(int vector, uint64_t ec, bool valid_ec)
{
	ucall(UCALL_UNHANDLED, 3, vector, ec, valid_ec);
	while (1)
		;
}

void assert_on_unhandled_exception(struct kvm_vm *vm, uint32_t vcpuid)
{
	struct ucall uc;

	if (get_ucall(vm, vcpuid, &uc) != UCALL_UNHANDLED)
		return;

	if (uc.args[2]) /* valid_ec */ {
		assert(VECTOR_IS_SYNC(uc.args[0]));
		TEST_FAIL("Unexpected exception (vector:0x%lx, ec:0x%lx)",
			  uc.args[0], uc.args[1]);
	} else {
		assert(!VECTOR_IS_SYNC(uc.args[0]));
		TEST_FAIL("Unexpected exception (vector:0x%lx)",
			  uc.args[0]);
	}
}

struct handlers {
	handler_fn exception_handlers[VECTOR_NUM][ESR_EC_NUM];
};

void vcpu_init_descriptor_tables(struct kvm_vm *vm, uint32_t vcpuid)
{
	extern char vectors;

	set_reg(vm, vcpuid, KVM_ARM64_SYS_REG(SYS_VBAR_EL1), (uint64_t)&vectors);
}

void route_exception(struct ex_regs *regs, int vector)
{
	struct handlers *handlers = (struct handlers *)exception_handlers;
	bool valid_ec;
	int ec = 0;

	switch (vector) {
	case VECTOR_SYNC_CURRENT:
	case VECTOR_SYNC_LOWER_64:
		ec = (read_sysreg(esr_el1) >> ESR_EC_SHIFT) & ESR_EC_MASK;
		valid_ec = true;
		break;
	case VECTOR_IRQ_CURRENT:
	case VECTOR_IRQ_LOWER_64:
	case VECTOR_FIQ_CURRENT:
	case VECTOR_FIQ_LOWER_64:
	case VECTOR_ERROR_CURRENT:
	case VECTOR_ERROR_LOWER_64:
		ec = 0;
		valid_ec = false;
		break;
	default:
		valid_ec = false;
		goto unexpected_exception;
	}

	if (handlers && handlers->exception_handlers[vector][ec])
		return handlers->exception_handlers[vector][ec](regs);

unexpected_exception:
	kvm_exit_unexpected_exception(vector, ec, valid_ec);
}

void vm_init_descriptor_tables(struct kvm_vm *vm)
{
	vm->handlers = vm_vaddr_alloc(vm, sizeof(struct handlers),
			vm->page_size);

	*(vm_vaddr_t *)addr_gva2hva(vm, (vm_vaddr_t)(&exception_handlers)) = vm->handlers;
}

void vm_install_sync_handler(struct kvm_vm *vm, int vector, int ec,
			 void (*handler)(struct ex_regs *))
{
	struct handlers *handlers = addr_gva2hva(vm, vm->handlers);

	assert(VECTOR_IS_SYNC(vector));
	assert(vector < VECTOR_NUM);
	assert(ec < ESR_EC_NUM);
	handlers->exception_handlers[vector][ec] = handler;
}

void vm_install_exception_handler(struct kvm_vm *vm, int vector,
			 void (*handler)(struct ex_regs *))
{
	struct handlers *handlers = addr_gva2hva(vm, vm->handlers);

	assert(!VECTOR_IS_SYNC(vector));
	assert(vector < VECTOR_NUM);
	handlers->exception_handlers[vector][0] = handler;
}

uint32_t guest_get_vcpuid(void)
{
	return read_sysreg(tpidr_el1);
}
