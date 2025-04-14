/* SPDX-License-Identifier: GPL-2.0 */
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
#define LIST_POISON1  ((void *) 0x100 + POISON_POINTER_DELTA)
#define LIST_POISON2  ((void *) 0x122 + POISON_POINTER_DELTA)

/********** include/linux/timer.h **********/
#define TIMER_ENTRY_STATIC	((void *) 0x300 + POISON_POINTER_DELTA)

/********** mm/page_poison.c **********/
#define PAGE_POISON 0xaa

/********** mm/page_alloc.c ************/

#define TAIL_MAPPING	((void *) 0x400 + POISON_POINTER_DELTA)

/********** mm/slab.c **********/
/*
 * Magic nums for obj red zoning.
 * Placed in the first word before and the first word after an obj.
 */
#define SLUB_RED_INACTIVE	0xbb	/* when obj is inactive */
#define SLUB_RED_ACTIVE		0xcc	/* when obj is active */

/* ...and for poisoning */
#define	POISON_INUSE	0x5a	/* for use-uninitialised poisoning */
#define POISON_FREE	0x6b	/* for use-after-free poisoning */
#define	POISON_END	0xa5	/* end-byte of poisoning */

/********** arch/$ARCH/mm/init.c **********/
#define POISON_FREE_INITMEM	0xcc

/********** fs/jbd/journal.c **********/
#define JBD_POISON_FREE		0x5b
#define JBD2_POISON_FREE	0x5c

/********** drivers/base/dmapool.c **********/
#define	POOL_POISON_FREED	0xa7	/* !inuse */
#define	POOL_POISON_ALLOCATED	0xa9	/* !initted */

/********** drivers/atm/ **********/
#define ATM_POISON_FREE		0x12
#define ATM_POISON		0xdeadbeef

/********** kernel/mutexes **********/
#define MUTEX_DEBUG_INIT	0x11
#define MUTEX_DEBUG_FREE	0x22
#define MUTEX_POISON_WW_CTX	((void *) 0x500 + POISON_POINTER_DELTA)

/********** security/ **********/
#define KEY_DESTROY		0xbd

/********** net/core/page_pool.c **********/
/*
 * page_pool uses additional free bits within this value to store data, see the
 * definition of PP_DMA_INDEX_MASK in mm.h
 */
#define PP_SIGNATURE		(0x40 + POISON_POINTER_DELTA)

/********** net/core/skbuff.c **********/
#define SKB_LIST_POISON_NEXT	((void *)(0x800 + POISON_POINTER_DELTA))
/********** net/ **********/
#define NET_PTR_POISON		((void *)(0x801 + POISON_POINTER_DELTA))

/********** kernel/bpf/ **********/
#define BPF_PTR_POISON ((void *)(0xeB9FUL + POISON_POINTER_DELTA))

/********** VFS **********/
#define VFS_PTR_POISON ((void *)(0xF5 + POISON_POINTER_DELTA))

/********** lib/stackdepot.c **********/
#define STACK_DEPOT_POISON ((void *)(0xD390 + POISON_POINTER_DELTA))

#endif
