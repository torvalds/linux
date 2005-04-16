#ifndef _ASMARM_PAGE_H
#define _ASMARM_PAGE_H

#include <linux/config.h>

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

extern void __clear_user_page(void *p, unsigned long user);
extern void __copy_user_page(void *to, const void *from, unsigned long user);
extern void copy_page(void *to, const void *from);

//FIXME these may be wrong on ARM26
#define clear_user_page(addr,vaddr,pg)			\
	do {						\
		preempt_disable();			\
		__clear_user_page(addr, vaddr);	\
		preempt_enable();			\
	} while (0)

#define copy_user_page(to,from,vaddr,pg)		\
	do {						\
		preempt_disable();			\
		__copy_user_page(to, from, vaddr);	\
		preempt_enable();			\
	} while (0)

#define clear_page(page)	memzero((void *)(page), PAGE_SIZE)
#define copy_page(to, from)  __copy_user_page(to, from, 0);

#undef STRICT_MM_TYPECHECKS

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pgd_val(x)      ((x).pgd)
#define pte_val(x)      ((x).pte)
#define pmd_val(x)      ((x).pmd)
#define pgprot_val(x)   ((x).pgprot)

#define __pte(x)        ((pte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pgprot(x)     ((pgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pgd_t;
typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgprot_t;

//FIXME - should these cast to unsigned long?
#define pgd_val(x)      (x)
#define pte_val(x)      (x)
#define pmd_val(x)      (x)
#define pgprot_val(x)   (x)

#define __pte(x)        (x)
#define __pmd(x)        (x)
#define __pgprot(x)     (x)

#endif /* STRICT_MM_TYPECHECKS */
#endif /* !__ASSEMBLY__ */
#endif /* __KERNEL__ */

/* PAGE_SHIFT determines the page size.  This is configurable. */
#if defined(CONFIG_PAGESIZE_16)
#define PAGE_SHIFT      14              /* 16K */
#else           /* default */
#define PAGE_SHIFT      15              /* 32K */
#endif

#define EXEC_PAGESIZE   32768

#define PAGE_SIZE		(1UL << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE-1))

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

/* Pure 2^n version of get_order */
static inline int get_order(unsigned long size)
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

#include <asm/memory.h>

#endif /* !__ASSEMBLY__ */

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#endif /* __KERNEL__ */

#endif
