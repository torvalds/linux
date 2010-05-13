#ifndef _LINUX_POISON_H
#define _LINUX_POISON_H

/********** include/linux/list.h **********/

/*
 * Architectures might want to move the poison pointer offset
 * into some well-recognized area such as 0xdead000000000000,
 * that is also not mappable by user-space exploits:
 */
#ifdef CONFIG_ILLEGAL_POINTER_VALUE
# define POISON_POINTER_DELTA _AC(CONFIG_ILLEGAL_POINTER_VALUE, UL)
#else
# define POISON_POINTER_DELTA 0
#endif

/*
 * These are non-NULL pointers that will result in page faults
 * under normal circumstances, used to verify that nobody uses
 * non-initialized list entries.
 */
#define LIST_POISON1  ((void *) 0x00100100 + POISON_POINTER_DELTA)
#define LIST_POISON2  ((void *) 0x00200200 + POISON_POINTER_DELTA)

/********** include/linux/timer.h **********/
/*
 * Magic number "tsta" to indicate a static timer initializer
 * for the object debugging code.
 */
#define TIMER_ENTRY_STATIC	((void *) 0x74737461)

/********** mm/debug-pagealloc.c **********/
#define PAGE_POISON 0xaa

/********** mm/slab.c **********/
/*
 * Magic nums for obj red zoning.
 * Placed in the first word before and the first word after an obj.
 */
#define	RED_INACTIVE	0x09F911029D74E35BULL	/* when obj is inactive */
#define	RED_ACTIVE	0xD84156C5635688C0ULL	/* when obj is active */

#define SLUB_RED_INACTIVE	0xbb
#define SLUB_RED_ACTIVE		0xcc

/* ...and for poisoning */
#define	POISON_INUSE	0x5a	/* for use-uninitialised poisoning */
#define POISON_FREE	0x6b	/* for use-after-free poisoning */
#define	POISON_END	0xa5	/* end-byte of poisoning */

/********** mm/hugetlb.c **********/
/*
 * Private mappings of hugetlb pages use this poisoned value for
 * page->mapping. The core VM should not be doing anything with this mapping
 * but futex requires the existence of some page->mapping value even though it
 * is unused if PAGE_MAPPING_ANON is set.
 */
#define HUGETLB_POISON	((void *)(0x00300300 + POISON_POINTER_DELTA + PAGE_MAPPING_ANON))

/********** arch/$ARCH/mm/init.c **********/
#define POISON_FREE_INITMEM	0xcc

/********** arch/ia64/hp/common/sba_iommu.c **********/
/*
 * arch/ia64/hp/common/sba_iommu.c uses a 16-byte poison string with a
 * value of "SBAIOMMU POISON\0" for spill-over poisoning.
 */

/********** fs/jbd/journal.c **********/
#define JBD_POISON_FREE		0x5b
#define JBD2_POISON_FREE	0x5c

/********** drivers/base/dmapool.c **********/
#define	POOL_POISON_FREED	0xa7	/* !inuse */
#define	POOL_POISON_ALLOCATED	0xa9	/* !initted */

/********** drivers/atm/ **********/
#define ATM_POISON_FREE		0x12
#define ATM_POISON		0xdeadbeef

/********** net/ **********/
#define NEIGHBOR_DEAD		0xdeadbeef
#define NETFILTER_LINK_POISON	0xdead57ac

/********** kernel/mutexes **********/
#define MUTEX_DEBUG_INIT	0x11
#define MUTEX_DEBUG_FREE	0x22

/********** lib/flex_array.c **********/
#define FLEX_ARRAY_FREE	0x6c	/* for use-after-free poisoning */

/********** security/ **********/
#define KEY_DESTROY		0xbd

/********** sound/oss/ **********/
#define OSS_POISON_FREE		0xAB

#endif
