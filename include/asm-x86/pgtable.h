#ifndef _ASM_X86_PGTABLE_H
#define _ASM_X86_PGTABLE_H

#define USER_PTRS_PER_PGD	((TASK_SIZE-1)/PGDIR_SIZE+1)
#define FIRST_USER_ADDRESS	0

#define _PAGE_BIT_PRESENT	0	/* is present */
#define _PAGE_BIT_RW		1	/* writeable */
#define _PAGE_BIT_USER		2	/* userspace addressable */
#define _PAGE_BIT_PWT		3	/* page write through */
#define _PAGE_BIT_PCD		4	/* page cache disabled */
#define _PAGE_BIT_ACCESSED	5	/* was accessed (raised by CPU) */
#define _PAGE_BIT_DIRTY		6	/* was written to (raised by CPU) */
#define _PAGE_BIT_FILE		6
#define _PAGE_BIT_PSE		7	/* 4 MB (or 2MB) page */
#define _PAGE_BIT_PAT		7	/* on 4KB pages */
#define _PAGE_BIT_GLOBAL	8	/* Global TLB entry PPro+ */
#define _PAGE_BIT_UNUSED1	9	/* available for programmer */
#define _PAGE_BIT_UNUSED2	10
#define _PAGE_BIT_UNUSED3	11
#define _PAGE_BIT_PAT_LARGE	12	/* On 2MB or 1GB pages */
#define _PAGE_BIT_NX           63       /* No execute: only valid after cpuid check */

/*
 * Note: we use _AC(1, L) instead of _AC(1, UL) so that we get a
 * sign-extended value on 32-bit with all 1's in the upper word,
 * which preserves the upper pte values on 64-bit ptes:
 */
#define _PAGE_PRESENT	(_AC(1, L)<<_PAGE_BIT_PRESENT)
#define _PAGE_RW	(_AC(1, L)<<_PAGE_BIT_RW)
#define _PAGE_USER	(_AC(1, L)<<_PAGE_BIT_USER)
#define _PAGE_PWT	(_AC(1, L)<<_PAGE_BIT_PWT)
#define _PAGE_PCD	(_AC(1, L)<<_PAGE_BIT_PCD)
#define _PAGE_ACCESSED	(_AC(1, L)<<_PAGE_BIT_ACCESSED)
#define _PAGE_DIRTY	(_AC(1, L)<<_PAGE_BIT_DIRTY)
#define _PAGE_PSE	(_AC(1, L)<<_PAGE_BIT_PSE)	/* 2MB page */
#define _PAGE_GLOBAL	(_AC(1, L)<<_PAGE_BIT_GLOBAL)	/* Global TLB entry */
#define _PAGE_UNUSED1	(_AC(1, L)<<_PAGE_BIT_UNUSED1)
#define _PAGE_UNUSED2	(_AC(1, L)<<_PAGE_BIT_UNUSED2)
#define _PAGE_UNUSED3	(_AC(1, L)<<_PAGE_BIT_UNUSED3)
#define _PAGE_PAT	(_AC(1, L)<<_PAGE_BIT_PAT)
#define _PAGE_PAT_LARGE (_AC(1, L)<<_PAGE_BIT_PAT_LARGE)

#if defined(CONFIG_X86_64) || defined(CONFIG_X86_PAE)
#define _PAGE_NX	(_AC(1, ULL) << _PAGE_BIT_NX)
#else
#define _PAGE_NX	0
#endif

/* If _PAGE_PRESENT is clear, we use these: */
#define _PAGE_FILE	_PAGE_DIRTY	/* nonlinear file mapping,
					 * saved PTE; unset:swap */
#define _PAGE_PROTNONE	_PAGE_PSE	/* if the user mapped it with PROT_NONE;
					   pte_present gives true */

#define _PAGE_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER |	\
			 _PAGE_ACCESSED | _PAGE_DIRTY)
#define _KERNPG_TABLE	(_PAGE_PRESENT | _PAGE_RW | _PAGE_ACCESSED |	\
			 _PAGE_DIRTY)

#define _PAGE_CHG_MASK	(PTE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)

#define _PAGE_CACHE_MASK	(_PAGE_PCD | _PAGE_PWT)
#define _PAGE_CACHE_WB		(0)
#define _PAGE_CACHE_WC		(_PAGE_PWT)
#define _PAGE_CACHE_UC_MINUS	(_PAGE_PCD)
#define _PAGE_CACHE_UC		(_PAGE_PCD | _PAGE_PWT)

#define PAGE_NONE	__pgprot(_PAGE_PROTNONE | _PAGE_ACCESSED)
#define PAGE_SHARED	__pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_USER | \
				 _PAGE_ACCESSED | _PAGE_NX)

#define PAGE_SHARED_EXEC	__pgprot(_PAGE_PRESENT | _PAGE_RW |	\
					 _PAGE_USER | _PAGE_ACCESSED)
#define PAGE_COPY_NOEXEC	__pgprot(_PAGE_PRESENT | _PAGE_USER |	\
					 _PAGE_ACCESSED | _PAGE_NX)
#define PAGE_COPY_EXEC		__pgprot(_PAGE_PRESENT | _PAGE_USER |	\
					 _PAGE_ACCESSED)
#define PAGE_COPY		PAGE_COPY_NOEXEC
#define PAGE_READONLY		__pgprot(_PAGE_PRESENT | _PAGE_USER |	\
					 _PAGE_ACCESSED | _PAGE_NX)
#define PAGE_READONLY_EXEC	__pgprot(_PAGE_PRESENT | _PAGE_USER |	\
					 _PAGE_ACCESSED)

#ifdef CONFIG_X86_32
#define _PAGE_KERNEL_EXEC \
	(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define _PAGE_KERNEL (_PAGE_KERNEL_EXEC | _PAGE_NX)

#ifndef __ASSEMBLY__
extern pteval_t __PAGE_KERNEL, __PAGE_KERNEL_EXEC;
#endif	/* __ASSEMBLY__ */
#else
#define __PAGE_KERNEL_EXEC						\
	(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED)
#define __PAGE_KERNEL		(__PAGE_KERNEL_EXEC | _PAGE_NX)
#endif

#define __PAGE_KERNEL_RO		(__PAGE_KERNEL & ~_PAGE_RW)
#define __PAGE_KERNEL_RX		(__PAGE_KERNEL_EXEC & ~_PAGE_RW)
#define __PAGE_KERNEL_EXEC_NOCACHE	(__PAGE_KERNEL_EXEC | _PAGE_PCD | _PAGE_PWT)
#define __PAGE_KERNEL_WC		(__PAGE_KERNEL | _PAGE_CACHE_WC)
#define __PAGE_KERNEL_NOCACHE		(__PAGE_KERNEL | _PAGE_PCD | _PAGE_PWT)
#define __PAGE_KERNEL_UC_MINUS		(__PAGE_KERNEL | _PAGE_PCD)
#define __PAGE_KERNEL_VSYSCALL		(__PAGE_KERNEL_RX | _PAGE_USER)
#define __PAGE_KERNEL_VSYSCALL_NOCACHE	(__PAGE_KERNEL_VSYSCALL | _PAGE_PCD | _PAGE_PWT)
#define __PAGE_KERNEL_LARGE		(__PAGE_KERNEL | _PAGE_PSE)
#define __PAGE_KERNEL_LARGE_EXEC	(__PAGE_KERNEL_EXEC | _PAGE_PSE)

#ifdef CONFIG_X86_32
# define MAKE_GLOBAL(x)			__pgprot((x))
#else
# define MAKE_GLOBAL(x)			__pgprot((x) | _PAGE_GLOBAL)
#endif

#define PAGE_KERNEL			MAKE_GLOBAL(__PAGE_KERNEL)
#define PAGE_KERNEL_RO			MAKE_GLOBAL(__PAGE_KERNEL_RO)
#define PAGE_KERNEL_EXEC		MAKE_GLOBAL(__PAGE_KERNEL_EXEC)
#define PAGE_KERNEL_RX			MAKE_GLOBAL(__PAGE_KERNEL_RX)
#define PAGE_KERNEL_WC			MAKE_GLOBAL(__PAGE_KERNEL_WC)
#define PAGE_KERNEL_NOCACHE		MAKE_GLOBAL(__PAGE_KERNEL_NOCACHE)
#define PAGE_KERNEL_UC_MINUS		MAKE_GLOBAL(__PAGE_KERNEL_UC_MINUS)
#define PAGE_KERNEL_EXEC_NOCACHE	MAKE_GLOBAL(__PAGE_KERNEL_EXEC_NOCACHE)
#define PAGE_KERNEL_LARGE		MAKE_GLOBAL(__PAGE_KERNEL_LARGE)
#define PAGE_KERNEL_LARGE_EXEC		MAKE_GLOBAL(__PAGE_KERNEL_LARGE_EXEC)
#define PAGE_KERNEL_VSYSCALL		MAKE_GLOBAL(__PAGE_KERNEL_VSYSCALL)
#define PAGE_KERNEL_VSYSCALL_NOCACHE	MAKE_GLOBAL(__PAGE_KERNEL_VSYSCALL_NOCACHE)

/*         xwr */
#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY_EXEC
#define __P101	PAGE_READONLY_EXEC
#define __P110	PAGE_COPY_EXEC
#define __P111	PAGE_COPY_EXEC

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY_EXEC
#define __S101	PAGE_READONLY_EXEC
#define __S110	PAGE_SHARED_EXEC
#define __S111	PAGE_SHARED_EXEC

#ifndef __ASSEMBLY__

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)];
#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

extern spinlock_t pgd_lock;
extern struct list_head pgd_list;

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
static inline int pte_dirty(pte_t pte)
{
	return pte_val(pte) & _PAGE_DIRTY;
}

static inline int pte_young(pte_t pte)
{
	return pte_val(pte) & _PAGE_ACCESSED;
}

static inline int pte_write(pte_t pte)
{
	return pte_val(pte) & _PAGE_RW;
}

static inline int pte_file(pte_t pte)
{
	return pte_val(pte) & _PAGE_FILE;
}

static inline int pte_huge(pte_t pte)
{
	return pte_val(pte) & _PAGE_PSE;
}

static inline int pte_global(pte_t pte)
{
	return pte_val(pte) & _PAGE_GLOBAL;
}

static inline int pte_exec(pte_t pte)
{
	return !(pte_val(pte) & _PAGE_NX);
}

static inline int pmd_large(pmd_t pte)
{
	return (pmd_val(pte) & (_PAGE_PSE | _PAGE_PRESENT)) ==
		(_PAGE_PSE | _PAGE_PRESENT);
}

static inline pte_t pte_mkclean(pte_t pte)
{
	return __pte(pte_val(pte) & ~(pteval_t)_PAGE_DIRTY);
}

static inline pte_t pte_mkold(pte_t pte)
{
	return __pte(pte_val(pte) & ~(pteval_t)_PAGE_ACCESSED);
}

static inline pte_t pte_wrprotect(pte_t pte)
{
	return __pte(pte_val(pte) & ~(pteval_t)_PAGE_RW);
}

static inline pte_t pte_mkexec(pte_t pte)
{
	return __pte(pte_val(pte) & ~(pteval_t)_PAGE_NX);
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_DIRTY);
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_ACCESSED);
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_RW);
}

static inline pte_t pte_mkhuge(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_PSE);
}

static inline pte_t pte_clrhuge(pte_t pte)
{
	return __pte(pte_val(pte) & ~(pteval_t)_PAGE_PSE);
}

static inline pte_t pte_mkglobal(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_GLOBAL);
}

static inline pte_t pte_clrglobal(pte_t pte)
{
	return __pte(pte_val(pte) & ~(pteval_t)_PAGE_GLOBAL);
}

extern pteval_t __supported_pte_mask;

static inline pte_t pfn_pte(unsigned long page_nr, pgprot_t pgprot)
{
	return __pte((((phys_addr_t)page_nr << PAGE_SHIFT) |
		      pgprot_val(pgprot)) & __supported_pte_mask);
}

static inline pmd_t pfn_pmd(unsigned long page_nr, pgprot_t pgprot)
{
	return __pmd((((phys_addr_t)page_nr << PAGE_SHIFT) |
		      pgprot_val(pgprot)) & __supported_pte_mask);
}

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	pteval_t val = pte_val(pte);

	/*
	 * Chop off the NX bit (if present), and add the NX portion of
	 * the newprot (if present):
	 */
	val &= _PAGE_CHG_MASK & ~_PAGE_NX;
	val |= pgprot_val(newprot) & __supported_pte_mask;

	return __pte(val);
}

#define pte_pgprot(x) __pgprot(pte_val(x) & (0xfff | _PAGE_NX))

#define canon_pgprot(p) __pgprot(pgprot_val(p) & __supported_pte_mask)

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else  /* !CONFIG_PARAVIRT */
#define set_pte(ptep, pte)		native_set_pte(ptep, pte)
#define set_pte_at(mm, addr, ptep, pte)	native_set_pte_at(mm, addr, ptep, pte)

#define set_pte_present(mm, addr, ptep, pte)				\
	native_set_pte_present(mm, addr, ptep, pte)
#define set_pte_atomic(ptep, pte)					\
	native_set_pte_atomic(ptep, pte)

#define set_pmd(pmdp, pmd)		native_set_pmd(pmdp, pmd)

#ifndef __PAGETABLE_PUD_FOLDED
#define set_pgd(pgdp, pgd)		native_set_pgd(pgdp, pgd)
#define pgd_clear(pgd)			native_pgd_clear(pgd)
#endif

#ifndef set_pud
# define set_pud(pudp, pud)		native_set_pud(pudp, pud)
#endif

#ifndef __PAGETABLE_PMD_FOLDED
#define pud_clear(pud)			native_pud_clear(pud)
#endif

#define pte_clear(mm, addr, ptep)	native_pte_clear(mm, addr, ptep)
#define pmd_clear(pmd)			native_pmd_clear(pmd)

#define pte_update(mm, addr, ptep)              do { } while (0)
#define pte_update_defer(mm, addr, ptep)        do { } while (0)
#endif	/* CONFIG_PARAVIRT */

#endif	/* __ASSEMBLY__ */

#ifdef CONFIG_X86_32
# include "pgtable_32.h"
#else
# include "pgtable_64.h"
#endif

#ifndef __ASSEMBLY__

enum {
	PG_LEVEL_NONE,
	PG_LEVEL_4K,
	PG_LEVEL_2M,
	PG_LEVEL_1G,
};

/*
 * Helper function that returns the kernel pagetable entry controlling
 * the virtual address 'address'. NULL means no pagetable entry present.
 * NOTE: the return type is pte_t but if the pmd is PSE then we return it
 * as a pte too.
 */
extern pte_t *lookup_address(unsigned long address, unsigned int *level);

/* local pte updates need not use xchg for locking */
static inline pte_t native_local_ptep_get_and_clear(pte_t *ptep)
{
	pte_t res = *ptep;

	/* Pure native function needs no input for mm, addr */
	native_pte_clear(NULL, 0, ptep);
	return res;
}

static inline void native_set_pte_at(struct mm_struct *mm, unsigned long addr,
				     pte_t *ptep , pte_t pte)
{
	native_set_pte(ptep, pte);
}

#ifndef CONFIG_PARAVIRT
/*
 * Rules for using pte_update - it must be called after any PTE update which
 * has not been done using the set_pte / clear_pte interfaces.  It is used by
 * shadow mode hypervisors to resynchronize the shadow page tables.  Kernel PTE
 * updates should either be sets, clears, or set_pte_atomic for P->P
 * transitions, which means this hook should only be called for user PTEs.
 * This hook implies a P->P protection or access change has taken place, which
 * requires a subsequent TLB flush.  The notification can optionally be delayed
 * until the TLB flush event by using the pte_update_defer form of the
 * interface, but care must be taken to assure that the flush happens while
 * still holding the same page table lock so that the shadow and primary pages
 * do not become out of sync on SMP.
 */
#define pte_update(mm, addr, ptep)		do { } while (0)
#define pte_update_defer(mm, addr, ptep)	do { } while (0)
#endif

/*
 * We only update the dirty/accessed state if we set
 * the dirty bit by hand in the kernel, since the hardware
 * will do the accessed bit for us, and we don't want to
 * race with other CPU's that might be updating the dirty
 * bit at the same time.
 */
#define  __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
#define ptep_set_access_flags(vma, address, ptep, entry, dirty)		\
({									\
	int __changed = !pte_same(*(ptep), entry);			\
	if (__changed && dirty) {					\
		*ptep = entry;						\
		pte_update_defer((vma)->vm_mm, (address), (ptep));	\
		flush_tlb_page(vma, address);				\
	}								\
	__changed;							\
})

#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define ptep_test_and_clear_young(vma, addr, ptep) ({			\
	int __ret = 0;							\
	if (pte_young(*(ptep)))						\
		__ret = test_and_clear_bit(_PAGE_BIT_ACCESSED,		\
					   &(ptep)->pte);		\
	if (__ret)							\
		pte_update((vma)->vm_mm, addr, ptep);			\
	__ret;								\
})

#define __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
#define ptep_clear_flush_young(vma, address, ptep)			\
({									\
	int __young;							\
	__young = ptep_test_and_clear_young((vma), (address), (ptep));	\
	if (__young)							\
		flush_tlb_page(vma, address);				\
	__young;							\
})

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
				       pte_t *ptep)
{
	pte_t pte = native_ptep_get_and_clear(ptep);
	pte_update(mm, addr, ptep);
	return pte;
}

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR_FULL
static inline pte_t ptep_get_and_clear_full(struct mm_struct *mm,
					    unsigned long addr, pte_t *ptep,
					    int full)
{
	pte_t pte;
	if (full) {
		/*
		 * Full address destruction in progress; paravirt does not
		 * care about updates and native needs no locking
		 */
		pte = native_local_ptep_get_and_clear(ptep);
	} else {
		pte = ptep_get_and_clear(mm, addr, ptep);
	}
	return pte;
}

#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm,
				      unsigned long addr, pte_t *ptep)
{
	clear_bit(_PAGE_BIT_RW, (unsigned long *)&ptep->pte);
	pte_update(mm, addr, ptep);
}

#include <asm-generic/pgtable.h>
#endif	/* __ASSEMBLY__ */

#endif	/* _ASM_X86_PGTABLE_H */
