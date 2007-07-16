#ifndef __ASM_SH_PAGE_H
#define __ASM_SH_PAGE_H

/*
 * Copyright (C) 1999  Niibe Yutaka
 */

/*
   [ P0/U0 (virtual) ]		0x00000000     <------ User space
   [ P1 (fixed)   cached ]	0x80000000     <------ Kernel space
   [ P2 (fixed)  non-cachable]	0xA0000000     <------ Physical access
   [ P3 (virtual) cached]	0xC0000000     <------ vmalloced area
   [ P4 control   ]		0xE0000000
 */

#ifdef __KERNEL__

/* PAGE_SHIFT determines the page size */
#if defined(CONFIG_PAGE_SIZE_4KB)
# define PAGE_SHIFT	12
#elif defined(CONFIG_PAGE_SIZE_8KB)
# define PAGE_SHIFT	13
#elif defined(CONFIG_PAGE_SIZE_64KB)
# define PAGE_SHIFT	16
#else
# error "Bogus kernel page size?"
#endif

#ifdef __ASSEMBLY__
#define PAGE_SIZE	(1 << PAGE_SHIFT)
#else
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#endif

#define PAGE_MASK	(~(PAGE_SIZE-1))
#define PTE_MASK	PAGE_MASK

#if defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
#define HPAGE_SHIFT	16
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_256K)
#define HPAGE_SHIFT	18
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_1MB)
#define HPAGE_SHIFT	20
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_4MB)
#define HPAGE_SHIFT	22
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_64MB)
#define HPAGE_SHIFT	26
#endif

#ifdef CONFIG_HUGETLB_PAGE
#define HPAGE_SIZE		(1UL << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE-1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT-PAGE_SHIFT)
#endif

#ifndef __ASSEMBLY__

extern void (*clear_page)(void *to);
extern void (*copy_page)(void *to, void *from);

extern unsigned long shm_align_mask;
extern unsigned long max_low_pfn, min_low_pfn;
extern unsigned long memory_start, memory_end;

#ifdef CONFIG_MMU
extern void clear_page_slow(void *to);
extern void copy_page_slow(void *to, void *from);
#else
extern void clear_page_nommu(void *to);
extern void copy_page_nommu(void *to, void *from);
#endif

#if defined(CONFIG_MMU) && (defined(CONFIG_CPU_SH4) || \
	defined(CONFIG_SH7705_CACHE_32KB))
struct page;
extern void clear_user_page(void *to, unsigned long address, struct page *pg);
extern void copy_user_page(void *to, void *from, unsigned long address, struct page *pg);
extern void __clear_user_page(void *to, void *orig_to);
extern void __copy_user_page(void *to, void *from, void *orig_to);
#elif defined(CONFIG_CPU_SH2) || defined(CONFIG_CPU_SH3) || !defined(CONFIG_MMU)
#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)
#endif

/*
 * These are used to make use of C type-checking..
 */
#ifdef CONFIG_X2TLB
typedef struct { unsigned long pte_low, pte_high; } pte_t;
typedef struct { unsigned long long pgprot; } pgprot_t;
#define pte_val(x) \
	((x).pte_low | ((unsigned long long)(x).pte_high << 32))
#define __pte(x) \
	({ pte_t __pte = {(x), ((unsigned long long)(x)) >> 32}; __pte; })
#else
typedef struct { unsigned long pte_low; } pte_t;
typedef struct { unsigned long pgprot; } pgprot_t;
#define pte_val(x)	((x).pte_low)
#define __pte(x) ((pte_t) { (x) } )
#endif

typedef struct { unsigned long pgd; } pgd_t;

#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pgd(x) ((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#endif /* !__ASSEMBLY__ */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

/*
 * IF YOU CHANGE THIS, PLEASE ALSO CHANGE
 *
 *	arch/sh/kernel/vmlinux.lds.S
 *
 * which has the same constant encoded..
 */

#define __MEMORY_START		CONFIG_MEMORY_START
#define __MEMORY_SIZE		CONFIG_MEMORY_SIZE

#define PAGE_OFFSET		CONFIG_PAGE_OFFSET
#define __pa(x)			((unsigned long)(x)-PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))
#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)

#define phys_to_page(phys)	(pfn_to_page(phys >> PAGE_SHIFT))
#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)

/* PFN start number, because of __MEMORY_START */
#define PFN_START		(__MEMORY_START >> PAGE_SHIFT)
#define ARCH_PFN_OFFSET		(PFN_START)
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#ifdef CONFIG_FLATMEM
#define pfn_valid(pfn)		((pfn) >= min_low_pfn && (pfn) < max_low_pfn)
#endif
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

/* vDSO support */
#ifdef CONFIG_VSYSCALL
#define __HAVE_ARCH_GATE_AREA
#endif

/*
 * Slub defaults to 8-byte alignment, we're only interested in 4.
 * Slab defaults to BYTES_PER_WORD, which ends up being the same anyways.
 */
#define ARCH_KMALLOC_MINALIGN	4
#define ARCH_SLAB_MINALIGN	4

#endif /* __KERNEL__ */
#endif /* __ASM_SH_PAGE_H */
