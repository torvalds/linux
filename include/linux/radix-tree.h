/*
 * Copyright (C) 2001 Momchil Velikov
 * Portions Copyright (C) 2001 Christoph Hellwig
 * Copyright (C) 2006 Nick Piggin
 * Copyright (C) 2012 Konstantin Khlebnikov
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef _LINUX_RADIX_TREE_H
#define _LINUX_RADIX_TREE_H

#include <linux/preempt.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/rcupdate.h>

/*
 * An indirect pointer (root->rnode pointing to a radix_tree_node, rather
 * than a data item) is signalled by the low bit set in the root->rnode
 * pointer.
 *
 * In this case root->height is > 0, but the indirect pointer tests are
 * needed for RCU lookups (because root->height is unreliable). The only
 * time callers need worry about this is when doing a lookup_slot under
 * RCU.
 *
 * Indirect pointer in fact is also used to tag the last pointer of a node
 * when it is shrunk, before we rcu free the node. See shrink code for
 * details.
 */
#define RADIX_TREE_INDIRECT_PTR		1
/*
 * A common use of the radix tree is to store pointers to struct pages;
 * but shmem/tmpfs needs also to store swap entries in the same tree:
 * those are marked as exceptional entries to distinguish them.
 * EXCEPTIONAL_ENTRY tests the bit, EXCEPTIONAL_SHIFT shifts content past it.
 */
#define RADIX_TREE_EXCEPTIONAL_ENTRY	2
#define RADIX_TREE_EXCEPTIONAL_SHIFT	2

#define RADIX_DAX_MASK	0xf
#define RADIX_DAX_SHIFT	4
#define RADIX_DAX_PTE  (0x4 | RADIX_TREE_EXCEPTIONAL_ENTRY)
#define RADIX_DAX_PMD  (0x8 | RADIX_TREE_EXCEPTIONAL_ENTRY)
#define RADIX_DAX_TYPE(entry) ((unsigned long)entry & RADIX_DAX_MASK)
#define RADIX_DAX_SECTOR(entry) (((unsigned long)entry >> RADIX_DAX_SHIFT))
#define RADIX_DAX_ENTRY(sector, pmd) ((void *)((unsigned long)sector << \
		RADIX_DAX_SHIFT | (pmd ? RADIX_DAX_PMD : RADIX_DAX_PTE)))

static inline int radix_tree_is_indirect_ptr(void *ptr)
{
	return (int)((unsigned long)ptr & RADIX_TREE_INDIRECT_PTR);
}

/*** radix-tree API starts here ***/

#define RADIX_TREE_MAX_TAGS 3

#ifdef __KERNEL__
#define RADIX_TREE_MAP_SHIFT	(CONFIG_BASE_SMALL ? 4 : 6)
#else
#define RADIX_TREE_MAP_SHIFT	3	/* For more stressful testing */
#endif

#define RADIX_TREE_MAP_SIZE	(1UL << RADIX_TREE_MAP_SHIFT)
#define RADIX_TREE_MAP_MASK	(RADIX_TREE_MAP_SIZE-1)

#define RADIX_TREE_TAG_LONGS	\
	((RADIX_TREE_MAP_SIZE + BITS_PER_LONG - 1) / BITS_PER_LONG)

#define RADIX_TREE_INDEX_BITS  (8 /* CHAR_BIT */ * sizeof(unsigned long))
#define RADIX_TREE_MAX_PATH (DIV_ROUND_UP(RADIX_TREE_INDEX_BITS, \
					  RADIX_TREE_MAP_SHIFT))

/* Height component in node->path */
#define RADIX_TREE_HEIGHT_SHIFT	(RADIX_TREE_MAX_PATH + 1)
#define RADIX_TREE_HEIGHT_MASK	((1UL << RADIX_TREE_HEIGHT_SHIFT) - 1)

/* Internally used bits of node->count */
#define RADIX_TREE_COUNT_SHIFT	(RADIX_TREE_MAP_SHIFT + 1)
#define RADIX_TREE_COUNT_MASK	((1UL << RADIX_TREE_COUNT_SHIFT) - 1)

struct radix_tree_node {
	unsigned int	path;	/* Offset in parent & height from the bottom */
	unsigned int	count;
	union {
		struct {
			/* Used when ascending tree */
			struct radix_tree_node *parent;
			/* For tree user */
			void *private_data;
		};
		/* Used when freeing node */
		struct rcu_head	rcu_head;
	};
	/* For tree user */
	struct list_head private_list;
	void __rcu	*slots[RADIX_TREE_MAP_SIZE];
	unsigned long	tags[RADIX_TREE_MAX_TAGS][RADIX_TREE_TAG_LONGS];
};

/* root tags are stored in gfp_mask, shifted by __GFP_BITS_SHIFT */
struct radix_tree_root {
	unsigned int		height;
	gfp_t			gfp_mask;
	struct radix_tree_node	__rcu *rnode;
};

#define RADIX_TREE_INIT(mask)	{					\
	.height = 0,							\
	.gfp_mask = (mask),						\
	.rnode = NULL,							\
}

#define RADIX_TREE(name, mask) \
	struct radix_tree_root name = RADIX_TREE_INIT(mask)

#define INIT_RADIX_TREE(root, mask)					\
do {									\
	(root)->height = 0;						\
	(root)->gfp_mask = (mask);					\
	(root)->rnode = NULL;						\
} while (0)

/**
 * Radix-tree synchronization
 *
 * The radix-tree API requires that users provide all synchronisation (with
 * specific exceptions, noted below).
 *
 * Synchronization of access to the data items being stored in the tree, and
 * management of their lifetimes must be completely managed by API users.
 *
 * For API usage, in general,
 * - any function _modifying_ the tree or tags (inserting or deleting
 *   items, setting or clearing tags) must exclude other modifications, and
 *   exclude any functions reading the tree.
 * - any function _reading_ the tree or tags (looking up items or tags,
 *   gang lookups) must exclude modifications to the tree, but may occur
 *   concurrently with other readers.
 *
 * The notable exceptions to this rule are the following functions:
 * __radix_tree_lookup
 * radix_tree_lookup
 * radix_tree_lookup_slot
 * radix_tree_tag_get
 * radix_tree_gang_lookup
 * radix_tree_gang_lookup_slot
 * radix_tree_gang_lookup_tag
 * radix_tree_gang_lookup_tag_slot
 * radix_tree_tagged
 *
 * The first 8 functions are able to be called locklessly, using RCU. The
 * caller must ensure calls to these functions are made within rcu_read_lock()
 * regions. Other readers (lock-free or otherwise) and modifications may be
 * running concurrently.
 *
 * It is still required that the caller manage the synchronization and lifetimes
 * of the items. So if RCU lock-free lookups are used, typically this would mean
 * that the items have their own locks, or are amenable to lock-free access; and
 * that the items are freed by RCU (or only freed after having been deleted from
 * the radix tree *and* a synchronize_rcu() grace period).
 *
 * (Note, rcu_assign_pointer and rcu_dereference are not needed to control
 * access to data items when inserting into or looking up from the radix tree)
 *
 * Note that the value returned by radix_tree_tag_get() may not be relied upon
 * if only the RCU read lock is held.  Functions to set/clear tags and to
 * delete nodes running concurrently with it may affect its result such that
 * two consecutive reads in the same locked section may return different
 * values.  If reliability is required, modification functions must also be
 * excluded from concurrency.
 *
 * radix_tree_tagged is able to be called without locking or RCU.
 */

/**
 * radix_tree_deref_slot	- dereference a slot
 * @pslot:	pointer to slot, returned by radix_tree_lookup_slot
 * Returns:	item that was stored in that slot with any direct pointer flag
 *		removed.
 *
 * For use with radix_tree_lookup_slot().  Caller must hold tree at least read
 * locked across slot lookup and dereference. Not required if write lock is
 * held (ie. items cannot be concurrently inserted).
 *
 * radix_tree_deref_retry must be used to confirm validity of the pointer if
 * only the read lock is held.
 */
static inline void *radix_tree_deref_slot(void **pslot)
{
	return rcu_dereference(*pslot);
}

/**
 * radix_tree_deref_slot_protected	- dereference a slot without RCU lock but with tree lock held
 * @pslot:	pointer to slot, returned by radix_tree_lookup_slot
 * Returns:	item that was stored in that slot with any direct pointer flag
 *		removed.
 *
 * Similar to radix_tree_deref_slot but only used during migration when a pages
 * mapping is being moved. The caller does not hold the RCU read lock but it
 * must hold the tree lock to prevent parallel updates.
 */
static inline void *radix_tree_deref_slot_protected(void **pslot,
							spinlock_t *treelock)
{
	return rcu_dereference_protected(*pslot, lockdep_is_held(treelock));
}

/**
 * radix_tree_deref_retry	- check radix_tree_deref_slot
 * @arg:	pointer returned by radix_tree_deref_slot
 * Returns:	0 if retry is not required, otherwise retry is required
 *
 * radix_tree_deref_retry must be used with radix_tree_deref_slot.
 */
static inline int radix_tree_deref_retry(void *arg)
{
	return unlikely((unsigned long)arg & RADIX_TREE_INDIRECT_PTR);
}

/**
 * radix_tree_exceptional_entry	- radix_tree_deref_slot gave exceptional entry?
 * @arg:	value returned by radix_tree_deref_slot
 * Returns:	0 if well-aligned pointer, non-0 if exceptional entry.
 */
static inline int radix_tree_exceptional_entry(void *arg)
{
	/* Not unlikely because radix_tree_exception often tested first */
	return (unsigned long)arg & RADIX_TREE_EXCEPTIONAL_ENTRY;
}

/**
 * radix_tree_exception	- radix_tree_deref_slot returned either exception?
 * @arg:	value returned by radix_tree_deref_slot
 * Returns:	0 if well-aligned pointer, non-0 if either kind of exception.
 */
static inline int radix_tree_exception(void *arg)
{
	return unlikely((unsigned long)arg &
		(RADIX_TREE_INDIRECT_PTR | RADIX_TREE_EXCEPTIONAL_ENTRY));
}

/**
 * radix_tree_replace_slot	- replace item in a slot
 * @pslot:	pointer to slot, returned by radix_tree_lookup_slot
 * @item:	new item to store in the slot.
 *
 * For use with radix_tree_lookup_slot().  Caller must hold tree write locked
 * across slot lookup and replacement.
 */
static inline void radix_tree_replace_slot(void **pslot, void *item)
{
	BUG_ON(radix_tree_is_indirect_ptr(item));
	rcu_assign_pointer(*pslot, item);
}

int __radix_tree_create(struct radix_tree_root *root, unsigned long index,
			struct radix_tree_node **nodep, void ***slotp);
int radix_tree_insert(struct radix_tree_root *, unsigned long, void *);
void *__radix_tree_lookup(struct radix_tree_root *root, unsigned long index,
			  struct radix_tree_node **nodep, void ***slotp);
void *radix_tree_lookup(struct radix_tree_root *, unsigned long);
void **radix_tree_lookup_slot(struct radix_tree_root *, unsigned long);
bool __radix_tree_delete_node(struct radix_tree_root *root,
			      struct radix_tree_node *node);
void *radix_tree_delete_item(struct radix_tree_root *, unsigned long, void *);
void *radix_tree_delete(struct radix_tree_root *, unsigned long);
unsigned int
radix_tree_gang_lookup(struct radix_tree_root *root, void **results,
			unsigned long first_index, unsigned int max_items);
unsigned int radix_tree_gang_lookup_slot(struct radix_tree_root *root,
			void ***results, unsigned long *indices,
			unsigned long first_index, unsigned int max_items);
int radix_tree_preload(gfp_t gfp_mask);
int radix_tree_maybe_preload(gfp_t gfp_mask);
void radix_tree_init(void);
void *radix_tree_tag_set(struct radix_tree_root *root,
			unsigned long index, unsigned int tag);
void *radix_tree_tag_clear(struct radix_tree_root *root,
			unsigned long index, unsigned int tag);
int radix_tree_tag_get(struct radix_tree_root *root,
			unsigned long index, unsigned int tag);
unsigned int
radix_tree_gang_lookup_tag(struct radix_tree_root *root, void **results,
		unsigned long first_index, unsigned int max_items,
		unsigned int tag);
unsigned int
radix_tree_gang_lookup_tag_slot(struct radix_tree_root *root, void ***results,
		unsigned long first_index, unsigned int max_items,
		unsigned int tag);
unsigned long radix_tree_range_tag_if_tagged(struct radix_tree_root *root,
		unsigned long *first_indexp, unsigned long last_index,
		unsigned long nr_to_tag,
		unsigned int fromtag, unsigned int totag);
int radix_tree_tagged(struct radix_tree_root *root, unsigned int tag);
unsigned long radix_tree_locate_item(struct radix_tree_root *root, void *item);

static inline void radix_tree_preload_end(void)
{
	preempt_enable();
}

/**
 * struct radix_tree_iter - radix tree iterator state
 *
 * @index:	index of current slot
 * @next_index:	next-to-last index for this chunk
 * @tags:	bit-mask for tag-iterating
 *
 * This radix tree iterator works in terms of "chunks" of slots.  A chunk is a
 * subinterval of slots contained within one radix tree leaf node.  It is
 * described by a pointer to its first slot and a struct radix_tree_iter
 * which holds the chunk's position in the tree and its size.  For tagged
 * iteration radix_tree_iter also holds the slots' bit-mask for one chosen
 * radix tree tag.
 */
struct radix_tree_iter {
	unsigned long	index;
	unsigned long	next_index;
	unsigned long	tags;
};

#define RADIX_TREE_ITER_TAG_MASK	0x00FF	/* tag index in lower byte */
#define RADIX_TREE_ITER_TAGGED		0x0100	/* lookup tagged slots */
#define RADIX_TREE_ITER_CONTIG		0x0200	/* stop at first hole */

/**
 * radix_tree_iter_init - initialize radix tree iterator
 *
 * @iter:	pointer to iterator state
 * @start:	iteration starting index
 * Returns:	NULL
 */
static __always_inline void **
radix_tree_iter_init(struct radix_tree_iter *iter, unsigned long start)
{
	/*
	 * Leave iter->tags uninitialized. radix_tree_next_chunk() will fill it
	 * in the case of a successful tagged chunk lookup.  If the lookup was
	 * unsuccessful or non-tagged then nobody cares about ->tags.
	 *
	 * Set index to zero to bypass next_index overflow protection.
	 * See the comment in radix_tree_next_chunk() for details.
	 */
	iter->index = 0;
	iter->next_index = start;
	return NULL;
}

/**
 * radix_tree_next_chunk - find next chunk of slots for iteration
 *
 * @root:	radix tree root
 * @iter:	iterator state
 * @flags:	RADIX_TREE_ITER_* flags and tag index
 * Returns:	pointer to chunk first slot, or NULL if there no more left
 *
 * This function looks up the next chunk in the radix tree starting from
 * @iter->next_index.  It returns a pointer to the chunk's first slot.
 * Also it fills @iter with data about chunk: position in the tree (index),
 * its end (next_index), and constructs a bit mask for tagged iterating (tags).
 */
void **radix_tree_next_chunk(struct radix_tree_root *root,
			     struct radix_tree_iter *iter, unsigned flags);

/**
 * radix_tree_iter_retry - retry this chunk of the iteration
 * @iter:	iterator state
 *
 * If we iterate over a tree protected only by the RCU lock, a race
 * against deletion or creation may result in seeing a slot for which
 * radix_tree_deref_retry() returns true.  If so, call this function
 * and continue the iteration.
 */
static inline __must_check
void **radix_tree_iter_retry(struct radix_tree_iter *iter)
{
	iter->next_index = iter->index;
	return NULL;
}

/**
 * radix_tree_chunk_size - get current chunk size
 *
 * @iter:	pointer to radix tree iterator
 * Returns:	current chunk size
 */
static __always_inline unsigned
radix_tree_chunk_size(struct radix_tree_iter *iter)
{
	return iter->next_index - iter->index;
}

/**
 * radix_tree_next_slot - find next slot in chunk
 *
 * @slot:	pointer to current slot
 * @iter:	pointer to interator state
 * @flags:	RADIX_TREE_ITER_*, should be constant
 * Returns:	pointer to next slot, or NULL if there no more left
 *
 * This function updates @iter->index in the case of a successful lookup.
 * For tagged lookup it also eats @iter->tags.
 */
static __always_inline void **
radix_tree_next_slot(void **slot, struct radix_tree_iter *iter, unsigned flags)
{
	if (flags & RADIX_TREE_ITER_TAGGED) {
		iter->tags >>= 1;
		if (likely(iter->tags & 1ul)) {
			iter->index++;
			return slot + 1;
		}
		if (!(flags & RADIX_TREE_ITER_CONTIG) && likely(iter->tags)) {
			unsigned offset = __ffs(iter->tags);

			iter->tags >>= offset;
			iter->index += offset + 1;
			return slot + offset + 1;
		}
	} else {
		unsigned size = radix_tree_chunk_size(iter) - 1;

		while (size--) {
			slot++;
			iter->index++;
			if (likely(*slot))
				return slot;
			if (flags & RADIX_TREE_ITER_CONTIG) {
				/* forbid switching to the next chunk */
				iter->next_index = 0;
				break;
			}
		}
	}
	return NULL;
}

/**
 * radix_tree_for_each_chunk - iterate over chunks
 *
 * @slot:	the void** variable for pointer to chunk first slot
 * @root:	the struct radix_tree_root pointer
 * @iter:	the struct radix_tree_iter pointer
 * @start:	iteration starting index
 * @flags:	RADIX_TREE_ITER_* and tag index
 *
 * Locks can be released and reacquired between iterations.
 */
#define radix_tree_for_each_chunk(slot, root, iter, start, flags)	\
	for (slot = radix_tree_iter_init(iter, start) ;			\
	      (slot = radix_tree_next_chunk(root, iter, flags)) ;)

/**
 * radix_tree_for_each_chunk_slot - iterate over slots in one chunk
 *
 * @slot:	the void** variable, at the beginning points to chunk first slot
 * @iter:	the struct radix_tree_iter pointer
 * @flags:	RADIX_TREE_ITER_*, should be constant
 *
 * This macro is designed to be nested inside radix_tree_for_each_chunk().
 * @slot points to the radix tree slot, @iter->index contains its index.
 */
#define radix_tree_for_each_chunk_slot(slot, iter, flags)		\
	for (; slot ; slot = radix_tree_next_slot(slot, iter, flags))

/**
 * radix_tree_for_each_slot - iterate over non-empty slots
 *
 * @slot:	the void** variable for pointer to slot
 * @root:	the struct radix_tree_root pointer
 * @iter:	the struct radix_tree_iter pointer
 * @start:	iteration starting index
 *
 * @slot points to radix tree slot, @iter->index contains its index.
 */
#define radix_tree_for_each_slot(slot, root, iter, start)		\
	for (slot = radix_tree_iter_init(iter, start) ;			\
	     slot || (slot = radix_tree_next_chunk(root, iter, 0)) ;	\
	     slot = radix_tree_next_slot(slot, iter, 0))

/**
 * radix_tree_for_each_contig - iterate over contiguous slots
 *
 * @slot:	the void** variable for pointer to slot
 * @root:	the struct radix_tree_root pointer
 * @iter:	the struct radix_tree_iter pointer
 * @start:	iteration starting index
 *
 * @slot points to radix tree slot, @iter->index contains its index.
 */
#define radix_tree_for_each_contig(slot, root, iter, start)		\
	for (slot = radix_tree_iter_init(iter, start) ;			\
	     slot || (slot = radix_tree_next_chunk(root, iter,		\
				RADIX_TREE_ITER_CONTIG)) ;		\
	     slot = radix_tree_next_slot(slot, iter,			\
				RADIX_TREE_ITER_CONTIG))

/**
 * radix_tree_for_each_tagged - iterate over tagged slots
 *
 * @slot:	the void** variable for pointer to slot
 * @root:	the struct radix_tree_root pointer
 * @iter:	the struct radix_tree_iter pointer
 * @start:	iteration starting index
 * @tag:	tag index
 *
 * @slot points to radix tree slot, @iter->index contains its index.
 */
#define radix_tree_for_each_tagged(slot, root, iter, start, tag)	\
	for (slot = radix_tree_iter_init(iter, start) ;			\
	     slot || (slot = radix_tree_next_chunk(root, iter,		\
			      RADIX_TREE_ITER_TAGGED | tag)) ;		\
	     slot = radix_tree_next_slot(slot, iter,			\
				RADIX_TREE_ITER_TAGGED))

#endif /* _LINUX_RADIX_TREE_H */
