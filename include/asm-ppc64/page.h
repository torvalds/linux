#ifndef _PPC64_PAGE_H
#define _PPC64_PAGE_H

/*
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>

#ifdef __ASSEMBLY__
  #define ASM_CONST(x) x
#else
  #define __ASM_CONST(x) x##UL
  #define ASM_CONST(x) __ASM_CONST(x)
#endif

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(ASM_CONST(1) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#define SID_SHIFT       28
#define SID_MASK        0xfffffffffUL
#define ESID_MASK	0xfffffffff0000000UL
#define GET_ESID(x)     (((x) >> SID_SHIFT) & SID_MASK)

#define HPAGE_SHIFT	24
#define HPAGE_SIZE	((1UL) << HPAGE_SHIFT)
#define HPAGE_MASK	(~(HPAGE_SIZE - 1))

#ifdef CONFIG_HUGETLB_PAGE

#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

#define HTLB_AREA_SHIFT		40
#define HTLB_AREA_SIZE		(1UL << HTLB_AREA_SHIFT)
#define GET_HTLB_AREA(x)	((x) >> HTLB_AREA_SHIFT)

#define LOW_ESID_MASK(addr, len)	(((1U << (GET_ESID(addr+len-1)+1)) \
	   	                	- (1U << GET_ESID(addr))) & 0xffff)
#define HTLB_AREA_MASK(addr, len)	(((1U << (GET_HTLB_AREA(addr+len-1)+1)) \
	   	                	- (1U << GET_HTLB_AREA(addr))) & 0xffff)

#define ARCH_HAS_HUGEPAGE_ONLY_RANGE
#define ARCH_HAS_PREPARE_HUGEPAGE_RANGE
#define ARCH_HAS_SETCLEAR_HUGE_PTE

#define touches_hugepage_low_range(mm, addr, len) \
	(LOW_ESID_MASK((addr), (len)) & (mm)->context.low_htlb_areas)
#define touches_hugepage_high_range(mm, addr, len) \
	(HTLB_AREA_MASK((addr), (len)) & (mm)->context.high_htlb_areas)

#define __within_hugepage_low_range(addr, len, segmask) \
	((LOW_ESID_MASK((addr), (len)) | (segmask)) == (segmask))
#define within_hugepage_low_range(addr, len) \
	__within_hugepage_low_range((addr), (len), \
				    current->mm->context.low_htlb_areas)
#define __within_hugepage_high_range(addr, len, zonemask) \
	((HTLB_AREA_MASK((addr), (len)) | (zonemask)) == (zonemask))
#define within_hugepage_high_range(addr, len) \
	__within_hugepage_high_range((addr), (len), \
				    current->mm->context.high_htlb_areas)

#define is_hugepage_only_range(mm, addr, len) \
	(touches_hugepage_high_range((mm), (addr), (len)) || \
	  touches_hugepage_low_range((mm), (addr), (len)))
#define HAVE_ARCH_HUGETLB_UNMAPPED_AREA

#define in_hugepage_area(context, addr) \
	(cpu_has_feature(CPU_FTR_16M_PAGE) && \
	 ( ((1 << GET_HTLB_AREA(addr)) & (context).high_htlb_areas) || \
	   ( ((addr) < 0x100000000L) && \
	     ((1 << GET_ESID(addr)) & (context).low_htlb_areas) ) ) )

#else /* !CONFIG_HUGETLB_PAGE */

#define in_hugepage_area(mm, addr)	0

#endif /* !CONFIG_HUGETLB_PAGE */

/* align addr on a size boundary - adjust address up/down if needed */
#define _ALIGN_UP(addr,size)	(((addr)+((size)-1))&(~((size)-1)))
#define _ALIGN_DOWN(addr,size)	((addr)&(~((size)-1)))

/* align addr on a size boundary - adjust address up if needed */
#define _ALIGN(addr,size)     _ALIGN_UP(addr,size)

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	_ALIGN(addr, PAGE_SIZE)

#ifdef __KERNEL__
#ifndef __ASSEMBLY__
#include <asm/cache.h>

#undef STRICT_MM_TYPECHECKS

#define REGION_SIZE   4UL
#define REGION_SHIFT  60UL
#define REGION_MASK   (((1UL<<REGION_SIZE)-1UL)<<REGION_SHIFT)

static __inline__ void clear_page(void *addr)
{
	unsigned long lines, line_size;

	line_size = ppc64_caches.dline_size;
	lines = ppc64_caches.dlines_per_page;

	__asm__ __volatile__(
	"mtctr  	%1	# clear_page\n\
1:      dcbz  	0,%0\n\
	add	%0,%0,%3\n\
	bdnz+	1b"
        : "=r" (addr)
        : "r" (lines), "0" (addr), "r" (line_size)
	: "ctr", "memory");
}

extern void copy_page(void *to, void *from);
struct page;
extern void clear_user_page(void *page, unsigned long vaddr, struct page *pg);
extern void copy_user_page(void *to, void *from, unsigned long vaddr, struct page *p);

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking.  
 * Entries in the pte table are 64b, while entries in the pgd & pmd are 32b.
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pud; } pud_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((x).pmd)
#define pud_val(x)	((x).pud)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) })
#define __pmd(x)	((pmd_t) { (x) })
#define __pud(x)	((pud_t) { (x) })
#define __pgd(x)	((pgd_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pud_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)	(x)
#define pmd_val(x)	(x)
#define pud_val(x)	(x)
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)

#define __pte(x)	(x)
#define __pmd(x)	(x)
#define __pud(x)	(x)
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#endif

#define __pa(x) ((unsigned long)(x)-PAGE_OFFSET)

extern int page_is_ram(unsigned long pfn);

extern u64 ppc64_pft_size;		/* Log 2 of page table size */

/* We do define AT_SYSINFO_EHDR but don't use the gate mecanism */
#define __HAVE_ARCH_GATE_AREA		1

#endif /* __ASSEMBLY__ */

#ifdef MODULE
#define __page_aligned __attribute__((__aligned__(PAGE_SIZE)))
#else
#define __page_aligned \
	__attribute__((__aligned__(PAGE_SIZE), \
		__section__(".data.page_aligned")))
#endif


/* This must match the -Ttext linker address            */
/* Note: tophys & tovirt make assumptions about how     */
/*       KERNELBASE is defined for performance reasons. */
/*       When KERNELBASE moves, those macros may have   */
/*             to change!                               */
#define PAGE_OFFSET     ASM_CONST(0xC000000000000000)
#define KERNELBASE      PAGE_OFFSET
#define VMALLOCBASE     ASM_CONST(0xD000000000000000)

#define VMALLOC_REGION_ID  (VMALLOCBASE >> REGION_SHIFT)
#define KERNEL_REGION_ID   (KERNELBASE >> REGION_SHIFT)
#define USER_REGION_ID     (0UL)
#define REGION_ID(ea)	   (((unsigned long)(ea)) >> REGION_SHIFT)

#define __va(x) ((void *)((unsigned long)(x) + KERNELBASE))

#ifdef CONFIG_DISCONTIGMEM
#define page_to_pfn(page)	discontigmem_page_to_pfn(page)
#define pfn_to_page(pfn)	discontigmem_pfn_to_page(pfn)
#define pfn_valid(pfn)		discontigmem_pfn_valid(pfn)
#endif
#ifdef CONFIG_FLATMEM
#define pfn_to_page(pfn)	(mem_map + (pfn))
#define page_to_pfn(page)	((unsigned long)((page) - mem_map))
#define pfn_valid(pfn)		((pfn) < max_mapnr)
#endif

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)

#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

/*
 * Unfortunately the PLT is in the BSS in the PPC32 ELF ABI,
 * and needs to be executable.  This means the whole heap ends
 * up being executable.
 */
#define VM_DATA_DEFAULT_FLAGS32	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#define VM_DATA_DEFAULT_FLAGS64	(VM_READ | VM_WRITE | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#define VM_DATA_DEFAULT_FLAGS \
	(test_thread_flag(TIF_32BIT) ? \
	 VM_DATA_DEFAULT_FLAGS32 : VM_DATA_DEFAULT_FLAGS64)

/*
 * This is the default if a program doesn't have a PT_GNU_STACK
 * program header entry. The PPC64 ELF ABI has a non executable stack
 * stack by default, so in the absense of a PT_GNU_STACK program header
 * we turn execute permission off.
 */
#define VM_STACK_DEFAULT_FLAGS32	(VM_READ | VM_WRITE | VM_EXEC | \
					 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#define VM_STACK_DEFAULT_FLAGS64	(VM_READ | VM_WRITE | \
					 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#define VM_STACK_DEFAULT_FLAGS \
	(test_thread_flag(TIF_32BIT) ? \
	 VM_STACK_DEFAULT_FLAGS32 : VM_STACK_DEFAULT_FLAGS64)

#endif /* __KERNEL__ */

#include <asm-generic/page.h>

#endif /* _PPC64_PAGE_H */
