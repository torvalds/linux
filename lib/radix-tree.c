/*
 * Copyright (C) 2001 Momchil Velikov
 * Portions Copyright (C) 2001 Christoph Hellwig
 * Copyright (C) 2005 SGI, Christoph Lameter
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
 * The height_to_maxindex array needs to be one deeper than the maximum
 * path as height 0 holds only 1 entry.
 */
static unsigned long height_to_maxindex[RADIX_TREE_MAX_PATH + 1] __read_mostly;

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
	int nr;
	/* nodes->private_data points to next preallocated node */
	struct radix_tree_node *nodes;
};
static DEFINE_PER_CPU(struct radix_tree_preload, radix_tree_preloads) = { 0, };

static inline void *ptr_to_indirect(void *ptr)
{
	return (void *)((unsigned long)ptr | RADIX_TREE_INDIRECT_PTR);
}

static inline void *indirect_to_ptr(void *ptr)
{
	return (void *)((unsigned long)ptr & ~RADIX_TREE_INDIRECT_PTR);
}

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

static inline void root_tag_clear(struct radix_tree_root *root, unsigned int tag)
{
	root->gfp_mask &= (__force gfp_t)~(1 << (tag + __GFP_BITS_SHIFT));
}

static inline void root_tag_clear_all(struct radix_tree_root *root)
{
	root->gfp_mask &= __GFP_BITS_MASK;
}

static inline int root_tag_get(struct radix_tree_root *root, unsigned int tag)
{
	return (__force unsigned)root->gfp_mask & (1 << (tag + __GFP_BITS_SHIFT));
}

/*
 * Returns 1 if any slot in the node has this tag set.
 * Otherwise returns 0.
 */
static inline int any_tag_set(struct radix_tree_node *node, unsigned int tag)
{
	int idx;
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

#if 0
static void dump_node(void *slot, int height, int offset)
{
	struct radix_tree_node *node;
	int i;

	if (!slot)
		return;

	if (height == 0) {
		pr_debug("radix entry %p offset %d\n", slot, offset);
		return;
	}

	node = indirect_to_ptr(slot);
	pr_debug("radix node: %p offset %d tags %lx %lx %lx path %x count %d parent %p\n",
		slot, offset, node->tags[0][0], node->tags[1][0],
		node->tags[2][0], node->path, node->count, node->parent);

	for (i = 0; i < RADIX_TREE_MAP_SIZE; i++)
		dump_node(node->slots[i], height - 1, i);
}

/* For debug */
static void radix_tree_dump(struct radix_tree_root *root)
{
	pr_debug("radix root: %p height %d rnode %p tags %x\n",
			root, root->height, root->rnode,
			root->gfp_mask >> __GFP_BITS_SHIFT);
	if (!radix_tree_is_indirect_ptr(root->rnode))
		return;
	dump_node(root->rnode, root->height, 0);
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
	 * Preload code isn't irq safe and it doesn't make sence to use
	 * preloading in the interrupt anyway as all the allocations have to
	 * be atomic. So just do normal allocation when in interrupt.
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
 *	Return the maximum key which can be store into a
 *	radix tree with height HEIGHT.
 */
static inline unsigned long radix_tree_maxindex(unsigned int height)
{
	return height_to_maxindex[height];
}

/*
 *	Extend a radix tree so it can store key @index.
 */
static int radix_tree_extend(struct radix_tree_root *root,
				unsigned long index, unsigned order)
{
	struct radix_tree_node *node;
	struct radix_tree_node *slot;
	unsigned int height;
	int tag;

	/* Figure out what the height should be.  */
	height = root->height + 1;
	while (index > radix_tree_maxindex(height))
		height++;

	if ((root->rnode == NULL) && (order == 0)) {
		root->height = height;
		goto out;
	}

	do {
		unsigned int newheight;
		if (!(node = radix_tree_node_alloc(root)))
			return -ENOMEM;

		/* Propagate the aggregated tag info into the new root */
		for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
			if (root_tag_get(root, tag))
				tag_set(node, tag, 0);
		}

		/* Increase the height.  */
		newheight = root->height+1;
		BUG_ON(newheight & ~RADIX_TREE_HEIGHT_MASK);
		node->path = newheight;
		node->count = 1;
		node->parent = NULL;
		slot = root->rnode;
		if (radix_tree_is_indirect_ptr(slot) && newheight > 1) {
			slot = indirect_to_ptr(slot);
			slot->parent = node;
			slot = ptr_to_indirect(slot);
		}
		node->slots[0] = slot;
		node = ptr_to_indirect(node);
		rcu_assign_pointer(root->rnode, node);
		root->height = newheight;
	} while (height > root->height);
out:
	return 0;
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
	unsigned int height, shift, offset;
	int error;

	BUG_ON((0 < order) && (order < RADIX_TREE_MAP_SHIFT));

	/* Make sure the tree is high enough.  */
	if (index > radix_tree_maxindex(root->height)) {
		error = radix_tree_extend(root, index, order);
		if (error)
			return error;
	}

	slot = root->rnode;

	height = root->height;
	shift = height * RADIX_TREE_MAP_SHIFT;

	offset = 0;			/* uninitialised var warning */
	while (shift > order) {
		if (slot == NULL) {
			/* Have to add a child node.  */
			if (!(slot = radix_tree_node_alloc(root)))
				return -ENOMEM;
			slot->path = height;
			slot->parent = node;
			if (node) {
				rcu_assign_pointer(node->slots[offset],
							ptr_to_indirect(slot));
				node->count++;
				slot->path |= offset << RADIX_TREE_HEIGHT_SHIFT;
			} else
				rcu_assign_pointer(root->rnode,
							ptr_to_indirect(slot));
		} else if (!radix_tree_is_indirect_ptr(slot))
			break;

		/* Go a level down */
		height--;
		shift -= RADIX_TREE_MAP_SHIFT;
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		node = indirect_to_ptr(slot);
		slot = node->slots[offset];
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
		node->count++;
		BUG_ON(tag_get(node, 0, index & RADIX_TREE_MAP_MASK));
		BUG_ON(tag_get(node, 1, index & RADIX_TREE_MAP_MASK));
	} else {
		BUG_ON(root_tag_get(root, 0));
		BUG_ON(root_tag_get(root, 1));
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
	unsigned int height, shift;
	void **slot;

	node = rcu_dereference_raw(root->rnode);
	if (node == NULL)
		return NULL;

	if (!radix_tree_is_indirect_ptr(node)) {
		if (index > 0)
			return NULL;

		if (nodep)
			*nodep = NULL;
		if (slotp)
			*slotp = (void **)&root->rnode;
		return node;
	}
	node = indirect_to_ptr(node);

	height = node->path & RADIX_TREE_HEIGHT_MASK;
	if (index > radix_tree_maxindex(height))
		return NULL;

	shift = (height-1) * RADIX_TREE_MAP_SHIFT;

	do {
		parent = node;
		slot = node->slots + ((index >> shift) & RADIX_TREE_MAP_MASK);
		node = rcu_dereference_raw(*slot);
		if (node == NULL)
			return NULL;
		if (!radix_tree_is_indirect_ptr(node))
			break;
		node = indirect_to_ptr(node);

		shift -= RADIX_TREE_MAP_SHIFT;
		height--;
	} while (height > 0);

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
 *	@tag: 		tag index
 *
 *	Set the search tag (which must be < RADIX_TREE_MAX_TAGS)
 *	corresponding to @index in the radix tree.  From
 *	the root all the way down to the leaf node.
 *
 *	Returns the address of the tagged item.   Setting a tag on a not-present
 *	item is a bug.
 */
void *radix_tree_tag_set(struct radix_tree_root *root,
			unsigned long index, unsigned int tag)
{
	unsigned int height, shift;
	struct radix_tree_node *slot;

	height = root->height;
	BUG_ON(index > radix_tree_maxindex(height));

	slot = indirect_to_ptr(root->rnode);
	shift = (height - 1) * RADIX_TREE_MAP_SHIFT;

	while (height > 0) {
		int offset;

		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		if (!tag_get(slot, tag, offset))
			tag_set(slot, tag, offset);
		slot = slot->slots[offset];
		BUG_ON(slot == NULL);
		if (!radix_tree_is_indirect_ptr(slot))
			break;
		slot = indirect_to_ptr(slot);
		shift -= RADIX_TREE_MAP_SHIFT;
		height--;
	}

	/* set the root's tag bit */
	if (slot && !root_tag_get(root, tag))
		root_tag_set(root, tag);

	return slot;
}
EXPORT_SYMBOL(radix_tree_tag_set);

/**
 *	radix_tree_tag_clear - clear a tag on a radix tree node
 *	@root:		radix tree root
 *	@index:		index key
 *	@tag: 		tag index
 *
 *	Clear the search tag (which must be < RADIX_TREE_MAX_TAGS)
 *	corresponding to @index in the radix tree.  If
 *	this causes the leaf node to have no tags set then clear the tag in the
 *	next-to-leaf node, etc.
 *
 *	Returns the address of the tagged item on success, else NULL.  ie:
 *	has the same return value and semantics as radix_tree_lookup().
 */
void *radix_tree_tag_clear(struct radix_tree_root *root,
			unsigned long index, unsigned int tag)
{
	struct radix_tree_node *node = NULL;
	struct radix_tree_node *slot = NULL;
	unsigned int height, shift;
	int uninitialized_var(offset);

	height = root->height;
	if (index > radix_tree_maxindex(height))
		goto out;

	shift = height * RADIX_TREE_MAP_SHIFT;
	slot = root->rnode;

	while (shift) {
		if (slot == NULL)
			goto out;
		if (!radix_tree_is_indirect_ptr(slot))
			break;
		slot = indirect_to_ptr(slot);

		shift -= RADIX_TREE_MAP_SHIFT;
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		node = slot;
		slot = slot->slots[offset];
	}

	if (slot == NULL)
		goto out;

	while (node) {
		if (!tag_get(node, tag, offset))
			goto out;
		tag_clear(node, tag, offset);
		if (any_tag_set(node, tag))
			goto out;

		index >>= RADIX_TREE_MAP_SHIFT;
		offset = index & RADIX_TREE_MAP_MASK;
		node = node->parent;
	}

	/* clear the root's tag bit */
	if (root_tag_get(root, tag))
		root_tag_clear(root, tag);

out:
	return slot;
}
EXPORT_SYMBOL(radix_tree_tag_clear);

/**
 * radix_tree_tag_get - get a tag on a radix tree node
 * @root:		radix tree root
 * @index:		index key
 * @tag: 		tag index (< RADIX_TREE_MAX_TAGS)
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
	unsigned int height, shift;
	struct radix_tree_node *node;

	/* check the root's tag bit */
	if (!root_tag_get(root, tag))
		return 0;

	node = rcu_dereference_raw(root->rnode);
	if (node == NULL)
		return 0;

	if (!radix_tree_is_indirect_ptr(node))
		return (index == 0);
	node = indirect_to_ptr(node);

	height = node->path & RADIX_TREE_HEIGHT_MASK;
	if (index > radix_tree_maxindex(height))
		return 0;

	shift = (height - 1) * RADIX_TREE_MAP_SHIFT;

	for ( ; ; ) {
		int offset;

		if (node == NULL)
			return 0;
		node = indirect_to_ptr(node);

		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		if (!tag_get(node, tag, offset))
			return 0;
		if (height == 1)
			return 1;
		node = rcu_dereference_raw(node->slots[offset]);
		if (!radix_tree_is_indirect_ptr(node))
			return 1;
		shift -= RADIX_TREE_MAP_SHIFT;
		height--;
	}
}
EXPORT_SYMBOL(radix_tree_tag_get);

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
	unsigned long index, offset, height;

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

	rnode = rcu_dereference_raw(root->rnode);
	if (radix_tree_is_indirect_ptr(rnode)) {
		rnode = indirect_to_ptr(rnode);
	} else if (rnode && !index) {
		/* Single-slot tree */
		iter->index = 0;
		iter->next_index = 1;
		iter->tags = 1;
		return (void **)&root->rnode;
	} else
		return NULL;

restart:
	height = rnode->path & RADIX_TREE_HEIGHT_MASK;
	shift = (height - 1) * RADIX_TREE_MAP_SHIFT;
	offset = index >> shift;

	/* Index outside of the tree */
	if (offset >= RADIX_TREE_MAP_SIZE)
		return NULL;

	node = rnode;
	while (1) {
		struct radix_tree_node *slot;
		if ((flags & RADIX_TREE_ITER_TAGGED) ?
				!test_bit(offset, node->tags[tag]) :
				!node->slots[offset]) {
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
					if (node->slots[offset])
						break;
				}
			index &= ~((RADIX_TREE_MAP_SIZE << shift) - 1);
			index += offset << shift;
			/* Overflow after ~0UL */
			if (!index)
				return NULL;
			if (offset == RADIX_TREE_MAP_SIZE)
				goto restart;
		}

		/* This is leaf-node */
		if (!shift)
			break;

		slot = rcu_dereference_raw(node->slots[offset]);
		if (slot == NULL)
			goto restart;
		if (!radix_tree_is_indirect_ptr(slot))
			break;
		node = indirect_to_ptr(slot);
		shift -= RADIX_TREE_MAP_SHIFT;
		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
	}

	/* Update the iterator state */
	iter->index = index;
	iter->next_index = (index | RADIX_TREE_MAP_MASK) + 1;

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
 * The function returns number of leaves where the tag was set and sets
 * *first_indexp to the first unscanned index.
 * WARNING! *first_indexp can wrap if last_index is ULONG_MAX. Caller must
 * be prepared to handle that.
 */
unsigned long radix_tree_range_tag_if_tagged(struct radix_tree_root *root,
		unsigned long *first_indexp, unsigned long last_index,
		unsigned long nr_to_tag,
		unsigned int iftag, unsigned int settag)
{
	unsigned int height = root->height;
	struct radix_tree_node *node = NULL;
	struct radix_tree_node *slot;
	unsigned int shift;
	unsigned long tagged = 0;
	unsigned long index = *first_indexp;

	last_index = min(last_index, radix_tree_maxindex(height));
	if (index > last_index)
		return 0;
	if (!nr_to_tag)
		return 0;
	if (!root_tag_get(root, iftag)) {
		*first_indexp = last_index + 1;
		return 0;
	}
	if (height == 0) {
		*first_indexp = last_index + 1;
		root_tag_set(root, settag);
		return 1;
	}

	shift = (height - 1) * RADIX_TREE_MAP_SHIFT;
	slot = indirect_to_ptr(root->rnode);

	for (;;) {
		unsigned long upindex;
		int offset;

		offset = (index >> shift) & RADIX_TREE_MAP_MASK;
		if (!slot->slots[offset])
			goto next;
		if (!tag_get(slot, iftag, offset))
			goto next;
		if (shift) {
			node = slot;
			slot = slot->slots[offset];
			if (radix_tree_is_indirect_ptr(slot)) {
				slot = indirect_to_ptr(slot);
				shift -= RADIX_TREE_MAP_SHIFT;
				continue;
			} else {
				slot = node;
				node = node->parent;
			}
		}

		/* tag the leaf */
		tagged += 1 << shift;
		tag_set(slot, settag, offset);

		/* walk back up the path tagging interior nodes */
		upindex = index;
		while (node) {
			upindex >>= RADIX_TREE_MAP_SHIFT;
			offset = upindex & RADIX_TREE_MAP_MASK;

			/* stop if we find a node with the tag already set */
			if (tag_get(node, settag, offset))
				break;
			tag_set(node, settag, offset);
			node = node->parent;
		}

		/*
		 * Small optimization: now clear that node pointer.
		 * Since all of this slot's ancestors now have the tag set
		 * from setting it above, we have no further need to walk
		 * back up the tree setting tags, until we update slot to
		 * point to another radix_tree_node.
		 */
		node = NULL;

next:
		/* Go to next item at level determined by 'shift' */
		index = ((index >> shift) + 1) << shift;
		/* Overflow can happen when last_index is ~0UL... */
		if (index > last_index || !index)
			break;
		if (tagged >= nr_to_tag)
			break;
		while (((index >> shift) & RADIX_TREE_MAP_MASK) == 0) {
			/*
			 * We've fully scanned this node. Go up. Because
			 * last_index is guaranteed to be in the tree, what
			 * we do below cannot wander astray.
			 */
			slot = slot->parent;
			shift += RADIX_TREE_MAP_SHIFT;
		}
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
 *	an atomic snapshot of the tree at a single point in time, the semantics
 *	of an RCU protected gang lookup are as though multiple radix_tree_lookups
 *	have been issued in individual locks, and results stored in 'results'.
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

/*
 * This linear search is at present only useful to shmem_unuse_inode().
 */
static unsigned long __locate(struct radix_tree_node *slot, void *item,
			      unsigned long index, unsigned long *found_index)
{
	unsigned int shift, height;
	unsigned long i;

	height = slot->path & RADIX_TREE_HEIGHT_MASK;
	shift = (height-1) * RADIX_TREE_MAP_SHIFT;

	for ( ; height > 1; height--) {
		i = (index >> shift) & RADIX_TREE_MAP_MASK;
		for (;;) {
			if (slot->slots[i] != NULL)
				break;
			index &= ~((1UL << shift) - 1);
			index += 1UL << shift;
			if (index == 0)
				goto out;	/* 32-bit wraparound */
			i++;
			if (i == RADIX_TREE_MAP_SIZE)
				goto out;
		}

		slot = rcu_dereference_raw(slot->slots[i]);
		if (slot == NULL)
			goto out;
		if (!radix_tree_is_indirect_ptr(slot)) {
			if (slot == item) {
				*found_index = index + i;
				index = 0;
			} else {
				index += shift;
			}
			goto out;
		}
		slot = indirect_to_ptr(slot);
		shift -= RADIX_TREE_MAP_SHIFT;
	}

	/* Bottom level: check items */
	for (i = 0; i < RADIX_TREE_MAP_SIZE; i++) {
		if (slot->slots[i] == item) {
			*found_index = index + i;
			index = 0;
			goto out;
		}
	}
	index += RADIX_TREE_MAP_SIZE;
out:
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
	unsigned long found_index = -1;

	do {
		rcu_read_lock();
		node = rcu_dereference_raw(root->rnode);
		if (!radix_tree_is_indirect_ptr(node)) {
			rcu_read_unlock();
			if (node == item)
				found_index = 0;
			break;
		}

		node = indirect_to_ptr(node);
		max_index = radix_tree_maxindex(node->path &
						RADIX_TREE_HEIGHT_MASK);
		if (cur_index > max_index) {
			rcu_read_unlock();
			break;
		}

		cur_index = __locate(node, item, cur_index, &found_index);
		rcu_read_unlock();
		cond_resched();
	} while (cur_index != 0 && cur_index <= max_index);

	return found_index;
}
#else
unsigned long radix_tree_locate_item(struct radix_tree_root *root, void *item)
{
	return -1;
}
#endif /* CONFIG_SHMEM && CONFIG_SWAP */

/**
 *	radix_tree_shrink    -    shrink height of a radix tree to minimal
 *	@root		radix tree root
 */
static inline void radix_tree_shrink(struct radix_tree_root *root)
{
	/* try to shrink tree height */
	while (root->height > 0) {
		struct radix_tree_node *to_free = root->rnode;
		struct radix_tree_node *slot;

		BUG_ON(!radix_tree_is_indirect_ptr(to_free));
		to_free = indirect_to_ptr(to_free);

		/*
		 * The candidate node has more than one child, or its child
		 * is not at the leftmost slot, or it is a multiorder entry,
		 * we cannot shrink.
		 */
		if (to_free->count != 1)
			break;
		slot = to_free->slots[0];
		if (!slot)
			break;

		/*
		 * We don't need rcu_assign_pointer(), since we are simply
		 * moving the node from one part of the tree to another: if it
		 * was safe to dereference the old pointer to it
		 * (to_free->slots[0]), it will be safe to dereference the new
		 * one (root->rnode) as far as dependent read barriers go.
		 */
		if (root->height > 1) {
			if (!radix_tree_is_indirect_ptr(slot))
				break;

			slot = indirect_to_ptr(slot);
			slot->parent = NULL;
			slot = ptr_to_indirect(slot);
		}
		root->rnode = slot;
		root->height--;

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
		 * the page pointer, and if the page is 0 refcount it means it
		 * was concurrently deleted from pagecache so try the deref
		 * again. Fortunately there is already a requirement for logic
		 * to retry the entire slot lookup -- the indirect pointer
		 * problem (replacing direct root node with an indirect pointer
		 * also results in a stale slot). So tag the slot as indirect
		 * to force callers to retry.
		 */
		if (root->height == 0)
			*((unsigned long *)&to_free->slots[0]) |=
						RADIX_TREE_INDIRECT_PTR;

		radix_tree_node_free(to_free);
	}
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
			if (node == indirect_to_ptr(root->rnode)) {
				radix_tree_shrink(root);
				if (root->height == 0)
					deleted = true;
			}
			return deleted;
		}

		parent = node->parent;
		if (parent) {
			unsigned int offset;

			offset = node->path >> RADIX_TREE_HEIGHT_SHIFT;
			parent->slots[offset] = NULL;
			parent->count--;
		} else {
			root_tag_clear_all(root);
			root->height = 0;
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

static __init unsigned long __maxindex(unsigned int height)
{
	unsigned int width = height * RADIX_TREE_MAP_SHIFT;
	int shift = RADIX_TREE_INDEX_BITS - width;

	if (shift < 0)
		return ~0UL;
	if (shift >= BITS_PER_LONG)
		return 0UL;
	return ~0UL >> shift;
}

static __init void radix_tree_init_maxindex(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(height_to_maxindex); i++)
		height_to_maxindex[i] = __maxindex(i);
}

static int radix_tree_callback(struct notifier_block *nfb,
                            unsigned long action,
                            void *hcpu)
{
       int cpu = (long)hcpu;
       struct radix_tree_preload *rtp;
       struct radix_tree_node *node;

       /* Free per-cpu pool of perloaded nodes */
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
	radix_tree_init_maxindex();
	hotcpu_notifier(radix_tree_callback, 0);
}
