#ifndef _ASM_POWERPC_PAGE_64_H
#define _ASM_POWERPC_PAGE_64_H
#ifdef __KERNEL__

/*
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * We always define HW_PAGE_SHIFT to 12 as use of 64K pages remains Linux
 * specific, every notion of page number shared with the firmware, TCEs,
 * iommu, etc... still uses a page size of 4K.
 */
#define HW_PAGE_SHIFT		12
#define HW_PAGE_SIZE		(ASM_CONST(1) << HW_PAGE_SHIFT)
#define HW_PAGE_MASK		(~(HW_PAGE_SIZE-1))

/*
 * PAGE_FACTOR is the number of bits factor between PAGE_SHIFT and
 * HW_PAGE_SHIFT, that is 4K pages.
 */
#define PAGE_FACTOR		(PAGE_SHIFT - HW_PAGE_SHIFT)

/* Segment size */
#define SID_SHIFT		28
#define SID_MASK		0xfffffffffUL
#define ESID_MASK		0xfffffffff0000000UL
#define GET_ESID(x)		(((x) >> SID_SHIFT) & SID_MASK)

#ifndef __ASSEMBLY__
#include <asm/cache.h>

typedef unsigned long pte_basic_t;

static __inline__ void clear_page(void *addr)
{
	unsigned long lines, line_size;

	line_size = ppc64_caches.dline_size;
	lines = ppc64_caches.dlines_per_page;

	__asm__ __volatile__(
	"mtctr	%1	# clear_page\n\
1:      dcbz	0,%0\n\
	add	%0,%0,%3\n\
	bdnz+	1b"
        : "=r" (addr)
        : "r" (lines), "0" (addr), "r" (line_size)
	: "ctr", "memory");
}

extern void copy_4K_page(void *to, void *from);

#ifdef CONFIG_PPC_64K_PAGES
static inline void copy_page(void *to, void *from)
{
	unsigned int i;
	for (i=0; i < (1 << (PAGE_SHIFT - 12)); i++) {
		copy_4K_page(to, from);
		to += 4096;
		from += 4096;
	}
}
#else /* CONFIG_PPC_64K_PAGES */
static inline void copy_page(void *to, void *from)
{
	copy_4K_page(to, from);
}
#endif /* CONFIG_PPC_64K_PAGES */

/* Log 2 of page table size */
extern u64 ppc64_pft_size;

/* Large pages size */
#ifdef CONFIG_HUGETLB_PAGE
extern unsigned int HPAGE_SHIFT;
#else
#define HPAGE_SHIFT PAGE_SHIFT
#endif
#define HPAGE_SIZE		((1UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)

#endif /* __ASSEMBLY__ */

#ifdef CONFIG_HUGETLB_PAGE

#define HTLB_AREA_SHIFT		40
#define HTLB_AREA_SIZE		(1UL << HTLB_AREA_SHIFT)
#define GET_HTLB_AREA(x)	((x) >> HTLB_AREA_SHIFT)

#define LOW_ESID_MASK(addr, len)    \
	(((1U << (GET_ESID(min((addr)+(len)-1, 0x100000000UL))+1)) \
	  - (1U << GET_ESID(min((addr), 0x100000000UL)))) & 0xffff)
#define HTLB_AREA_MASK(addr, len)   (((1U << (GET_HTLB_AREA(addr+len-1)+1)) \
		                      - (1U << GET_HTLB_AREA(addr))) & 0xffff)

#define ARCH_HAS_HUGEPAGE_ONLY_RANGE
#define ARCH_HAS_HUGETLB_FREE_PGD_RANGE
#define ARCH_HAS_PREPARE_HUGEPAGE_RANGE
#define ARCH_HAS_SETCLEAR_HUGE_PTE

#define touches_hugepage_low_range(mm, addr, len) \
	(((addr) < 0x100000000UL) \
	 && (LOW_ESID_MASK((addr), (len)) & (mm)->context.low_htlb_areas))
#define touches_hugepage_high_range(mm, addr, len) \
	((((addr) + (len)) > 0x100000000UL) \
	  && (HTLB_AREA_MASK((addr), (len)) & (mm)->context.high_htlb_areas))

#define __within_hugepage_low_range(addr, len, segmask) \
	( (((addr)+(len)) <= 0x100000000UL) \
	  && ((LOW_ESID_MASK((addr), (len)) | (segmask)) == (segmask)))
#define within_hugepage_low_range(addr, len) \
	__within_hugepage_low_range((addr), (len), \
				    current->mm->context.low_htlb_areas)
#define __within_hugepage_high_range(addr, len, zonemask) \
	( ((addr) >= 0x100000000UL) \
	  && ((HTLB_AREA_MASK((addr), (len)) | (zonemask)) == (zonemask)))
#define within_hugepage_high_range(addr, len) \
	__within_hugepage_high_range((addr), (len), \
				    current->mm->context.high_htlb_areas)

#define is_hugepage_only_range(mm, addr, len) \
	(touches_hugepage_high_range((mm), (addr), (len)) || \
	  touches_hugepage_low_range((mm), (addr), (len)))
#define HAVE_ARCH_HUGETLB_UNMAPPED_AREA

#define in_hugepage_area(context, addr) \
	(cpu_has_feature(CPU_FTR_16M_PAGE) && \
	 ( ( (addr) >= 0x100000000UL) \
	   ? ((1 << GET_HTLB_AREA(addr)) & (context).high_htlb_areas) \
	   : ((1 << GET_ESID(addr)) & (context).low_htlb_areas) ) )

#else /* !CONFIG_HUGETLB_PAGE */

#define in_hugepage_area(mm, addr)	0

#endif /* !CONFIG_HUGETLB_PAGE */

#ifdef MODULE
#define __page_aligned __attribute__((__aligned__(PAGE_SIZE)))
#else
#define __page_aligned \
	__attribute__((__aligned__(PAGE_SIZE), \
		__section__(".data.page_aligned")))
#endif

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

#include <asm-generic/page.h>

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_PAGE_64_H */
