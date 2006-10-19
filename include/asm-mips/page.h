/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 1999, 2000, 03 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_PAGE_H
#define _ASM_PAGE_H


#ifdef __KERNEL__

#include <spaces.h>

/*
 * PAGE_SHIFT determines the page size
 */
#ifdef CONFIG_PAGE_SIZE_4KB
#define PAGE_SHIFT	12
#endif
#ifdef CONFIG_PAGE_SIZE_8KB
#define PAGE_SHIFT	13
#endif
#ifdef CONFIG_PAGE_SIZE_16KB
#define PAGE_SHIFT	14
#endif
#ifdef CONFIG_PAGE_SIZE_64KB
#define PAGE_SHIFT	16
#endif
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK       (~((1 << PAGE_SHIFT) - 1))

#ifndef __ASSEMBLY__

#include <linux/pfn.h>
#include <asm/cpu-features.h>
#include <asm/io.h>

extern void clear_page(void * page);
extern void copy_page(void * to, void * from);

extern unsigned long shm_align_mask;

static inline unsigned long pages_do_alias(unsigned long addr1,
	unsigned long addr2)
{
	return (addr1 ^ addr2) & shm_align_mask;
}

struct page;

static inline void clear_user_page(void *addr, unsigned long vaddr,
	struct page *page)
{
	extern void (*flush_data_cache_page)(unsigned long addr);

	clear_page(addr);
	if (pages_do_alias((unsigned long) addr, vaddr & PAGE_MASK))
		flush_data_cache_page((unsigned long)addr);
}

static inline void copy_user_page(void *vto, void *vfrom, unsigned long vaddr,
	struct page *to)
{
	extern void (*flush_data_cache_page)(unsigned long addr);

	copy_page(vto, vfrom);
	if (!cpu_has_ic_fills_f_dc ||
	    pages_do_alias((unsigned long)vto, vaddr & PAGE_MASK))
		flush_data_cache_page((unsigned long)vto);
}

/*
 * These are used to make use of C type-checking..
 */
#ifdef CONFIG_64BIT_PHYS_ADDR
  #ifdef CONFIG_CPU_MIPS32
    typedef struct { unsigned long pte_low, pte_high; } pte_t;
    #define pte_val(x)    ((x).pte_low | ((unsigned long long)(x).pte_high << 32))
    #define __pte(x)      ({ pte_t __pte = {(x), ((unsigned long long)(x)) >> 32}; __pte; })
  #else
     typedef struct { unsigned long long pte; } pte_t;
     #define pte_val(x)	((x).pte)
     #define __pte(x)	((pte_t) { (x) } )
  #endif
#else
typedef struct { unsigned long pte; } pte_t;
#define pte_val(x)	((x).pte)
#define __pte(x)	((pte_t) { (x) } )
#endif

/*
 * For 3-level pagetables we defines these ourselves, for 2-level the
 * definitions are supplied by <asm-generic/pgtable-nopmd.h>.
 */
#ifdef CONFIG_64BIT

typedef struct { unsigned long pmd; } pmd_t;
#define pmd_val(x)	((x).pmd)
#define __pmd(x)	((pmd_t) { (x) } )

#endif

/*
 * Right now we don't support 4-level pagetables, so all pud-related
 * definitions come from <asm-generic/pgtable-nopud.h>.
 */

/*
 * Finall the top of the hierarchy, the pgd
 */
typedef struct { unsigned long pgd; } pgd_t;
#define pgd_val(x)	((x).pgd)
#define __pgd(x)	((pgd_t) { (x) } )

/*
 * Manipulate page protection bits
 */
typedef struct { unsigned long pgprot; } pgprot_t;
#define pgprot_val(x)	((x).pgprot)
#define __pgprot(x)	((pgprot_t) { (x) } )

/*
 * On R4000-style MMUs where a TLB entry is mapping a adjacent even / odd
 * pair of pages we only have a single global bit per pair of pages.  When
 * writing to the TLB make sure we always have the bit set for both pages
 * or none.  This macro is used to access the `buddy' of the pte we're just
 * working on.
 */
#define ptep_buddy(x)	((pte_t *)((unsigned long)(x) ^ sizeof(pte_t)))

#endif /* !__ASSEMBLY__ */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr) + PAGE_SIZE - 1) & PAGE_MASK)

#if defined(CONFIG_64BIT) && !defined(CONFIG_BUILD_ELF64)
#define __pa_page_offset(x)	((unsigned long)(x) < CKSEG0 ? PAGE_OFFSET : CKSEG0)
#else
#define __pa_page_offset(x)	PAGE_OFFSET
#endif
#define __pa(x)			((unsigned long)(x) - __pa_page_offset(x))
#define __va(x)			((void *)((unsigned long)(x) + PAGE_OFFSET))

#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)

#ifdef CONFIG_FLATMEM

#define pfn_valid(pfn)		((pfn) < max_mapnr)

#elif defined(CONFIG_SPARSEMEM)

/* pfn_valid is defined in linux/mmzone.h */

#elif defined(CONFIG_NEED_MULTIPLE_NODES)

#define pfn_valid(pfn)							\
({									\
	unsigned long __pfn = (pfn);					\
	int __n = pfn_to_nid(__pfn);					\
	((__n >= 0) ? (__pfn < NODE_DATA(__n)->node_start_pfn +		\
	                       NODE_DATA(__n)->node_spanned_pages)	\
	            : 0);						\
})

#endif

#define virt_to_page(kaddr)	pfn_to_page(PFN_DOWN(virt_to_phys(kaddr)))
#define virt_addr_valid(kaddr)	pfn_valid(PFN_DOWN(virt_to_phys(kaddr)))

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#define UNCAC_ADDR(addr)	((addr) - PAGE_OFFSET + UNCAC_BASE)
#define CAC_ADDR(addr)		((addr) - UNCAC_BASE + PAGE_OFFSET)

#ifdef CONFIG_LIMITED_DMA
#define WANT_PAGE_VIRTUAL
#endif

#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

#endif /* defined (__KERNEL__) */

#endif /* _ASM_PAGE_H */
