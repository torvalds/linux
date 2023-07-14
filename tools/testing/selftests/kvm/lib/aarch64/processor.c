// SPDX-License-Identifier: GPL-2.0
/*
 * AArch64 code
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */

#include <linux/compiler.h>
#include <assert.h>

#include "guest_modes.h"
#include "kvm_util.h"
#include "processor.h"
#include <linux/bitfield.h>

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

static uint64_t addr_pte(struct kvm_vm *vm, uint64_t pa, uint64_t attrs)
{
	uint64_t pte;

	pte = pa & GENMASK(47, vm->page_shift);
	if (vm->page_shift == 16)
		pte |= FIELD_GET(GENMASK(51, 48), pa) << 12;
	pte |= attrs;

	return pte;
}

static uint64_t pte_addr(struct kvm_vm *vm, uint64_t pte)
{
	uint64_t pa;

	pa = pte & GENMASK(47, vm->page_shift);
	if (vm->page_shift == 16)
		pa |= FIELD_GET(GENMASK(15, 12), pte) << 48;

	return pa;
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

void virt_arch_pgd_alloc(struct kvm_vm *vm)
{
	size_t nr_pages = page_align(vm, ptrs_per_pgd(vm) * 8) / vm->page_size;

	if (vm->pgd_created)
		return;

	vm->pgd = vm_phy_pages_alloc(vm, nr_pages,
				     KVM_GUEST_PAGE_TABLE_MIN_PADDR,
				     vm->memslots[MEM_REGION_PT]);
	vm->pgd_created = true;
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
		*ptep = addr_pte(vm, vm_alloc_page_table(vm), 3);

	switch (vm->pgtable_levels) {
	case 4:
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) + pud_index(vm, vaddr) * 8;
		if (!*ptep)
			*ptep = addr_pte(vm, vm_alloc_page_table(vm), 3);
		/* fall through */
	case 3:
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) + pmd_index(vm, vaddr) * 8;
		if (!*ptep)
			*ptep = addr_pte(vm, vm_alloc_page_table(vm), 3);
		/* fall through */
	case 2:
		ptep = addr_gpa2hva(vm, pte_addr(vm, *ptep)) + pte_index(vm, vaddr) * 8;
		break;
	default:
		TEST_FAIL("Page table levels must be 2, 3, or 4");
	}

	*ptep = addr_pte(vm, paddr, (attr_idx << 2) | (1 << 10) | 3);  /* AF */
}

void virt_arch_pg_map(struct kvm_vm *vm, uint64_t vaddr, uint64_t paddr)
{
	uint64_t attr_idx = MT_NORMAL;

	_virt_pg_map(vm, vaddr, paddr, attr_idx);
}

uint64_t *virt_get_pte_hva(struct kvm_vm *vm, vm_vaddr_t gva)
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

	return ptep;

unmapped_gva:
	TEST_FAIL("No mapping for vm virtual address, gva: 0x%lx", gva);
	exit(EXIT_FAILURE);
}

vm_paddr_t addr_arch_gva2gpa(struct kvm_vm *vm, vm_vaddr_t gva)
{
	uint64_t *ptep = virt_get_pte_hva(vm, gva);

	return pte_addr(vm, *ptep) + (gva & (vm->page_size - 1));
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

void virt_arch_dump(FILE *stream, struct kvm_vm *vm, uint8_t indent)
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

void aarch64_vcpu_setup(struct kvm_vcpu *vcpu, struct kvm_vcpu_init *init)
{
	struct kvm_vcpu_init default_init = { .target = -1, };
	struct kvm_vm *vm = vcpu->vm;
	uint64_t sctlr_el1, tcr_el1, ttbr0_el1;

	if (!init)
		init = &default_init;

	if (init->target == -1) {
		struct kvm_vcpu_init preferred;
		vm_ioctl(vm, KVM_ARM_PREFERRED_TARGET, &preferred);
		init->target = preferred.target;
	}

	vcpu_ioctl(vcpu, KVM_ARM_VCPU_INIT, init);

	/*
	 * Enable FP/ASIMD to avoid trapping when accessing Q0-Q15
	 * registers, which the variable argument list macros do.
	 */
	vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_CPACR_EL1), 3 << 20);

	vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_SCTLR_EL1), &sctlr_el1);
	vcpu_get_reg(vcpu, KVM_ARM64_SYS_REG(SYS_TCR_EL1), &tcr_el1);

	/* Configure base granule size */
	switch (vm->mode) {
	case VM_MODE_P52V48_4K:
		TEST_FAIL("AArch64 does not support 4K sized pages "
			  "with 52-bit physical address ranges");
	case VM_MODE_PXXV48_4K:
		TEST_FAIL("AArch64 does not support 4K sized pages "
			  "with ANY-bit physical address ranges");
	case VM_MODE_P52V48_64K:
	case VM_MODE_P48V48_64K:
	case VM_MODE_P40V48_64K:
	case VM_MODE_P36V48_64K:
		tcr_el1 |= 1ul << 14; /* TG0 = 64KB */
		break;
	case VM_MODE_P48V48_16K:
	case VM_MODE_P40V48_16K:
	case VM_MODE_P36V48_16K:
	case VM_MODE_P36V47_16K:
		tcr_el1 |= 2ul << 14; /* TG0 = 16KB */
		break;
	case VM_MODE_P48V48_4K:
	case VM_MODE_P40V48_4K:
	case VM_MODE_P36V48_4K:
		tcr_el1 |= 0ul << 14; /* TG0 = 4KB */
		break;
	default:
		TEST_FAIL("Unknown guest mode, mode: 0x%x", vm->mode);
	}

	ttbr0_el1 = vm->pgd & GENMASK(47, vm->page_shift);

	/* Configure output size */
	switch (vm->mode) {
	case VM_MODE_P52V48_64K:
		tcr_el1 |= 6ul << 32; /* IPS = 52 bits */
		ttbr0_el1 |= FIELD_GET(GENMASK(51, 48), vm->pgd) << 2;
		break;
	case VM_MODE_P48V48_4K:
	case VM_MODE_P48V48_16K:
	case VM_MODE_P48V48_64K:
		tcr_el1 |= 5ul << 32; /* IPS = 48 bits */
		break;
	case VM_MODE_P40V48_4K:
	case VM_MODE_P40V48_16K:
	case VM_MODE_P40V48_64K:
		tcr_el1 |= 2ul << 32; /* IPS = 40 bits */
		break;
	case VM_MODE_P36V48_4K:
	case VM_MODE_P36V48_16K:
	case VM_MODE_P36V48_64K:
	case VM_MODE_P36V47_16K:
		tcr_el1 |= 1ul << 32; /* IPS = 36 bits */
		break;
	default:
		TEST_FAIL("Unknown guest mode, mode: 0x%x", vm->mode);
	}

	sctlr_el1 |= (1 << 0) | (1 << 2) | (1 << 12) /* M | C | I */;
	/* TCR_EL1 |= IRGN0:WBWA | ORGN0:WBWA | SH0:Inner-Shareable */;
	tcr_el1 |= (1 << 8) | (1 << 10) | (3 << 12);
	tcr_el1 |= (64 - vm->va_bits) /* T0SZ */;

	vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_SCTLR_EL1), sctlr_el1);
	vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_TCR_EL1), tcr_el1);
	vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_MAIR_EL1), DEFAULT_MAIR_EL1);
	vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_TTBR0_EL1), ttbr0_el1);
	vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_TPIDR_EL1), vcpu->id);
}

void vcpu_arch_dump(FILE *stream, struct kvm_vcpu *vcpu, uint8_t indent)
{
	uint64_t pstate, pc;

	vcpu_get_reg(vcpu, ARM64_CORE_REG(regs.pstate), &pstate);
	vcpu_get_reg(vcpu, ARM64_CORE_REG(regs.pc), &pc);

	fprintf(stream, "%*spstate: 0x%.16lx pc: 0x%.16lx\n",
		indent, "", pstate, pc);
}

struct kvm_vcpu *aarch64_vcpu_add(struct kvm_vm *vm, uint32_t vcpu_id,
				  struct kvm_vcpu_init *init, void *guest_code)
{
	size_t stack_size;
	uint64_t stack_vaddr;
	struct kvm_vcpu *vcpu = __vm_vcpu_add(vm, vcpu_id);

	stack_size = vm->page_size == 4096 ? DEFAULT_STACK_PGS * vm->page_size :
					     vm->page_size;
	stack_vaddr = __vm_vaddr_alloc(vm, stack_size,
				       DEFAULT_ARM64_GUEST_STACK_VADDR_MIN,
				       MEM_REGION_DATA);

	aarch64_vcpu_setup(vcpu, init);

	vcpu_set_reg(vcpu, ARM64_CORE_REG(sp_el1), stack_vaddr + stack_size);
	vcpu_set_reg(vcpu, ARM64_CORE_REG(regs.pc), (uint64_t)guest_code);

	return vcpu;
}

struct kvm_vcpu *vm_arch_vcpu_add(struct kvm_vm *vm, uint32_t vcpu_id,
				  void *guest_code)
{
	return aarch64_vcpu_add(vm, vcpu_id, NULL, guest_code);
}

void vcpu_args_set(struct kvm_vcpu *vcpu, unsigned int num, ...)
{
	va_list ap;
	int i;

	TEST_ASSERT(num >= 1 && num <= 8, "Unsupported number of args,\n"
		    "  num: %u\n", num);

	va_start(ap, num);

	for (i = 0; i < num; i++) {
		vcpu_set_reg(vcpu, ARM64_CORE_REG(regs.regs[i]),
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

void assert_on_unhandled_exception(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

	if (get_ucall(vcpu, &uc) != UCALL_UNHANDLED)
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

void vcpu_init_descriptor_tables(struct kvm_vcpu *vcpu)
{
	extern char vectors;

	vcpu_set_reg(vcpu, KVM_ARM64_SYS_REG(SYS_VBAR_EL1), (uint64_t)&vectors);
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
	vm->handlers = __vm_vaddr_alloc(vm, sizeof(struct handlers),
					vm->page_size, MEM_REGION_DATA);

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

void aarch64_get_supported_page_sizes(uint32_t ipa,
				      bool *ps4k, bool *ps16k, bool *ps64k)
{
	struct kvm_vcpu_init preferred_init;
	int kvm_fd, vm_fd, vcpu_fd, err;
	uint64_t val;
	struct kvm_one_reg reg = {
		.id	= KVM_ARM64_SYS_REG(SYS_ID_AA64MMFR0_EL1),
		.addr	= (uint64_t)&val,
	};

	kvm_fd = open_kvm_dev_path_or_exit();
	vm_fd = __kvm_ioctl(kvm_fd, KVM_CREATE_VM, (void *)(unsigned long)ipa);
	TEST_ASSERT(vm_fd >= 0, KVM_IOCTL_ERROR(KVM_CREATE_VM, vm_fd));

	vcpu_fd = ioctl(vm_fd, KVM_CREATE_VCPU, 0);
	TEST_ASSERT(vcpu_fd >= 0, KVM_IOCTL_ERROR(KVM_CREATE_VCPU, vcpu_fd));

	err = ioctl(vm_fd, KVM_ARM_PREFERRED_TARGET, &preferred_init);
	TEST_ASSERT(err == 0, KVM_IOCTL_ERROR(KVM_ARM_PREFERRED_TARGET, err));
	err = ioctl(vcpu_fd, KVM_ARM_VCPU_INIT, &preferred_init);
	TEST_ASSERT(err == 0, KVM_IOCTL_ERROR(KVM_ARM_VCPU_INIT, err));

	err = ioctl(vcpu_fd, KVM_GET_ONE_REG, &reg);
	TEST_ASSERT(err == 0, KVM_IOCTL_ERROR(KVM_GET_ONE_REG, vcpu_fd));

	*ps4k = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64MMFR0_TGRAN4), val) != 0xf;
	*ps64k = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64MMFR0_TGRAN64), val) == 0;
	*ps16k = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64MMFR0_TGRAN16), val) != 0;

	close(vcpu_fd);
	close(vm_fd);
	close(kvm_fd);
}

#define __smccc_call(insn, function_id, arg0, arg1, arg2, arg3, arg4, arg5,	\
		     arg6, res)							\
	asm volatile("mov   w0, %w[function_id]\n"				\
		     "mov   x1, %[arg0]\n"					\
		     "mov   x2, %[arg1]\n"					\
		     "mov   x3, %[arg2]\n"					\
		     "mov   x4, %[arg3]\n"					\
		     "mov   x5, %[arg4]\n"					\
		     "mov   x6, %[arg5]\n"					\
		     "mov   x7, %[arg6]\n"					\
		     #insn  "#0\n"						\
		     "mov   %[res0], x0\n"					\
		     "mov   %[res1], x1\n"					\
		     "mov   %[res2], x2\n"					\
		     "mov   %[res3], x3\n"					\
		     : [res0] "=r"(res->a0), [res1] "=r"(res->a1),		\
		       [res2] "=r"(res->a2), [res3] "=r"(res->a3)		\
		     : [function_id] "r"(function_id), [arg0] "r"(arg0),	\
		       [arg1] "r"(arg1), [arg2] "r"(arg2), [arg3] "r"(arg3),	\
		       [arg4] "r"(arg4), [arg5] "r"(arg5), [arg6] "r"(arg6)	\
		     : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7")


void smccc_hvc(uint32_t function_id, uint64_t arg0, uint64_t arg1,
	       uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5,
	       uint64_t arg6, struct arm_smccc_res *res)
{
	__smccc_call(hvc, function_id, arg0, arg1, arg2, arg3, arg4, arg5,
		     arg6, res);
}

void smccc_smc(uint32_t function_id, uint64_t arg0, uint64_t arg1,
	       uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5,
	       uint64_t arg6, struct arm_smccc_res *res)
{
	__smccc_call(smc, function_id, arg0, arg1, arg2, arg3, arg4, arg5,
		     arg6, res);
}

void kvm_selftest_arch_init(void)
{
	/*
	 * arm64 doesn't have a true default mode, so start by computing the
	 * available IPA space and page sizes early.
	 */
	guest_modes_append_default();
}

void vm_vaddr_populate_bitmap(struct kvm_vm *vm)
{
	/*
	 * arm64 selftests use only TTBR0_EL1, meaning that the valid VA space
	 * is [0, 2^(64 - TCR_EL1.T0SZ)).
	 */
	sparsebit_set_num(vm->vpages_valid, 0,
			  (1ULL << vm->va_bits) >> vm->page_shift);
}
