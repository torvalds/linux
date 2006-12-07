/*
 * Copyright (C) 2001 Momchil Velikov
 * Portions Copyright (C) 2001 Christoph Hellwig
 * Copyright (C) 2006 Nick Piggin
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
#include <linux/kernel.h>
#include <linux/rcupdate.h>

/*
 * A direct pointer (root->rnode pointing directly to a data item,
 * rather than another radix_tree_node) is signalled by the low bit
 * set in the root->rnode pointer.
 *
 * In this case root->height is also NULL, but the direct pointer tests are
 * needed for RCU lookups when root->height is unreliable.
 */
#define RADIX_TREE_DIRECT_PTR	1

static inline void *radix_tree_ptr_to_direct(void *ptr)
{
	return (void *)((unsigned long)ptr | RADIX_TREE_DIRECT_PTR);
}

static inline void *radix_tree_direct_to_ptr(void *ptr)
{
	return (void *)((unsigned long)ptr & ~RADIX_TREE_DIRECT_PTR);
}

static inline int radix_tree_is_direct_ptr(void *ptr)
{
	return (int)((unsigned long)ptr & RADIX_TREE_DIRECT_PTR);
}

/*** radix-tree API starts here ***/

#define RADIX_TREE_MAX_TAGS 2

/* root tags are stored in gfp_mask, shifted by __GFP_BITS_SHIFT */
struct radix_tree_root {
	unsigned int		height;
	gfp_t			gfp_mask;
	struct radix_tree_node	*rnode;
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
 * - any function _modifying_ the the tree or tags (inserting or deleting
 *   items, setting or clearing tags must exclude other modifications, and
 *   exclude any functions reading the tree.
 * - any function _reading_ the the tree or tags (looking up items or tags,
 *   gang lookups) must exclude modifications to the tree, but may occur
 *   concurrently with other readers.
 *
 * The notable exceptions to this rule are the following functions:
 * radix_tree_lookup
 * radix_tree_tag_get
 * radix_tree_gang_lookup
 * radix_tree_gang_lookup_tag
 * radix_tree_tagged
 *
 * The first 4 functions are able to be called locklessly, using RCU. The
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
 * radix_tree_tagged is able to be called without locking or RCU.
 */

/**
 * radix_tree_deref_slot	- dereference a slot
 * @pslot:	pointer to slot, returned by radix_tree_lookup_slot
 * Returns:	item that was stored in that slot with any direct pointer flag
 *		removed.
 *
 * For use with radix_tree_lookup_slot().  Caller must hold tree at least read
 * locked across slot lookup and dereference.  More likely, will be used with
 * radix_tree_replace_slot(), as well, so caller will hold tree write locked.
 */
static inline void *radix_tree_deref_slot(void **pslot)
{
	return radix_tree_direct_to_ptr(*pslot);
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
	BUG_ON(radix_tree_is_direct_ptr(item));
	rcu_assign_pointer(*pslot,
		(void *)((unsigned long)item |
			((unsigned long)*pslot & RADIX_TREE_DIRECT_PTR)));
}

int radix_tree_insert(struct radix_tree_root *, unsigned long, void *);
void *radix_tree_lookup(struct radix_tree_root *, unsigned long);
void **radix_tree_lookup_slot(struct radix_tree_root *, unsigned long);
void *radix_tree_delete(struct radix_tree_root *, unsigned long);
unsigned int
radix_tree_gang_lookup(struct radix_tree_root *root, void **results,
			unsigned long first_index, unsigned int max_items);
int radix_tree_preload(gfp_t gfp_mask);
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
int radix_tree_tagged(struct radix_tree_root *root, unsigned int tag);

static inline void radix_tree_preload_end(void)
{
	preempt_enable();
}

#endif /* _LINUX_RADIX_TREE_H */
