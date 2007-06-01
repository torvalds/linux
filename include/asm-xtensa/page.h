/*
 * linux/include/asm-xtensa/page.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version2 as
 * published by the Free Software Foundation.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_PAGE_H
#define _XTENSA_PAGE_H

#ifdef __KERNEL__

#include <asm/processor.h>

#define XCHAL_KSEG_CACHED_VADDR 0xd0000000
#define XCHAL_KSEG_BYPASS_VADDR 0xd8000000
#define XCHAL_KSEG_PADDR        0x00000000
#define XCHAL_KSEG_SIZE         0x08000000

/*
 * PAGE_SHIFT determines the page size
 * PAGE_ALIGN(x) aligns the pointer to the (next) page boundary
 */

#define PAGE_SHIFT		12
#define PAGE_SIZE		(1 << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE-1))
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE - 1) & PAGE_MASK)

#define PAGE_OFFSET		XCHAL_KSEG_CACHED_VADDR
#define MAX_MEM_PFN             XCHAL_KSEG_SIZE
#define PGTABLE_START           0x80000000

#ifdef __ASSEMBLY__

#define __pgprot(x)	(x)

#else

/*
 * These are used to make use of C type-checking..
 */

typedef struct { unsigned long pte; } pte_t;		/* page table entry */
typedef struct { unsigned long pgd; } pgd_t;		/* PGD table entry */
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

/*
 * Pure 2^n version of get_order
 */

static inline int get_order(unsigned long size)
{
	int order;
#ifndef XCHAL_HAVE_NSU
	unsigned long x1, x2, x4, x8, x16;

	size = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	x1  = size & 0xAAAAAAAA;
	x2  = size & 0xCCCCCCCC;
	x4  = size & 0xF0F0F0F0;
	x8  = size & 0xFF00FF00;
	x16 = size & 0xFFFF0000;
	order = x2 ? 2 : 0;
	order += (x16 != 0) * 16;
	order += (x8 != 0) * 8;
	order += (x4 != 0) * 4;
	order += (x1 != 0);

	return order;
#else
	size = (size - 1) >> PAGE_SHIFT;
	asm ("nsau %0, %1" : "=r" (order) : "r" (size));
	return 32 - order;
#endif
}


struct page;
extern void clear_page(void *page);
extern void copy_page(void *to, void *from);

/*
 * If we have cache aliasing and writeback caches, we might have to do
 * some extra work
 */

#if (DCACHE_WAY_SIZE > PAGE_SIZE)
void clear_user_page(void *addr, unsigned long vaddr, struct page* page);
void copy_user_page(void *to,void* from,unsigned long vaddr,struct page* page);
#else
# define clear_user_page(page,vaddr,pg)		clear_page(page)
# define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)
#endif

/*
 * This handles the memory map.  We handle pages at
 * XCHAL_KSEG_CACHED_VADDR for kernels with 32 bit address space.
 * These macros are for conversion of kernel address, not user
 * addresses.
 */

#define __pa(x)			((unsigned long) (x) - PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long) (x) + PAGE_OFFSET))
#define pfn_valid(pfn)		((unsigned long)pfn < max_mapnr)
#ifdef CONFIG_DISCONTIGMEM
# error CONFIG_DISCONTIGMEM not supported
#endif

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define page_to_virt(page)	__va(page_to_pfn(page) << PAGE_SHIFT)
#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)
#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)

#define WANT_PAGE_VIRTUAL


#endif /* __ASSEMBLY__ */

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/memory_model.h>
#endif /* __KERNEL__ */
#endif /* _XTENSA_PAGE_H */
