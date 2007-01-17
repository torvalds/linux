/*
 * highmem.h: virtual kernel memory mappings for high memory
 *
 * Used in CONFIG_HIGHMEM systems for memory pages which
 * are not addressable by direct kernel virtual addresses.
 *
 * Copyright (C) 1999 Gerhard Wichert, Siemens AG
 *		      Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with
 * up to 16 Terabyte physical memory. With current x86 CPUs
 * we now support up to 64 Gigabytes physical RAM.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */
#ifndef _ASM_HIGHMEM_H
#define _ASM_HIGHMEM_H

#ifdef __KERNEL__

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <asm/kmap_types.h>

/* undef for production */
#define HIGHMEM_DEBUG 1

/* declarations for highmem.c */
extern unsigned long highstart_pfn, highend_pfn;

extern pte_t *kmap_pte;
extern pgprot_t kmap_prot;
extern pte_t *pkmap_page_table;

/*
 * Right now we initialize only a single pte table. It can be extended
 * easily, subsequent pte tables have to be allocated in one physical
 * chunk of RAM.
 */
#define PKMAP_BASE (0xfe000000UL)
#define LAST_PKMAP 1024
#define LAST_PKMAP_MASK (LAST_PKMAP-1)
#define PKMAP_NR(virt)  ((virt-PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)  (PKMAP_BASE + ((nr) << PAGE_SHIFT))

extern void * kmap_high(struct page *page);
extern void kunmap_high(struct page *page);

/*
 * CONFIG_LIMITED_DMA is for systems with DMA limitations such as Momentum's
 * Jaguar ATX.  This option exploits the highmem code in the kernel so is
 * always enabled together with CONFIG_HIGHMEM but at this time doesn't
 * actually add highmem functionality.
 */

#ifdef CONFIG_LIMITED_DMA

/*
 * These are the default functions for the no-highmem case from
 * <linux/highmem.h>
 */
static inline void *kmap(struct page *page)
{
	might_sleep();
	return page_address(page);
}

#define kunmap(page) do { (void) (page); } while (0)

static inline void *kmap_atomic(struct page *page, enum km_type type)
{
	pagefault_disable();
	return page_address(page);
}

static inline void kunmap_atomic(void *kvaddr, enum km_type type)
{
	pagefault_enable();
}

#define kmap_atomic_pfn(pfn, idx) kmap_atomic(pfn_to_page(pfn), (idx))

#define kmap_atomic_to_page(ptr) virt_to_page(ptr)

#define flush_cache_kmaps()	do { } while (0)

#else /* LIMITED_DMA */

extern void *__kmap(struct page *page);
extern void __kunmap(struct page *page);
extern void *__kmap_atomic(struct page *page, enum km_type type);
extern void __kunmap_atomic(void *kvaddr, enum km_type type);
extern void *kmap_atomic_pfn(unsigned long pfn, enum km_type type);
extern struct page *__kmap_atomic_to_page(void *ptr);

#define kmap			__kmap
#define kunmap			__kunmap
#define kmap_atomic		__kmap_atomic
#define kunmap_atomic		__kunmap_atomic
#define kmap_atomic_to_page	__kmap_atomic_to_page

#define flush_cache_kmaps()	flush_cache_all()

#endif /* LIMITED_DMA */

#endif /* __KERNEL__ */

#endif /* _ASM_HIGHMEM_H */
