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

int idr_alloc_cmn(struct idr *idr, void *ptr, unsigned long *index,
		  unsigned long start, unsigned long end, gfp_t gfp,
		  bool ext);

/**
 * idr_alloc - allocate an id
 * @idr: idr handle
 * @ptr: pointer to be associated with the new id
 * @start: the minimum id (inclusive)
 * @end: the maximum id (exclusive)
 * @gfp: memory allocation flags
 *
 * Allocates an unused ID in the range [start, end).  Returns -ENOSPC
 * if there are no unused IDs in that range.
 *
 * Note that @end is treated as max when <= 0.  This is to always allow
 * using @start + N as @end as long as N is inside integer range.
 *
 * Simultaneous modifications to the @idr are not allowed and should be
 * prevented by the user, usually with a lock.  idr_alloc() may be called
 * concurrently with read-only accesses to the @idr, such as idr_find() and
 * idr_for_each_entry().
 */
static inline int idr_alloc(struct idr *idr, void *ptr,
			    int start, int end, gfp_t gfp)
{
	unsigned long id;
	int ret;

	if (WARN_ON_ONCE(start < 0))
		return -EINVAL;

	ret = idr_alloc_cmn(idr, ptr, &id, start, end, gfp, false);

	if (ret)
		return ret;

	return id;
}

static inline int idr_alloc_ext(struct idr *idr, void *ptr,
				unsigned long *index,
				unsigned long start,
				unsigned long end,
				gfp_t gfp)
{
	return idr_alloc_cmn(idr, ptr, index, start, end, gfp, true);
}

int idr_alloc_cyclic(struct idr *, void *entry, int start, int end, gfp_t);
int idr_for_each(const struct idr *,
		 int (*fn)(int id, void *p, void *data), void *data);
void *idr_get_next(struct idr *, int *nextid);
void *idr_get_next_ext(struct idr *idr, unsigned long *nextid);
void *idr_replace(struct idr *, void *, int id);
void *idr_replace_ext(struct idr *idr, void *ptr, unsigned long id);
void idr_destroy(struct idr *);

static inline void *idr_remove_ext(struct idr *idr, unsigned long id)
{
	return radix_tree_delete_item(&idr->idr_rt, id, NULL);
}

static inline void *idr_remove(struct idr *idr, int id)
{
	return idr_remove_ext(idr, id);
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
 * idr_find - return pointer for given id
 * @idr: idr handle
 * @id: lookup key
 *
 * Return the pointer given the id it has been registered with.  A %NULL
 * return indicates that @id is not valid or you passed %NULL in
 * idr_get_new().
 *
 * This function can be called under rcu_read_lock(), given that the leaf
 * pointers lifetimes are correctly managed.
 */
static inline void *idr_find_ext(const struct idr *idr, unsigned long id)
{
	return radix_tree_lookup(&idr->idr_rt, id);
}

static inline void *idr_find(const struct idr *idr, int id)
{
	return idr_find_ext(idr, id);
}

/**
 * idr_for_each_entry - iterate over an idr's elements of a given type
 * @idr:     idr handle
 * @entry:   the type * to use as cursor
 * @id:      id entry's key
 *
 * @entry and @id do not need to be initialized before the loop, and
 * after normal terminatinon @entry is left with the value NULL.  This
 * is convenient for a "not found" value.
 */
#define idr_for_each_entry(idr, entry, id)			\
	for (id = 0; ((entry) = idr_get_next(idr, &(id))) != NULL; ++id)
#define idr_for_each_entry_ext(idr, entry, id)			\
	for (id = 0; ((entry) = idr_get_next_ext(idr, &(id))) != NULL; ++id)

/**
 * idr_for_each_entry_continue - continue iteration over an idr's elements of a given type
 * @idr:     idr handle
 * @entry:   the type * to use as cursor
 * @id:      id entry's key
 *
 * Continue to iterate over list of given type, continuing after
 * the current position.
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
