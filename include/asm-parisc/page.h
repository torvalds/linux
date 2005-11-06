#ifndef _PARISC_PAGE_H
#define _PARISC_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__
#include <linux/config.h>
#ifndef __ASSEMBLY__

#include <asm/types.h>
#include <asm/cache.h>

#define clear_page(page)	memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to,from)      copy_user_page_asm((void *)(to), (void *)(from))

struct page;

extern void purge_kernel_dcache_page(unsigned long);
extern void copy_user_page_asm(void *to, void *from);
extern void clear_user_page_asm(void *page, unsigned long vaddr);

static inline void
copy_user_page(void *vto, void *vfrom, unsigned long vaddr, struct page *pg)
{
	copy_user_page_asm(vto, vfrom);
	flush_kernel_dcache_page(vto);
	/* XXX: ppc flushes icache too, should we? */
}

static inline void
clear_user_page(void *page, unsigned long vaddr, struct page *pg)
{
	purge_kernel_dcache_page((unsigned long)page);
	clear_user_page_asm(page, vaddr);
}

/*
 * These are used to make use of C type-checking..
 */
#ifdef __LP64__
typedef struct { unsigned long pte; } pte_t;
#else
typedef struct {
	unsigned long pte;
	unsigned long flags;
} pte_t;
#endif
/* NOTE: even on 64 bits, these entries are __u32 because we allocate
 * the pmd and pgd in ZONE_DMA (i.e. under 4GB) */
typedef struct { __u32 pmd; } pmd_t;
typedef struct { __u32 pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#ifdef __LP64__
#define pte_flags(x)	(*(__u32 *)&((x).pte))
#else
#define pte_flags(x)	((x).flags)
#endif

/* These do not work lvalues, so make sure we don't use them as such. */
#define pmd_val(x)	((x).pmd + 0)
#define pgd_val(x)	((x).pgd + 0)
#define pgprot_val(x)	((x).pgprot)

#define __pmd_val_set(x,n) (x).pmd = (n)
#define __pgd_val_set(x,n) (x).pgd = (n)

#define __pte(x)	((pte_t) { (x) } )
#define __pmd(x)	((pmd_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

typedef struct __physmem_range {
	unsigned long start_pfn;
	unsigned long pages;       /* PAGE_SIZE pages */
} physmem_range_t;

extern physmem_range_t pmem_ranges[];
extern int npmem_ranges;

#endif /* !__ASSEMBLY__ */

/* WARNING: The definitions below must match exactly to sizeof(pte_t)
 * etc
 */
#ifdef __LP64__
#define BITS_PER_PTE_ENTRY	3
#define BITS_PER_PMD_ENTRY	2
#define BITS_PER_PGD_ENTRY	2
#else
#define BITS_PER_PTE_ENTRY	3
#define BITS_PER_PMD_ENTRY	2
#define BITS_PER_PGD_ENTRY	BITS_PER_PMD_ENTRY
#endif
#define PGD_ENTRY_SIZE	(1UL << BITS_PER_PGD_ENTRY)
#define PMD_ENTRY_SIZE	(1UL << BITS_PER_PMD_ENTRY)
#define PTE_ENTRY_SIZE	(1UL << BITS_PER_PTE_ENTRY)

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)


#define LINUX_GATEWAY_SPACE     0

/* This governs the relationship between virtual and physical addresses.
 * If you alter it, make sure to take care of our various fixed mapping
 * segments in fixmap.h */
#define __PAGE_OFFSET           (0x10000000)

#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)

/* The size of the gateway page (we leave lots of room for expansion) */
#define GATEWAY_PAGE_SIZE	0x4000

/* The start of the actual kernel binary---used in vmlinux.lds.S
 * Leave some space after __PAGE_OFFSET for detecting kernel null
 * ptr derefs */
#define KERNEL_BINARY_TEXT_START	(__PAGE_OFFSET + 0x100000)

/* These macros don't work for 64-bit C code -- don't allow in C at all */
#ifdef __ASSEMBLY__
#   define PA(x)	((x)-__PAGE_OFFSET)
#   define VA(x)	((x)+__PAGE_OFFSET)
#endif
#define __pa(x)			((unsigned long)(x)-PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long)(x)+PAGE_OFFSET))

#ifndef CONFIG_DISCONTIGMEM
#define pfn_to_page(pfn)	(mem_map + (pfn))
#define page_to_pfn(page)	((unsigned long)((page) - mem_map))
#define pfn_valid(pfn)		((pfn) < max_mapnr)
#endif /* CONFIG_DISCONTIGMEM */

#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)
#define virt_to_page(kaddr)     pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#endif /* __KERNEL__ */

#include <asm-generic/page.h>

#endif /* _PARISC_PAGE_H */
