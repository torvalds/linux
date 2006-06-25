/* highmem.h: virtual kernel memory mappings for high memory
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from include/asm-i386/highmem.h
 *
 * See Documentation/fujitsu/frv/mmu-layout.txt for more information.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_HIGHMEM_H
#define _ASM_HIGHMEM_H

#ifdef __KERNEL__

#include <linux/init.h>
#include <asm/mem-layout.h>
#include <asm/spr-regs.h>
#include <asm/mb-regs.h>

#define NR_TLB_LINES		64	/* number of lines in the TLB */

#ifndef __ASSEMBLY__

#include <linux/interrupt.h>
#include <asm/kmap_types.h>
#include <asm/pgtable.h>

#ifdef CONFIG_DEBUG_HIGHMEM
#define HIGHMEM_DEBUG 1
#else
#define HIGHMEM_DEBUG 0
#endif

/* declarations for highmem.c */
extern unsigned long highstart_pfn, highend_pfn;

#define kmap_prot PAGE_KERNEL
#define kmap_pte ______kmap_pte_in_TLB
extern pte_t *pkmap_page_table;

#define flush_cache_kmaps()  do { } while (0)

/*
 * Right now we initialize only a single pte table. It can be extended
 * easily, subsequent pte tables have to be allocated in one physical
 * chunk of RAM.
 */
#define LAST_PKMAP	PTRS_PER_PTE
#define LAST_PKMAP_MASK	(LAST_PKMAP - 1)
#define PKMAP_NR(virt)	((virt - PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)	(PKMAP_BASE + ((nr) << PAGE_SHIFT))

extern void *kmap_high(struct page *page);
extern void kunmap_high(struct page *page);

extern void *kmap(struct page *page);
extern void kunmap(struct page *page);

extern struct page *kmap_atomic_to_page(void *ptr);

#endif /* !__ASSEMBLY__ */

/*
 * The use of kmap_atomic/kunmap_atomic is discouraged - kmap/kunmap
 * gives a more generic (and caching) interface. But kmap_atomic can
 * be used in IRQ contexts, so in some (very limited) cases we need
 * it.
 */
#define KMAP_ATOMIC_CACHE_DAMR		8

#ifndef __ASSEMBLY__

#define __kmap_atomic_primary(type, paddr, ampr)						\
({												\
	unsigned long damlr, dampr;								\
												\
	dampr = paddr | xAMPRx_L | xAMPRx_M | xAMPRx_S | xAMPRx_SS_16Kb | xAMPRx_V;		\
												\
	if (type != __KM_CACHE)									\
		asm volatile("movgs %0,dampr"#ampr :: "r"(dampr));				\
	else											\
		asm volatile("movgs %0,iampr"#ampr"\n"						\
			     "movgs %0,dampr"#ampr"\n"						\
			     :: "r"(dampr)							\
			     );									\
												\
	asm("movsg damlr"#ampr",%0" : "=r"(damlr));						\
												\
	/*printk("DAMR"#ampr": PRIM sl=%d L=%08lx P=%08lx\n", type, damlr, dampr);*/		\
												\
	(void *) damlr;										\
})

#define __kmap_atomic_secondary(slot, paddr)							  \
({												  \
	unsigned long damlr = KMAP_ATOMIC_SECONDARY_FRAME + (slot) * PAGE_SIZE;			  \
	unsigned long dampr = paddr | xAMPRx_L | xAMPRx_M | xAMPRx_S | xAMPRx_SS_16Kb | xAMPRx_V; \
												  \
	asm volatile("movgs %0,tplr \n"								  \
		     "movgs %1,tppr \n"								  \
		     "tlbpr %0,gr0,#2,#1"							  \
		     : : "r"(damlr), "r"(dampr));						  \
												  \
	/*printk("TLB: SECN sl=%d L=%08lx P=%08lx\n", slot, damlr, dampr);*/			  \
												  \
	(void *) damlr;										  \
})

static inline void *kmap_atomic(struct page *page, enum km_type type)
{
	unsigned long paddr;

	preempt_disable();
	paddr = page_to_phys(page);

	switch (type) {
        case 0:		return __kmap_atomic_primary(0, paddr, 2);
        case 1:		return __kmap_atomic_primary(1, paddr, 3);
        case 2:		return __kmap_atomic_primary(2, paddr, 4);
        case 3:		return __kmap_atomic_primary(3, paddr, 5);
        case 4:		return __kmap_atomic_primary(4, paddr, 6);
        case 5:		return __kmap_atomic_primary(5, paddr, 7);
        case 6:		return __kmap_atomic_primary(6, paddr, 8);
        case 7:		return __kmap_atomic_primary(7, paddr, 9);
        case 8:		return __kmap_atomic_primary(8, paddr, 10);

	case 9 ... 9 + NR_TLB_LINES - 1:
		return __kmap_atomic_secondary(type - 9, paddr);

	default:
		BUG();
		return NULL;
	}
}

#define __kunmap_atomic_primary(type, ampr)			\
do {								\
	asm volatile("movgs gr0,dampr"#ampr"\n");		\
	if (type == __KM_CACHE)					\
		asm volatile("movgs gr0,iampr"#ampr"\n");	\
} while(0)

#define __kunmap_atomic_secondary(slot, vaddr)			\
do {								\
	asm volatile("tlbpr %0,gr0,#4,#1" : : "r"(vaddr));	\
} while(0)

static inline void kunmap_atomic(void *kvaddr, enum km_type type)
{
	switch (type) {
        case 0:		__kunmap_atomic_primary(0, 2);	break;
        case 1:		__kunmap_atomic_primary(1, 3);	break;
        case 2:		__kunmap_atomic_primary(2, 4);	break;
        case 3:		__kunmap_atomic_primary(3, 5);	break;
        case 4:		__kunmap_atomic_primary(4, 6);	break;
        case 5:		__kunmap_atomic_primary(5, 7);	break;
        case 6:		__kunmap_atomic_primary(6, 8);	break;
        case 7:		__kunmap_atomic_primary(7, 9);	break;
        case 8:		__kunmap_atomic_primary(8, 10);	break;

	case 9 ... 9 + NR_TLB_LINES - 1:
		__kunmap_atomic_secondary(type - 9, kvaddr);
		break;

	default:
		BUG();
	}
	preempt_enable();
}

#endif /* !__ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* _ASM_HIGHMEM_H */
