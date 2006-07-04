#ifndef _LINUX_POISON_H
#define _LINUX_POISON_H

/********** include/linux/list.h **********/
/*
 * These are non-NULL pointers that will result in page faults
 * under normal circumstances, used to verify that nobody uses
 * non-initialized list entries.
 */
#define LIST_POISON1  ((void *) 0x00100100)
#define LIST_POISON2  ((void *) 0x00200200)

/********** mm/slab.c **********/
/*
 * Magic nums for obj red zoning.
 * Placed in the first word before and the first word after an obj.
 */
#define	RED_INACTIVE	0x5A2CF071UL	/* when obj is inactive */
#define	RED_ACTIVE	0x170FC2A5UL	/* when obj is active */

/* ...and for poisoning */
#define	POISON_INUSE	0x5a	/* for use-uninitialised poisoning */
#define POISON_FREE	0x6b	/* for use-after-free poisoning */
#define	POISON_END	0xa5	/* end-byte of poisoning */

/********** arch/$ARCH/mm/init.c **********/
#define POISON_FREE_INITMEM	0xcc

/********** arch/x86_64/mm/init.c **********/
#define	POISON_FREE_INITDATA	0xba

/********** arch/ia64/hp/common/sba_iommu.c **********/
/*
 * arch/ia64/hp/common/sba_iommu.c uses a 16-byte poison string with a
 * value of "SBAIOMMU POISON\0" for spill-over poisoning.
 */

/********** fs/jbd/journal.c **********/
#define JBD_POISON_FREE	0x5b

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

/********** security/ **********/
#define KEY_DESTROY		0xbd

/********** sound/oss/ **********/
#define OSS_POISON_FREE		0xAB

#endif
