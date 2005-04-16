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

#include <linux/config.h>

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))
#define PTE_MASK	PAGE_MASK

#if defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
#define HPAGE_SHIFT	16
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_1MB)
#define HPAGE_SHIFT	20
#endif

#ifdef CONFIG_HUGETLB_PAGE
#define HPAGE_SIZE		(1UL << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE-1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT-PAGE_SHIFT)
#endif

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

extern void (*clear_page)(void *to);
extern void (*copy_page)(void *to, void *from);

extern void clear_page_slow(void *to);
extern void copy_page_slow(void *to, void *from);

#if defined(CONFIG_SH7705_CACHE_32KB) && defined(CONFIG_MMU)
struct page;
extern void clear_user_page(void *to, unsigned long address, struct page *pg);
extern void copy_user_page(void *to, void *from, unsigned long address, struct page *pg);
extern void __clear_user_page(void *to, void *orig_to);
extern void __copy_user_page(void *to, void *from, void *orig_to);
#elif defined(CONFIG_CPU_SH2) || defined(CONFIG_CPU_SH3) || !defined(CONFIG_MMU)
#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)
#elif defined(CONFIG_CPU_SH4)
struct page;
extern void clear_user_page(void *to, unsigned long address, struct page *pg);
extern void copy_user_page(void *to, void *from, unsigned long address, struct page *pg);
extern void __clear_user_page(void *to, void *orig_to);
extern void __copy_user_page(void *to, void *from, void *orig_to);
#endif

/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x) ((pte_t) { (x) } )
#define __pmd(x) ((pmd_t) { (x) } )
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
#ifdef CONFIG_DISCONTIGMEM
/* Just for HP690, for now.. */
#define __MEMORY_START_2ND	(__MEMORY_START+0x02000000)
#define __MEMORY_SIZE_2ND	0x001000000 /* 16MB */
#endif

#define PAGE_OFFSET		(0x80000000UL)
#define __pa(x)			((unsigned long)(x)-PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))

#define MAP_NR(addr)		(((unsigned long)(addr)-PAGE_OFFSET) >> PAGE_SHIFT)

#ifndef CONFIG_DISCONTIGMEM
#define phys_to_page(phys)	(mem_map + (((phys)-__MEMORY_START) >> PAGE_SHIFT))
#define page_to_phys(page)	(((page - mem_map) << PAGE_SHIFT) + __MEMORY_START)
#endif

/* PFN start number, because of __MEMORY_START */
#define PFN_START		(__MEMORY_START >> PAGE_SHIFT)

#define pfn_to_page(pfn)	(mem_map + (pfn) - PFN_START)
#define page_to_pfn(page)	((unsigned long)((page) - mem_map) + PFN_START)
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_valid(pfn)		(((pfn) - PFN_START) < max_mapnr)
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#ifndef __ASSEMBLY__

/* Pure 2^n version of get_order */
static __inline__ int get_order(unsigned long size)
{
	int order;

	size = (size-1) >> (PAGE_SHIFT-1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

#endif

#endif /* __KERNEL__ */

#endif /* __ASM_SH_PAGE_H */
