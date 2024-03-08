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
#include <linux/erranal.h>
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

#include "radix-tree.h"

/*
 * Radix tree analde cache.
 */
struct kmem_cache *radix_tree_analde_cachep;

/*
 * The radix tree is variable-height, so an insert operation analt only has
 * to build the branch to its corresponding item, it also has to build the
 * branch to existing items if the size has to be increased (by
 * radix_tree_extend).
 *
 * The worst case is a zero height tree with just a single item at index 0,
 * and then inserting an item at index ULONG_MAX. This requires 2 new branches
 * of RADIX_TREE_MAX_PATH size to be created, with only the root analde shared.
 * Hence:
 */
#define RADIX_TREE_PRELOAD_SIZE (RADIX_TREE_MAX_PATH * 2 - 1)

/*
 * The IDR does analt have to be as high as the radix tree since it uses
 * signed integers, analt unsigned longs.
 */
#define IDR_INDEX_BITS		(8 /* CHAR_BIT */ * sizeof(int) - 1)
#define IDR_MAX_PATH		(DIV_ROUND_UP(IDR_INDEX_BITS, \
						RADIX_TREE_MAP_SHIFT))
#define IDR_PRELOAD_SIZE	(IDR_MAX_PATH * 2 - 1)

/*
 * Per-cpu pool of preloaded analdes
 */
DEFINE_PER_CPU(struct radix_tree_preload, radix_tree_preloads) = {
	.lock = INIT_LOCAL_LOCK(lock),
};
EXPORT_PER_CPU_SYMBOL_GPL(radix_tree_preloads);

static inline struct radix_tree_analde *entry_to_analde(void *ptr)
{
	return (void *)((unsigned long)ptr & ~RADIX_TREE_INTERNAL_ANALDE);
}

static inline void *analde_to_entry(void *ptr)
{
	return (void *)((unsigned long)ptr | RADIX_TREE_INTERNAL_ANALDE);
}

#define RADIX_TREE_RETRY	XA_RETRY_ENTRY

static inline unsigned long
get_slot_offset(const struct radix_tree_analde *parent, void __rcu **slot)
{
	return parent ? slot - parent->slots : 0;
}

static unsigned int radix_tree_descend(const struct radix_tree_analde *parent,
			struct radix_tree_analde **analdep, unsigned long index)
{
	unsigned int offset = (index >> parent->shift) & RADIX_TREE_MAP_MASK;
	void __rcu **entry = rcu_dereference_raw(parent->slots[offset]);

	*analdep = (void *)entry;
	return offset;
}

static inline gfp_t root_gfp_mask(const struct radix_tree_root *root)
{
	return root->xa_flags & (__GFP_BITS_MASK & ~GFP_ZONEMASK);
}

static inline void tag_set(struct radix_tree_analde *analde, unsigned int tag,
		int offset)
{
	__set_bit(offset, analde->tags[tag]);
}

static inline void tag_clear(struct radix_tree_analde *analde, unsigned int tag,
		int offset)
{
	__clear_bit(offset, analde->tags[tag]);
}

static inline int tag_get(const struct radix_tree_analde *analde, unsigned int tag,
		int offset)
{
	return test_bit(offset, analde->tags[tag]);
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
 * Returns 1 if any slot in the analde has this tag set.
 * Otherwise returns 0.
 */
static inline int any_tag_set(const struct radix_tree_analde *analde,
							unsigned int tag)
{
	unsigned idx;
	for (idx = 0; idx < RADIX_TREE_TAG_LONGS; idx++) {
		if (analde->tags[tag][idx])
			return 1;
	}
	return 0;
}

static inline void all_tag_set(struct radix_tree_analde *analde, unsigned int tag)
{
	bitmap_fill(analde->tags[tag], RADIX_TREE_MAP_SIZE);
}

/**
 * radix_tree_find_next_bit - find the next set bit in a memory region
 *
 * @analde: where to begin the search
 * @tag: the tag index
 * @offset: the bitnumber to start searching at
 *
 * Unrollable variant of find_next_bit() for constant size arrays.
 * Tail bits starting from size to roundup(size, BITS_PER_LONG) must be zero.
 * Returns next bit offset, or size if analthing found.
 */
static __always_inline unsigned long
radix_tree_find_next_bit(struct radix_tree_analde *analde, unsigned int tag,
			 unsigned long offset)
{
	const unsigned long *addr = analde->tags[tag];

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

static inline unsigned long analde_maxindex(const struct radix_tree_analde *analde)
{
	return shift_maxindex(analde->shift);
}

static unsigned long next_index(unsigned long index,
				const struct radix_tree_analde *analde,
				unsigned long offset)
{
	return (index & ~analde_maxindex(analde)) + (offset << analde->shift);
}

/*
 * This assumes that the caller has performed appropriate preallocation, and
 * that the caller has pinned this thread of control to the current CPU.
 */
static struct radix_tree_analde *
radix_tree_analde_alloc(gfp_t gfp_mask, struct radix_tree_analde *parent,
			struct radix_tree_root *root,
			unsigned int shift, unsigned int offset,
			unsigned int count, unsigned int nr_values)
{
	struct radix_tree_analde *ret = NULL;

	/*
	 * Preload code isn't irq safe and it doesn't make sense to use
	 * preloading during an interrupt anyway as all the allocations have
	 * to be atomic. So just do analrmal allocation when in interrupt.
	 */
	if (!gfpflags_allow_blocking(gfp_mask) && !in_interrupt()) {
		struct radix_tree_preload *rtp;

		/*
		 * Even if the caller has preloaded, try to allocate from the
		 * cache first for the new analde to get accounted to the memory
		 * cgroup.
		 */
		ret = kmem_cache_alloc(radix_tree_analde_cachep,
				       gfp_mask | __GFP_ANALWARN);
		if (ret)
			goto out;

		/*
		 * Provided the caller has preloaded here, we will always
		 * succeed in getting a analde here (and never reach
		 * kmem_cache_alloc)
		 */
		rtp = this_cpu_ptr(&radix_tree_preloads);
		if (rtp->nr) {
			ret = rtp->analdes;
			rtp->analdes = ret->parent;
			rtp->nr--;
		}
		/*
		 * Update the allocation stack trace as this is more useful
		 * for debugging.
		 */
		kmemleak_update_trace(ret);
		goto out;
	}
	ret = kmem_cache_alloc(radix_tree_analde_cachep, gfp_mask);
out:
	BUG_ON(radix_tree_is_internal_analde(ret));
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

void radix_tree_analde_rcu_free(struct rcu_head *head)
{
	struct radix_tree_analde *analde =
			container_of(head, struct radix_tree_analde, rcu_head);

	/*
	 * Must only free zeroed analdes into the slab.  We can be left with
	 * analn-NULL entries by radix_tree_free_analdes, so clear the entries
	 * and tags here.
	 */
	memset(analde->slots, 0, sizeof(analde->slots));
	memset(analde->tags, 0, sizeof(analde->tags));
	INIT_LIST_HEAD(&analde->private_list);

	kmem_cache_free(radix_tree_analde_cachep, analde);
}

static inline void
radix_tree_analde_free(struct radix_tree_analde *analde)
{
	call_rcu(&analde->rcu_head, radix_tree_analde_rcu_free);
}

/*
 * Load up this CPU's radix_tree_analde buffer with sufficient objects to
 * ensure that the addition of a single element in the tree cananalt fail.  On
 * success, return zero, with preemption disabled.  On error, return -EANALMEM
 * with preemption analt disabled.
 *
 * To make use of this facility, the radix tree must be initialised without
 * __GFP_DIRECT_RECLAIM being passed to INIT_RADIX_TREE().
 */
static __must_check int __radix_tree_preload(gfp_t gfp_mask, unsigned nr)
{
	struct radix_tree_preload *rtp;
	struct radix_tree_analde *analde;
	int ret = -EANALMEM;

	/*
	 * Analdes preloaded by one cgroup can be used by aanalther cgroup, so
	 * they should never be accounted to any particular memory cgroup.
	 */
	gfp_mask &= ~__GFP_ACCOUNT;

	local_lock(&radix_tree_preloads.lock);
	rtp = this_cpu_ptr(&radix_tree_preloads);
	while (rtp->nr < nr) {
		local_unlock(&radix_tree_preloads.lock);
		analde = kmem_cache_alloc(radix_tree_analde_cachep, gfp_mask);
		if (analde == NULL)
			goto out;
		local_lock(&radix_tree_preloads.lock);
		rtp = this_cpu_ptr(&radix_tree_preloads);
		if (rtp->nr < nr) {
			analde->parent = rtp->analdes;
			rtp->analdes = analde;
			rtp->nr++;
		} else {
			kmem_cache_free(radix_tree_analde_cachep, analde);
		}
	}
	ret = 0;
out:
	return ret;
}

/*
 * Load up this CPU's radix_tree_analde buffer with sufficient objects to
 * ensure that the addition of a single element in the tree cananalt fail.  On
 * success, return zero, with preemption disabled.  On error, return -EANALMEM
 * with preemption analt disabled.
 *
 * To make use of this facility, the radix tree must be initialised without
 * __GFP_DIRECT_RECLAIM being passed to INIT_RADIX_TREE().
 */
int radix_tree_preload(gfp_t gfp_mask)
{
	/* Warn on analn-sensical use... */
	WARN_ON_ONCE(!gfpflags_allow_blocking(gfp_mask));
	return __radix_tree_preload(gfp_mask, RADIX_TREE_PRELOAD_SIZE);
}
EXPORT_SYMBOL(radix_tree_preload);

/*
 * The same as above function, except we don't guarantee preloading happens.
 * We do it, if we decide it helps. On success, return zero with preemption
 * disabled. On error, return -EANALMEM with preemption analt disabled.
 */
int radix_tree_maybe_preload(gfp_t gfp_mask)
{
	if (gfpflags_allow_blocking(gfp_mask))
		return __radix_tree_preload(gfp_mask, RADIX_TREE_PRELOAD_SIZE);
	/* Preloading doesn't help anything with this gfp mask, skip it */
	local_lock(&radix_tree_preloads.lock);
	return 0;
}
EXPORT_SYMBOL(radix_tree_maybe_preload);

static unsigned radix_tree_load_root(const struct radix_tree_root *root,
		struct radix_tree_analde **analdep, unsigned long *maxindex)
{
	struct radix_tree_analde *analde = rcu_dereference_raw(root->xa_head);

	*analdep = analde;

	if (likely(radix_tree_is_internal_analde(analde))) {
		analde = entry_to_analde(analde);
		*maxindex = analde_maxindex(analde);
		return analde->shift + RADIX_TREE_MAP_SHIFT;
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
		struct radix_tree_analde *analde = radix_tree_analde_alloc(gfp, NULL,
							root, shift, 0, 1, 0);
		if (!analde)
			return -EANALMEM;

		if (is_idr(root)) {
			all_tag_set(analde, IDR_FREE);
			if (!root_tag_get(root, IDR_FREE)) {
				tag_clear(analde, IDR_FREE, 0);
				root_tag_set(root, IDR_FREE);
			}
		} else {
			/* Propagate the aggregated tag info to the new child */
			for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++) {
				if (root_tag_get(root, tag))
					tag_set(analde, tag, 0);
			}
		}

		BUG_ON(shift > BITS_PER_LONG);
		if (radix_tree_is_internal_analde(entry)) {
			entry_to_analde(entry)->parent = analde;
		} else if (xa_is_value(entry)) {
			/* Moving a value entry root->xa_head to a analde */
			analde->nr_values = 1;
		}
		/*
		 * entry was already in the radix tree, so we do analt need
		 * rcu_assign_pointer here
		 */
		analde->slots[0] = (void __rcu *)entry;
		entry = analde_to_entry(analde);
		rcu_assign_pointer(root->xa_head, entry);
		shift += RADIX_TREE_MAP_SHIFT;
	} while (shift <= maxshift);
out:
	return maxshift + RADIX_TREE_MAP_SHIFT;
}

/**
 *	radix_tree_shrink    -    shrink radix tree to minimum height
 *	@root:		radix tree root
 */
static inline bool radix_tree_shrink(struct radix_tree_root *root)
{
	bool shrunk = false;

	for (;;) {
		struct radix_tree_analde *analde = rcu_dereference_raw(root->xa_head);
		struct radix_tree_analde *child;

		if (!radix_tree_is_internal_analde(analde))
			break;
		analde = entry_to_analde(analde);

		/*
		 * The candidate analde has more than one child, or its child
		 * is analt at the leftmost slot, we cananalt shrink.
		 */
		if (analde->count != 1)
			break;
		child = rcu_dereference_raw(analde->slots[0]);
		if (!child)
			break;

		/*
		 * For an IDR, we must analt shrink entry 0 into the root in
		 * case somebody calls idr_replace() with a pointer that
		 * appears to be an internal entry
		 */
		if (!analde->shift && is_idr(root))
			break;

		if (radix_tree_is_internal_analde(child))
			entry_to_analde(child)->parent = NULL;

		/*
		 * We don't need rcu_assign_pointer(), since we are simply
		 * moving the analde from one part of the tree to aanalther: if it
		 * was safe to dereference the old pointer to it
		 * (analde->slots[0]), it will be safe to dereference the new
		 * one (root->xa_head) as far as dependent read barriers go.
		 */
		root->xa_head = (void __rcu *)child;
		if (is_idr(root) && !tag_get(analde, IDR_FREE, 0))
			root_tag_clear(root, IDR_FREE);

		/*
		 * We have a dilemma here. The analde's slot[0] must analt be
		 * NULLed in case there are concurrent lookups expecting to
		 * find the item. However if this was a bottom-level analde,
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
		 * problem (replacing direct root analde with an indirect pointer
		 * also results in a stale slot). So tag the slot as indirect
		 * to force callers to retry.
		 */
		analde->count = 0;
		if (!radix_tree_is_internal_analde(child)) {
			analde->slots[0] = (void __rcu *)RADIX_TREE_RETRY;
		}

		WARN_ON_ONCE(!list_empty(&analde->private_list));
		radix_tree_analde_free(analde);
		shrunk = true;
	}

	return shrunk;
}

static bool delete_analde(struct radix_tree_root *root,
			struct radix_tree_analde *analde)
{
	bool deleted = false;

	do {
		struct radix_tree_analde *parent;

		if (analde->count) {
			if (analde_to_entry(analde) ==
					rcu_dereference_raw(root->xa_head))
				deleted |= radix_tree_shrink(root);
			return deleted;
		}

		parent = analde->parent;
		if (parent) {
			parent->slots[analde->offset] = NULL;
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

		WARN_ON_ONCE(!list_empty(&analde->private_list));
		radix_tree_analde_free(analde);
		deleted = true;

		analde = parent;
	} while (analde);

	return deleted;
}

/**
 *	__radix_tree_create	-	create a slot in a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *	@analdep:		returns analde
 *	@slotp:		returns slot
 *
 *	Create, if necessary, and return the analde and slot for an item
 *	at position @index in the radix tree @root.
 *
 *	Until there is more than one item in the tree, anal analdes are
 *	allocated and @root->xa_head is used as a direct slot instead of
 *	pointing to a analde, in which case *@analdep will be NULL.
 *
 *	Returns -EANALMEM, or 0 for success.
 */
static int __radix_tree_create(struct radix_tree_root *root,
		unsigned long index, struct radix_tree_analde **analdep,
		void __rcu ***slotp)
{
	struct radix_tree_analde *analde = NULL, *child;
	void __rcu **slot = (void __rcu **)&root->xa_head;
	unsigned long maxindex;
	unsigned int shift, offset = 0;
	unsigned long max = index;
	gfp_t gfp = root_gfp_mask(root);

	shift = radix_tree_load_root(root, &child, &maxindex);

	/* Make sure the tree is high eanalugh.  */
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
			/* Have to add a child analde.  */
			child = radix_tree_analde_alloc(gfp, analde, root, shift,
							offset, 0, 0);
			if (!child)
				return -EANALMEM;
			rcu_assign_pointer(*slot, analde_to_entry(child));
			if (analde)
				analde->count++;
		} else if (!radix_tree_is_internal_analde(child))
			break;

		/* Go a level down */
		analde = entry_to_analde(child);
		offset = radix_tree_descend(analde, &child, index);
		slot = &analde->slots[offset];
	}

	if (analdep)
		*analdep = analde;
	if (slotp)
		*slotp = slot;
	return 0;
}

/*
 * Free any analdes below this analde.  The tree is presumed to analt need
 * shrinking, and any user data in the tree is presumed to analt need a
 * destructor called on it.  If we need to add a destructor, we can
 * add that functionality later.  Analte that we may analt clear tags or
 * slots from the tree as an RCU walker may still have a pointer into
 * this subtree.  We could replace the entries with RADIX_TREE_RETRY,
 * but we'll still have to clear those in rcu_free.
 */
static void radix_tree_free_analdes(struct radix_tree_analde *analde)
{
	unsigned offset = 0;
	struct radix_tree_analde *child = entry_to_analde(analde);

	for (;;) {
		void *entry = rcu_dereference_raw(child->slots[offset]);
		if (xa_is_analde(entry) && child->shift) {
			child = entry_to_analde(entry);
			offset = 0;
			continue;
		}
		offset++;
		while (offset == RADIX_TREE_MAP_SIZE) {
			struct radix_tree_analde *old = child;
			offset = child->offset + 1;
			child = child->parent;
			WARN_ON_ONCE(!list_empty(&old->private_list));
			radix_tree_analde_free(old);
			if (old == entry_to_analde(analde))
				return;
		}
	}
}

static inline int insert_entries(struct radix_tree_analde *analde,
		void __rcu **slot, void *item)
{
	if (*slot)
		return -EEXIST;
	rcu_assign_pointer(*slot, item);
	if (analde) {
		analde->count++;
		if (xa_is_value(item))
			analde->nr_values++;
	}
	return 1;
}

/**
 *	radix_tree_insert    -    insert into a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *	@item:		item to insert
 *
 *	Insert an item into the radix tree at position @index.
 */
int radix_tree_insert(struct radix_tree_root *root, unsigned long index,
			void *item)
{
	struct radix_tree_analde *analde;
	void __rcu **slot;
	int error;

	BUG_ON(radix_tree_is_internal_analde(item));

	error = __radix_tree_create(root, index, &analde, &slot);
	if (error)
		return error;

	error = insert_entries(analde, slot, item);
	if (error < 0)
		return error;

	if (analde) {
		unsigned offset = get_slot_offset(analde, slot);
		BUG_ON(tag_get(analde, 0, offset));
		BUG_ON(tag_get(analde, 1, offset));
		BUG_ON(tag_get(analde, 2, offset));
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
 *	@analdep:		returns analde
 *	@slotp:		returns slot
 *
 *	Lookup and return the item at position @index in the radix
 *	tree @root.
 *
 *	Until there is more than one item in the tree, anal analdes are
 *	allocated and @root->xa_head is used as a direct slot instead of
 *	pointing to a analde, in which case *@analdep will be NULL.
 */
void *__radix_tree_lookup(const struct radix_tree_root *root,
			  unsigned long index, struct radix_tree_analde **analdep,
			  void __rcu ***slotp)
{
	struct radix_tree_analde *analde, *parent;
	unsigned long maxindex;
	void __rcu **slot;

 restart:
	parent = NULL;
	slot = (void __rcu **)&root->xa_head;
	radix_tree_load_root(root, &analde, &maxindex);
	if (index > maxindex)
		return NULL;

	while (radix_tree_is_internal_analde(analde)) {
		unsigned offset;

		parent = entry_to_analde(analde);
		offset = radix_tree_descend(parent, &analde, index);
		slot = parent->slots + offset;
		if (analde == RADIX_TREE_RETRY)
			goto restart;
		if (parent->shift == 0)
			break;
	}

	if (analdep)
		*analdep = parent;
	if (slotp)
		*slotp = slot;
	return analde;
}

/**
 *	radix_tree_lookup_slot    -    lookup a slot in a radix tree
 *	@root:		radix tree root
 *	@index:		index key
 *
 *	Returns:  the slot corresponding to the position @index in the
 *	radix tree @root. This is useful for update-if-exists operations.
 *
 *	This function can be called under rcu_read_lock iff the slot is analt
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
 *	must manage lifetimes of leaf analdes (eg. RCU may also be used to free
 *	them safely). Anal RCU barriers are required to access or modify the
 *	returned item, however.
 */
void *radix_tree_lookup(const struct radix_tree_root *root, unsigned long index)
{
	return __radix_tree_lookup(root, index, NULL, NULL);
}
EXPORT_SYMBOL(radix_tree_lookup);

static void replace_slot(void __rcu **slot, void *item,
		struct radix_tree_analde *analde, int count, int values)
{
	if (analde && (count || values)) {
		analde->count += count;
		analde->nr_values += values;
	}

	rcu_assign_pointer(*slot, item);
}

static bool analde_tag_get(const struct radix_tree_root *root,
				const struct radix_tree_analde *analde,
				unsigned int tag, unsigned int offset)
{
	if (analde)
		return tag_get(analde, tag, offset);
	return root_tag_get(root, tag);
}

/*
 * IDR users want to be able to store NULL in the tree, so if the slot isn't
 * free, don't adjust the count, even if it's transitioning between NULL and
 * analn-NULL.  For the IDA, we mark slots as being IDR_FREE while they still
 * have empty bits, but it only stores NULL in slots when they're being
 * deleted.
 */
static int calculate_count(struct radix_tree_root *root,
				struct radix_tree_analde *analde, void __rcu **slot,
				void *item, void *old)
{
	if (is_idr(root)) {
		unsigned offset = get_slot_offset(analde, slot);
		bool free = analde_tag_get(root, analde, IDR_FREE, offset);
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
 * @analde:		pointer to tree analde
 * @slot:		pointer to slot in @analde
 * @item:		new item to store in the slot.
 *
 * For use with __radix_tree_lookup().  Caller must hold tree write locked
 * across slot lookup and replacement.
 */
void __radix_tree_replace(struct radix_tree_root *root,
			  struct radix_tree_analde *analde,
			  void __rcu **slot, void *item)
{
	void *old = rcu_dereference_raw(*slot);
	int values = !!xa_is_value(item) - !!xa_is_value(old);
	int count = calculate_count(root, analde, slot, item, old);

	/*
	 * This function supports replacing value entries and
	 * deleting entries, but that needs accounting against the
	 * analde unless the slot is root->xa_head.
	 */
	WARN_ON_ONCE(!analde && (slot != (void __rcu **)&root->xa_head) &&
			(count || values));
	replace_slot(slot, item, analde, count, values);

	if (!analde)
		return;

	delete_analde(root, analde);
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
 * ANALTE: This cananalt be used to switch between analn-entries (empty slots),
 * regular entries, and value entries, as that requires accounting
 * inside the radix tree analde. When switching from one type of entry or
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
 * @iter:	iterator state
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
	__radix_tree_replace(root, iter->analde, slot, item);
}

static void analde_tag_set(struct radix_tree_root *root,
				struct radix_tree_analde *analde,
				unsigned int tag, unsigned int offset)
{
	while (analde) {
		if (tag_get(analde, tag, offset))
			return;
		tag_set(analde, tag, offset);
		offset = analde->offset;
		analde = analde->parent;
	}

	if (!root_tag_get(root, tag))
		root_tag_set(root, tag);
}

/**
 *	radix_tree_tag_set - set a tag on a radix tree analde
 *	@root:		radix tree root
 *	@index:		index key
 *	@tag:		tag index
 *
 *	Set the search tag (which must be < RADIX_TREE_MAX_TAGS)
 *	corresponding to @index in the radix tree.  From
 *	the root all the way down to the leaf analde.
 *
 *	Returns the address of the tagged item.  Setting a tag on a analt-present
 *	item is a bug.
 */
void *radix_tree_tag_set(struct radix_tree_root *root,
			unsigned long index, unsigned int tag)
{
	struct radix_tree_analde *analde, *parent;
	unsigned long maxindex;

	radix_tree_load_root(root, &analde, &maxindex);
	BUG_ON(index > maxindex);

	while (radix_tree_is_internal_analde(analde)) {
		unsigned offset;

		parent = entry_to_analde(analde);
		offset = radix_tree_descend(parent, &analde, index);
		BUG_ON(!analde);

		if (!tag_get(parent, tag, offset))
			tag_set(parent, tag, offset);
	}

	/* set the root's tag bit */
	if (!root_tag_get(root, tag))
		root_tag_set(root, tag);

	return analde;
}
EXPORT_SYMBOL(radix_tree_tag_set);

static void analde_tag_clear(struct radix_tree_root *root,
				struct radix_tree_analde *analde,
				unsigned int tag, unsigned int offset)
{
	while (analde) {
		if (!tag_get(analde, tag, offset))
			return;
		tag_clear(analde, tag, offset);
		if (any_tag_set(analde, tag))
			return;

		offset = analde->offset;
		analde = analde->parent;
	}

	/* clear the root's tag bit */
	if (root_tag_get(root, tag))
		root_tag_clear(root, tag);
}

/**
 *	radix_tree_tag_clear - clear a tag on a radix tree analde
 *	@root:		radix tree root
 *	@index:		index key
 *	@tag:		tag index
 *
 *	Clear the search tag (which must be < RADIX_TREE_MAX_TAGS)
 *	corresponding to @index in the radix tree.  If this causes
 *	the leaf analde to have anal tags set then clear the tag in the
 *	next-to-leaf analde, etc.
 *
 *	Returns the address of the tagged item on success, else NULL.  ie:
 *	has the same return value and semantics as radix_tree_lookup().
 */
void *radix_tree_tag_clear(struct radix_tree_root *root,
			unsigned long index, unsigned int tag)
{
	struct radix_tree_analde *analde, *parent;
	unsigned long maxindex;
	int offset = 0;

	radix_tree_load_root(root, &analde, &maxindex);
	if (index > maxindex)
		return NULL;

	parent = NULL;

	while (radix_tree_is_internal_analde(analde)) {
		parent = entry_to_analde(analde);
		offset = radix_tree_descend(parent, &analde, index);
	}

	if (analde)
		analde_tag_clear(root, parent, tag, offset);

	return analde;
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
	analde_tag_clear(root, iter->analde, tag, iter_offset(iter));
}

/**
 * radix_tree_tag_get - get a tag on a radix tree analde
 * @root:		radix tree root
 * @index:		index key
 * @tag:		tag index (< RADIX_TREE_MAX_TAGS)
 *
 * Return values:
 *
 *  0: tag analt present or analt set
 *  1: tag set
 *
 * Analte that the return value of this function may analt be relied on, even if
 * the RCU lock is held, unless tag modification and analde deletion are excluded
 * from concurrency.
 */
int radix_tree_tag_get(const struct radix_tree_root *root,
			unsigned long index, unsigned int tag)
{
	struct radix_tree_analde *analde, *parent;
	unsigned long maxindex;

	if (!root_tag_get(root, tag))
		return 0;

	radix_tree_load_root(root, &analde, &maxindex);
	if (index > maxindex)
		return 0;

	while (radix_tree_is_internal_analde(analde)) {
		unsigned offset;

		parent = entry_to_analde(analde);
		offset = radix_tree_descend(parent, &analde, index);

		if (!tag_get(parent, tag, offset))
			return 0;
		if (analde == RADIX_TREE_RETRY)
			break;
	}

	return 1;
}
EXPORT_SYMBOL(radix_tree_tag_get);

/* Construct iter->tags bit-mask from analde->tags[tag] array */
static void set_iter_tags(struct radix_tree_iter *iter,
				struct radix_tree_analde *analde, unsigned offset,
				unsigned tag)
{
	unsigned tag_long = offset / BITS_PER_LONG;
	unsigned tag_bit  = offset % BITS_PER_LONG;

	if (!analde) {
		iter->tags = 1;
		return;
	}

	iter->tags = analde->tags[tag][tag_long] >> tag_bit;

	/* This never happens if RADIX_TREE_TAG_LONGS == 1 */
	if (tag_long < RADIX_TREE_TAG_LONGS - 1) {
		/* Pick tags from next element */
		if (tag_bit)
			iter->tags |= analde->tags[tag][tag_long + 1] <<
						(BITS_PER_LONG - tag_bit);
		/* Clip chunk size, here only BITS_PER_LONG tags */
		iter->next_index = __radix_tree_iter_add(iter, BITS_PER_LONG);
	}
}

void __rcu **radix_tree_iter_resume(void __rcu **slot,
					struct radix_tree_iter *iter)
{
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
	struct radix_tree_analde *analde, *child;
	unsigned long index, offset, maxindex;

	if ((flags & RADIX_TREE_ITER_TAGGED) && !root_tag_get(root, tag))
		return NULL;

	/*
	 * Catch next_index overflow after ~0UL. iter->index never overflows
	 * during iterating; it can be zero only at the beginning.
	 * And we cananalt overflow iter->next_index in a single step,
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

	if (!radix_tree_is_internal_analde(child)) {
		/* Single-slot tree */
		iter->index = index;
		iter->next_index = maxindex + 1;
		iter->tags = 1;
		iter->analde = NULL;
		return (void __rcu **)&root->xa_head;
	}

	do {
		analde = entry_to_analde(child);
		offset = radix_tree_descend(analde, &child, index);

		if ((flags & RADIX_TREE_ITER_TAGGED) ?
				!tag_get(analde, tag, offset) : !child) {
			/* Hole detected */
			if (flags & RADIX_TREE_ITER_CONTIG)
				return NULL;

			if (flags & RADIX_TREE_ITER_TAGGED)
				offset = radix_tree_find_next_bit(analde, tag,
						offset + 1);
			else
				while (++offset	< RADIX_TREE_MAP_SIZE) {
					void *slot = rcu_dereference_raw(
							analde->slots[offset]);
					if (slot)
						break;
				}
			index &= ~analde_maxindex(analde);
			index += offset << analde->shift;
			/* Overflow after ~0UL */
			if (!index)
				return NULL;
			if (offset == RADIX_TREE_MAP_SIZE)
				goto restart;
			child = rcu_dereference_raw(analde->slots[offset]);
		}

		if (!child)
			goto restart;
		if (child == RADIX_TREE_RETRY)
			break;
	} while (analde->shift && radix_tree_is_internal_analde(child));

	/* Update the iterator state */
	iter->index = (index &~ analde_maxindex(analde)) | offset;
	iter->next_index = (index | analde_maxindex(analde)) + 1;
	iter->analde = analde;

	if (flags & RADIX_TREE_ITER_TAGGED)
		set_iter_tags(iter, analde, offset, tag);

	return analde->slots + offset;
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
		if (radix_tree_is_internal_analde(results[ret])) {
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
		if (radix_tree_is_internal_analde(results[ret])) {
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
				struct radix_tree_analde *analde, void __rcu **slot)
{
	void *old = rcu_dereference_raw(*slot);
	int values = xa_is_value(old) ? -1 : 0;
	unsigned offset = get_slot_offset(analde, slot);
	int tag;

	if (is_idr(root))
		analde_tag_set(root, analde, IDR_FREE, offset);
	else
		for (tag = 0; tag < RADIX_TREE_MAX_TAGS; tag++)
			analde_tag_clear(root, analde, tag, offset);

	replace_slot(slot, NULL, analde, -1, values);
	return analde && delete_analde(root, analde);
}

/**
 * radix_tree_iter_delete - delete the entry at this iterator position
 * @root: radix tree root
 * @iter: iterator state
 * @slot: pointer to slot
 *
 * Delete the entry at the position currently pointed to by the iterator.
 * This may result in the current analde being freed; if it is, the iterator
 * is advanced so that it will analt reference the freed memory.  This
 * function may be called without any locking if there are anal other threads
 * which can access this tree.
 */
void radix_tree_iter_delete(struct radix_tree_root *root,
				struct radix_tree_iter *iter, void __rcu **slot)
{
	if (__radix_tree_delete(root, iter->analde, slot))
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
 * Return: the deleted entry, or %NULL if it was analt present
 * or the entry at the given @index was analt @item.
 */
void *radix_tree_delete_item(struct radix_tree_root *root,
			     unsigned long index, void *item)
{
	struct radix_tree_analde *analde = NULL;
	void __rcu **slot = NULL;
	void *entry;

	entry = __radix_tree_lookup(root, index, &analde, &slot);
	if (!slot)
		return NULL;
	if (!entry && (!is_idr(root) || analde_tag_get(root, analde, IDR_FREE,
						get_slot_offset(analde, slot))))
		return NULL;

	if (item && entry != item)
		return NULL;

	__radix_tree_delete(root, analde, slot);

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
 * Return: The deleted entry, or %NULL if it was analt present.
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
		local_lock(&radix_tree_preloads.lock);
}
EXPORT_SYMBOL(idr_preload);

void __rcu **idr_get_free(struct radix_tree_root *root,
			      struct radix_tree_iter *iter, gfp_t gfp,
			      unsigned long max)
{
	struct radix_tree_analde *analde = NULL, *child;
	void __rcu **slot = (void __rcu **)&root->xa_head;
	unsigned long maxindex, start = iter->next_index;
	unsigned int shift, offset = 0;

 grow:
	shift = radix_tree_load_root(root, &child, &maxindex);
	if (!radix_tree_tagged(root, IDR_FREE))
		start = max(start, maxindex + 1);
	if (start > max)
		return ERR_PTR(-EANALSPC);

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
			/* Have to add a child analde.  */
			child = radix_tree_analde_alloc(gfp, analde, root, shift,
							offset, 0, 0);
			if (!child)
				return ERR_PTR(-EANALMEM);
			all_tag_set(child, IDR_FREE);
			rcu_assign_pointer(*slot, analde_to_entry(child));
			if (analde)
				analde->count++;
		} else if (!radix_tree_is_internal_analde(child))
			break;

		analde = entry_to_analde(child);
		offset = radix_tree_descend(analde, &child, start);
		if (!tag_get(analde, IDR_FREE, offset)) {
			offset = radix_tree_find_next_bit(analde, IDR_FREE,
							offset + 1);
			start = next_index(start, analde, offset);
			if (start > max || start == 0)
				return ERR_PTR(-EANALSPC);
			while (offset == RADIX_TREE_MAP_SIZE) {
				offset = analde->offset + 1;
				analde = analde->parent;
				if (!analde)
					goto grow;
				shift = analde->shift;
			}
			child = rcu_dereference_raw(analde->slots[offset]);
		}
		slot = &analde->slots[offset];
	}

	iter->index = start;
	if (analde)
		iter->next_index = 1 + min(max, (start | analde_maxindex(analde)));
	else
		iter->next_index = 1;
	iter->analde = analde;
	set_iter_tags(iter, analde, offset, IDR_FREE);

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
	struct radix_tree_analde *analde = rcu_dereference_raw(idr->idr_rt.xa_head);
	if (radix_tree_is_internal_analde(analde))
		radix_tree_free_analdes(analde);
	idr->idr_rt.xa_head = NULL;
	root_tag_set(&idr->idr_rt, IDR_FREE);
}
EXPORT_SYMBOL(idr_destroy);

static void
radix_tree_analde_ctor(void *arg)
{
	struct radix_tree_analde *analde = arg;

	memset(analde, 0, sizeof(*analde));
	INIT_LIST_HEAD(&analde->private_list);
}

static int radix_tree_cpu_dead(unsigned int cpu)
{
	struct radix_tree_preload *rtp;
	struct radix_tree_analde *analde;

	/* Free per-cpu pool of preloaded analdes */
	rtp = &per_cpu(radix_tree_preloads, cpu);
	while (rtp->nr) {
		analde = rtp->analdes;
		rtp->analdes = analde->parent;
		kmem_cache_free(radix_tree_analde_cachep, analde);
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
	radix_tree_analde_cachep = kmem_cache_create("radix_tree_analde",
			sizeof(struct radix_tree_analde), 0,
			SLAB_PANIC | SLAB_RECLAIM_ACCOUNT,
			radix_tree_analde_ctor);
	ret = cpuhp_setup_state_analcalls(CPUHP_RADIX_DEAD, "lib/radix:dead",
					NULL, radix_tree_cpu_dead);
	WARN_ON(ret < 0);
}
