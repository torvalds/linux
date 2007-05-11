#ifndef _ASM_POWERPC_PAGE_32_H
#define _ASM_POWERPC_PAGE_32_H
#ifdef __KERNEL__

#define VM_DATA_DEFAULT_FLAGS	VM_DATA_DEFAULT_FLAGS32

#define PPC_MEMSTART	0

#ifndef __ASSEMBLY__
/*
 * The basic type of a PTE - 64 bits for those CPUs with > 32 bit
 * physical addressing.  For now this just the IBM PPC440.
 */
#ifdef CONFIG_PTE_64BIT
typedef unsigned long long pte_basic_t;
#define PTE_SHIFT	(PAGE_SHIFT - 3)	/* 512 ptes per page */
#else
typedef unsigned long pte_basic_t;
#define PTE_SHIFT	(PAGE_SHIFT - 2)	/* 1024 ptes per page */
#endif

struct page;
extern void clear_pages(void *page, int order);
static inline void clear_page(void *page) { clear_pages(page, 0); }
extern void copy_page(void *to, void *from);

#include <asm-generic/page.h>

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_PAGE_32_H */
