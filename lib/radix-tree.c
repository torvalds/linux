// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2001 Momchil Velikov
 * Portions Copyright (C) 2001 Christoph Hellwig
 * Copyright (C) 2005 SGI, Christoph Lameter
 * Copyright (C) 2006 Nick Piggin
 * Copyright (C) 2012 Konstantin Khlebnikov
 * Copyright (C) 2016 Intel, Matthew Wilcox
 * Copyright (C) 2016 Intel, Ross Zwisler
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kmemleak.h>
#include <linux/percpu.h>
#include <linux/preempt.h>		/* in_interrupt() */
#include <linux/radix-tree.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/xarray.h>


/*
 * Radix tree node cache.
 */
struct kmem_cache *radix_tree_node_cachep;

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
 * The IDR does not have to be as high as the radix tree since it uses
 * signed integers, not unsigned longs.
 */
#define IDR_INDEX_BITS		(8 /* CHAR_BIT */ * sizeof(int) - 1)
#define IDR_MAX_PATH		(DIV_ROUND_UP(IDR_INDEX_BITS, \
						RADIX_TREE_MAP_SHIFT))
#define IDR_PRELOAD_SIZE	(IDR_MAX_PATH * 2 - 1)

/*
 * The IDA is even shorter since it uses a bitmap at the last level.
 */
#define IDA_INDEX_BITS		(8 * sizeof(int) - 1 - ilog2(IDA_BITMAP_BITS))
#define IDA_MAX_PATH		(DIV_ROUND_UP(IDA_INDEX_BITS, \
						RADIX_TREE_MAP_SHIFT))
#define IDA_PRELOAD_SIZE	(IDA_MAX_PATH * 2 - 1)

/*
 * Per-cpu pool of preloaded nodes
 */
struct radix_tree_preload {
	unsigned nr;
	/* nodes->parent points to next preallocated node */
	struct radix_tree_node *nodes;
};
static DEFINE_PER_CPU(struct radix_tree_preload, radix_tree_preloads) = { 0, };

static inline struct radix_tree_node *entry_to_node(void *ptr)
{
	return (void *)((unsigned long)ptr & ~RADIX_TREE_INTERNAL_NODE);
}

static inline void *node_to_entry(void *ptr)
{
	return (void *)((unsigned long)ptr | RADIX_TREE_INTERNAL_NODE);
}

#define RADIX_TREE_RETRY	XA_RETRY_ENTRY

static inline unsigned long
get_slot_offset(const struct radix_tree_node *parent, void __rcu **slot)
{
	return parent ? slot - parent->slots : 0;
}

static unsigned int radix_tree_descend(const struct radix_tree_node *parent,
			struct radix_tree_node **nodep, unsigned long index)
{
	unsigned int offset = (index >> parent->shift) & RADIX_TREE_MAP_MASK;
	void __rcu **entry = rcu_dereference_raw(parent->slots[offset]);

	*nodep = (void *)entry;
	return offset;
}

static inline gfp_t root_gfp_mask(const struct radix_tree_root *root)
{
	return root->xa_flags & (__GFP_BITS_MASK & ~GFP_ZONEMASK);
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

static inline int tag_get(const struct radix_tree_node *node, unsigned int tag,
		int offset)
{
	return test_bit(offset, node->tags[tag]);
}

static inline void root_tag_set(struct radix_tree_root *root, unsigned tag)
{
	root->xa_flags |= (__force gfp_t)(1 << (tag + ROOT_TAG_SHIFT));
}

static inline void root_tag_clear(struct radix_tree_root *root, unsigned tag)
{
	root->xa_flags &= (__force gfp_t)~(1 << (tag + ROOT_TAG_SHIFT));
}

static inline void root_tag_clear_all(struct radix_tree_root *root)
{
	root->xa_flags &= (__force gfp_t)((1 << ROOT_TAG_SHIFT) - 1);
}

static inline int root_tag_get(const struct radix_tree_root *root, unsigned tag)
{
	return (__force int)root->xa_flags & (1 << (tag + ROOT_TAG_SHIFT));
}

static inline unsigned root_tags_get(const struct radix_tree_root *root)
{
	return (__force unsigned)root->xa_flags >> ROOT_TAG_SHIFT;
}

static inline bool is_idr(const struct radix_tree_root *root)
{
	return !!(root->xa_flags & ROOT_IS_IDR);
}

/*
 * Returns 1 if any slot in the node has this tag set.
 * Otherwise returns 0.
 */
static inline int any_tag_set(const struct radix_tree_node *node,
							unsigned int tag)
{
	unsigned idx;
	for (idx = 0; idx < RADIX_TREE_TAG_LONGS; idx++) {
		if (node->tags[tag][idx])
			return 1;
	}
	return 0;
}

static inline void all_tag_set(struct radix_tree_node *node, unsigned int tag)
{
	bitmap_fill(node->tags[tag], RADIX_TREE_MAP_SIZE);
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
radix_tree_find_next_bit(struct radix_tree_node *node, unsigned int tag,
			 unsigned long offset)
{
	const unsigned long *addr = node->tags[tag];

	if (offset < RADIX_TREE_MAP_SIZE) {
		unsigned long tmp;

		addr += offset / BITS_PER_LONG;
		tmp = *addr >> (offset % BITS_PER_LONG);
		if (tmp)
			return __ffs(tmp) + offset;
		offset = (offset + BITS_PER_LONG) & ~(BITS_PER_LONG - 1);
		while (offset < RADIX_TREE_MAP_SIZE) {
			tmp = *++addr;
			if (tmp)
				return __ffs(tmp) + offset;
			offset += BITS_PER_LONG;
		}
	}
	return RADIX_TREE_MAP_SIZE;
}

static unsigned int iter_offset(const struct radix_tree_iter *iter)
{
	return iter->index & RADIX_TREE_MAP_MASK;
}

/*
 * The maximum index which can be stored in a radix tree
 */
static inline unsigned long shift_maxindex(unsigned int shift)
{
	return (RADIX_TREE_MAP_SIZE << shift) - 1;
}

static inline unsigned long node_maxindex(const struct radix_tree_node *node)
{
	return shift_maxindex(node->shift);
}

static unsigned long next_index(unsigned long index,
				const struct radix_tree_node *node,
				unsigned long offset)
{
	return (index & ~node_maxindex(node)) + (offset << node->shift);
}

/*
 * This assumes that the caller has performed appropriate preallocation, and
 * that the caller has pinned this thread of control to the current CPU.
 */
static struct radix_tree_node *
radix_tree_node_alloc(gfp_t gfp_mask, struct radix_tree_node *parent,
			struct radix_tree_root *root,
			unsigned int shift, unsigned int offset,
			unsigned int count, unsigned int nr_values)
{
	struct radix_tree_node *ret = NULL;

	/*
	 * Preload code isn't irq safe and it doesn't make sense to use
	 * preloading during an interrupt anyway as all the allocations have
	 * to be atomic. So just do normal allocation when in interrupt.
	 */
	if (!gfpflags_allow_blocking(gfp_mask) && !in_interrupt()) {
		struct radix_tree_preload *rtp;

		/*
		 * Even if the caller has preloaded, try to allocate from the
		 * cache first for the new node to get accounted to the memory
		 * cgroup.
		 */
		ret = kmem_cache_alloc(radix_tree_node_cachep,
				       gfp_mask | __GFP_NOWARN);
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
			rtp->nodes = ret->parent;
			rtp->nr--;
		}
		/*
		 * Update the allocation stack trace as this is more useful
		 * for debugging.
		 */
		kmemleak_update_trace(ret);
		goto out;
	}
	ret = kmem_cache_alloc(radix_tree_node_cachep, gfp_mask);
out:
	BUG_ON(radix_tree_is_internal_node(ret));
	if (ret) {
		ret->shift = shift;
		ret->offset = offset;
		ret->count = count;
		ret->nr_values = nr_values;
		ret->parent = parent;
		ret->array = root;
	}
	return ret;
}

void radix_tree_node_rcu_free(struct rcu_head *head)
{
	struct radix_tree_node *node =
			container_of(head, struct radix_tree_node, rcu_head);

	/*
	 * Must only free zeroed nodes into the slab.  We can be left with
	 * non-NULL entries by radix_tree_free_nodes, so clear the entries
	 * and tags here.
	 */
	memset(node->slots, 0, sizeof(node->slots));
	memset(node->tags, 0, sizeof(node->tags));
	INIT_LIST_HEAD(&node->private_list);

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
static __must_check int __radix_tree_preload(gfp_t gfp_mask, unsigned nr)
{
	struct radix_tree_preload *rtp;
	struct radix_tree_node *node;
	int ret = -ENOMEM;

	/*
	 * Nodes preloaded by one cgroup can be be used by another cgroup, so
	 * they should never be accounted to any particular memory cgroup.
	 */
	gfp_mask &= ~__GFP_ACCOUNT;

	preempt_disable();
	rtp = this_cpu_ptr(&radix_tree_preloads);
	while (rtp->nr < nr) {
		preempt_enable();
		node = kmem_cache_alloc(radix_tree_node_cachep, gfp_mask);
		if (node == NULL)
			goto out;
		preempt_disable();
		rtp = this_cpu_ptr(&radix_tree_preloads);
		if (rtp->nr < nr) {
			node->parent = rtp->nodes;
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
	return __radix_tree_preload(gfp_mask, RADIX_TREE_PRELOAD_SIZE);
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
		return __radix_tree_preload(gfp_mask, RADIX_TREE_PRELOAD_SIZE);
	/* Preloading doesn't help anything with this gfp mask, skip it */
	preempt_disable();
	return 0;
}
EXPORT_SYMBOL(radix_tree_maybe_preload);

static unsigned radix_tree_load_root(const struct radix_tree_root *root,
		struct radix_tree_node **nodep, unsigned long *maxindex)
{
	struct radix_tree_node *node = rcu_dereference_raw(root->xa_head);

	*nodep = node;

	if (likely(radix_tree_is_internal_node(node))) {
		node = entry_to_node(node);
		*maxindex = node_maxindex(node);
		return node->shift + RADIX_TREE_MAP_SHIFT;
	}

	*maxindex = 0;
	return 0;
}

/*
 *	Extend a radix tree so it can store key @index.
 */
static int radix_tree_extend(struct radix_tree_root *root, gfp_t gfp,
				unsigned long index, unsigned int shift)
{
	void *entry;
	unsigned int maxshift;
	int tag;

	/* Figure out what the shift should be.  */
	maxshift = shift;
	while (index > shift_maxindex(maxshift))
		maxshift += RADIX_TREE_MAP_SHIFT;

	entry = rcu_dereference_raw(root->xa_head);
	if (!entry && (!is_idr(root) || root_tag_get(root, IDR_FREE)))
		goto out;

	do {
		struct radix_tree_node *node = radix_tree_node_alloc(gfp, NULL,
							root, shift, 0, 1, 0);
		if (!node)
			return -ENOMEM;

		if (is_idr(root)) {
			all_tag_set(node, IDR_FREE);
			if (!root_tag_get(root, IDR_FREE)) {
				tag_clear(node, IDR_FREE, 0);
				root_tag_set(root, IDR_FREE);
			}
		} else {
			/* Propagate the aggregated tag info to the new child */
			for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
				if (root_tag_get(root, tag))
					tag_set(node, tag, 0);
			}
		}

		BUG_ON(shift > BITS_PER_LONG);
		if (radix_tree_is_internal_node(entry)) {
			entry_to_node(entry)->parent = node;
		} else if (xa_is_value(entry)) {
			/* Moving a value entry root->xa_head to a node */
			node->nr_values = 1;
		}
		/*
		 * entry was already in the radix tree, so we do not need
		 * rcu_assign_pointer here
		 */
		node->slots[0] = (void __rcu *)entry;
		entry = node_to_entry(node);
		rcu_assign_pointer(root->xa_head, entry);
		shift += RADIX_TREE_MAP_SHIFT;
	} while (shift <= maxshift);
out:
	return maxshift + RADIX_TREE_MAP_SHIFT;
}

/**
 *	radix_tree_shrink    -    shrink radix tree to minimum height
 *	@root		radix tree root
 */
static inline bool radix_tree_shrink(struct radix_tree_root *root)
{
	bool shrunk = false;

	for (;;) {
		struct radix_tree_node *node = rcu_dereference_raw(root->xa_head);
		struct radix_tree_node *child;

		if (!radix_tree_is_internal_node(node))
			break;
		node = entry_to_node(node);

		/*
		 * The candidate node has more than one child, or its child
		 * is not at the leftmost slot, we cannot shrink.
		 */
		if (node->count != 1)
			break;
		child = rcu_dereference_raw(node->slots[0]);
		if (!child)
			break;

		/*
		 * For an IDR, we must not shrink entry 0 into the root in
		 * case somebody calls idr_replace() with a pointer that
		 * appears to be an internal entry
		 */
		if (!node->shift && is_idr(root))
			break;

		if (radix_tree_is_internal_node(child))
			entry_to_node(child)->parent = NULL;

		/*
		 * We don't need rcu_assign_pointer(), since we are simply
		 * moving the node from one part of the tree to another: if it
		 * was safe to dereference the old pointer to it
		 * (node->slots[0]), it will be safe to dereference the new
		 * one (root->xa_head) as far as dependent read barriers go.
		 */
		root->xa_head = (void __rcu *)child;
		if (is_idr(root) && !tag_get(node, IDR_FREE, 0))
			root_tag_clear(root, IDR_FREE);

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
		node->count = 0;
		if (!radix_tree_is_internal_node(child)) {
			node->slots[0] = (void __rcu *)RADIX_TREE_RETRY;
		}

		WARN_ON_ONCE(!list_empty(&node->private_list));
		radix_tree_node_free(node);
		shrunk = true;
	}

	return shrunk;
}

static bool delete_node(struct radix_tree_root *root,
			struct radix_tree_node *node)
{
	bool deleted = false;

	do {
		struct radix_tree_node *parent;

		if (node->count) {
			if (node_to_entry(node) ==
					rcu_dereference_raw(root->xa_head))
				deleted |= radix_tree_shrink(root);
			return deleted;
		}

		parent = node->parent;
		if (parent) {
			parent->slots[node->offset] = NULL;
			parent->count--;
		} else {
			/*
			 * Shouldn't the tags already have all been cleared
			 * by the caller?
			 */
			if (!is_idr(root))
				root_tag_clear_all(root);
			root->xa_head = NULL;
		}

		WARN_ON_ONCE(!list_empty(&node->private_list));
		radix_tree_node_free(node);
		deleted = true;

		node = parent;
	} while (node);

	return deleted;
}

/**
 *	__radix_tree_create	-	create a slot in a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *	@nodep:		returns node
 *	@slotp:		returns slot
 *
 *	Create, if necessary, and return the node and slot for an item
 *	at position @index in the radix tree @root.
 *
 *	Until there is more than one item in the tree, no nodes are
 *	allocated and @root->xa_head is used as a direct slot instead of
 *	pointing to a node, in which case *@nodep will be NULL.
 *
 *	Returns -ENOMEM, or 0 for success.
 */
static int __radix_tree_create(struct radix_tree_root *root,
		unsigned long index, struct radix_tree_node **nodep,
		void __rcu ***slotp)
{
	struct radix_tree_node *node = NULL, *child;
	void __rcu **slot = (void __rcu **)&root->xa_head;
	unsigned long maxindex;
	unsigned int shift, offset = 0;
	unsigned long max = index;
	gfp_t gfp = root_gfp_mask(root);

	shift = radix_tree_load_root(root, &child, &maxindex);

	/* Make sure the tree is high enough.  */
	if (max > maxindex) {
		int error = radix_tree_extend(root, gfp, max, shift);
		if (error < 0)
			return error;
		shift = error;
		child = rcu_dereference_raw(root->xa_head);
	}

	while (shift > 0) {
		shift -= RADIX_TREE_MAP_SHIFT;
		if (child == NULL) {
			/* Have to add a child node.  */
			child = radix_tree_node_alloc(gfp, node, root, shift,
							offset, 0, 0);
			if (!child)
				return -ENOMEM;
			rcu_assign_pointer(*slot, node_to_entry(child));
			if (node)
				node->count++;
		} else if (!radix_tree_is_internal_node(child))
			break;

		/* Go a level down */
		node = entry_to_node(child);
		offset = radix_tree_descend(node, &child, index);
		slot = &node->slots[offset];
	}

	if (nodep)
		*nodep = node;
	if (slotp)
		*slotp = slot;
	return 0;
}

/*
 * Free any nodes below this node.  The tree is presumed to not need
 * shrinking, and any user data in the tree is presumed to not need a
 * destructor called on it.  If we need to add a destructor, we can
 * add that functionality later.  Note that we may not clear tags or
 * slots from the tree as an RCU walker may still have a pointer into
 * this subtree.  We could replace the entries with RADIX_TREE_RETRY,
 * but we'll still have to clear those in rcu_free.
 */
static void radix_tree_free_nodes(struct radix_tree_node *node)
{
	unsigned offset = 0;
	struct radix_tree_node *child = entry_to_node(node);

	for (;;) {
		void *entry = rcu_dereference_raw(child->slots[offset]);
		if (xa_is_node(entry) && child->shift) {
			child = entry_to_node(entry);
			offset = 0;
			continue;
		}
		offset++;
		while (offset == RADIX_TREE_MAP_SIZE) {
			struct radix_tree_node *old = child;
			offset = child->offset + 1;
			child = child->parent;
			WARN_ON_ONCE(!list_empty(&old->private_list));
			radix_tree_node_free(old);
			if (old == entry_to_node(node))
				return;
		}
	}
}

static inline int insert_entries(struct radix_tree_node *node,
		void __rcu **slot, void *item, bool replace)
{
	if (*slot)
		return -EEXIST;
	rcu_assign_pointer(*slot, item);
	if (node) {
		node->count++;
		if (xa_is_value(item))
			node->nr_values++;
	}
	return 1;
}

/**
 *	__radix_tree_insert    -    insert into a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *	@item:		item to insert
 *
 *	Insert an item into the radix tree at position @index.
 */
int radix_tree_insert(struct radix_tree_root *root, unsigned long index,
			void *item)
{
	struct radix_tree_node *node;
	void __rcu **slot;
	int error;

	BUG_ON(radix_tree_is_internal_node(item));

	error = __radix_tree_create(root, index, &node, &slot);
	if (error)
		return error;

	error = insert_entries(node, slot, item, false);
	if (error < 0)
		return error;

	if (node) {
		unsigned offset = get_slot_offset(node, slot);
		BUG_ON(tag_get(node, 0, offset));
		BUG_ON(tag_get(node, 1, offset));
		BUG_ON(tag_get(node, 2, offset));
	} else {
		BUG_ON(root_tags_get(root));
	}

	return 0;
}
EXPORT_SYMBOL(radix_tree_insert);

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
 *	allocated and @root->xa_head is used as a direct slot instead of
 *	pointing to a node, in which case *@nodep will be NULL.
 */
void *__radix_tree_lookup(const struct radix_tree_root *root,
			  unsigned long index, struct radix_tree_node **nodep,
			  void __rcu ***slotp)
{
	struct radix_tree_node *node, *parent;
	unsigned long maxindex;
	void __rcu **slot;

 restart:
	parent = NULL;
	slot = (void __rcu **)&root->xa_head;
	radix_tree_load_root(root, &node, &maxindex);
	if (index > maxindex)
		return NULL;

	while (radix_tree_is_internal_node(node)) {
		unsigned offset;

		parent = entry_to_node(node);
		offset = radix_tree_descend(parent, &node, index);
		slot = parent->slots + offset;
		if (node == RADIX_TREE_RETRY)
			goto restart;
		if (parent->shift == 0)
			break;
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
void __rcu **radix_tree_lookup_slot(const struct radix_tree_root *root,
				unsigned long index)
{
	void __rcu **slot;

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
void *radix_tree_lookup(const struct radix_tree_root *root, unsigned long index)
{
	return __radix_tree_lookup(root, index, NULL, NULL);
}
EXPORT_SYMBOL(radix_tree_lookup);

static void replace_slot(void __rcu **slot, void *item,
		struct radix_tree_node *node, int count, int values)
{
	if (node && (count || values)) {
		node->count += count;
		node->nr_values += values;
	}

	rcu_assign_pointer(*slot, item);
}

static bool node_tag_get(const struct radix_tree_root *root,
				const struct radix_tree_node *node,
				unsigned int tag, unsigned int offset)
{
	if (node)
		return tag_get(node, tag, offset);
	return root_tag_get(root, tag);
}

/*
 * IDR users want to be able to store NULL in the tree, so if the slot isn't
 * free, don't adjust the count, even if it's transitioning between NULL and
 * non-NULL.  For the IDA, we mark slots as being IDR_FREE while they still
 * have empty bits, but it only stores NULL in slots when they're being
 * deleted.
 */
static int calculate_count(struct radix_tree_root *root,
				struct radix_tree_node *node, void __rcu **slot,
				void *item, void *old)
{
	if (is_idr(root)) {
		unsigned offset = get_slot_offset(node, slot);
		bool free = node_tag_get(root, node, IDR_FREE, offset);
		if (!free)
			return 0;
		if (!old)
			return 1;
	}
	return !!item - !!old;
}

/**
 * __radix_tree_replace		- replace item in a slot
 * @root:		radix tree root
 * @node:		pointer to tree node
 * @slot:		pointer to slot in @node
 * @item:		new item to store in the slot.
 *
 * For use with __radix_tree_lookup().  Caller must hold tree write locked
 * across slot lookup and replacement.
 */
void __radix_tree_replace(struct radix_tree_root *root,
			  struct radix_tree_node *node,
			  void __rcu **slot, void *item)
{
	void *old = rcu_dereference_raw(*slot);
	int values = !!xa_is_value(item) - !!xa_is_value(old);
	int count = calculate_count(root, node, slot, item, old);

	/*
	 * This function supports replacing value entries and
	 * deleting entries, but that needs accounting against the
	 * node unless the slot is root->xa_head.
	 */
	WARN_ON_ONCE(!node && (slot != (void __rcu **)&root->xa_head) &&
			(count || values));
	replace_slot(slot, item, node, count, values);

	if (!node)
		return;

	delete_node(root, node);
}

/**
 * radix_tree_replace_slot	- replace item in a slot
 * @root:	radix tree root
 * @slot:	pointer to slot
 * @item:	new item to store in the slot.
 *
 * For use with radix_tree_lookup_slot() and
 * radix_tree_gang_lookup_tag_slot().  Caller must hold tree write locked
 * across slot lookup and replacement.
 *
 * NOTE: This cannot be used to switch between non-entries (empty slots),
 * regular entries, and value entries, as that requires accounting
 * inside the radix tree node. When switching from one type of entry or
 * deleting, use __radix_tree_lookup() and __radix_tree_replace() or
 * radix_tree_iter_replace().
 */
void radix_tree_replace_slot(struct radix_tree_root *root,
			     void __rcu **slot, void *item)
{
	__radix_tree_replace(root, NULL, slot, item);
}
EXPORT_SYMBOL(radix_tree_replace_slot);

/**
 * radix_tree_iter_replace - replace item in a slot
 * @root:	radix tree root
 * @slot:	pointer to slot
 * @item:	new item to store in the slot.
 *
 * For use with radix_tree_for_each_slot().
 * Caller must hold tree write locked.
 */
void radix_tree_iter_replace(struct radix_tree_root *root,
				const struct radix_tree_iter *iter,
				void __rcu **slot, void *item)
{
	__radix_tree_replace(root, iter->node, slot, item);
}

static void node_tag_set(struct radix_tree_root *root,
				struct radix_tree_node *node,
				unsigned int tag, unsigned int offset)
{
	while (node) {
		if (tag_get(node, tag, offset))
			return;
		tag_set(node, tag, offset);
		offset = node->offset;
		node = node->parent;
	}

	if (!root_tag_get(root, tag))
		root_tag_set(root, tag);
}

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

	radix_tree_load_root(root, &node, &maxindex);
	BUG_ON(index > maxindex);

	while (radix_tree_is_internal_node(node)) {
		unsigned offset;

		parent = entry_to_node(node);
		offset = radix_tree_descend(parent, &node, index);
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

static void node_tag_clear(struct radix_tree_root *root,
				struct radix_tree_node *node,
				unsigned int tag, unsigned int offset)
{
	while (node) {
		if (!tag_get(node, tag, offset))
			return;
		tag_clear(node, tag, offset);
		if (any_tag_set(node, tag))
			return;

		offset = node->offset;
		node = node->parent;
	}

	/* clear the root's tag bit */
	if (root_tag_get(root, tag))
		root_tag_clear(root, tag);
}

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
	int uninitialized_var(offset);

	radix_tree_load_root(root, &node, &maxindex);
	if (index > maxindex)
		return NULL;

	parent = NULL;

	while (radix_tree_is_internal_node(node)) {
		parent = entry_to_node(node);
		offset = radix_tree_descend(parent, &node, index);
	}

	if (node)
		node_tag_clear(root, parent, tag, offset);

	return node;
}
EXPORT_SYMBOL(radix_tree_tag_clear);

/**
  * radix_tree_iter_tag_clear - clear a tag on the current iterator entry
  * @root: radix tree root
  * @iter: iterator state
  * @tag: tag to clear
  */
void radix_tree_iter_tag_clear(struct radix_tree_root *root,
			const struct radix_tree_iter *iter, unsigned int tag)
{
	node_tag_clear(root, iter->node, tag, iter_offset(iter));
}

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
int radix_tree_tag_get(const struct radix_tree_root *root,
			unsigned long index, unsigned int tag)
{
	struct radix_tree_node *node, *parent;
	unsigned long maxindex;

	if (!root_tag_get(root, tag))
		return 0;

	radix_tree_load_root(root, &node, &maxindex);
	if (index > maxindex)
		return 0;

	while (radix_tree_is_internal_node(node)) {
		unsigned offset;

		parent = entry_to_node(node);
		offset = radix_tree_descend(parent, &node, index);

		if (!tag_get(parent, tag, offset))
			return 0;
		if (node == RADIX_TREE_RETRY)
			break;
	}

	return 1;
}
EXPORT_SYMBOL(radix_tree_tag_get);

/* Construct iter->tags bit-mask from node->tags[tag] array */
static void set_iter_tags(struct radix_tree_iter *iter,
				struct radix_tree_node *node, unsigned offset,
				unsigned tag)
{
	unsigned tag_long = offset / BITS_PER_LONG;
	unsigned tag_bit  = offset % BITS_PER_LONG;

	if (!node) {
		iter->tags = 1;
		return;
	}

	iter->tags = node->tags[tag][tag_long] >> tag_bit;

	/* This never happens if RADIX_TREE_TAG_LONGS == 1 */
	if (tag_long < RADIX_TREE_TAG_LONGS - 1) {
		/* Pick tags from next element */
		if (tag_bit)
			iter->tags |= node->tags[tag][tag_long + 1] <<
						(BITS_PER_LONG - tag_bit);
		/* Clip chunk size, here only BITS_PER_LONG tags */
		iter->next_index = __radix_tree_iter_add(iter, BITS_PER_LONG);
	}
}

void __rcu **radix_tree_iter_resume(void __rcu **slot,
					struct radix_tree_iter *iter)
{
	slot++;
	iter->index = __radix_tree_iter_add(iter, 1);
	iter->next_index = iter->index;
	iter->tags = 0;
	return NULL;
}
EXPORT_SYMBOL(radix_tree_iter_resume);

/**
 * radix_tree_next_chunk - find next chunk of slots for iteration
 *
 * @root:	radix tree root
 * @iter:	iterator state
 * @flags:	RADIX_TREE_ITER_* flags and tag index
 * Returns:	pointer to chunk first slot, or NULL if iteration is over
 */
void __rcu **radix_tree_next_chunk(const struct radix_tree_root *root,
			     struct radix_tree_iter *iter, unsigned flags)
{
	unsigned tag = flags & RADIX_TREE_ITER_TAG_MASK;
	struct radix_tree_node *node, *child;
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
	 * contiguous iterating, and forbid switching to the next chunk.
	 */
	index = iter->next_index;
	if (!index && iter->index)
		return NULL;

 restart:
	radix_tree_load_root(root, &child, &maxindex);
	if (index > maxindex)
		return NULL;
	if (!child)
		return NULL;

	if (!radix_tree_is_internal_node(child)) {
		/* Single-slot tree */
		iter->index = index;
		iter->next_index = maxindex + 1;
		iter->tags = 1;
		iter->node = NULL;
		return (void __rcu **)&root->xa_head;
	}

	do {
		node = entry_to_node(child);
		offset = radix_tree_descend(node, &child, index);

		if ((flags & RADIX_TREE_ITER_TAGGED) ?
				!tag_get(node, tag, offset) : !child) {
			/* Hole detected */
			if (flags & RADIX_TREE_ITER_CONTIG)
				return NULL;

			if (flags & RADIX_TREE_ITER_TAGGED)
				offset = radix_tree_find_next_bit(node, tag,
						offset + 1);
			else
				while (++offset	< RADIX_TREE_MAP_SIZE) {
					void *slot = rcu_dereference_raw(
							node->slots[offset]);
					if (slot)
						break;
				}
			index &= ~node_maxindex(node);
			index += offset << node->shift;
			/* Overflow after ~0UL */
			if (!index)
				return NULL;
			if (offset == RADIX_TREE_MAP_SIZE)
				goto restart;
			child = rcu_dereference_raw(node->slots[offset]);
		}

		if (!child)
			goto restart;
		if (child == RADIX_TREE_RETRY)
			break;
	} while (node->shift && radix_tree_is_internal_node(child));

	/* Update the iterator state */
	iter->index = (index &~ node_maxindex(node)) | offset;
	iter->next_index = (index | node_maxindex(node)) + 1;
	iter->node = node;

	if (flags & RADIX_TREE_ITER_TAGGED)
		set_iter_tags(iter, node, offset, tag);

	return node->slots + offset;
}
EXPORT_SYMBOL(radix_tree_next_chunk);

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
radix_tree_gang_lookup(const struct radix_tree_root *root, void **results,
			unsigned long first_index, unsigned int max_items)
{
	struct radix_tree_iter iter;
	void __rcu **slot;
	unsigned int ret = 0;

	if (unlikely(!max_items))
		return 0;

	radix_tree_for_each_slot(slot, root, &iter, first_index) {
		results[ret] = rcu_dereference_raw(*slot);
		if (!results[ret])
			continue;
		if (radix_tree_is_internal_node(results[ret])) {
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
radix_tree_gang_lookup_tag(const struct radix_tree_root *root, void **results,
		unsigned long first_index, unsigned int max_items,
		unsigned int tag)
{
	struct radix_tree_iter iter;
	void __rcu **slot;
	unsigned int ret = 0;

	if (unlikely(!max_items))
		return 0;

	radix_tree_for_each_tagged(slot, root, &iter, first_index, tag) {
		results[ret] = rcu_dereference_raw(*slot);
		if (!results[ret])
			continue;
		if (radix_tree_is_internal_node(results[ret])) {
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
radix_tree_gang_lookup_tag_slot(const struct radix_tree_root *root,
		void __rcu ***results, unsigned long first_index,
		unsigned int max_items, unsigned int tag)
{
	struct radix_tree_iter iter;
	void __rcu **slot;
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

static bool __radix_tree_delete(struct radix_tree_root *root,
				struct radix_tree_node *node, void __rcu **slot)
{
	void *old = rcu_dereference_raw(*slot);
	int values = xa_is_value(old) ? -1 : 0;
	unsigned offset = get_slot_offset(node, slot);
	int tag;

	if (is_idr(root))
		node_tag_set(root, node, IDR_FREE, offset);
	else
		for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++)
			node_tag_clear(root, node, tag, offset);

	replace_slot(slot, NULL, node, -1, values);
	return node && delete_node(root, node);
}

/**
 * radix_tree_iter_delete - delete the entry at this iterator position
 * @root: radix tree root
 * @iter: iterator state
 * @slot: pointer to slot
 *
 * Delete the entry at the position currently pointed to by the iterator.
 * This may result in the current node being freed; if it is, the iterator
 * is advanced so that it will not reference the freed memory.  This
 * function may be called without any locking if there are no other threads
 * which can access this tree.
 */
void radix_tree_iter_delete(struct radix_tree_root *root,
				struct radix_tree_iter *iter, void __rcu **slot)
{
	if (__radix_tree_delete(root, iter->node, slot))
		iter->index = iter->next_index;
}
EXPORT_SYMBOL(radix_tree_iter_delete);

/**
 * radix_tree_delete_item - delete an item from a radix tree
 * @root: radix tree root
 * @index: index key
 * @item: expected item
 *
 * Remove @item at @index from the radix tree rooted at @root.
 *
 * Return: the deleted entry, or %NULL if it was not present
 * or the entry at the given @index was not @item.
 */
void *radix_tree_delete_item(struct radix_tree_root *root,
			     unsigned long index, void *item)
{
	struct radix_tree_node *node = NULL;
	void __rcu **slot = NULL;
	void *entry;

	entry = __radix_tree_lookup(root, index, &node, &slot);
	if (!slot)
		return NULL;
	if (!entry && (!is_idr(root) || node_tag_get(root, node, IDR_FREE,
						get_slot_offset(node, slot))))
		return NULL;

	if (item && entry != item)
		return NULL;

	__radix_tree_delete(root, node, slot);

	return entry;
}
EXPORT_SYMBOL(radix_tree_delete_item);

/**
 * radix_tree_delete - delete an entry from a radix tree
 * @root: radix tree root
 * @index: index key
 *
 * Remove the entry at @index from the radix tree rooted at @root.
 *
 * Return: The deleted entry, or %NULL if it was not present.
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
int radix_tree_tagged(const struct radix_tree_root *root, unsigned int tag)
{
	return root_tag_get(root, tag);
}
EXPORT_SYMBOL(radix_tree_tagged);

/**
 * idr_preload - preload for idr_alloc()
 * @gfp_mask: allocation mask to use for preloading
 *
 * Preallocate memory to use for the next call to idr_alloc().  This function
 * returns with preemption disabled.  It will be enabled by idr_preload_end().
 */
void idr_preload(gfp_t gfp_mask)
{
	if (__radix_tree_preload(gfp_mask, IDR_PRELOAD_SIZE))
		preempt_disable();
}
EXPORT_SYMBOL(idr_preload);

void __rcu **idr_get_free(struct radix_tree_root *root,
			      struct radix_tree_iter *iter, gfp_t gfp,
			      unsigned long max)
{
	struct radix_tree_node *node = NULL, *child;
	void __rcu **slot = (void __rcu **)&root->xa_head;
	unsigned long maxindex, start = iter->next_index;
	unsigned int shift, offset = 0;

 grow:
	shift = radix_tree_load_root(root, &child, &maxindex);
	if (!radix_tree_tagged(root, IDR_FREE))
		start = max(start, maxindex + 1);
	if (start > max)
		return ERR_PTR(-ENOSPC);

	if (start > maxindex) {
		int error = radix_tree_extend(root, gfp, start, shift);
		if (error < 0)
			return ERR_PTR(error);
		shift = error;
		child = rcu_dereference_raw(root->xa_head);
	}
	if (start == 0 && shift == 0)
		shift = RADIX_TREE_MAP_SHIFT;

	while (shift) {
		shift -= RADIX_TREE_MAP_SHIFT;
		if (child == NULL) {
			/* Have to add a child node.  */
			child = radix_tree_node_alloc(gfp, node, root, shift,
							offset, 0, 0);
			if (!child)
				return ERR_PTR(-ENOMEM);
			all_tag_set(child, IDR_FREE);
			rcu_assign_pointer(*slot, node_to_entry(child));
			if (node)
				node->count++;
		} else if (!radix_tree_is_internal_node(child))
			break;

		node = entry_to_node(child);
		offset = radix_tree_descend(node, &child, start);
		if (!tag_get(node, IDR_FREE, offset)) {
			offset = radix_tree_find_next_bit(node, IDR_FREE,
							offset + 1);
			start = next_index(start, node, offset);
			if (start > max || start == 0)
				return ERR_PTR(-ENOSPC);
			while (offset == RADIX_TREE_MAP_SIZE) {
				offset = node->offset + 1;
				node = node->parent;
				if (!node)
					goto grow;
				shift = node->shift;
			}
			child = rcu_dereference_raw(node->slots[offset]);
		}
		slot = &node->slots[offset];
	}

	iter->index = start;
	if (node)
		iter->next_index = 1 + min(max, (start | node_maxindex(node)));
	else
		iter->next_index = 1;
	iter->node = node;
	set_iter_tags(iter, node, offset, IDR_FREE);

	return slot;
}

/**
 * idr_destroy - release all internal memory from an IDR
 * @idr: idr handle
 *
 * After this function is called, the IDR is empty, and may be reused or
 * the data structure containing it may be freed.
 *
 * A typical clean-up sequence for objects stored in an idr tree will use
 * idr_for_each() to free all objects, if necessary, then idr_destroy() to
 * free the memory used to keep track of those objects.
 */
void idr_destroy(struct idr *idr)
{
	struct radix_tree_node *node = rcu_dereference_raw(idr->idr_rt.xa_head);
	if (radix_tree_is_internal_node(node))
		radix_tree_free_nodes(node);
	idr->idr_rt.xa_head = NULL;
	root_tag_set(&idr->idr_rt, IDR_FREE);
}
EXPORT_SYMBOL(idr_destroy);

static void
radix_tree_node_ctor(void *arg)
{
	struct radix_tree_node *node = arg;

	memset(node, 0, sizeof(*node));
	INIT_LIST_HEAD(&node->private_list);
}

static int radix_tree_cpu_dead(unsigned int cpu)
{
	struct radix_tree_preload *rtp;
	struct radix_tree_node *node;

	/* Free per-cpu pool of preloaded nodes */
	rtp = &per_cpu(radix_tree_preloads, cpu);
	while (rtp->nr) {
		node = rtp->nodes;
		rtp->nodes = node->parent;
		kmem_cache_free(radix_tree_node_cachep, node);
		rtp->nr--;
	}
	return 0;
}

void __init radix_tree_init(void)
{
	int ret;

	BUILD_BUG_ON(RADIX_TREE_MAX_TAGS + __GFP_BITS_SHIFT > 32);
	BUILD_BUG_ON(ROOT_IS_IDR & ~GFP_ZONEMASK);
	BUILD_BUG_ON(XA_CHUNK_SIZE > 255);
	radix_tree_node_cachep = kmem_cache_create("radix_tree_node",
			sizeof(struct radix_tree_node), 0,
			SLAB_PANIC | SLAB_RECLAIM_ACCOUNT,
			radix_tree_node_ctor);
	ret = cpuhp_setup_state_nocalls(CPUHP_RADIX_DEAD, "lib/radix:dead",
					NULL, radix_tree_cpu_dead);
	WARN_ON(ret < 0);
}
