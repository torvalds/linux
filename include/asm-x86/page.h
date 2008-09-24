#ifndef _ASM_X86_PAGE_H
#define _ASM_X86_PAGE_H

#include <linux/const.h>

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__

#define __PHYSICAL_MASK		((phys_addr_t)(1ULL << __PHYSICAL_MASK_SHIFT) - 1)
#define __VIRTUAL_MASK		((1UL << __VIRTUAL_MASK_SHIFT) - 1)

/* Cast PAGE_MASK to a signed type so that it is sign-extended if
   virtual addresses are 32-bits but physical addresses are larger
   (ie, 32-bit PAE). */
#define PHYSICAL_PAGE_MASK	(((signed long)PAGE_MASK) & __PHYSICAL_MASK)

/* PTE_PFN_MASK extracts the PFN from a (pte|pmd|pud|pgd)val_t */
#define PTE_PFN_MASK		((pteval_t)PHYSICAL_PAGE_MASK)

/* PTE_FLAGS_MASK extracts the flags from a (pte|pmd|pud|pgd)val_t */
#define PTE_FLAGS_MASK		(~PTE_PFN_MASK)

#define PMD_PAGE_SIZE		(_AC(1, UL) << PMD_SHIFT)
#define PMD_PAGE_MASK		(~(PMD_PAGE_SIZE-1))

#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		(_AC(1,UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

#define HUGE_MAX_HSTATE 2

#ifndef __ASSEMBLY__
#include <linux/types.h>
#endif

#ifdef CONFIG_X86_64
#include <asm/page_64.h>
#else
#include <asm/page_32.h>
#endif	/* CONFIG_X86_64 */

#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)

#define VM_DATA_DEFAULT_FLAGS \
	(((current->personality & READ_IMPLIES_EXEC) ? VM_EXEC : 0 ) | \
	 VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)


#ifndef __ASSEMBLY__

typedef struct { pgdval_t pgd; } pgd_t;
typedef struct { pgprotval_t pgprot; } pgprot_t;

extern int page_is_ram(unsigned long pagenr);
extern int pagerange_is_ram(unsigned long start, unsigned long end);
extern int devmem_is_allowed(unsigned long pagenr);
extern void map_devmem(unsigned long pfn, unsigned long size,
		       pgprot_t vma_prot);
extern void unmap_devmem(unsigned long pfn, unsigned long size,
			 pgprot_t vma_prot);

extern unsigned long max_low_pfn_mapped;
extern unsigned long max_pfn_mapped;

struct page;

static inline void clear_user_page(void *page, unsigned long vaddr,
				struct page *pg)
{
	clear_page(page);
}

static inline void copy_user_page(void *to, void *from, unsigned long vaddr,
				struct page *topage)
{
	copy_page(to, from);
}

#define __alloc_zeroed_user_highpage(movableflags, vma, vaddr) \
	alloc_page_vma(GFP_HIGHUSER | __GFP_ZERO | movableflags, vma, vaddr)
#define __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE

static inline pgd_t native_make_pgd(pgdval_t val)
{
	return (pgd_t) { val };
}

static inline pgdval_t native_pgd_val(pgd_t pgd)
{
	return pgd.pgd;
}

#if PAGETABLE_LEVELS >= 3
#if PAGETABLE_LEVELS == 4
typedef struct { pudval_t pud; } pud_t;

static inline pud_t native_make_pud(pmdval_t val)
{
	return (pud_t) { val };
}

static inline pudval_t native_pud_val(pud_t pud)
{
	return pud.pud;
}
#else	/* PAGETABLE_LEVELS == 3 */
#include <asm-generic/pgtable-nopud.h>

static inline pudval_t native_pud_val(pud_t pud)
{
	return native_pgd_val(pud.pgd);
}
#endif	/* PAGETABLE_LEVELS == 4 */

typedef struct { pmdval_t pmd; } pmd_t;

static inline pmd_t native_make_pmd(pmdval_t val)
{
	return (pmd_t) { val };
}

static inline pmdval_t native_pmd_val(pmd_t pmd)
{
	return pmd.pmd;
}
#else  /* PAGETABLE_LEVELS == 2 */
#include <asm-generic/pgtable-nopmd.h>

static inline pmdval_t native_pmd_val(pmd_t pmd)
{
	return native_pgd_val(pmd.pud.pgd);
}
#endif	/* PAGETABLE_LEVELS >= 3 */

static inline pte_t native_make_pte(pteval_t val)
{
	return (pte_t) { .pte = val };
}

static inline pteval_t native_pte_val(pte_t pte)
{
	return pte.pte;
}

static inline pteval_t native_pte_flags(pte_t pte)
{
	return native_pte_val(pte) & PTE_FLAGS_MASK;
}

#define pgprot_val(x)	((x).pgprot)
#define __pgprot(x)	((pgprot_t) { (x) } )

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else  /* !CONFIG_PARAVIRT */

#define pgd_val(x)	native_pgd_val(x)
#define __pgd(x)	native_make_pgd(x)

#ifndef __PAGETABLE_PUD_FOLDED
#define pud_val(x)	native_pud_val(x)
#define __pud(x)	native_make_pud(x)
#endif

#ifndef __PAGETABLE_PMD_FOLDED
#define pmd_val(x)	native_pmd_val(x)
#define __pmd(x)	native_make_pmd(x)
#endif

#define pte_val(x)	native_pte_val(x)
#define pte_flags(x)	native_pte_flags(x)
#define __pte(x)	native_make_pte(x)

#endif	/* CONFIG_PARAVIRT */

#define __pa(x)		__phys_addr((unsigned long)(x))
/* __pa_symbol should be used for C visible symbols.
   This seems to be the official gcc blessed way to do such arithmetic. */
#define __pa_symbol(x)	__pa(__phys_reloc_hide((unsigned long)(x)))

#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))

#define __boot_va(x)		__va(x)
#define __boot_pa(x)		__pa(x)

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_kaddr(pfn)      __va((pfn) << PAGE_SHIFT)
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#endif	/* __ASSEMBLY__ */

#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

#define __HAVE_ARCH_GATE_AREA 1

#endif	/* __KERNEL__ */
#endif	/* _ASM_X86_PAGE_H */
