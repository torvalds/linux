#ifndef _ASM_X86_PAGE_H
#define _ASM_X86_PAGE_H

#include <linux/const.h>

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#define PHYSICAL_PAGE_MASK	(PAGE_MASK & __PHYSICAL_MASK)
#define PTE_MASK		PHYSICAL_PAGE_MASK

#define LARGE_PAGE_SIZE		(_AC(1,UL) << PMD_SHIFT)
#define LARGE_PAGE_MASK		(~(LARGE_PAGE_SIZE-1))

#define HPAGE_SHIFT		PMD_SHIFT
#define HPAGE_SIZE		(_AC(1,UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

#define __PHYSICAL_MASK		_AT(phys_addr_t, (_AC(1,ULL) << __PHYSICAL_MASK_SHIFT) - 1)
#define __VIRTUAL_MASK		((_AC(1,UL) << __VIRTUAL_MASK_SHIFT) - 1)

#ifndef __ASSEMBLY__
#include <linux/types.h>
#endif

#ifdef CONFIG_X86_64
#define PAGETABLE_LEVELS	4

#define THREAD_ORDER	1
#define THREAD_SIZE  (PAGE_SIZE << THREAD_ORDER)
#define CURRENT_MASK (~(THREAD_SIZE-1))

#define EXCEPTION_STACK_ORDER 0
#define EXCEPTION_STKSZ (PAGE_SIZE << EXCEPTION_STACK_ORDER)

#define DEBUG_STACK_ORDER (EXCEPTION_STACK_ORDER + 1)
#define DEBUG_STKSZ (PAGE_SIZE << DEBUG_STACK_ORDER)

#define IRQSTACK_ORDER 2
#define IRQSTACKSIZE (PAGE_SIZE << IRQSTACK_ORDER)

#define STACKFAULT_STACK 1
#define DOUBLEFAULT_STACK 2
#define NMI_STACK 3
#define DEBUG_STACK 4
#define MCE_STACK 5
#define N_EXCEPTION_STACKS 5  /* hw limit: 7 */

#define __PAGE_OFFSET           _AC(0xffff810000000000, UL)

#define __PHYSICAL_START	CONFIG_PHYSICAL_START
#define __KERNEL_ALIGN		0x200000

/*
 * Make sure kernel is aligned to 2MB address. Catching it at compile
 * time is better. Change your config file and compile the kernel
 * for a 2MB aligned address (CONFIG_PHYSICAL_START)
 */
#if (CONFIG_PHYSICAL_START % __KERNEL_ALIGN) != 0
#error "CONFIG_PHYSICAL_START must be a multiple of 2MB"
#endif

#define __START_KERNEL		(__START_KERNEL_map + __PHYSICAL_START)
#define __START_KERNEL_map	_AC(0xffffffff80000000, UL)

/* See Documentation/x86_64/mm.txt for a description of the memory map. */
#define __PHYSICAL_MASK_SHIFT	46
#define __VIRTUAL_MASK_SHIFT	48

#define KERNEL_TEXT_SIZE  (40*1024*1024)
#define KERNEL_TEXT_START _AC(0xffffffff80000000, UL)

#ifndef __ASSEMBLY__
void clear_page(void *page);
void copy_page(void *to, void *from);

/*
 * These are used to make use of C type-checking..
 */
typedef unsigned long	pteval_t;
typedef unsigned long	pmdval_t;
typedef unsigned long	pudval_t;
typedef unsigned long	pgdval_t;
typedef unsigned long	pgprotval_t;
typedef unsigned long	phys_addr_t;

typedef struct { pteval_t pte; } pte_t;

#define native_pte_val(x)	((x).pte)
#define native_make_pte(x) ((pte_t) { (x) } )

#endif	/* !__ASSEMBLY__ */

#endif	/* CONFIG_X86_64 */

#ifdef CONFIG_X86_32

/*
 * This handles the memory map.
 *
 * A __PAGE_OFFSET of 0xC0000000 means that the kernel has
 * a virtual address space of one gigabyte, which limits the
 * amount of physical memory you can use to about 950MB.
 *
 * If you want more physical memory than this then see the CONFIG_HIGHMEM4G
 * and CONFIG_HIGHMEM64G options in the kernel configuration.
 */
#define __PAGE_OFFSET		_AC(CONFIG_PAGE_OFFSET, UL)

#ifdef CONFIG_X86_PAE
#define __PHYSICAL_MASK_SHIFT	36
#define __VIRTUAL_MASK_SHIFT	32
#define PAGETABLE_LEVELS	3

#ifndef __ASSEMBLY__
typedef u64	pteval_t;
typedef u64	pmdval_t;
typedef u64	pudval_t;
typedef u64	pgdval_t;
typedef u64	pgprotval_t;
typedef u64	phys_addr_t;

typedef struct { unsigned long pte_low, pte_high; } pte_t;

static inline unsigned long long native_pte_val(pte_t pte)
{
	return pte.pte_low | ((unsigned long long)pte.pte_high << 32);
}

static inline pte_t native_make_pte(unsigned long long val)
{
	return (pte_t) { .pte_low = val, .pte_high = (val >> 32) } ;
}

#endif	/* __ASSEMBLY__
 */
#else  /* !CONFIG_X86_PAE */
#define __PHYSICAL_MASK_SHIFT	32
#define __VIRTUAL_MASK_SHIFT	32
#define PAGETABLE_LEVELS	2

#ifndef __ASSEMBLY__
typedef unsigned long	pteval_t;
typedef unsigned long	pmdval_t;
typedef unsigned long	pudval_t;
typedef unsigned long	pgdval_t;
typedef unsigned long	pgprotval_t;
typedef unsigned long	phys_addr_t;

typedef struct { pteval_t pte_low; } pte_t;
typedef pte_t boot_pte_t;

static inline unsigned long native_pte_val(pte_t pte)
{
	return pte.pte_low;
}

static inline pte_t native_make_pte(unsigned long val)
{
	return (pte_t) { .pte_low = val };
}

#endif	/* __ASSEMBLY__ */
#endif	/* CONFIG_X86_PAE */

#ifdef CONFIG_HUGETLB_PAGE
#define HAVE_ARCH_HUGETLB_UNMAPPED_AREA
#endif

#ifndef __ASSEMBLY__
#ifdef CONFIG_X86_USE_3DNOW
#include <asm/mmx.h>

static inline void clear_page(void *page)
{
	mmx_clear_page(page);
}

static inline void copy_page(void *to, void *from)
{
	mmx_copy_page(to, from);
}
#else  /* !CONFIG_X86_USE_3DNOW */
#include <linux/string.h>

static inline void clear_page(void *page)
{
	memset(page, 0, PAGE_SIZE);
}

static inline void copy_page(void *to, void *from)
{
	memcpy(to, from, PAGE_SIZE);
}
#endif	/* CONFIG_X86_3DNOW */
#endif	/* !__ASSEMBLY__ */

#endif	/* CONFIG_X86_32 */

#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)

#define VM_DATA_DEFAULT_FLAGS \
	(((current->personality & READ_IMPLIES_EXEC) ? VM_EXEC : 0 ) | \
	 VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)


#ifndef __ASSEMBLY__
struct page;

static void inline clear_user_page(void *page, unsigned long vaddr,
				struct page *pg)
{
	clear_page(page);
}

static void inline copy_user_page(void *to, void *from, unsigned long vaddr,
				struct page *topage)
{
	copy_page(to, from);
}

#define __alloc_zeroed_user_highpage(movableflags, vma, vaddr) \
	alloc_page_vma(GFP_HIGHUSER | __GFP_ZERO | movableflags, vma, vaddr)
#define __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE

typedef struct { pgdval_t pgd; } pgd_t;
typedef struct { pgprotval_t pgprot; } pgprot_t;

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
#endif	/* PAGETABLE_LEVELS >= 3 */

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
#define __pte(x)	native_make_pte(x)

#endif	/* CONFIG_PARAVIRT */

#endif	/* __ASSEMBLY__ */


#ifdef CONFIG_X86_32
# include "page_32.h"
#else
# include "page_64.h"
#endif

#endif	/* _ASM_X86_PAGE_H */
