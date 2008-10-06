#ifndef ASM_X86__PAGE_32_H
#define ASM_X86__PAGE_32_H

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

#ifdef CONFIG_4KSTACKS
#define THREAD_ORDER	0
#else
#define THREAD_ORDER	1
#endif
#define THREAD_SIZE 	(PAGE_SIZE << THREAD_ORDER)


#ifdef CONFIG_X86_PAE
/* 44=32+12, the limit we can fit into an unsigned long pfn */
#define __PHYSICAL_MASK_SHIFT	44
#define __VIRTUAL_MASK_SHIFT	32
#define PAGETABLE_LEVELS	3

#ifndef __ASSEMBLY__
typedef u64	pteval_t;
typedef u64	pmdval_t;
typedef u64	pudval_t;
typedef u64	pgdval_t;
typedef u64	pgprotval_t;
typedef u64	phys_addr_t;

typedef union {
	struct {
		unsigned long pte_low, pte_high;
	};
	pteval_t pte;
} pte_t;
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

typedef union {
	pteval_t pte;
	pteval_t pte_low;
} pte_t;

#endif	/* __ASSEMBLY__ */
#endif	/* CONFIG_X86_PAE */

#ifndef __ASSEMBLY__
typedef struct page *pgtable_t;
#endif

#ifdef CONFIG_HUGETLB_PAGE
#define HAVE_ARCH_HUGETLB_UNMAPPED_AREA
#endif

#ifndef __ASSEMBLY__
#define __phys_addr(x)		((x) - PAGE_OFFSET)
#define __phys_reloc_hide(x)	RELOC_HIDE((x), 0)

#ifdef CONFIG_FLATMEM
#define pfn_valid(pfn)		((pfn) < max_mapnr)
#endif /* CONFIG_FLATMEM */

extern int nx_enabled;

/*
 * This much address space is reserved for vmalloc() and iomap()
 * as well as fixmap mappings.
 */
extern unsigned int __VMALLOC_RESERVE;
extern int sysctl_legacy_va_layout;

#define VMALLOC_RESERVE		((unsigned long)__VMALLOC_RESERVE)
#define MAXMEM			(-__PAGE_OFFSET - __VMALLOC_RESERVE)

extern void find_low_pfn_range(void);
extern unsigned long init_memory_mapping(unsigned long start,
					 unsigned long end);
extern void initmem_init(unsigned long, unsigned long);
extern void free_initmem(void);
extern void setup_bootmem_allocator(void);


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

#endif /* ASM_X86__PAGE_32_H */
