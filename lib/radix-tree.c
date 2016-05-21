/*
 * Copyright (C) 2001 Momchil Velikov
 * Portions Copyright (C) 2001 Christoph Hellwig
 * Copyright (C) 2005 SGI, Christoph Lameter
 * Copyright (C) 2006 Nick Piggin
 * Copyright (C) 2012 Konstantin Khlebnikov
 * Copyright (C) 2016 Intel, Matthew Wilcox
 * Copyright (C) 2016 Intel, Ross Zwisler
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

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/radix-tree.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/kmemleak.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/rcupdate.h>
#include <linux/preempt.h>		/* in_interrupt() */


/*
 * Radix tree node cache.
 */
static struct kmem_cache *radix_tree_node_cachep;

/*
 * The radix tree is variable-height, so an insert operation not only has
 * to build the branch to its corresponding item, it also has to build the
 * branch to existing items if the size has to be increased (by
 * radix_tree_extend).
 *
 * The worst case is a zero height tree with just a single item at index 0,
 * and then inserting an item at index ULONG_MAX. This requires 2 new branches
 * of RADIX_TREE_MAX_PATH size to be created, with only the root node shared.
 * Hence:
 */
#define RADIX_TREE_PRELOAD_SIZE (RADIX_TREE_MAX_PATH * 2 - 1)

/*
 * Per-cpu pool of preloaded nodes
 */
struct radix_tree_preload {
	unsigned nr;
	/* nodes->private_data points to next preallocated node */
	struct radix_tree_node *nodes;
};
static DEFINE_PER_CPU(struct radix_tree_preload, radix_tree_preloads) = { 0, };

static inline void *ptr_to_indirect(void *ptr)
{
	return (void *)((unsigned long)ptr | RADIX_TREE_INTERNAL_NODE);
}

#define RADIX_TREE_RETRY	ptr_to_indirect(NULL)

#ifdef CONFIG_RADIX_TREE_MULTIORDER
/* Sibling slots point directly to another slot in the same node */
static inline bool is_sibling_entry(struct radix_tree_node *parent, void *node)
{
	void **ptr = node;
	return (parent->slots <= ptr) &&
			(ptr < parent->slots + RADIX_TREE_MAP_SIZE);
}
#else
static inline bool is_sibling_entry(struct radix_tree_node *parent, void *node)
{
	return false;
}
#endif

static inline unsigned long get_slot_offset(struct radix_tree_node *parent,
						 void **slot)
{
	return slot - parent->slots;
}

static unsigned radix_tree_descend(struct radix_tree_node *parent,
				struct radix_tree_node **nodep, unsigned offset)
{
	void **entry = rcu_dereference_raw(parent->slots[offset]);

#ifdef CONFIG_RADIX_TREE_MULTIORDER
	if (radix_tree_is_indirect_ptr(entry)) {
		unsigned long siboff = get_slot_offset(parent, entry);
		if (siboff < RADIX_TREE_MAP_SIZE) {
			offset = siboff;
			entry = rcu_dereference_raw(parent->slots[offset]);
		}
	}
#endif

	*nodep = (void *)entry;
	return offset;
}

static inline gfp_t root_gfp_mask(struct radix_tree_root *root)
{
	return root->gfp_mask & __GFP_BITS_MASK;
}

static inline void tag_set(struct radix_tree_node *node, unsigned int tag,
		int offset)
{
	__set_bit(offset, node->tags[tag]);
}

static inline void tag_clear(struct radix_tree_node *node, unsigned int tag,
		int offset)
{
	__clear_bit(offset, node->tags[tag]);
}

static inline int tag_get(struct radix_tree_node *node, unsigned int tag,
		int offset)
{
	return test_bit(offset, node->tags[tag]);
}

static inline void root_tag_set(struct radix_tree_root *root, unsigned int tag)
{
	root->gfp_mask |= (__force gfp_t)(1 << (tag + __GFP_BITS_SHIFT));
}

static inline void root_tag_clear(struct radix_tree_root *root, unsigned tag)
{
	root->gfp_mask &= (__force gfp_t)~(1 << (tag + __GFP_BITS_SHIFT));
}

static inline void root_tag_clear_all(struct radix_tree_root *root)
{
	root->gfp_mask &= __GFP_BITS_MASK;
}

static inline int root_tag_get(struct radix_tree_root *root, unsigned int tag)
{
	return (__force int)root->gfp_mask & (1 << (tag + __GFP_BITS_SHIFT));
}

static inline unsigned root_tags_get(struct radix_tree_root *root)
{
	return (__force unsigned)root->gfp_mask >> __GFP_BITS_SHIFT;
}

/*
 * Returns 1 if any slot in the node has this tag set.
 * Otherwise returns 0.
 */
static inline int any_tag_set(struct radix_tree_node *node, unsigned int tag)
{
	unsigned idx;
	for (idx = 0; idx < RADIX_TREE_TAG_LONGS; idx++) {
		if (node->tags[tag][idx])
			return 1;
	}
	return 0;
}

/**
 * radix_tree_find_next_bit - find the next set bit in a memory region
 *
 * @addr: The address to base the search on
 * @size: The bitmap size in bits
 * @offset: The bitnumber to start searching at
 *
 * Unrollable variant of find_next_bit() for constant size arrays.
 * Tail bits starting from size to roundup(size, BITS_PER_LONG) must be zero.
 * Returns next bit offset, or size if nothing found.
 */
static __always_inline unsigned long
radix_tree_find_next_bit(const unsigned long *addr,
			 unsigned long size, unsigned long offset)
{
	if (!__builtin_constant_p(size))
		return find_next_bit(addr, size, offset);

	if (offset < size) {
		unsigned long tmp;

		addr += offset / BITS_PER_LONG;
		tmp = *addr >> (offset % BITS_PER_LONG);
		if (tmp)
			return __ffs(tmp) + offset;
		offset = (offset + BITS_PER_LONG) & ~(BITS_PER_LONG - 1);
		while (offset < size) {
			tmp = *++addr;
			if (tmp)
				return __ffs(tmp) + offset;
			offset += BITS_PER_LONG;
		}
	}
	return size;
}

#ifndef __KERNEL__
static void dump_node(struct radix_tree_node *node, unsigned long index)
{
	unsigned long i;

	pr_debug("radix node: %p offset %d tags %lx %lx %lx shift %d count %d parent %p\n",
		node, node->offset,
		node->tags[0][0], node->tags[1][0], node->tags[2][0],
		node->shift, node->count, node->parent);

	for (i = 0; i < RADIX_TREE_MAP_SIZE; i++) {
		unsigned long first = index | (i << node->shift);
		unsigned long last = first | ((1UL << node->shift) - 1);
		void *entry = node->slots[i];
		if (!entry)
			continue;
		if (is_sibling_entry(node, entry)) {
			pr_debug("radix sblng %p offset %ld val %p indices %ld-%ld\n",
					entry, i,
					*(void **)indirect_to_ptr(entry),
					first, last);
		} else if (!radix_tree_is_indirect_ptr(entry)) {
			pr_debug("radix entry %p offset %ld indices %ld-%ld\n",
					entry, i, first, last);
		} else {
			dump_node(indirect_to_ptr(entry), first);
		}
	}
}

/* For debug */
static void radix_tree_dump(struct radix_tree_root *root)
{
	pr_debug("radix root: %p rnode %p tags %x\n",
			root, root->rnode,
			root->gfp_mask >> __GFP_BITS_SHIFT);
	if (!radix_tree_is_indirect_ptr(root->rnode))
		return;
	dump_node(indirect_to_ptr(root->rnode), 0);
}
#endif

/*
 * This assumes that the caller has performed appropriate preallocation, and
 * that the caller has pinned this thread of control to the current CPU.
 */
static struct radix_tree_node *
radix_tree_node_alloc(struct radix_tree_root *root)
{
	struct radix_tree_node *ret = NULL;
	gfp_t gfp_mask = root_gfp_mask(root);

	/*
	 * Preload code isn't irq safe and it doesn't make sense to use
	 * preloading during an interrupt anyway as all the allocations have
	 * to be atomic. So just do normal allocation when in interrupt.
	 */
	if (!gfpflags_allow_blocking(gfp_mask) && !in_interrupt()) {
		struct radix_tree_preload *rtp;

		/*
		 * Even if the caller has preloaded, try to allocate from the
		 * cache first for the new node to get accounted.
		 */
		ret = kmem_cache_alloc(radix_tree_node_cachep,
				       gfp_mask | __GFP_ACCOUNT | __GFP_NOWARN);
		if (ret)
			goto out;

		/*
		 * Provided the caller has preloaded here, we will always
		 * succeed in getting a node here (and never reach
		 * kmem_cache_alloc)
		 */
		rtp = this_cpu_ptr(&radix_tree_preloads);
		if (rtp->nr) {
			ret = rtp->nodes;
			rtp->nodes = ret->private_data;
			ret->private_data = NULL;
			rtp->nr--;
		}
		/*
		 * Update the allocation stack trace as this is more useful
		 * for debugging.
		 */
		kmemleak_update_trace(ret);
		goto out;
	}
	ret = kmem_cache_alloc(radix_tree_node_cachep,
			       gfp_mask | __GFP_ACCOUNT);
out:
	BUG_ON(radix_tree_is_indirect_ptr(ret));
	return ret;
}

static void radix_tree_node_rcu_free(struct rcu_head *head)
{
	struct radix_tree_node *node =
			container_of(head, struct radix_tree_node, rcu_head);
	int i;

	/*
	 * must only free zeroed nodes into the slab. radix_tree_shrink
	 * can leave us with a non-NULL entry in the first slot, so clear
	 * that here to make sure.
	 */
	for (i = 0; i < RADIX_TREE_MAX_TAGS; i++)
		tag_clear(node, i, 0);

	node->slots[0] = NULL;
	node->count = 0;

	kmem_cache_free(radix_tree_node_cachep, node);
}

static inline void
radix_tree_node_free(struct radix_tree_node *node)
{
	call_rcu(&node->rcu_head, radix_tree_node_rcu_free);
}

/*
 * Load up this CPU's radix_tree_node buffer with sufficient objects to
 * ensure that the addition of a single element in the tree cannot fail.  On
 * success, return zero, with preemption disabled.  On error, return -ENOMEM
 * with preemption not disabled.
 *
 * To make use of this facility, the radix tree must be initialised without
 * __GFP_DIRECT_RECLAIM being passed to INIT_RADIX_TREE().
 */
static int __radix_tree_preload(gfp_t gfp_mask)
{
	struct radix_tree_preload *rtp;
	struct radix_tree_node *node;
	int ret = -ENOMEM;

	preempt_disable();
	rtp = this_cpu_ptr(&radix_tree_preloads);
	while (rtp->nr < RADIX_TREE_PRELOAD_SIZE) {
		preempt_enable();
		node = kmem_cache_alloc(radix_tree_node_cachep, gfp_mask);
		if (node == NULL)
			goto out;
		preempt_disable();
		rtp = this_cpu_ptr(&radix_tree_preloads);
		if (rtp->nr < RADIX_TREE_PRELOAD_SIZE) {
			node->private_data = rtp->nodes;
			rtp->nodes = node;
			rtp->nr++;
		} else {
			kmem_cache_free(radix_tree_node_cachep, node);
		}
	}
	ret = 0;
out:
	return ret;
}

/*
 * Load up this CPU's radix_tree_node buffer with sufficient objects to
 * ensure that the addition of a single element in the tree cannot fail.  On
 * success, return zero, with preemption disabled.  On error, return -ENOMEM
 * with preemption not disabled.
 *
 * To make use of this facility, the radix tree must be initialised without
 * __GFP_DIRECT_RECLAIM being passed to INIT_RADIX_TREE().
 */
int radix_tree_preload(gfp_t gfp_mask)
{
	/* Warn on non-sensical use... */
	WARN_ON_ONCE(!gfpflags_allow_blocking(gfp_mask));
	return __radix_tree_preload(gfp_mask);
}
EXPORT_SYMBOL(radix_tree_preload);

/*
 * The same as above function, except we don't guarantee preloading happens.
 * We do it, if we decide it helps. On success, return zero with preemption
 * disabled. On error, return -ENOMEM with preemption not disabled.
 */
int radix_tree_maybe_preload(gfp_t gfp_mask)
{
	if (gfpflags_allow_blocking(gfp_mask))
		return __radix_tree_preload(gfp_mask);
	/* Preloading doesn't help anything with this gfp mask, skip it */
	preempt_disable();
	return 0;
}
EXPORT_SYMBOL(radix_tree_maybe_preload);

/*
 * The maximum index which can be stored in a radix tree
 */
static inline unsigned long shift_maxindex(unsigned int shift)
{
	return (RADIX_TREE_MAP_SIZE << shift) - 1;
}

static inline unsigned long node_maxindex(struct radix_tree_node *node)
{
	return shift_maxindex(node->shift);
}

static unsigned radix_tree_load_root(struct radix_tree_root *root,
		struct radix_tree_node **nodep, unsigned long *maxindex)
{
	struct radix_tree_node *node = rcu_dereference_raw(root->rnode);

	*nodep = node;

	if (likely(radix_tree_is_indirect_ptr(node))) {
		node = indirect_to_ptr(node);
		*maxindex = node_maxindex(node);
		return node->shift + RADIX_TREE_MAP_SHIFT;
	}

	*maxindex = 0;
	return 0;
}

/*
 *	Extend a radix tree so it can store key @index.
 */
static int radix_tree_extend(struct radix_tree_root *root,
				unsigned long index, unsigned int shift)
{
	struct radix_tree_node *slot;
	unsigned int maxshift;
	int tag;

	/* Figure out what the shift should be.  */
	maxshift = shift;
	while (index > shift_maxindex(maxshift))
		maxshift += RADIX_TREE_MAP_SHIFT;

	slot = root->rnode;
	if (!slot)
		goto out;

	do {
		struct radix_tree_node *node = radix_tree_node_alloc(root);

		if (!node)
			return -ENOMEM;

		/* Propagate the aggregated tag info into the new root */
		for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
			if (root_tag_get(root, tag))
				tag_set(node, tag, 0);
		}

		BUG_ON(shift > BITS_PER_LONG);
		node->shift = shift;
		node->offset = 0;
		node->count = 1;
		node->parent = NULL;
		if (radix_tree_is_indirect_ptr(slot)) {
			slot = indirect_to_ptr(slot);
			slot->parent = node;
			slot = ptr_to_indirect(slot);
		}
		node->slots[0] = slot;
		node = ptr_to_indirect(node);
		rcu_assign_pointer(root->rnode, node);
		shift += RADIX_TREE_MAP_SHIFT;
		slot = node;
	} while (shift <= maxshift);
out:
	return maxshift + RADIX_TREE_MAP_SHIFT;
}

/**
 *	__radix_tree_create	-	create a slot in a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *	@order:		index occupies 2^order aligned slots
 *	@nodep:		returns node
 *	@slotp:		returns slot
 *
 *	Create, if necessary, and return the node and slot for an item
 *	at position @index in the radix tree @root.
 *
 *	Until there is more than one item in the tree, no nodes are
 *	allocated and @root->rnode is used as a direct slot instead of
 *	pointing to a node, in which case *@nodep will be NULL.
 *
 *	Returns -ENOMEM, or 0 for success.
 */
int __radix_tree_create(struct radix_tree_root *root, unsigned long index,
			unsigned order, struct radix_tree_node **nodep,
			void ***slotp)
{
	struct radix_tree_node *node = NULL, *slot;
	unsigned long maxindex;
	unsigned int shift, offset;
	unsigned long max = index | ((1UL << order) - 1);

	shift = radix_tree_load_root(root, &slot, &maxindex);

	/* Make sure the tree is high enough.  */
	if (max > maxindex) {
		int error = radix_tree_extend(root, max, shift);
		if (error < 0)
			return error;
		shift = error;
		slot = root->rnode;
		if (order == shift)
			shift += RADIX_TREE_MAP_SHIFT;
	}

	offset = 0;			/* uninitialised var warning */
	while (shift > order) {
		shift -= RADIX_TREE_MAP_SHIFT;
		if (slot == NULL) {
			/* Have to add a child node.  */
			slot = radix_tree_node_alloc(root);
			if (!slot)
				return -ENOMEM;
			slot->shift = shift;
			slot->offset = offset;
			slot->parent = node;
			if (node) {
				rcu_assign_pointer(node->slots[offset],
							ptr_to_indirect(slot));
				node->count++;
			} else
				rcu_assign_pointer(root->rnode,
							ptr_to_indirect(slot));
		} else if (!radix_tree_is_indirect_ptr(slot))
			break;

		/* Go a level down */
		node = indirect_to_ptr(slot);
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		offset = radix_tree_descend(node, &slot, offset);
	}

#ifdef CONFIG_RADIX_TREE_MULTIORDER
	/* Insert pointers to the canonical entry */
	if (order > shift) {
		int i, n = 1 << (order - shift);
		offset = offset & ~(n - 1);
		slot = ptr_to_indirect(&node->slots[offset]);
		for (i = 0; i < n; i++) {
			if (node->slots[offset + i])
				return -EEXIST;
		}

		for (i = 1; i < n; i++) {
			rcu_assign_pointer(node->slots[offset + i], slot);
			node->count++;
		}
	}
#endif

	if (nodep)
		*nodep = node;
	if (slotp)
		*slotp = node ? node->slots + offset : (void **)&root->rnode;
	return 0;
}

/**
 *	__radix_tree_insert    -    insert into a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *	@order:		key covers the 2^order indices around index
 *	@item:		item to insert
 *
 *	Insert an item into the radix tree at position @index.
 */
int __radix_tree_insert(struct radix_tree_root *root, unsigned long index,
			unsigned order, void *item)
{
	struct radix_tree_node *node;
	void **slot;
	int error;

	BUG_ON(radix_tree_is_indirect_ptr(item));

	error = __radix_tree_create(root, index, order, &node, &slot);
	if (error)
		return error;
	if (*slot != NULL)
		return -EEXIST;
	rcu_assign_pointer(*slot, item);

	if (node) {
		unsigned offset = get_slot_offset(node, slot);
		node->count++;
		BUG_ON(tag_get(node, 0, offset));
		BUG_ON(tag_get(node, 1, offset));
		BUG_ON(tag_get(node, 2, offset));
	} else {
		BUG_ON(root_tags_get(root));
	}

	return 0;
}
EXPORT_SYMBOL(__radix_tree_insert);

/**
 *	__radix_tree_lookup	-	lookup an item in a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *	@nodep:		returns node
 *	@slotp:		returns slot
 *
 *	Lookup and return the item at position @index in the radix
 *	tree @root.
 *
 *	Until there is more than one item in the tree, no nodes are
 *	allocated and @root->rnode is used as a direct slot instead of
 *	pointing to a node, in which case *@nodep will be NULL.
 */
void *__radix_tree_lookup(struct radix_tree_root *root, unsigned long index,
			  struct radix_tree_node **nodep, void ***slotp)
{
	struct radix_tree_node *node, *parent;
	unsigned long maxindex;
	unsigned int shift;
	void **slot;

 restart:
	parent = NULL;
	slot = (void **)&root->rnode;
	shift = radix_tree_load_root(root, &node, &maxindex);
	if (index > maxindex)
		return NULL;

	while (radix_tree_is_indirect_ptr(node)) {
		unsigned offset;

		if (node == RADIX_TREE_RETRY)
			goto restart;
		parent = indirect_to_ptr(node);
		shift -= RADIX_TREE_MAP_SHIFT;
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		offset = radix_tree_descend(parent, &node, offset);
		slot = parent->slots + offset;
	}

	if (nodep)
		*nodep = parent;
	if (slotp)
		*slotp = slot;
	return node;
}

/**
 *	radix_tree_lookup_slot    -    lookup a slot in a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *
 *	Returns:  the slot corresponding to the position @index in the
 *	radix tree @root. This is useful for update-if-exists operations.
 *
 *	This function can be called under rcu_read_lock iff the slot is not
 *	modified by radix_tree_replace_slot, otherwise it must be called
 *	exclusive from other writers. Any dereference of the slot must be done
 *	using radix_tree_deref_slot.
 */
void **radix_tree_lookup_slot(struct radix_tree_root *root, unsigned long index)
{
	void **slot;

	if (!__radix_tree_lookup(root, index, NULL, &slot))
		return NULL;
	return slot;
}
EXPORT_SYMBOL(radix_tree_lookup_slot);

/**
 *	radix_tree_lookup    -    perform lookup operation on a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *
 *	Lookup the item at the position @index in the radix tree @root.
 *
 *	This function can be called under rcu_read_lock, however the caller
 *	must manage lifetimes of leaf nodes (eg. RCU may also be used to free
 *	them safely). No RCU barriers are required to access or modify the
 *	returned item, however.
 */
void *radix_tree_lookup(struct radix_tree_root *root, unsigned long index)
{
	return __radix_tree_lookup(root, index, NULL, NULL);
}
EXPORT_SYMBOL(radix_tree_lookup);

/**
 *	radix_tree_tag_set - set a tag on a radix tree node
 *	@root:		radix tree root
 *	@index:		index key
 *	@tag:		tag index
 *
 *	Set the search tag (which must be < RADIX_TREE_MAX_TAGS)
 *	corresponding to @index in the radix tree.  From
 *	the root all the way down to the leaf node.
 *
 *	Returns the address of the tagged item.  Setting a tag on a not-present
 *	item is a bug.
 */
void *radix_tree_tag_set(struct radix_tree_root *root,
			unsigned long index, unsigned int tag)
{
	struct radix_tree_node *node, *parent;
	unsigned long maxindex;
	unsigned int shift;

	shift = radix_tree_load_root(root, &node, &maxindex);
	BUG_ON(index > maxindex);

	while (radix_tree_is_indirect_ptr(node)) {
		unsigned offset;

		shift -= RADIX_TREE_MAP_SHIFT;
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;

		parent = indirect_to_ptr(node);
		offset = radix_tree_descend(parent, &node, offset);
		BUG_ON(!node);

		if (!tag_get(parent, tag, offset))
			tag_set(parent, tag, offset);
	}

	/* set the root's tag bit */
	if (!root_tag_get(root, tag))
		root_tag_set(root, tag);

	return node;
}
EXPORT_SYMBOL(radix_tree_tag_set);

/**
 *	radix_tree_tag_clear - clear a tag on a radix tree node
 *	@root:		radix tree root
 *	@index:		index key
 *	@tag:		tag index
 *
 *	Clear the search tag (which must be < RADIX_TREE_MAX_TAGS)
 *	corresponding to @index in the radix tree.  If this causes
 *	the leaf node to have no tags set then clear the tag in the
 *	next-to-leaf node, etc.
 *
 *	Returns the address of the tagged item on success, else NULL.  ie:
 *	has the same return value and semantics as radix_tree_lookup().
 */
void *radix_tree_tag_clear(struct radix_tree_root *root,
			unsigned long index, unsigned int tag)
{
	struct radix_tree_node *node, *parent;
	unsigned long maxindex;
	unsigned int shift;
	int uninitialized_var(offset);

	shift = radix_tree_load_root(root, &node, &maxindex);
	if (index > maxindex)
		return NULL;

	parent = NULL;

	while (radix_tree_is_indirect_ptr(node)) {
		shift -= RADIX_TREE_MAP_SHIFT;
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;

		parent = indirect_to_ptr(node);
		offset = radix_tree_descend(parent, &node, offset);
	}

	if (node == NULL)
		goto out;

	index >>= shift;

	while (parent) {
		if (!tag_get(parent, tag, offset))
			goto out;
		tag_clear(parent, tag, offset);
		if (any_tag_set(parent, tag))
			goto out;

		index >>= RADIX_TREE_MAP_SHIFT;
		offset = index & RADIX_TREE_MAP_MASK;
		parent = parent->parent;
	}

	/* clear the root's tag bit */
	if (root_tag_get(root, tag))
		root_tag_clear(root, tag);

out:
	return node;
}
EXPORT_SYMBOL(radix_tree_tag_clear);

/**
 * radix_tree_tag_get - get a tag on a radix tree node
 * @root:		radix tree root
 * @index:		index key
 * @tag:		tag index (< RADIX_TREE_MAX_TAGS)
 *
 * Return values:
 *
 *  0: tag not present or not set
 *  1: tag set
 *
 * Note that the return value of this function may not be relied on, even if
 * the RCU lock is held, unless tag modification and node deletion are excluded
 * from concurrency.
 */
int radix_tree_tag_get(struct radix_tree_root *root,
			unsigned long index, unsigned int tag)
{
	struct radix_tree_node *node, *parent;
	unsigned long maxindex;
	unsigned int shift;

	if (!root_tag_get(root, tag))
		return 0;

	shift = radix_tree_load_root(root, &node, &maxindex);
	if (index > maxindex)
		return 0;
	if (node == NULL)
		return 0;

	while (radix_tree_is_indirect_ptr(node)) {
		int offset;

		shift -= RADIX_TREE_MAP_SHIFT;
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;

		parent = indirect_to_ptr(node);
		offset = radix_tree_descend(parent, &node, offset);

		if (!node)
			return 0;
		if (!tag_get(parent, tag, offset))
			return 0;
		if (node == RADIX_TREE_RETRY)
			break;
	}

	return 1;
}
EXPORT_SYMBOL(radix_tree_tag_get);

static inline void __set_iter_shift(struct radix_tree_iter *iter,
					unsigned int shift)
{
#ifdef CONFIG_RADIX_TREE_MULTIORDER
	iter->shift = shift;
#endif
}

/**
 * radix_tree_next_chunk - find next chunk of slots for iteration
 *
 * @root:	radix tree root
 * @iter:	iterator state
 * @flags:	RADIX_TREE_ITER_* flags and tag index
 * Returns:	pointer to chunk first slot, or NULL if iteration is over
 */
void **radix_tree_next_chunk(struct radix_tree_root *root,
			     struct radix_tree_iter *iter, unsigned flags)
{
	unsigned shift, tag = flags & RADIX_TREE_ITER_TAG_MASK;
	struct radix_tree_node *rnode, *node;
	unsigned long index, offset, maxindex;

	if ((flags & RADIX_TREE_ITER_TAGGED) && !root_tag_get(root, tag))
		return NULL;

	/*
	 * Catch next_index overflow after ~0UL. iter->index never overflows
	 * during iterating; it can be zero only at the beginning.
	 * And we cannot overflow iter->next_index in a single step,
	 * because RADIX_TREE_MAP_SHIFT < BITS_PER_LONG.
	 *
	 * This condition also used by radix_tree_next_slot() to stop
	 * contiguous iterating, and forbid swithing to the next chunk.
	 */
	index = iter->next_index;
	if (!index && iter->index)
		return NULL;

 restart:
	shift = radix_tree_load_root(root, &rnode, &maxindex);
	if (index > maxindex)
		return NULL;

	if (radix_tree_is_indirect_ptr(rnode)) {
		rnode = indirect_to_ptr(rnode);
	} else if (rnode) {
		/* Single-slot tree */
		iter->index = index;
		iter->next_index = maxindex + 1;
		iter->tags = 1;
		__set_iter_shift(iter, shift);
		return (void **)&root->rnode;
	} else
		return NULL;

	shift -= RADIX_TREE_MAP_SHIFT;
	offset = index >> shift;

	node = rnode;
	while (1) {
		struct radix_tree_node *slot;
		unsigned new_off = radix_tree_descend(node, &slot, offset);

		if (new_off < offset) {
			offset = new_off;
			index &= ~((RADIX_TREE_MAP_SIZE << shift) - 1);
			index |= offset << shift;
		}

		if ((flags & RADIX_TREE_ITER_TAGGED) ?
				!tag_get(node, tag, offset) : !slot) {
			/* Hole detected */
			if (flags & RADIX_TREE_ITER_CONTIG)
				return NULL;

			if (flags & RADIX_TREE_ITER_TAGGED)
				offset = radix_tree_find_next_bit(
						node->tags[tag],
						RADIX_TREE_MAP_SIZE,
						offset + 1);
			else
				while (++offset	< RADIX_TREE_MAP_SIZE) {
					void *slot = node->slots[offset];
					if (is_sibling_entry(node, slot))
						continue;
					if (slot)
						break;
				}
			index &= ~((RADIX_TREE_MAP_SIZE << shift) - 1);
			index += offset << shift;
			/* Overflow after ~0UL */
			if (!index)
				return NULL;
			if (offset == RADIX_TREE_MAP_SIZE)
				goto restart;
			slot = rcu_dereference_raw(node->slots[offset]);
		}

		if ((slot == NULL) || (slot == RADIX_TREE_RETRY))
			goto restart;
		if (!radix_tree_is_indirect_ptr(slot))
			break;

		node = indirect_to_ptr(slot);
		shift -= RADIX_TREE_MAP_SHIFT;
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
	}

	/* Update the iterator state */
	iter->index = index & ~((1 << shift) - 1);
	iter->next_index = (index | ((RADIX_TREE_MAP_SIZE << shift) - 1)) + 1;
	__set_iter_shift(iter, shift);

	/* Construct iter->tags bit-mask from node->tags[tag] array */
	if (flags & RADIX_TREE_ITER_TAGGED) {
		unsigned tag_long, tag_bit;

		tag_long = offset / BITS_PER_LONG;
		tag_bit  = offset % BITS_PER_LONG;
		iter->tags = node->tags[tag][tag_long] >> tag_bit;
		/* This never happens if RADIX_TREE_TAG_LONGS == 1 */
		if (tag_long < RADIX_TREE_TAG_LONGS - 1) {
			/* Pick tags from next element */
			if (tag_bit)
				iter->tags |= node->tags[tag][tag_long + 1] <<
						(BITS_PER_LONG - tag_bit);
			/* Clip chunk size, here only BITS_PER_LONG tags */
			iter->next_index = index + BITS_PER_LONG;
		}
	}

	return node->slots + offset;
}
EXPORT_SYMBOL(radix_tree_next_chunk);

/**
 * radix_tree_range_tag_if_tagged - for each item in given range set given
 *				   tag if item has another tag set
 * @root:		radix tree root
 * @first_indexp:	pointer to a starting index of a range to scan
 * @last_index:		last index of a range to scan
 * @nr_to_tag:		maximum number items to tag
 * @iftag:		tag index to test
 * @settag:		tag index to set if tested tag is set
 *
 * This function scans range of radix tree from first_index to last_index
 * (inclusive).  For each item in the range if iftag is set, the function sets
 * also settag. The function stops either after tagging nr_to_tag items or
 * after reaching last_index.
 *
 * The tags must be set from the leaf level only and propagated back up the
 * path to the root. We must do this so that we resolve the full path before
 * setting any tags on intermediate nodes. If we set tags as we descend, then
 * we can get to the leaf node and find that the index that has the iftag
 * set is outside the range we are scanning. This reults in dangling tags and
 * can lead to problems with later tag operations (e.g. livelocks on lookups).
 *
 * The function returns the number of leaves where the tag was set and sets
 * *first_indexp to the first unscanned index.
 * WARNING! *first_indexp can wrap if last_index is ULONG_MAX. Caller must
 * be prepared to handle that.
 */
unsigned long radix_tree_range_tag_if_tagged(struct radix_tree_root *root,
		unsigned long *first_indexp, unsigned long last_index,
		unsigned long nr_to_tag,
		unsigned int iftag, unsigned int settag)
{
	struct radix_tree_node *slot, *node = NULL;
	unsigned long maxindex;
	unsigned int shift = radix_tree_load_root(root, &slot, &maxindex);
	unsigned long tagged = 0;
	unsigned long index = *first_indexp;

	last_index = min(last_index, maxindex);
	if (index > last_index)
		return 0;
	if (!nr_to_tag)
		return 0;
	if (!root_tag_get(root, iftag)) {
		*first_indexp = last_index + 1;
		return 0;
	}
	if (!radix_tree_is_indirect_ptr(slot)) {
		*first_indexp = last_index + 1;
		root_tag_set(root, settag);
		return 1;
	}

	node = indirect_to_ptr(slot);
	shift -= RADIX_TREE_MAP_SHIFT;

	for (;;) {
		unsigned long upindex;
		unsigned offset;

		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		offset = radix_tree_descend(node, &slot, offset);
		if (!slot)
			goto next;
		if (!tag_get(node, iftag, offset))
			goto next;
		/* Sibling slots never have tags set on them */
		if (radix_tree_is_indirect_ptr(slot)) {
			node = indirect_to_ptr(slot);
			shift -= RADIX_TREE_MAP_SHIFT;
			continue;
		}

		/* tag the leaf */
		tagged++;
		tag_set(node, settag, offset);

		slot = node->parent;
		/* walk back up the path tagging interior nodes */
		upindex = index >> shift;
		while (slot) {
			upindex >>= RADIX_TREE_MAP_SHIFT;
			offset = upindex & RADIX_TREE_MAP_MASK;

			/* stop if we find a node with the tag already set */
			if (tag_get(slot, settag, offset))
				break;
			tag_set(slot, settag, offset);
			slot = slot->parent;
		}

 next:
		/* Go to next item at level determined by 'shift' */
		index = ((index >> shift) + 1) << shift;
		/* Overflow can happen when last_index is ~0UL... */
		if (index > last_index || !index)
			break;
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		while (offset == 0) {
			/*
			 * We've fully scanned this node. Go up. Because
			 * last_index is guaranteed to be in the tree, what
			 * we do below cannot wander astray.
			 */
			node = node->parent;
			shift += RADIX_TREE_MAP_SHIFT;
			offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		}
		if (is_sibling_entry(node, node->slots[offset]))
			goto next;
		if (tagged >= nr_to_tag)
			break;
	}
	/*
	 * We need not to tag the root tag if there is no tag which is set with
	 * settag within the range from *first_indexp to last_index.
	 */
	if (tagged > 0)
		root_tag_set(root, settag);
	*first_indexp = index;

	return tagged;
}
EXPORT_SYMBOL(radix_tree_range_tag_if_tagged);

/**
 *	radix_tree_gang_lookup - perform multiple lookup on a radix tree
 *	@root:		radix tree root
 *	@results:	where the results of the lookup are placed
 *	@first_index:	start the lookup from this key
 *	@max_items:	place up to this many items at *results
 *
 *	Performs an index-ascending scan of the tree for present items.  Places
 *	them at *@results and returns the number of items which were placed at
 *	*@results.
 *
 *	The implementation is naive.
 *
 *	Like radix_tree_lookup, radix_tree_gang_lookup may be called under
 *	rcu_read_lock. In this case, rather than the returned results being
 *	an atomic snapshot of the tree at a single point in time, the
 *	semantics of an RCU protected gang lookup are as though multiple
 *	radix_tree_lookups have been issued in individual locks, and results
 *	stored in 'results'.
 */
unsigned int
radix_tree_gang_lookup(struct radix_tree_root *root, void **results,
			unsigned long first_index, unsigned int max_items)
{
	struct radix_tree_iter iter;
	void **slot;
	unsigned int ret = 0;

	if (unlikely(!max_items))
		return 0;

	radix_tree_for_each_slot(slot, root, &iter, first_index) {
		results[ret] = rcu_dereference_raw(*slot);
		if (!results[ret])
			continue;
		if (radix_tree_is_indirect_ptr(results[ret])) {
			slot = radix_tree_iter_retry(&iter);
			continue;
		}
		if (++ret == max_items)
			break;
	}

	return ret;
}
EXPORT_SYMBOL(radix_tree_gang_lookup);

/**
 *	radix_tree_gang_lookup_slot - perform multiple slot lookup on radix tree
 *	@root:		radix tree root
 *	@results:	where the results of the lookup are placed
 *	@indices:	where their indices should be placed (but usually NULL)
 *	@first_index:	start the lookup from this key
 *	@max_items:	place up to this many items at *results
 *
 *	Performs an index-ascending scan of the tree for present items.  Places
 *	their slots at *@results and returns the number of items which were
 *	placed at *@results.
 *
 *	The implementation is naive.
 *
 *	Like radix_tree_gang_lookup as far as RCU and locking goes. Slots must
 *	be dereferenced with radix_tree_deref_slot, and if using only RCU
 *	protection, radix_tree_deref_slot may fail requiring a retry.
 */
unsigned int
radix_tree_gang_lookup_slot(struct radix_tree_root *root,
			void ***results, unsigned long *indices,
			unsigned long first_index, unsigned int max_items)
{
	struct radix_tree_iter iter;
	void **slot;
	unsigned int ret = 0;

	if (unlikely(!max_items))
		return 0;

	radix_tree_for_each_slot(slot, root, &iter, first_index) {
		results[ret] = slot;
		if (indices)
			indices[ret] = iter.index;
		if (++ret == max_items)
			break;
	}

	return ret;
}
EXPORT_SYMBOL(radix_tree_gang_lookup_slot);

/**
 *	radix_tree_gang_lookup_tag - perform multiple lookup on a radix tree
 *	                             based on a tag
 *	@root:		radix tree root
 *	@results:	where the results of the lookup are placed
 *	@first_index:	start the lookup from this key
 *	@max_items:	place up to this many items at *results
 *	@tag:		the tag index (< RADIX_TREE_MAX_TAGS)
 *
 *	Performs an index-ascending scan of the tree for present items which
 *	have the tag indexed by @tag set.  Places the items at *@results and
 *	returns the number of items which were placed at *@results.
 */
unsigned int
radix_tree_gang_lookup_tag(struct radix_tree_root *root, void **results,
		unsigned long first_index, unsigned int max_items,
		unsigned int tag)
{
	struct radix_tree_iter iter;
	void **slot;
	unsigned int ret = 0;

	if (unlikely(!max_items))
		return 0;

	radix_tree_for_each_tagged(slot, root, &iter, first_index, tag) {
		results[ret] = rcu_dereference_raw(*slot);
		if (!results[ret])
			continue;
		if (radix_tree_is_indirect_ptr(results[ret])) {
			slot = radix_tree_iter_retry(&iter);
			continue;
		}
		if (++ret == max_items)
			break;
	}

	return ret;
}
EXPORT_SYMBOL(radix_tree_gang_lookup_tag);

/**
 *	radix_tree_gang_lookup_tag_slot - perform multiple slot lookup on a
 *					  radix tree based on a tag
 *	@root:		radix tree root
 *	@results:	where the results of the lookup are placed
 *	@first_index:	start the lookup from this key
 *	@max_items:	place up to this many items at *results
 *	@tag:		the tag index (< RADIX_TREE_MAX_TAGS)
 *
 *	Performs an index-ascending scan of the tree for present items which
 *	have the tag indexed by @tag set.  Places the slots at *@results and
 *	returns the number of slots which were placed at *@results.
 */
unsigned int
radix_tree_gang_lookup_tag_slot(struct radix_tree_root *root, void ***results,
		unsigned long first_index, unsigned int max_items,
		unsigned int tag)
{
	struct radix_tree_iter iter;
	void **slot;
	unsigned int ret = 0;

	if (unlikely(!max_items))
		return 0;

	radix_tree_for_each_tagged(slot, root, &iter, first_index, tag) {
		results[ret] = slot;
		if (++ret == max_items)
			break;
	}

	return ret;
}
EXPORT_SYMBOL(radix_tree_gang_lookup_tag_slot);

#if defined(CONFIG_SHMEM) && defined(CONFIG_SWAP)
#include <linux/sched.h> /* for cond_resched() */

struct locate_info {
	unsigned long found_index;
	bool stop;
};

/*
 * This linear search is at present only useful to shmem_unuse_inode().
 */
static unsigned long __locate(struct radix_tree_node *slot, void *item,
			      unsigned long index, struct locate_info *info)
{
	unsigned int shift;
	unsigned long i;

	shift = slot->shift + RADIX_TREE_MAP_SHIFT;

	do {
		shift -= RADIX_TREE_MAP_SHIFT;

		for (i = (index >> shift) & RADIX_TREE_MAP_MASK;
		     i < RADIX_TREE_MAP_SIZE;
		     i++, index += (1UL << shift)) {
			struct radix_tree_node *node =
					rcu_dereference_raw(slot->slots[i]);
			if (node == RADIX_TREE_RETRY)
				goto out;
			if (!radix_tree_is_indirect_ptr(node)) {
				if (node == item) {
					info->found_index = index;
					info->stop = true;
					goto out;
				}
				continue;
			}
			node = indirect_to_ptr(node);
			if (is_sibling_entry(slot, node))
				continue;
			slot = node;
			break;
		}
		if (i == RADIX_TREE_MAP_SIZE)
			break;
	} while (shift);

out:
	if ((index == 0) && (i == RADIX_TREE_MAP_SIZE))
		info->stop = true;
	return index;
}

/**
 *	radix_tree_locate_item - search through radix tree for item
 *	@root:		radix tree root
 *	@item:		item to be found
 *
 *	Returns index where item was found, or -1 if not found.
 *	Caller must hold no lock (since this time-consuming function needs
 *	to be preemptible), and must check afterwards if item is still there.
 */
unsigned long radix_tree_locate_item(struct radix_tree_root *root, void *item)
{
	struct radix_tree_node *node;
	unsigned long max_index;
	unsigned long cur_index = 0;
	struct locate_info info = {
		.found_index = -1,
		.stop = false,
	};

	do {
		rcu_read_lock();
		node = rcu_dereference_raw(root->rnode);
		if (!radix_tree_is_indirect_ptr(node)) {
			rcu_read_unlock();
			if (node == item)
				info.found_index = 0;
			break;
		}

		node = indirect_to_ptr(node);

		max_index = node_maxindex(node);
		if (cur_index > max_index) {
			rcu_read_unlock();
			break;
		}

		cur_index = __locate(node, item, cur_index, &info);
		rcu_read_unlock();
		cond_resched();
	} while (!info.stop && cur_index <= max_index);

	return info.found_index;
}
#else
unsigned long radix_tree_locate_item(struct radix_tree_root *root, void *item)
{
	return -1;
}
#endif /* CONFIG_SHMEM && CONFIG_SWAP */

/**
 *	radix_tree_shrink    -    shrink radix tree to minimum height
 *	@root		radix tree root
 */
static inline bool radix_tree_shrink(struct radix_tree_root *root)
{
	bool shrunk = false;

	for (;;) {
		struct radix_tree_node *to_free = root->rnode;
		struct radix_tree_node *slot;

		if (!radix_tree_is_indirect_ptr(to_free))
			break;
		to_free = indirect_to_ptr(to_free);

		/*
		 * The candidate node has more than one child, or its child
		 * is not at the leftmost slot, or the child is a multiorder
		 * entry, we cannot shrink.
		 */
		if (to_free->count != 1)
			break;
		slot = to_free->slots[0];
		if (!slot)
			break;
		if (!radix_tree_is_indirect_ptr(slot) && to_free->shift)
			break;

		if (radix_tree_is_indirect_ptr(slot)) {
			slot = indirect_to_ptr(slot);
			slot->parent = NULL;
			slot = ptr_to_indirect(slot);
		}

		/*
		 * We don't need rcu_assign_pointer(), since we are simply
		 * moving the node from one part of the tree to another: if it
		 * was safe to dereference the old pointer to it
		 * (to_free->slots[0]), it will be safe to dereference the new
		 * one (root->rnode) as far as dependent read barriers go.
		 */
		root->rnode = slot;

		/*
		 * We have a dilemma here. The node's slot[0] must not be
		 * NULLed in case there are concurrent lookups expecting to
		 * find the item. However if this was a bottom-level node,
		 * then it may be subject to the slot pointer being visible
		 * to callers dereferencing it. If item corresponding to
		 * slot[0] is subsequently deleted, these callers would expect
		 * their slot to become empty sooner or later.
		 *
		 * For example, lockless pagecache will look up a slot, deref
		 * the page pointer, and if the page has 0 refcount it means it
		 * was concurrently deleted from pagecache so try the deref
		 * again. Fortunately there is already a requirement for logic
		 * to retry the entire slot lookup -- the indirect pointer
		 * problem (replacing direct root node with an indirect pointer
		 * also results in a stale slot). So tag the slot as indirect
		 * to force callers to retry.
		 */
		if (!radix_tree_is_indirect_ptr(slot))
			to_free->slots[0] = RADIX_TREE_RETRY;

		radix_tree_node_free(to_free);
		shrunk = true;
	}

	return shrunk;
}

/**
 *	__radix_tree_delete_node    -    try to free node after clearing a slot
 *	@root:		radix tree root
 *	@node:		node containing @index
 *
 *	After clearing the slot at @index in @node from radix tree
 *	rooted at @root, call this function to attempt freeing the
 *	node and shrinking the tree.
 *
 *	Returns %true if @node was freed, %false otherwise.
 */
bool __radix_tree_delete_node(struct radix_tree_root *root,
			      struct radix_tree_node *node)
{
	bool deleted = false;

	do {
		struct radix_tree_node *parent;

		if (node->count) {
			if (node == indirect_to_ptr(root->rnode))
				deleted |= radix_tree_shrink(root);
			return deleted;
		}

		parent = node->parent;
		if (parent) {
			parent->slots[node->offset] = NULL;
			parent->count--;
		} else {
			root_tag_clear_all(root);
			root->rnode = NULL;
		}

		radix_tree_node_free(node);
		deleted = true;

		node = parent;
	} while (node);

	return deleted;
}

static inline void delete_sibling_entries(struct radix_tree_node *node,
					void *ptr, unsigned offset)
{
#ifdef CONFIG_RADIX_TREE_MULTIORDER
	int i;
	for (i = 1; offset + i < RADIX_TREE_MAP_SIZE; i++) {
		if (node->slots[offset + i] != ptr)
			break;
		node->slots[offset + i] = NULL;
		node->count--;
	}
#endif
}

/**
 *	radix_tree_delete_item    -    delete an item from a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *	@item:		expected item
 *
 *	Remove @item at @index from the radix tree rooted at @root.
 *
 *	Returns the address of the deleted item, or NULL if it was not present
 *	or the entry at the given @index was not @item.
 */
void *radix_tree_delete_item(struct radix_tree_root *root,
			     unsigned long index, void *item)
{
	struct radix_tree_node *node;
	unsigned int offset;
	void **slot;
	void *entry;
	int tag;

	entry = __radix_tree_lookup(root, index, &node, &slot);
	if (!entry)
		return NULL;

	if (item && entry != item)
		return NULL;

	if (!node) {
		root_tag_clear_all(root);
		root->rnode = NULL;
		return entry;
	}

	offset = get_slot_offset(node, slot);

	/*
	 * Clear all tags associated with the item to be deleted.
	 * This way of doing it would be inefficient, but seldom is any set.
	 */
	for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
		if (tag_get(node, tag, offset))
			radix_tree_tag_clear(root, index, tag);
	}

	delete_sibling_entries(node, ptr_to_indirect(slot), offset);
	node->slots[offset] = NULL;
	node->count--;

	__radix_tree_delete_node(root, node);

	return entry;
}
EXPORT_SYMBOL(radix_tree_delete_item);

/**
 *	radix_tree_delete    -    delete an item from a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *
 *	Remove the item at @index from the radix tree rooted at @root.
 *
 *	Returns the address of the deleted item, or NULL if it was not present.
 */
void *radix_tree_delete(struct radix_tree_root *root, unsigned long index)
{
	return radix_tree_delete_item(root, index, NULL);
}
EXPORT_SYMBOL(radix_tree_delete);

/**
 *	radix_tree_tagged - test whether any items in the tree are tagged
 *	@root:		radix tree root
 *	@tag:		tag to test
 */
int radix_tree_tagged(struct radix_tree_root *root, unsigned int tag)
{
	return root_tag_get(root, tag);
}
EXPORT_SYMBOL(radix_tree_tagged);

static void
radix_tree_node_ctor(void *arg)
{
	struct radix_tree_node *node = arg;

	memset(node, 0, sizeof(*node));
	INIT_LIST_HEAD(&node->private_list);
}

static int radix_tree_callback(struct notifier_block *nfb,
				unsigned long action, void *hcpu)
{
	int cpu = (long)hcpu;
	struct radix_tree_preload *rtp;
	struct radix_tree_node *node;

	/* Free per-cpu pool of preloaded nodes */
	if (action == CPU_DEAD || action == CPU_DEAD_FROZEN) {
		rtp = &per_cpu(radix_tree_preloads, cpu);
		while (rtp->nr) {
			node = rtp->nodes;
			rtp->nodes = node->private_data;
			kmem_cache_free(radix_tree_node_cachep, node);
			rtp->nr--;
		}
	}
	return NOTIFY_OK;
}

void __init radix_tree_init(void)
{
	radix_tree_node_cachep = kmem_cache_create("radix_tree_node",
			sizeof(struct radix_tree_node), 0,
			SLAB_PANIC | SLAB_RECLAIM_ACCOUNT,
			radix_tree_node_ctor);
	hotcpu_notifier(radix_tree_callback, 0);
}
