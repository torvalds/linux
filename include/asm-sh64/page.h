#ifndef __ASM_SH64_PAGE_H
#define __ASM_SH64_PAGE_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/page.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003, 2004  Paul Mundt
 *
 * benedict.gaster@superh.com 19th, 24th July 2002.
 *
 * Modified to take account of enabling for D-CACHE support.
 *
 */


/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#ifdef __ASSEMBLY__
#define PAGE_SIZE	4096
#else
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#endif
#define PAGE_MASK	(~(PAGE_SIZE-1))
#define PTE_MASK	PAGE_MASK

#if defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
#define HPAGE_SHIFT	16
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_1MB)
#define HPAGE_SHIFT	20
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_512MB)
#define HPAGE_SHIFT	29
#endif

#ifdef CONFIG_HUGETLB_PAGE
#define HPAGE_SIZE		(1UL << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE-1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT-PAGE_SHIFT)
#define ARCH_HAS_SETCLEAR_HUGE_PTE
#endif

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

extern struct page *mem_map;
extern void sh64_page_clear(void *page);
extern void sh64_page_copy(void *from, void *to);

#define clear_page(page)               sh64_page_clear(page)
#define copy_page(to,from)             sh64_page_copy(from, to)

#if defined(CONFIG_DCACHE_DISABLED)

#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

#else

extern void clear_user_page(void *to, unsigned long address, struct page *pg);
extern void copy_user_page(void *to, void *from, unsigned long address, struct page *pg);

#endif /* defined(CONFIG_DCACHE_DISABLED) */

/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long long pte; } pte_t;
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
 * Kconfig defined.
 */
#define __MEMORY_START		(CONFIG_MEMORY_START)
#define PAGE_OFFSET		(CONFIG_CACHED_MEMORY_OFFSET)

#define __pa(x)			((unsigned long)(x)-PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))
#define MAP_NR(addr)		((__pa(addr)-__MEMORY_START) >> PAGE_SHIFT)
#define VALID_PAGE(page)	((page - mem_map) < max_mapnr)

#define phys_to_page(phys)	(mem_map + (((phys) - __MEMORY_START) >> PAGE_SHIFT))
#define page_to_phys(page)	(((page - mem_map) << PAGE_SHIFT) + __MEMORY_START)

/* PFN start number, because of __MEMORY_START */
#define PFN_START		(__MEMORY_START >> PAGE_SHIFT)
#define ARCH_PFN_OFFSET		(PFN_START)
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_valid(pfn)		(((pfn) - PFN_START) < max_mapnr)
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#endif /* __KERNEL__ */

#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

#endif /* __ASM_SH64_PAGE_H */
