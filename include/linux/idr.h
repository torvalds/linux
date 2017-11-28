/*
 * include/linux/idr.h
 * 
 * 2002-10-18  written by Jim Houston jim.houston@ccur.com
 *	Copyright (C) 2002 by Concurrent Computer Corporation
 *	Distributed under the GNU GPL license version 2.
 *
 * Small id to pointer translation service avoiding fixed sized
 * tables.
 */

#ifndef __IDR_H__
#define __IDR_H__

#include <linux/radix-tree.h>
#include <linux/gfp.h>
#include <linux/percpu.h>

struct idr {
	struct radix_tree_root	idr_rt;
	unsigned int		idr_next;
};

/*
 * The IDR API does not expose the tagging functionality of the radix tree
 * to users.  Use tag 0 to track whether a node has free space below it.
 */
#define IDR_FREE	0

/* Set the IDR flag and the IDR_FREE tag */
#define IDR_RT_MARKER		((__force gfp_t)(3 << __GFP_BITS_SHIFT))

#define IDR_INIT							\
{									\
	.idr_rt = RADIX_TREE_INIT(IDR_RT_MARKER)			\
}
#define DEFINE_IDR(name)	struct idr name = IDR_INIT

/**
 * idr_get_cursor - Return the current position of the cyclic allocator
 * @idr: idr handle
 *
 * The value returned is the value that will be next returned from
 * idr_alloc_cyclic() if it is free (otherwise the search will start from
 * this position).
 */
static inline unsigned int idr_get_cursor(const struct idr *idr)
{
	return READ_ONCE(idr->idr_next);
}

/**
 * idr_set_cursor - Set the current position of the cyclic allocator
 * @idr: idr handle
 * @val: new position
 *
 * The next call to idr_alloc_cyclic() will return @val if it is free
 * (otherwise the search will start from this position).
 */
static inline void idr_set_cursor(struct idr *idr, unsigned int val)
{
	WRITE_ONCE(idr->idr_next, val);
}

/**
 * DOC: idr sync
 * idr synchronization (stolen from radix-tree.h)
 *
 * idr_find() is able to be called locklessly, using RCU. The caller must
 * ensure calls to this function are made within rcu_read_lock() regions.
 * Other readers (lock-free or otherwise) and modifications may be running
 * concurrently.
 *
 * It is still required that the caller manage the synchronization and
 * lifetimes of the items. So if RCU lock-free lookups are used, typically
 * this would mean that the items have their own locks, or are amenable to
 * lock-free access; and that the items are freed by RCU (or only freed after
 * having been deleted from the idr tree *and* a synchronize_rcu() grace
 * period).
 */

void idr_preload(gfp_t gfp_mask);

int idr_alloc(struct idr *, void *, int start, int end, gfp_t);
int __must_check idr_alloc_u32(struct idr *, void *ptr, u32 *nextid,
				unsigned long max, gfp_t);
int idr_alloc_cyclic(struct idr *, void *entry, int start, int end, gfp_t);
int idr_for_each(const struct idr *,
		 int (*fn)(int id, void *p, void *data), void *data);
void *idr_get_next(struct idr *, int *nextid);
void *idr_get_next_ul(struct idr *, unsigned long *nextid);
void *idr_replace(struct idr *, void *, unsigned long id);
void idr_destroy(struct idr *);

static inline void *idr_remove(struct idr *idr, unsigned long id)
{
	return radix_tree_delete_item(&idr->idr_rt, id, NULL);
}

static inline void idr_init(struct idr *idr)
{
	INIT_RADIX_TREE(&idr->idr_rt, IDR_RT_MARKER);
	idr->idr_next = 0;
}

static inline bool idr_is_empty(const struct idr *idr)
{
	return radix_tree_empty(&idr->idr_rt) &&
		radix_tree_tagged(&idr->idr_rt, IDR_FREE);
}

/**
 * idr_preload_end - end preload section started with idr_preload()
 *
 * Each idr_preload() should be matched with an invocation of this
 * function.  See idr_preload() for details.
 */
static inline void idr_preload_end(void)
{
	preempt_enable();
}

/**
 * idr_find() - Return pointer for given ID.
 * @idr: IDR handle.
 * @id: Pointer ID.
 *
 * Looks up the pointer associated with this ID.  A %NULL pointer may
 * indicate that @id is not allocated or that the %NULL pointer was
 * associated with this ID.
 *
 * This function can be called under rcu_read_lock(), given that the leaf
 * pointers lifetimes are correctly managed.
 *
 * Return: The pointer associated with this ID.
 */
static inline void *idr_find(const struct idr *idr, unsigned long id)
{
	return radix_tree_lookup(&idr->idr_rt, id);
}

/**
 * idr_for_each_entry() - Iterate over an IDR's elements of a given type.
 * @idr: IDR handle.
 * @entry: The type * to use as cursor
 * @id: Entry ID.
 *
 * @entry and @id do not need to be initialized before the loop, and
 * after normal termination @entry is left with the value NULL.  This
 * is convenient for a "not found" value.
 */
#define idr_for_each_entry(idr, entry, id)			\
	for (id = 0; ((entry) = idr_get_next(idr, &(id))) != NULL; ++id)

/**
 * idr_for_each_entry_ul() - Iterate over an IDR's elements of a given type.
 * @idr: IDR handle.
 * @entry: The type * to use as cursor.
 * @id: Entry ID.
 *
 * @entry and @id do not need to be initialized before the loop, and
 * after normal termination @entry is left with the value NULL.  This
 * is convenient for a "not found" value.
 */
#define idr_for_each_entry_ul(idr, entry, id)			\
	for (id = 0; ((entry) = idr_get_next_ul(idr, &(id))) != NULL; ++id)

/**
 * idr_for_each_entry_continue() - Continue iteration over an IDR's elements of a given type
 * @idr: IDR handle.
 * @entry: The type * to use as a cursor.
 * @id: Entry ID.
 *
 * Continue to iterate over entries, continuing after the current position.
 */
#define idr_for_each_entry_continue(idr, entry, id)			\
	for ((entry) = idr_get_next((idr), &(id));			\
	     entry;							\
	     ++id, (entry) = idr_get_next((idr), &(id)))

/*
 * IDA - IDR based id allocator, use when translation from id to
 * pointer isn't necessary.
 */
#define IDA_CHUNK_SIZE		128	/* 128 bytes per chunk */
#define IDA_BITMAP_LONGS	(IDA_CHUNK_SIZE / sizeof(long))
#define IDA_BITMAP_BITS 	(IDA_BITMAP_LONGS * sizeof(long) * 8)

struct ida_bitmap {
	unsigned long		bitmap[IDA_BITMAP_LONGS];
};

DECLARE_PER_CPU(struct ida_bitmap *, ida_bitmap);

struct ida {
	struct radix_tree_root	ida_rt;
};

#define IDA_INIT	{						\
	.ida_rt = RADIX_TREE_INIT(IDR_RT_MARKER | GFP_NOWAIT),		\
}
#define DEFINE_IDA(name)	struct ida name = IDA_INIT

int ida_pre_get(struct ida *ida, gfp_t gfp_mask);
int ida_get_new_above(struct ida *ida, int starting_id, int *p_id);
void ida_remove(struct ida *ida, int id);
void ida_destroy(struct ida *ida);

int ida_simple_get(struct ida *ida, unsigned int start, unsigned int end,
		   gfp_t gfp_mask);
void ida_simple_remove(struct ida *ida, unsigned int id);

static inline void ida_init(struct ida *ida)
{
	INIT_RADIX_TREE(&ida->ida_rt, IDR_RT_MARKER | GFP_NOWAIT);
}

/**
 * ida_get_new - allocate new ID
 * @ida:	idr handle
 * @p_id:	pointer to the allocated handle
 *
 * Simple wrapper around ida_get_new_above() w/ @starting_id of zero.
 */
static inline int ida_get_new(struct ida *ida, int *p_id)
{
	return ida_get_new_above(ida, 0, p_id);
}

static inline bool ida_is_empty(const struct ida *ida)
{
	return radix_tree_empty(&ida->ida_rt);
}
#endif /* __IDR_H__ */
