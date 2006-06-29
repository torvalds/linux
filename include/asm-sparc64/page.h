/* $Id: page.h,v 1.39 2002/02/09 19:49:31 davem Exp $ */

#ifndef _SPARC64_PAGE_H
#define _SPARC64_PAGE_H

#include <asm/const.h>

#if defined(CONFIG_SPARC64_PAGE_SIZE_8KB)
#define PAGE_SHIFT   13
#elif defined(CONFIG_SPARC64_PAGE_SIZE_64KB)
#define PAGE_SHIFT   16
#elif defined(CONFIG_SPARC64_PAGE_SIZE_512KB)
#define PAGE_SHIFT   19
#elif defined(CONFIG_SPARC64_PAGE_SIZE_4MB)
#define PAGE_SHIFT   22
#else
#error No page size specified in kernel configuration
#endif

#define PAGE_SIZE    (_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK    (~(PAGE_SIZE-1))

/* Flushing for D-cache alias handling is only needed if
 * the page size is smaller than 16K.
 */
#if PAGE_SHIFT < 14
#define DCACHE_ALIASING_POSSIBLE
#endif

#ifdef __KERNEL__

#if defined(CONFIG_HUGETLB_PAGE_SIZE_4MB)
#define HPAGE_SHIFT		22
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_512K)
#define HPAGE_SHIFT		19
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
#define HPAGE_SHIFT		16
#endif

#ifdef CONFIG_HUGETLB_PAGE
#define HPAGE_SIZE		(_AC(1,UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1UL))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)
#define ARCH_HAS_SETCLEAR_HUGE_PTE
#define ARCH_HAS_HUGETLB_PREFAULT_HOOK
#define HAVE_ARCH_HUGETLB_UNMAPPED_AREA
#endif

#ifndef __ASSEMBLY__

extern void _clear_page(void *page);
#define clear_page(X)	_clear_page((void *)(X))
struct page;
extern void clear_user_page(void *addr, unsigned long vaddr, struct page *page);
#define copy_page(X,Y)	memcpy((void *)(X), (void *)(Y), PAGE_SIZE)
extern void copy_user_page(void *to, void *from, unsigned long vaddr, struct page *topage);

/* Unlike sparc32, sparc64's parameter passing API is more
 * sane in that structures which as small enough are passed
 * in registers instead of on the stack.  Thus, setting
 * STRICT_MM_TYPECHECKS does not generate worse code so
 * let's enable it to get the type checking.
 */

#define STRICT_MM_TYPECHECKS

#ifdef STRICT_MM_TYPECHECKS
/* These are used to make use of C type-checking.. */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long iopte; } iopte_t;
typedef struct { unsigned int pmd; } pmd_t;
typedef struct { unsigned int pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define iopte_val(x)	((x).iopte)
#define pmd_val(x)      ((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __iopte(x)	((iopte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#else
/* .. while these make it easier on the compiler */
typedef unsigned long pte_t;
typedef unsigned long iopte_t;
typedef unsigned int pmd_t;
typedef unsigned int pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)	(x)
#define iopte_val(x)	(x)
#define pmd_val(x)      (x)
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)

#define __pte(x)	(x)
#define __iopte(x)	(x)
#define __pmd(x)        (x)
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#endif /* (STRICT_MM_TYPECHECKS) */

#define TASK_UNMAPPED_BASE	(test_thread_flag(TIF_32BIT) ? \
				 (_AC(0x0000000070000000,UL)) : \
				 (_AC(0xfffff80000000000,UL) + (1UL << 32UL)))

#include <asm-generic/memory_model.h>

#endif /* !(__ASSEMBLY__) */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

/* We used to stick this into a hard-coded global register (%g4)
 * but that does not make sense anymore.
 */
#define PAGE_OFFSET		_AC(0xFFFFF80000000000,UL)

#ifndef __ASSEMBLY__

#define __pa(x)			((unsigned long)(x) - PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long) (x) + PAGE_OFFSET))

#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr)>>PAGE_SHIFT)

#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#define virt_to_phys __pa
#define phys_to_virt __va

#endif /* !(__ASSEMBLY__) */

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#endif /* !(__KERNEL__) */

#include <asm-generic/page.h>

#endif /* !(_SPARC64_PAGE_H) */
