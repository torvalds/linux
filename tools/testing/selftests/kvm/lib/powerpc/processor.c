// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM selftest powerpc library code - CPU-related functions (page tables...)
 */

#include <linux/sizes.h>

#include "processor.h"
#include "kvm_util.h"
#include "ucall_common.h"
#include "guest_modes.h"
#include "hcall.h"

#define RADIX_TREE_SIZE ((0x2UL << 61) | (0x5UL << 5)) /* 52-bits */
#define RADIX_PGD_INDEX_SIZE 13

/*
 * A radix table must be aligned to its size, and the tables are larger than
 * the guest base page size for several level/page size combinations.
 */
bool kvm_arch_needs_naturally_aligned_page_tables(void)
{
	return true;
}

static void set_proc_table(struct kvm_vm *vm, int pid, u64 dw0, u64 dw1)
{
	u64 *proc_table;

	proc_table = addr_gpa2hva(vm, vm->arch.prtb);
	proc_table[pid * 2 + 0] = cpu_to_be64(dw0);
	proc_table[pid * 2 + 1] = cpu_to_be64(dw1);
}

static void set_radix_proc_table(struct kvm_vm *vm, int pid, gpa_t pgd)
{
	set_proc_table(vm, pid, pgd | RADIX_TREE_SIZE | RADIX_PGD_INDEX_SIZE, 0);
}

void virt_arch_pgd_alloc(struct kvm_vm *vm)
{
	struct kvm_ppc_mmuv3_cfg mmu_cfg;
	gpa_t prtb, pgtb;
	size_t pgd_pages;

	TEST_ASSERT((vm->mode == VM_MODE_P52V52_4K) ||
		    (vm->mode == VM_MODE_P52V52_64K),
		    "Unsupported guest mode, mode: 0x%x", vm->mode);

	if (vm->mmu.pgd_created)
		return;

	prtb = vm_alloc_page_table(vm);
	vm->arch.prtb = prtb;

	pgd_pages = (1UL << (RADIX_PGD_INDEX_SIZE + 3)) >> vm->page_shift;
	if (!pgd_pages)
		pgd_pages = 1;
	pgtb = vm_alloc_page_table_pages(vm, pgd_pages);
	vm->mmu.pgd = pgtb;

	/* Set the base page directory in the proc table */
	set_radix_proc_table(vm, 0, pgtb);

	if (vm->mode == VM_MODE_P52V52_4K)
		mmu_cfg.process_table = prtb | 0x8000000000000000UL | 0x0; /* 4K size */
	else /* vm->mode == VM_MODE_P52V52_64K */
		mmu_cfg.process_table = prtb | 0x8000000000000000UL | 0x4; /* 64K size */
	mmu_cfg.flags = KVM_PPC_MMUV3_RADIX | KVM_PPC_MMUV3_GTSE;

	vm_ioctl(vm, KVM_PPC_CONFIGURE_V3_MMU, &mmu_cfg);

	vm->mmu.pgd_created = true;
}

static int pt_shift(struct kvm_vm *vm, int level)
{
	switch (level) {
	case 1:
		return 13;
	case 2:
	case 3:
		return 9;
	case 4:
		if (vm->mode == VM_MODE_P52V52_4K)
			return 9;
		else /* vm->mode == VM_MODE_P52V52_64K */
			return 5;
	default:
		TEST_ASSERT(false, "Invalid page table level %d\n", level);
		return 0;
	}
}

static u64 pt_entry_coverage(struct kvm_vm *vm, int level)
{
	u64 size = vm->page_size;

	if (level == 4)
		return size;
	size <<= pt_shift(vm, 4);
	if (level == 3)
		return size;
	size <<= pt_shift(vm, 3);
	if (level == 2)
		return size;
	size <<= pt_shift(vm, 2);
	return size;
}

static int pt_idx(struct kvm_vm *vm, gva_t gva, int level, u64 *nls)
{
	switch (level) {
	case 1:
		if (nls)
			*nls = 0x9;
		return (gva >> 39) & 0x1fff;
	case 2:
		if (nls)
			*nls = 0x9;
		return (gva >> 30) & 0x1ff;
	case 3:
		if (vm->mode == VM_MODE_P52V52_4K) {
			if (nls)
				*nls = 0x9;
		} else { /* vm->mode == VM_MODE_P52V52_64K */
			if (nls)
				*nls = 0x5;
		}
		return (gva >> 21) & 0x1ff;
	case 4:
		if (vm->mode == VM_MODE_P52V52_4K)
			return (gva >> 12) & 0x1ff;
		else /* vm->mode == VM_MODE_P52V52_64K */
			return (gva >> 16) & 0x1f;
	default:
		TEST_ASSERT(false, "Invalid page table level %d\n", level);
		return 0;
	}
}

static u64 *virt_get_pte(struct kvm_vm *vm, gpa_t pt,
			  gva_t gva, int level, u64 *nls)
{
	int idx = pt_idx(vm, gva, level, nls);
	u64 *ptep = addr_gpa2hva(vm, pt + idx * 8);

	return ptep;
}

#define PTE_VALID	0x8000000000000000ull
#define PTE_LEAF	0x4000000000000000ull
#define PTE_REFERENCED	0x0000000000000100ull
#define PTE_CHANGED	0x0000000000000080ull
#define PTE_PRIV	0x0000000000000008ull
#define PTE_READ	0x0000000000000004ull
#define PTE_RW		0x0000000000000002ull
#define PTE_EXEC	0x0000000000000001ull
#define PTE_PAGE_MASK	0x01fffffffffff000ull

#define PDE_VALID	PTE_VALID
#define PDE_NLS		0x0000000000000011ull
#define PDE_PT_MASK	0x0fffffffffffff00ull

static gpa_t __vm_alloc_pt(struct kvm_vm *vm, u64 pt_shift)
{
	gpa_t pt;

	if (pt_shift >= vm->page_shift) {
		size_t pt_pages = 1ULL << (pt_shift - vm->page_shift);

		pt = vm_alloc_page_table_pages(vm, pt_pages);
	} else {
		struct vm_pt_frag_cache *pt_frag_cache;

		if (pt_shift == 8) {
			pt_frag_cache = &vm->arch.pt_frag_cache[0];
		} else if (pt_shift == 12) {
			pt_frag_cache = &vm->arch.pt_frag_cache[1];
		} else {
			TEST_ASSERT(0, "Invalid pt_shift:%lu\n", pt_shift);
			return 0;
		}

		if (!pt_frag_cache->page)
			pt_frag_cache->page = vm_alloc_page_table(vm);
		pt = pt_frag_cache->page + pt_frag_cache->page_nr_used;
		pt_frag_cache->page_nr_used += (1 << pt_shift);
		if (pt_frag_cache->page_nr_used == vm->page_size) {
			pt_frag_cache->page = 0;
			pt_frag_cache->page_nr_used = 0;
		}
	}

	return pt;
}

void virt_arch_pg_map(struct kvm_vm *vm, gva_t gva, gpa_t gpa)
{
	gpa_t pt = vm->mmu.pgd;
	u64 *ptep, pte;
	int level;

	for (level = 1; level <= 3; level++) {
		u64 nls;
		u64 *pdep = virt_get_pte(vm, pt, gva, level, &nls);
		u64 pde = be64_to_cpu(*pdep);

		if (pde) {
			TEST_ASSERT((pde & PDE_VALID) && !(pde & PTE_LEAF),
				    "Invalid PDE at level: %u gva: 0x%lx pde:0x%lx\n",
				    level, gva, pde);
			pt = pde & PDE_PT_MASK;
			continue;
		}

		pt = __vm_alloc_pt(vm, nls + 3);
		pde = PDE_VALID | nls | pt;
		*pdep = cpu_to_be64(pde);
	}

	ptep = virt_get_pte(vm, pt, gva, level, NULL);
	pte = be64_to_cpu(*ptep);

	TEST_ASSERT(!pte, "PTE already present at level: %u gva: 0x%lx pte:0x%lx\n",
		    level, gva, pte);

	pte = PTE_VALID | PTE_LEAF | PTE_REFERENCED | PTE_CHANGED | PTE_PRIV |
	      PTE_READ | PTE_RW | PTE_EXEC | (gpa & PTE_PAGE_MASK);
	*ptep = cpu_to_be64(pte);
}

gpa_t addr_arch_gva2gpa(struct kvm_vm *vm, gva_t gva)
{
	gpa_t pt = vm->mmu.pgd;
	u64 *ptep, pte;
	int level;

	for (level = 1; level <= 3; level++) {
		u64 nls;
		u64 *pdep = virt_get_pte(vm, pt, gva, level, &nls);
		u64 pde = be64_to_cpu(*pdep);

		TEST_ASSERT((pde & PDE_VALID) && !(pde & PTE_LEAF),
			"PDE not present at level: %u gva: 0x%lx pde:0x%lx\n",
			level, gva, pde);
		pt = pde & PDE_PT_MASK;
	}

	ptep = virt_get_pte(vm, pt, gva, level, NULL);
	pte = be64_to_cpu(*ptep);

	TEST_ASSERT(pte,
		"PTE not present at level: %u gva: 0x%lx pte:0x%lx\n",
		level, gva, pte);

	TEST_ASSERT((pte & PTE_VALID) && (pte & PTE_LEAF),
		    "PTE not valid at level: %u gva: 0x%lx pte:0x%lx\n",
		    level, gva, pte);

	return (pte & PTE_PAGE_MASK) + (gva & (vm->page_size - 1));
}

static void virt_dump_pt(FILE *stream, struct kvm_vm *vm, gpa_t pt,
			 gva_t va, int level, u8 indent)
{
	int size, idx;

	size = 1U << (pt_shift(vm, level) + 3);

	for (idx = 0; idx < size; idx += 8, va += pt_entry_coverage(vm, level)) {
		u64 *page_table = addr_gpa2hva(vm, pt + idx);
		u64 pte = be64_to_cpu(*page_table);

		if (!(pte & PTE_VALID))
			continue;

		if (pte & PTE_LEAF) {
			fprintf(stream,
				"%*s PTE[%d] gVA:0x%016lx -> gRA:0x%016llx\n",
				indent, "", idx / 8, va, pte & PTE_PAGE_MASK);
		} else {
			fprintf(stream, "%*sPDE%d[%d] gVA:0x%016lx\n",
				indent, "", level, idx / 8, va);
			virt_dump_pt(stream, vm, pte & PDE_PT_MASK, va,
				     level + 1, indent + 2);
		}
	}

}

void virt_arch_dump(FILE *stream, struct kvm_vm *vm, u8 indent)
{
	gpa_t pt = vm->mmu.pgd;

	if (!vm->mmu.pgd_created)
		return;

	virt_dump_pt(stream, vm, pt, 0, 1, indent);
}

static unsigned long get_r2(void)
{
	unsigned long r2;

	asm("mr %0,%%r2" : "=r"(r2));

	return r2;
}

/*
 * Under ELFv1 a function pointer is the address of a descriptor holding the
 * entry point and the TOC, not the entry point itself.  ELFv2 dropped
 * descriptors, i.e. the pointer is already the entry point.  Dereference the
 * descriptor as needed so that the guest starts executing code and not data.
 */
static uintptr_t function_entry(void *fn)
{
#if defined(_CALL_ELF) && _CALL_ELF == 2
	return (uintptr_t)fn;
#else
	return ((const u64 *)fn)[0];
#endif
}

void vcpu_arch_set_entry_point(struct kvm_vcpu *vcpu, void *guest_code)
{
	uintptr_t entry = function_entry(guest_code);
	struct kvm_regs regs;

	vcpu_regs_get(vcpu, &regs);
	regs.pc = entry;
	regs.gpr[12] = entry;
	vcpu_regs_set(vcpu, &regs);
}

struct kvm_vcpu *vm_arch_vcpu_add(struct kvm_vm *vm, u32 vcpu_id)
{
	const size_t stack_size = SZ_64K;
	gva_t stack_vaddr, ex_regs_vaddr;
	gpa_t ex_regs_paddr;
	struct ex_regs *ex_regs;
	struct kvm_regs regs;
	struct kvm_vcpu *vcpu;
	u64 lpcr;

	stack_vaddr = __vm_alloc(vm, stack_size,
				       DEFAULT_GUEST_STACK_VADDR_MIN,
				       MEM_REGION_DATA);

	ex_regs_vaddr = __vm_alloc(vm, stack_size,
				       DEFAULT_GUEST_STACK_VADDR_MIN,
				       MEM_REGION_DATA);
	ex_regs_paddr = addr_gva2gpa(vm, ex_regs_vaddr);
	ex_regs = addr_gpa2hva(vm, ex_regs_paddr);
	ex_regs->gva = ex_regs_vaddr;

	vcpu = __vm_vcpu_add(vm, vcpu_id);

	vcpu_enable_cap(vcpu, KVM_CAP_PPC_PAPR, 1);

	/* Setup guest registers */
	vcpu_regs_get(vcpu, &regs);
	lpcr = vcpu_get_reg(vcpu, KVM_REG_PPC_LPCR_64);

	regs.gpr[1] = stack_vaddr + stack_size - 256;
	regs.gpr[2] = (uintptr_t)get_r2();
	regs.gpr[13] = (uintptr_t)ex_regs_paddr;

	regs.msr = MSR_SF | MSR_VEC | MSR_VSX | MSR_FP |
		   MSR_ME | MSR_IR | MSR_DR | MSR_RI;

	if (BYTE_ORDER == LITTLE_ENDIAN) {
		regs.msr |= MSR_LE;
		lpcr |= LPCR_ILE;
	} else {
		lpcr &= ~LPCR_ILE;
	}

	vcpu_regs_set(vcpu, &regs);
	vcpu_set_reg(vcpu, KVM_REG_PPC_LPCR_64, lpcr);

	return vcpu;
}

void vcpu_args_set(struct kvm_vcpu *vcpu, unsigned int num, ...)
{
	va_list ap;
	struct kvm_regs regs;
	int i;

	TEST_ASSERT(num >= 1 && num <= 5, "Unsupported number of args: %u\n",
		    num);

	va_start(ap, num);
	vcpu_regs_get(vcpu, &regs);

	for (i = 0; i < num; i++)
		regs.gpr[i + 3] = va_arg(ap, u64);

	vcpu_regs_set(vcpu, &regs);
	va_end(ap);
}

void vcpu_arch_dump(FILE *stream, struct kvm_vcpu *vcpu, u8 indent)
{
	struct kvm_regs regs;

	vcpu_regs_get(vcpu, &regs);

	fprintf(stream, "%*sNIA: 0x%016llx  MSR: 0x%016llx\n",
			indent, "", regs.pc, regs.msr);
	fprintf(stream, "%*sLR:  0x%016llx  CTR :0x%016llx\n",
			indent, "", regs.lr, regs.ctr);
	fprintf(stream, "%*sCR:  0x%08llx          XER :0x%016llx\n",
			indent, "", regs.cr, regs.xer);
}

void kvm_arch_vm_post_create(struct kvm_vm *vm, unsigned int nr_vcpus)
{
	size_t excp_size = __interrupts_end - __interrupts_start;
	size_t excp_pages = (excp_size + vm->page_size - 1) / vm->page_size;
	gpa_t excp_paddr;
	void *mem;

	/*
	 * Guest interrupts are taken in real-mode, i.e. the vectors must live
	 * at gPA 0.  Use the innermost allocator to override the minimum GPA
	 * that MEM_REGION_DATA would otherwise impose.
	 */
	excp_paddr = ____vm_phy_pages_alloc(vm, excp_pages, 0,
					    vm->memslots[MEM_REGION_DATA],
					    vm_arch_has_protected_memory(vm),
					    false);

	TEST_ASSERT(excp_paddr == 0,
		    "Interrupt vectors not allocated at gPA address 0: (0x%lx)",
		    excp_paddr);

	mem = addr_gpa2hva(vm, excp_paddr);
	memcpy(mem, __interrupts_start, excp_size);
}

void assert_on_unhandled_exception(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

	if (get_ucall(vcpu, &uc) == UCALL_UNHANDLED) {
		gpa_t ex_regs_paddr;
		struct ex_regs *ex_regs;
		struct kvm_regs regs;

		vcpu_regs_get(vcpu, &regs);
		ex_regs_paddr = (gpa_t)regs.gpr[13];
		ex_regs = addr_gpa2hva(vcpu->vm, ex_regs_paddr);

		TEST_FAIL("Unexpected interrupt in guest NIA:0x%016lx MSR:0x%016lx TRAP:0x%04x",
			  ex_regs->nia, ex_regs->msr, ex_regs->trap);
	}
}

struct handler {
	void (*fn)(struct ex_regs *regs);
	int trap;
};

#define NR_HANDLERS	10
static struct handler handlers[NR_HANDLERS];

void route_interrupt(struct ex_regs *regs)
{
	int i;

	for (i = 0; i < NR_HANDLERS; i++) {
		if (handlers[i].trap == regs->trap) {
			handlers[i].fn(regs);
			return;
		}
	}

	ucall(UCALL_UNHANDLED, 0);
}

void vm_install_exception_handler(struct kvm_vm *vm, int trap,
			       void (*fn)(struct ex_regs *))
{
	int i;

	for (i = 0; i < NR_HANDLERS; i++) {
		if (!handlers[i].trap || handlers[i].trap == trap) {
			if (fn == NULL)
				trap = 0; /* Clear handler */
			handlers[i].trap = trap;
			handlers[i].fn = fn;
			sync_global_to_guest(vm, handlers[i]);
			return;
		}
	}

	TEST_FAIL("Out of exception handlers");
}

void kvm_selftest_arch_init(void)
{
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_PPC_MMU_RADIX));

	/*
	 * powerpc default mode is set by host page size and not static,
	 * so start by computing that early.
	 */
	guest_modes_append_default();
}
