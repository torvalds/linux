/*
 *  linux/include/asm-arm/page.h
 *
 *  Copyright (C) 1995-2003 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASMARM_PAGE_H
#define _ASMARM_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT		12
#define PAGE_SIZE		(1UL << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE-1))

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

#ifndef __ASSEMBLY__

#ifndef CONFIG_MMU

#include "page-nommu.h"

#else

#include <asm/glue.h>

/*
 *	User Space Model
 *	================
 *
 *	This section selects the correct set of functions for dealing with
 *	page-based copying and clearing for user space for the particular
 *	processor(s) we're building for.
 *
 *	We have the following to choose from:
 *	  v3		- ARMv3
 *	  v4wt		- ARMv4 with writethrough cache, without minicache
 *	  v4wb		- ARMv4 with writeback cache, without minicache
 *	  v4_mc		- ARMv4 with minicache
 *	  xscale	- Xscale
 *	  xsc3		- XScalev3
 */
#undef _USER
#undef MULTI_USER

#ifdef CONFIG_CPU_COPY_V3
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER v3
# endif
#endif

#ifdef CONFIG_CPU_COPY_V4WT
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER v4wt
# endif
#endif

#ifdef CONFIG_CPU_COPY_V4WB
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER v4wb
# endif
#endif

#ifdef CONFIG_CPU_SA1100
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER v4_mc
# endif
#endif

#ifdef CONFIG_CPU_XSCALE
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER xscale_mc
# endif
#endif

#ifdef CONFIG_CPU_XSC3
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER xsc3_mc
# endif
#endif

#ifdef CONFIG_CPU_COPY_V6
# define MULTI_USER 1
#endif

#if !defined(_USER) && !defined(MULTI_USER)
#error Unknown user operations model
#endif

struct cpu_user_fns {
	void (*cpu_clear_user_page)(void *p, unsigned long user);
	void (*cpu_copy_user_page)(void *to, const void *from,
				   unsigned long user);
};

#ifdef MULTI_USER
extern struct cpu_user_fns cpu_user;

#define __cpu_clear_user_page	cpu_user.cpu_clear_user_page
#define __cpu_copy_user_page	cpu_user.cpu_copy_user_page

#else

#define __cpu_clear_user_page	__glue(_USER,_clear_user_page)
#define __cpu_copy_user_page	__glue(_USER,_copy_user_page)

extern void __cpu_clear_user_page(void *p, unsigned long user);
extern void __cpu_copy_user_page(void *to, const void *from,
				 unsigned long user);
#endif

#define clear_user_page(addr,vaddr,pg)	 __cpu_clear_user_page(addr, vaddr)
#define copy_user_page(to,from,vaddr,pg) __cpu_copy_user_page(to, from, vaddr)

#define clear_page(page)	memzero((void *)(page), PAGE_SIZE)
extern void copy_page(void *to, const void *from);

#undef STRICT_MM_TYPECHECKS

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd[2]; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)      ((x).pte)
#define pmd_val(x)      ((x).pmd)
#define pgd_val(x)	((x).pgd[0])
#define pgprot_val(x)   ((x).pgprot)

#define __pte(x)        ((pte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pgprot(x)     ((pgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t[2];
typedef unsigned long pgprot_t;

#define pte_val(x)      (x)
#define pmd_val(x)      (x)
#define pgd_val(x)	((x)[0])
#define pgprot_val(x)   (x)

#define __pte(x)        (x)
#define __pmd(x)        (x)
#define __pgprot(x)     (x)

#endif /* STRICT_MM_TYPECHECKS */

#endif /* CONFIG_MMU */

#include <asm/memory.h>

#endif /* !__ASSEMBLY__ */

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

/*
 * With EABI on ARMv5 and above we must have 64-bit aligned slab pointers.
 */
#if defined(CONFIG_AEABI) && (__LINUX_ARM_ARCH__ >= 5)
#define ARCH_SLAB_MINALIGN 8
#endif

#include <asm-generic/page.h>

#endif
