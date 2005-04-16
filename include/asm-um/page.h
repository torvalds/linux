/*
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Copyright 2003 PathScale, Inc.
 * Licensed under the GPL
 */

#ifndef __UM_PAGE_H
#define __UM_PAGE_H

struct page;

#include <linux/config.h>
#include <asm/vm-flags.h>

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

/*
 * These are used to make use of C type-checking..
 */

#define clear_page(page)	memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to,from)	memcpy((void *)(to), (void *)(from), PAGE_SIZE)

#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

#if defined(CONFIG_3_LEVEL_PGTABLES) && !defined(CONFIG_64_BIT)

typedef struct { unsigned long pte_low, pte_high; } pte_t;
typedef struct { unsigned long long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
#define pte_val(x) ((x).pte_low | ((unsigned long long) (x).pte_high << 32))

#define pte_get_bits(pte, bits) ((pte).pte_low & (bits))
#define pte_set_bits(pte, bits) ((pte).pte_low |= (bits))
#define pte_clear_bits(pte, bits) ((pte).pte_low &= ~(bits))
#define pte_copy(to, from) ({ (to).pte_high = (from).pte_high; \
			      smp_wmb(); \
			      (to).pte_low = (from).pte_low; })
#define pte_is_zero(pte) (!((pte).pte_low & ~_PAGE_NEWPAGE) && !(pte).pte_high)
#define pte_set_val(pte, phys, prot) \
	({ (pte).pte_high = (phys) >> 32; \
	   (pte).pte_low = (phys) | pgprot_val(prot); })

typedef unsigned long long pfn_t;
typedef unsigned long long phys_t;

#else

typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pgd; } pgd_t;

#ifdef CONFIG_3_LEVEL_PGTABLES
typedef struct { unsigned long pmd; } pmd_t;
#define pmd_val(x)	((x).pmd)
#define __pmd(x) ((pmd_t) { (x) } )
#endif

#define pte_val(x)	((x).pte)


#define pte_get_bits(p, bits) ((p).pte & (bits))
#define pte_set_bits(p, bits) ((p).pte |= (bits))
#define pte_clear_bits(p, bits) ((p).pte &= ~(bits))
#define pte_copy(to, from) ((to).pte = (from).pte)
#define pte_is_zero(p) (!((p).pte & ~_PAGE_NEWPAGE))
#define pte_set_val(p, phys, prot) (p).pte = (phys | pgprot_val(prot))

typedef unsigned long pfn_t;
typedef unsigned long phys_t;

#endif

typedef struct { unsigned long pgprot; } pgprot_t;

#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x) ((pte_t) { (x) } )
#define __pgd(x) ((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

extern unsigned long uml_physmem;

#define PAGE_OFFSET (uml_physmem)
#define KERNELBASE PAGE_OFFSET

#define __va_space (8*1024*1024)

extern unsigned long to_phys(void *virt);
extern void *to_virt(unsigned long phys);
#define __pa(virt) to_phys((void *) virt)
#define __va(phys) to_virt((unsigned long) phys)

#define page_to_pfn(page) ((page) - mem_map)
#define pfn_to_page(pfn) (mem_map + (pfn))

#define phys_to_pfn(p) ((p) >> PAGE_SHIFT)
#define pfn_to_phys(pfn) ((pfn) << PAGE_SHIFT)

#define pfn_valid(pfn) ((pfn) < max_mapnr)
#define virt_addr_valid(v) pfn_valid(phys_to_pfn(__pa(v)))

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

extern struct page *arch_validate(struct page *page, int mask, int order);
#define HAVE_ARCH_VALIDATE

extern void arch_free_page(struct page *page, int order);
#define HAVE_ARCH_FREE_PAGE

#endif
