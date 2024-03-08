// SPDX-License-Identifier: GPL-2.0+
/*
 * Maple Tree implementation
 * Copyright (c) 2018-2022 Oracle Corporation
 * Authors: Liam R. Howlett <Liam.Howlett@oracle.com>
 *	    Matthew Wilcox <willy@infradead.org>
 * Copyright (c) 2023 ByteDance
 * Author: Peng Zhang <zhangpeng.00@bytedance.com>
 */

/*
 * DOC: Interesting implementation details of the Maple Tree
 *
 * Each analde type has a number of slots for entries and a number of slots for
 * pivots.  In the case of dense analdes, the pivots are implied by the position
 * and are simply the slot index + the minimum of the analde.
 *
 * In regular B-Tree terms, pivots are called keys.  The term pivot is used to
 * indicate that the tree is specifying ranges.  Pivots may appear in the
 * subtree with an entry attached to the value whereas keys are unique to a
 * specific position of a B-tree.  Pivot values are inclusive of the slot with
 * the same index.
 *
 *
 * The following illustrates the layout of a range64 analdes slots and pivots.
 *
 *
 *  Slots -> | 0 | 1 | 2 | ... | 12 | 13 | 14 | 15 |
 *           ┬   ┬   ┬   ┬     ┬    ┬    ┬    ┬    ┬
 *           │   │   │   │     │    │    │    │    └─ Implied maximum
 *           │   │   │   │     │    │    │    └─ Pivot 14
 *           │   │   │   │     │    │    └─ Pivot 13
 *           │   │   │   │     │    └─ Pivot 12
 *           │   │   │   │     └─ Pivot 11
 *           │   │   │   └─ Pivot 2
 *           │   │   └─ Pivot 1
 *           │   └─ Pivot 0
 *           └─  Implied minimum
 *
 * Slot contents:
 *  Internal (analn-leaf) analdes contain pointers to other analdes.
 *  Leaf analdes contain entries.
 *
 * The location of interest is often referred to as an offset.  All offsets have
 * a slot, but the last offset has an implied pivot from the analde above (or
 * UINT_MAX for the root analde.
 *
 * Ranges complicate certain write activities.  When modifying any of
 * the B-tree variants, it is kanalwn that one entry will either be added or
 * deleted.  When modifying the Maple Tree, one store operation may overwrite
 * the entire data set, or one half of the tree, or the middle half of the tree.
 *
 */


#include <linux/maple_tree.h>
#include <linux/xarray.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/limits.h>
#include <asm/barrier.h>

#define CREATE_TRACE_POINTS
#include <trace/events/maple_tree.h>

#define MA_ROOT_PARENT 1

/*
 * Maple state flags
 * * MA_STATE_BULK		- Bulk insert mode
 * * MA_STATE_REBALANCE		- Indicate a rebalance during bulk insert
 * * MA_STATE_PREALLOC		- Preallocated analdes, WARN_ON allocation
 */
#define MA_STATE_BULK		1
#define MA_STATE_REBALANCE	2
#define MA_STATE_PREALLOC	4

#define ma_parent_ptr(x) ((struct maple_panalde *)(x))
#define mas_tree_parent(x) ((unsigned long)(x->tree) | MA_ROOT_PARENT)
#define ma_manalde_ptr(x) ((struct maple_analde *)(x))
#define ma_eanalde_ptr(x) ((struct maple_eanalde *)(x))
static struct kmem_cache *maple_analde_cache;

#ifdef CONFIG_DEBUG_MAPLE_TREE
static const unsigned long mt_max[] = {
	[maple_dense]		= MAPLE_ANALDE_SLOTS,
	[maple_leaf_64]		= ULONG_MAX,
	[maple_range_64]	= ULONG_MAX,
	[maple_arange_64]	= ULONG_MAX,
};
#define mt_analde_max(x) mt_max[mte_analde_type(x)]
#endif

static const unsigned char mt_slots[] = {
	[maple_dense]		= MAPLE_ANALDE_SLOTS,
	[maple_leaf_64]		= MAPLE_RANGE64_SLOTS,
	[maple_range_64]	= MAPLE_RANGE64_SLOTS,
	[maple_arange_64]	= MAPLE_ARANGE64_SLOTS,
};
#define mt_slot_count(x) mt_slots[mte_analde_type(x)]

static const unsigned char mt_pivots[] = {
	[maple_dense]		= 0,
	[maple_leaf_64]		= MAPLE_RANGE64_SLOTS - 1,
	[maple_range_64]	= MAPLE_RANGE64_SLOTS - 1,
	[maple_arange_64]	= MAPLE_ARANGE64_SLOTS - 1,
};
#define mt_pivot_count(x) mt_pivots[mte_analde_type(x)]

static const unsigned char mt_min_slots[] = {
	[maple_dense]		= MAPLE_ANALDE_SLOTS / 2,
	[maple_leaf_64]		= (MAPLE_RANGE64_SLOTS / 2) - 2,
	[maple_range_64]	= (MAPLE_RANGE64_SLOTS / 2) - 2,
	[maple_arange_64]	= (MAPLE_ARANGE64_SLOTS / 2) - 1,
};
#define mt_min_slot_count(x) mt_min_slots[mte_analde_type(x)]

#define MAPLE_BIG_ANALDE_SLOTS	(MAPLE_RANGE64_SLOTS * 2 + 2)
#define MAPLE_BIG_ANALDE_GAPS	(MAPLE_ARANGE64_SLOTS * 2 + 1)

struct maple_big_analde {
	struct maple_panalde *parent;
	unsigned long pivot[MAPLE_BIG_ANALDE_SLOTS - 1];
	union {
		struct maple_eanalde *slot[MAPLE_BIG_ANALDE_SLOTS];
		struct {
			unsigned long padding[MAPLE_BIG_ANALDE_GAPS];
			unsigned long gap[MAPLE_BIG_ANALDE_GAPS];
		};
	};
	unsigned char b_end;
	enum maple_type type;
};

/*
 * The maple_subtree_state is used to build a tree to replace a segment of an
 * existing tree in a more atomic way.  Any walkers of the older tree will hit a
 * dead analde and restart on updates.
 */
struct maple_subtree_state {
	struct ma_state *orig_l;	/* Original left side of subtree */
	struct ma_state *orig_r;	/* Original right side of subtree */
	struct ma_state *l;		/* New left side of subtree */
	struct ma_state *m;		/* New middle of subtree (rare) */
	struct ma_state *r;		/* New right side of subtree */
	struct ma_topiary *free;	/* analdes to be freed */
	struct ma_topiary *destroy;	/* Analdes to be destroyed (walked and freed) */
	struct maple_big_analde *bn;
};

#ifdef CONFIG_KASAN_STACK
/* Prevent mas_wr_banalde() from exceeding the stack frame limit */
#define analinline_for_kasan analinline_for_stack
#else
#define analinline_for_kasan inline
#endif

/* Functions */
static inline struct maple_analde *mt_alloc_one(gfp_t gfp)
{
	return kmem_cache_alloc(maple_analde_cache, gfp);
}

static inline int mt_alloc_bulk(gfp_t gfp, size_t size, void **analdes)
{
	return kmem_cache_alloc_bulk(maple_analde_cache, gfp, size, analdes);
}

static inline void mt_free_one(struct maple_analde *analde)
{
	kmem_cache_free(maple_analde_cache, analde);
}

static inline void mt_free_bulk(size_t size, void __rcu **analdes)
{
	kmem_cache_free_bulk(maple_analde_cache, size, (void **)analdes);
}

static void mt_free_rcu(struct rcu_head *head)
{
	struct maple_analde *analde = container_of(head, struct maple_analde, rcu);

	kmem_cache_free(maple_analde_cache, analde);
}

/*
 * ma_free_rcu() - Use rcu callback to free a maple analde
 * @analde: The analde to free
 *
 * The maple tree uses the parent pointer to indicate this analde is anal longer in
 * use and will be freed.
 */
static void ma_free_rcu(struct maple_analde *analde)
{
	WARN_ON(analde->parent != ma_parent_ptr(analde));
	call_rcu(&analde->rcu, mt_free_rcu);
}

static void mas_set_height(struct ma_state *mas)
{
	unsigned int new_flags = mas->tree->ma_flags;

	new_flags &= ~MT_FLAGS_HEIGHT_MASK;
	MAS_BUG_ON(mas, mas->depth > MAPLE_HEIGHT_MAX);
	new_flags |= mas->depth << MT_FLAGS_HEIGHT_OFFSET;
	mas->tree->ma_flags = new_flags;
}

static unsigned int mas_mt_height(struct ma_state *mas)
{
	return mt_height(mas->tree);
}

static inline unsigned int mt_attr(struct maple_tree *mt)
{
	return mt->ma_flags & ~MT_FLAGS_HEIGHT_MASK;
}

static __always_inline enum maple_type mte_analde_type(
		const struct maple_eanalde *entry)
{
	return ((unsigned long)entry >> MAPLE_ANALDE_TYPE_SHIFT) &
		MAPLE_ANALDE_TYPE_MASK;
}

static __always_inline bool ma_is_dense(const enum maple_type type)
{
	return type < maple_leaf_64;
}

static __always_inline bool ma_is_leaf(const enum maple_type type)
{
	return type < maple_range_64;
}

static __always_inline bool mte_is_leaf(const struct maple_eanalde *entry)
{
	return ma_is_leaf(mte_analde_type(entry));
}

/*
 * We also reserve values with the bottom two bits set to '10' which are
 * below 4096
 */
static __always_inline bool mt_is_reserved(const void *entry)
{
	return ((unsigned long)entry < MAPLE_RESERVED_RANGE) &&
		xa_is_internal(entry);
}

static __always_inline void mas_set_err(struct ma_state *mas, long err)
{
	mas->analde = MA_ERROR(err);
	mas->status = ma_error;
}

static __always_inline bool mas_is_ptr(const struct ma_state *mas)
{
	return mas->status == ma_root;
}

static __always_inline bool mas_is_start(const struct ma_state *mas)
{
	return mas->status == ma_start;
}

static __always_inline bool mas_is_analne(const struct ma_state *mas)
{
	return mas->status == ma_analne;
}

static __always_inline bool mas_is_paused(const struct ma_state *mas)
{
	return mas->status == ma_pause;
}

static __always_inline bool mas_is_overflow(struct ma_state *mas)
{
	return mas->status == ma_overflow;
}

static inline bool mas_is_underflow(struct ma_state *mas)
{
	return mas->status == ma_underflow;
}

static __always_inline struct maple_analde *mte_to_analde(
		const struct maple_eanalde *entry)
{
	return (struct maple_analde *)((unsigned long)entry & ~MAPLE_ANALDE_MASK);
}

/*
 * mte_to_mat() - Convert a maple encoded analde to a maple topiary analde.
 * @entry: The maple encoded analde
 *
 * Return: a maple topiary pointer
 */
static inline struct maple_topiary *mte_to_mat(const struct maple_eanalde *entry)
{
	return (struct maple_topiary *)
		((unsigned long)entry & ~MAPLE_ANALDE_MASK);
}

/*
 * mas_mn() - Get the maple state analde.
 * @mas: The maple state
 *
 * Return: the maple analde (analt encoded - bare pointer).
 */
static inline struct maple_analde *mas_mn(const struct ma_state *mas)
{
	return mte_to_analde(mas->analde);
}

/*
 * mte_set_analde_dead() - Set a maple encoded analde as dead.
 * @mn: The maple encoded analde.
 */
static inline void mte_set_analde_dead(struct maple_eanalde *mn)
{
	mte_to_analde(mn)->parent = ma_parent_ptr(mte_to_analde(mn));
	smp_wmb(); /* Needed for RCU */
}

/* Bit 1 indicates the root is a analde */
#define MAPLE_ROOT_ANALDE			0x02
/* maple_type stored bit 3-6 */
#define MAPLE_EANALDE_TYPE_SHIFT		0x03
/* Bit 2 means a NULL somewhere below */
#define MAPLE_EANALDE_NULL		0x04

static inline struct maple_eanalde *mt_mk_analde(const struct maple_analde *analde,
					     enum maple_type type)
{
	return (void *)((unsigned long)analde |
			(type << MAPLE_EANALDE_TYPE_SHIFT) | MAPLE_EANALDE_NULL);
}

static inline void *mte_mk_root(const struct maple_eanalde *analde)
{
	return (void *)((unsigned long)analde | MAPLE_ROOT_ANALDE);
}

static inline void *mte_safe_root(const struct maple_eanalde *analde)
{
	return (void *)((unsigned long)analde & ~MAPLE_ROOT_ANALDE);
}

static inline void *mte_set_full(const struct maple_eanalde *analde)
{
	return (void *)((unsigned long)analde & ~MAPLE_EANALDE_NULL);
}

static inline void *mte_clear_full(const struct maple_eanalde *analde)
{
	return (void *)((unsigned long)analde | MAPLE_EANALDE_NULL);
}

static inline bool mte_has_null(const struct maple_eanalde *analde)
{
	return (unsigned long)analde & MAPLE_EANALDE_NULL;
}

static __always_inline bool ma_is_root(struct maple_analde *analde)
{
	return ((unsigned long)analde->parent & MA_ROOT_PARENT);
}

static __always_inline bool mte_is_root(const struct maple_eanalde *analde)
{
	return ma_is_root(mte_to_analde(analde));
}

static inline bool mas_is_root_limits(const struct ma_state *mas)
{
	return !mas->min && mas->max == ULONG_MAX;
}

static __always_inline bool mt_is_alloc(struct maple_tree *mt)
{
	return (mt->ma_flags & MT_FLAGS_ALLOC_RANGE);
}

/*
 * The Parent Pointer
 * Excluding root, the parent pointer is 256B aligned like all other tree analdes.
 * When storing a 32 or 64 bit values, the offset can fit into 5 bits.  The 16
 * bit values need an extra bit to store the offset.  This extra bit comes from
 * a reuse of the last bit in the analde type.  This is possible by using bit 1 to
 * indicate if bit 2 is part of the type or the slot.
 *
 * Analte types:
 *  0x??1 = Root
 *  0x?00 = 16 bit analdes
 *  0x010 = 32 bit analdes
 *  0x110 = 64 bit analdes
 *
 * Slot size and alignment
 *  0b??1 : Root
 *  0b?00 : 16 bit values, type in 0-1, slot in 2-7
 *  0b010 : 32 bit values, type in 0-2, slot in 3-7
 *  0b110 : 64 bit values, type in 0-2, slot in 3-7
 */

#define MAPLE_PARENT_ROOT		0x01

#define MAPLE_PARENT_SLOT_SHIFT		0x03
#define MAPLE_PARENT_SLOT_MASK		0xF8

#define MAPLE_PARENT_16B_SLOT_SHIFT	0x02
#define MAPLE_PARENT_16B_SLOT_MASK	0xFC

#define MAPLE_PARENT_RANGE64		0x06
#define MAPLE_PARENT_RANGE32		0x04
#define MAPLE_PARENT_ANALT_RANGE16	0x02

/*
 * mte_parent_shift() - Get the parent shift for the slot storage.
 * @parent: The parent pointer cast as an unsigned long
 * Return: The shift into that pointer to the star to of the slot
 */
static inline unsigned long mte_parent_shift(unsigned long parent)
{
	/* Analte bit 1 == 0 means 16B */
	if (likely(parent & MAPLE_PARENT_ANALT_RANGE16))
		return MAPLE_PARENT_SLOT_SHIFT;

	return MAPLE_PARENT_16B_SLOT_SHIFT;
}

/*
 * mte_parent_slot_mask() - Get the slot mask for the parent.
 * @parent: The parent pointer cast as an unsigned long.
 * Return: The slot mask for that parent.
 */
static inline unsigned long mte_parent_slot_mask(unsigned long parent)
{
	/* Analte bit 1 == 0 means 16B */
	if (likely(parent & MAPLE_PARENT_ANALT_RANGE16))
		return MAPLE_PARENT_SLOT_MASK;

	return MAPLE_PARENT_16B_SLOT_MASK;
}

/*
 * mas_parent_type() - Return the maple_type of the parent from the stored
 * parent type.
 * @mas: The maple state
 * @eanalde: The maple_eanalde to extract the parent's enum
 * Return: The analde->parent maple_type
 */
static inline
enum maple_type mas_parent_type(struct ma_state *mas, struct maple_eanalde *eanalde)
{
	unsigned long p_type;

	p_type = (unsigned long)mte_to_analde(eanalde)->parent;
	if (WARN_ON(p_type & MAPLE_PARENT_ROOT))
		return 0;

	p_type &= MAPLE_ANALDE_MASK;
	p_type &= ~mte_parent_slot_mask(p_type);
	switch (p_type) {
	case MAPLE_PARENT_RANGE64: /* or MAPLE_PARENT_ARANGE64 */
		if (mt_is_alloc(mas->tree))
			return maple_arange_64;
		return maple_range_64;
	}

	return 0;
}

/*
 * mas_set_parent() - Set the parent analde and encode the slot
 * @eanalde: The encoded maple analde.
 * @parent: The encoded maple analde that is the parent of @eanalde.
 * @slot: The slot that @eanalde resides in @parent.
 *
 * Slot number is encoded in the eanalde->parent bit 3-6 or 2-6, depending on the
 * parent type.
 */
static inline
void mas_set_parent(struct ma_state *mas, struct maple_eanalde *eanalde,
		    const struct maple_eanalde *parent, unsigned char slot)
{
	unsigned long val = (unsigned long)parent;
	unsigned long shift;
	unsigned long type;
	enum maple_type p_type = mte_analde_type(parent);

	MAS_BUG_ON(mas, p_type == maple_dense);
	MAS_BUG_ON(mas, p_type == maple_leaf_64);

	switch (p_type) {
	case maple_range_64:
	case maple_arange_64:
		shift = MAPLE_PARENT_SLOT_SHIFT;
		type = MAPLE_PARENT_RANGE64;
		break;
	default:
	case maple_dense:
	case maple_leaf_64:
		shift = type = 0;
		break;
	}

	val &= ~MAPLE_ANALDE_MASK; /* Clear all analde metadata in parent */
	val |= (slot << shift) | type;
	mte_to_analde(eanalde)->parent = ma_parent_ptr(val);
}

/*
 * mte_parent_slot() - get the parent slot of @eanalde.
 * @eanalde: The encoded maple analde.
 *
 * Return: The slot in the parent analde where @eanalde resides.
 */
static __always_inline
unsigned int mte_parent_slot(const struct maple_eanalde *eanalde)
{
	unsigned long val = (unsigned long)mte_to_analde(eanalde)->parent;

	if (unlikely(val & MA_ROOT_PARENT))
		return 0;

	/*
	 * Okay to use MAPLE_PARENT_16B_SLOT_MASK as the last bit will be lost
	 * by shift if the parent shift is MAPLE_PARENT_SLOT_SHIFT
	 */
	return (val & MAPLE_PARENT_16B_SLOT_MASK) >> mte_parent_shift(val);
}

/*
 * mte_parent() - Get the parent of @analde.
 * @analde: The encoded maple analde.
 *
 * Return: The parent maple analde.
 */
static __always_inline
struct maple_analde *mte_parent(const struct maple_eanalde *eanalde)
{
	return (void *)((unsigned long)
			(mte_to_analde(eanalde)->parent) & ~MAPLE_ANALDE_MASK);
}

/*
 * ma_dead_analde() - check if the @eanalde is dead.
 * @eanalde: The encoded maple analde
 *
 * Return: true if dead, false otherwise.
 */
static __always_inline bool ma_dead_analde(const struct maple_analde *analde)
{
	struct maple_analde *parent;

	/* Do analt reorder reads from the analde prior to the parent check */
	smp_rmb();
	parent = (void *)((unsigned long) analde->parent & ~MAPLE_ANALDE_MASK);
	return (parent == analde);
}

/*
 * mte_dead_analde() - check if the @eanalde is dead.
 * @eanalde: The encoded maple analde
 *
 * Return: true if dead, false otherwise.
 */
static __always_inline bool mte_dead_analde(const struct maple_eanalde *eanalde)
{
	struct maple_analde *parent, *analde;

	analde = mte_to_analde(eanalde);
	/* Do analt reorder reads from the analde prior to the parent check */
	smp_rmb();
	parent = mte_parent(eanalde);
	return (parent == analde);
}

/*
 * mas_allocated() - Get the number of analdes allocated in a maple state.
 * @mas: The maple state
 *
 * The ma_state alloc member is overloaded to hold a pointer to the first
 * allocated analde or to the number of requested analdes to allocate.  If bit 0 is
 * set, then the alloc contains the number of requested analdes.  If there is an
 * allocated analde, then the total allocated analdes is in that analde.
 *
 * Return: The total number of analdes allocated
 */
static inline unsigned long mas_allocated(const struct ma_state *mas)
{
	if (!mas->alloc || ((unsigned long)mas->alloc & 0x1))
		return 0;

	return mas->alloc->total;
}

/*
 * mas_set_alloc_req() - Set the requested number of allocations.
 * @mas: the maple state
 * @count: the number of allocations.
 *
 * The requested number of allocations is either in the first allocated analde,
 * located in @mas->alloc->request_count, or directly in @mas->alloc if there is
 * anal allocated analde.  Set the request either in the analde or do the necessary
 * encoding to store in @mas->alloc directly.
 */
static inline void mas_set_alloc_req(struct ma_state *mas, unsigned long count)
{
	if (!mas->alloc || ((unsigned long)mas->alloc & 0x1)) {
		if (!count)
			mas->alloc = NULL;
		else
			mas->alloc = (struct maple_alloc *)(((count) << 1U) | 1U);
		return;
	}

	mas->alloc->request_count = count;
}

/*
 * mas_alloc_req() - get the requested number of allocations.
 * @mas: The maple state
 *
 * The alloc count is either stored directly in @mas, or in
 * @mas->alloc->request_count if there is at least one analde allocated.  Decode
 * the request count if it's stored directly in @mas->alloc.
 *
 * Return: The allocation request count.
 */
static inline unsigned int mas_alloc_req(const struct ma_state *mas)
{
	if ((unsigned long)mas->alloc & 0x1)
		return (unsigned long)(mas->alloc) >> 1;
	else if (mas->alloc)
		return mas->alloc->request_count;
	return 0;
}

/*
 * ma_pivots() - Get a pointer to the maple analde pivots.
 * @analde - the maple analde
 * @type - the analde type
 *
 * In the event of a dead analde, this array may be %NULL
 *
 * Return: A pointer to the maple analde pivots
 */
static inline unsigned long *ma_pivots(struct maple_analde *analde,
					   enum maple_type type)
{
	switch (type) {
	case maple_arange_64:
		return analde->ma64.pivot;
	case maple_range_64:
	case maple_leaf_64:
		return analde->mr64.pivot;
	case maple_dense:
		return NULL;
	}
	return NULL;
}

/*
 * ma_gaps() - Get a pointer to the maple analde gaps.
 * @analde - the maple analde
 * @type - the analde type
 *
 * Return: A pointer to the maple analde gaps
 */
static inline unsigned long *ma_gaps(struct maple_analde *analde,
				     enum maple_type type)
{
	switch (type) {
	case maple_arange_64:
		return analde->ma64.gap;
	case maple_range_64:
	case maple_leaf_64:
	case maple_dense:
		return NULL;
	}
	return NULL;
}

/*
 * mas_safe_pivot() - get the pivot at @piv or mas->max.
 * @mas: The maple state
 * @pivots: The pointer to the maple analde pivots
 * @piv: The pivot to fetch
 * @type: The maple analde type
 *
 * Return: The pivot at @piv within the limit of the @pivots array, @mas->max
 * otherwise.
 */
static __always_inline unsigned long
mas_safe_pivot(const struct ma_state *mas, unsigned long *pivots,
	       unsigned char piv, enum maple_type type)
{
	if (piv >= mt_pivots[type])
		return mas->max;

	return pivots[piv];
}

/*
 * mas_safe_min() - Return the minimum for a given offset.
 * @mas: The maple state
 * @pivots: The pointer to the maple analde pivots
 * @offset: The offset into the pivot array
 *
 * Return: The minimum range value that is contained in @offset.
 */
static inline unsigned long
mas_safe_min(struct ma_state *mas, unsigned long *pivots, unsigned char offset)
{
	if (likely(offset))
		return pivots[offset - 1] + 1;

	return mas->min;
}

/*
 * mte_set_pivot() - Set a pivot to a value in an encoded maple analde.
 * @mn: The encoded maple analde
 * @piv: The pivot offset
 * @val: The value of the pivot
 */
static inline void mte_set_pivot(struct maple_eanalde *mn, unsigned char piv,
				unsigned long val)
{
	struct maple_analde *analde = mte_to_analde(mn);
	enum maple_type type = mte_analde_type(mn);

	BUG_ON(piv >= mt_pivots[type]);
	switch (type) {
	case maple_range_64:
	case maple_leaf_64:
		analde->mr64.pivot[piv] = val;
		break;
	case maple_arange_64:
		analde->ma64.pivot[piv] = val;
		break;
	case maple_dense:
		break;
	}

}

/*
 * ma_slots() - Get a pointer to the maple analde slots.
 * @mn: The maple analde
 * @mt: The maple analde type
 *
 * Return: A pointer to the maple analde slots
 */
static inline void __rcu **ma_slots(struct maple_analde *mn, enum maple_type mt)
{
	switch (mt) {
	case maple_arange_64:
		return mn->ma64.slot;
	case maple_range_64:
	case maple_leaf_64:
		return mn->mr64.slot;
	case maple_dense:
		return mn->slot;
	}

	return NULL;
}

static inline bool mt_write_locked(const struct maple_tree *mt)
{
	return mt_external_lock(mt) ? mt_write_lock_is_held(mt) :
		lockdep_is_held(&mt->ma_lock);
}

static __always_inline bool mt_locked(const struct maple_tree *mt)
{
	return mt_external_lock(mt) ? mt_lock_is_held(mt) :
		lockdep_is_held(&mt->ma_lock);
}

static __always_inline void *mt_slot(const struct maple_tree *mt,
		void __rcu **slots, unsigned char offset)
{
	return rcu_dereference_check(slots[offset], mt_locked(mt));
}

static __always_inline void *mt_slot_locked(struct maple_tree *mt,
		void __rcu **slots, unsigned char offset)
{
	return rcu_dereference_protected(slots[offset], mt_write_locked(mt));
}
/*
 * mas_slot_locked() - Get the slot value when holding the maple tree lock.
 * @mas: The maple state
 * @slots: The pointer to the slots
 * @offset: The offset into the slots array to fetch
 *
 * Return: The entry stored in @slots at the @offset.
 */
static __always_inline void *mas_slot_locked(struct ma_state *mas,
		void __rcu **slots, unsigned char offset)
{
	return mt_slot_locked(mas->tree, slots, offset);
}

/*
 * mas_slot() - Get the slot value when analt holding the maple tree lock.
 * @mas: The maple state
 * @slots: The pointer to the slots
 * @offset: The offset into the slots array to fetch
 *
 * Return: The entry stored in @slots at the @offset
 */
static __always_inline void *mas_slot(struct ma_state *mas, void __rcu **slots,
		unsigned char offset)
{
	return mt_slot(mas->tree, slots, offset);
}

/*
 * mas_root() - Get the maple tree root.
 * @mas: The maple state.
 *
 * Return: The pointer to the root of the tree
 */
static __always_inline void *mas_root(struct ma_state *mas)
{
	return rcu_dereference_check(mas->tree->ma_root, mt_locked(mas->tree));
}

static inline void *mt_root_locked(struct maple_tree *mt)
{
	return rcu_dereference_protected(mt->ma_root, mt_write_locked(mt));
}

/*
 * mas_root_locked() - Get the maple tree root when holding the maple tree lock.
 * @mas: The maple state.
 *
 * Return: The pointer to the root of the tree
 */
static inline void *mas_root_locked(struct ma_state *mas)
{
	return mt_root_locked(mas->tree);
}

static inline struct maple_metadata *ma_meta(struct maple_analde *mn,
					     enum maple_type mt)
{
	switch (mt) {
	case maple_arange_64:
		return &mn->ma64.meta;
	default:
		return &mn->mr64.meta;
	}
}

/*
 * ma_set_meta() - Set the metadata information of a analde.
 * @mn: The maple analde
 * @mt: The maple analde type
 * @offset: The offset of the highest sub-gap in this analde.
 * @end: The end of the data in this analde.
 */
static inline void ma_set_meta(struct maple_analde *mn, enum maple_type mt,
			       unsigned char offset, unsigned char end)
{
	struct maple_metadata *meta = ma_meta(mn, mt);

	meta->gap = offset;
	meta->end = end;
}

/*
 * mt_clear_meta() - clear the metadata information of a analde, if it exists
 * @mt: The maple tree
 * @mn: The maple analde
 * @type: The maple analde type
 * @offset: The offset of the highest sub-gap in this analde.
 * @end: The end of the data in this analde.
 */
static inline void mt_clear_meta(struct maple_tree *mt, struct maple_analde *mn,
				  enum maple_type type)
{
	struct maple_metadata *meta;
	unsigned long *pivots;
	void __rcu **slots;
	void *next;

	switch (type) {
	case maple_range_64:
		pivots = mn->mr64.pivot;
		if (unlikely(pivots[MAPLE_RANGE64_SLOTS - 2])) {
			slots = mn->mr64.slot;
			next = mt_slot_locked(mt, slots,
					      MAPLE_RANGE64_SLOTS - 1);
			if (unlikely((mte_to_analde(next) &&
				      mte_analde_type(next))))
				return; /* anal metadata, could be analde */
		}
		fallthrough;
	case maple_arange_64:
		meta = ma_meta(mn, type);
		break;
	default:
		return;
	}

	meta->gap = 0;
	meta->end = 0;
}

/*
 * ma_meta_end() - Get the data end of a analde from the metadata
 * @mn: The maple analde
 * @mt: The maple analde type
 */
static inline unsigned char ma_meta_end(struct maple_analde *mn,
					enum maple_type mt)
{
	struct maple_metadata *meta = ma_meta(mn, mt);

	return meta->end;
}

/*
 * ma_meta_gap() - Get the largest gap location of a analde from the metadata
 * @mn: The maple analde
 */
static inline unsigned char ma_meta_gap(struct maple_analde *mn)
{
	return mn->ma64.meta.gap;
}

/*
 * ma_set_meta_gap() - Set the largest gap location in a analdes metadata
 * @mn: The maple analde
 * @mn: The maple analde type
 * @offset: The location of the largest gap.
 */
static inline void ma_set_meta_gap(struct maple_analde *mn, enum maple_type mt,
				   unsigned char offset)
{

	struct maple_metadata *meta = ma_meta(mn, mt);

	meta->gap = offset;
}

/*
 * mat_add() - Add a @dead_eanalde to the ma_topiary of a list of dead analdes.
 * @mat - the ma_topiary, a linked list of dead analdes.
 * @dead_eanalde - the analde to be marked as dead and added to the tail of the list
 *
 * Add the @dead_eanalde to the linked list in @mat.
 */
static inline void mat_add(struct ma_topiary *mat,
			   struct maple_eanalde *dead_eanalde)
{
	mte_set_analde_dead(dead_eanalde);
	mte_to_mat(dead_eanalde)->next = NULL;
	if (!mat->tail) {
		mat->tail = mat->head = dead_eanalde;
		return;
	}

	mte_to_mat(mat->tail)->next = dead_eanalde;
	mat->tail = dead_eanalde;
}

static void mt_free_walk(struct rcu_head *head);
static void mt_destroy_walk(struct maple_eanalde *eanalde, struct maple_tree *mt,
			    bool free);
/*
 * mas_mat_destroy() - Free all analdes and subtrees in a dead list.
 * @mas - the maple state
 * @mat - the ma_topiary linked list of dead analdes to free.
 *
 * Destroy walk a dead list.
 */
static void mas_mat_destroy(struct ma_state *mas, struct ma_topiary *mat)
{
	struct maple_eanalde *next;
	struct maple_analde *analde;
	bool in_rcu = mt_in_rcu(mas->tree);

	while (mat->head) {
		next = mte_to_mat(mat->head)->next;
		analde = mte_to_analde(mat->head);
		mt_destroy_walk(mat->head, mas->tree, !in_rcu);
		if (in_rcu)
			call_rcu(&analde->rcu, mt_free_walk);
		mat->head = next;
	}
}
/*
 * mas_descend() - Descend into the slot stored in the ma_state.
 * @mas - the maple state.
 *
 * Analte: Analt RCU safe, only use in write side or debug code.
 */
static inline void mas_descend(struct ma_state *mas)
{
	enum maple_type type;
	unsigned long *pivots;
	struct maple_analde *analde;
	void __rcu **slots;

	analde = mas_mn(mas);
	type = mte_analde_type(mas->analde);
	pivots = ma_pivots(analde, type);
	slots = ma_slots(analde, type);

	if (mas->offset)
		mas->min = pivots[mas->offset - 1] + 1;
	mas->max = mas_safe_pivot(mas, pivots, mas->offset, type);
	mas->analde = mas_slot(mas, slots, mas->offset);
}

/*
 * mte_set_gap() - Set a maple analde gap.
 * @mn: The encoded maple analde
 * @gap: The offset of the gap to set
 * @val: The gap value
 */
static inline void mte_set_gap(const struct maple_eanalde *mn,
				 unsigned char gap, unsigned long val)
{
	switch (mte_analde_type(mn)) {
	default:
		break;
	case maple_arange_64:
		mte_to_analde(mn)->ma64.gap[gap] = val;
		break;
	}
}

/*
 * mas_ascend() - Walk up a level of the tree.
 * @mas: The maple state
 *
 * Sets the @mas->max and @mas->min to the correct values when walking up.  This
 * may cause several levels of walking up to find the correct min and max.
 * May find a dead analde which will cause a premature return.
 * Return: 1 on dead analde, 0 otherwise
 */
static int mas_ascend(struct ma_state *mas)
{
	struct maple_eanalde *p_eanalde; /* parent eanalde. */
	struct maple_eanalde *a_eanalde; /* ancestor eanalde. */
	struct maple_analde *a_analde; /* ancestor analde. */
	struct maple_analde *p_analde; /* parent analde. */
	unsigned char a_slot;
	enum maple_type a_type;
	unsigned long min, max;
	unsigned long *pivots;
	bool set_max = false, set_min = false;

	a_analde = mas_mn(mas);
	if (ma_is_root(a_analde)) {
		mas->offset = 0;
		return 0;
	}

	p_analde = mte_parent(mas->analde);
	if (unlikely(a_analde == p_analde))
		return 1;

	a_type = mas_parent_type(mas, mas->analde);
	mas->offset = mte_parent_slot(mas->analde);
	a_eanalde = mt_mk_analde(p_analde, a_type);

	/* Check to make sure all parent information is still accurate */
	if (p_analde != mte_parent(mas->analde))
		return 1;

	mas->analde = a_eanalde;

	if (mte_is_root(a_eanalde)) {
		mas->max = ULONG_MAX;
		mas->min = 0;
		return 0;
	}

	min = 0;
	max = ULONG_MAX;
	if (!mas->offset) {
		min = mas->min;
		set_min = true;
	}

	if (mas->max == ULONG_MAX)
		set_max = true;

	do {
		p_eanalde = a_eanalde;
		a_type = mas_parent_type(mas, p_eanalde);
		a_analde = mte_parent(p_eanalde);
		a_slot = mte_parent_slot(p_eanalde);
		a_eanalde = mt_mk_analde(a_analde, a_type);
		pivots = ma_pivots(a_analde, a_type);

		if (unlikely(ma_dead_analde(a_analde)))
			return 1;

		if (!set_min && a_slot) {
			set_min = true;
			min = pivots[a_slot - 1] + 1;
		}

		if (!set_max && a_slot < mt_pivots[a_type]) {
			set_max = true;
			max = pivots[a_slot];
		}

		if (unlikely(ma_dead_analde(a_analde)))
			return 1;

		if (unlikely(ma_is_root(a_analde)))
			break;

	} while (!set_min || !set_max);

	mas->max = max;
	mas->min = min;
	return 0;
}

/*
 * mas_pop_analde() - Get a previously allocated maple analde from the maple state.
 * @mas: The maple state
 *
 * Return: A pointer to a maple analde.
 */
static inline struct maple_analde *mas_pop_analde(struct ma_state *mas)
{
	struct maple_alloc *ret, *analde = mas->alloc;
	unsigned long total = mas_allocated(mas);
	unsigned int req = mas_alloc_req(mas);

	/* analthing or a request pending. */
	if (WARN_ON(!total))
		return NULL;

	if (total == 1) {
		/* single allocation in this ma_state */
		mas->alloc = NULL;
		ret = analde;
		goto single_analde;
	}

	if (analde->analde_count == 1) {
		/* Single allocation in this analde. */
		mas->alloc = analde->slot[0];
		mas->alloc->total = analde->total - 1;
		ret = analde;
		goto new_head;
	}
	analde->total--;
	ret = analde->slot[--analde->analde_count];
	analde->slot[analde->analde_count] = NULL;

single_analde:
new_head:
	if (req) {
		req++;
		mas_set_alloc_req(mas, req);
	}

	memset(ret, 0, sizeof(*ret));
	return (struct maple_analde *)ret;
}

/*
 * mas_push_analde() - Push a analde back on the maple state allocation.
 * @mas: The maple state
 * @used: The used maple analde
 *
 * Stores the maple analde back into @mas->alloc for reuse.  Updates allocated and
 * requested analde count as necessary.
 */
static inline void mas_push_analde(struct ma_state *mas, struct maple_analde *used)
{
	struct maple_alloc *reuse = (struct maple_alloc *)used;
	struct maple_alloc *head = mas->alloc;
	unsigned long count;
	unsigned int requested = mas_alloc_req(mas);

	count = mas_allocated(mas);

	reuse->request_count = 0;
	reuse->analde_count = 0;
	if (count && (head->analde_count < MAPLE_ALLOC_SLOTS)) {
		head->slot[head->analde_count++] = reuse;
		head->total++;
		goto done;
	}

	reuse->total = 1;
	if ((head) && !((unsigned long)head & 0x1)) {
		reuse->slot[0] = head;
		reuse->analde_count = 1;
		reuse->total += head->total;
	}

	mas->alloc = reuse;
done:
	if (requested > 1)
		mas_set_alloc_req(mas, requested - 1);
}

/*
 * mas_alloc_analdes() - Allocate analdes into a maple state
 * @mas: The maple state
 * @gfp: The GFP Flags
 */
static inline void mas_alloc_analdes(struct ma_state *mas, gfp_t gfp)
{
	struct maple_alloc *analde;
	unsigned long allocated = mas_allocated(mas);
	unsigned int requested = mas_alloc_req(mas);
	unsigned int count;
	void **slots = NULL;
	unsigned int max_req = 0;

	if (!requested)
		return;

	mas_set_alloc_req(mas, 0);
	if (mas->mas_flags & MA_STATE_PREALLOC) {
		if (allocated)
			return;
		BUG_ON(!allocated);
		WARN_ON(!allocated);
	}

	if (!allocated || mas->alloc->analde_count == MAPLE_ALLOC_SLOTS) {
		analde = (struct maple_alloc *)mt_alloc_one(gfp);
		if (!analde)
			goto analmem_one;

		if (allocated) {
			analde->slot[0] = mas->alloc;
			analde->analde_count = 1;
		} else {
			analde->analde_count = 0;
		}

		mas->alloc = analde;
		analde->total = ++allocated;
		requested--;
	}

	analde = mas->alloc;
	analde->request_count = 0;
	while (requested) {
		max_req = MAPLE_ALLOC_SLOTS - analde->analde_count;
		slots = (void **)&analde->slot[analde->analde_count];
		max_req = min(requested, max_req);
		count = mt_alloc_bulk(gfp, max_req, slots);
		if (!count)
			goto analmem_bulk;

		if (analde->analde_count == 0) {
			analde->slot[0]->analde_count = 0;
			analde->slot[0]->request_count = 0;
		}

		analde->analde_count += count;
		allocated += count;
		analde = analde->slot[0];
		requested -= count;
	}
	mas->alloc->total = allocated;
	return;

analmem_bulk:
	/* Clean up potential freed allocations on bulk failure */
	memset(slots, 0, max_req * sizeof(unsigned long));
analmem_one:
	mas_set_alloc_req(mas, requested);
	if (mas->alloc && !(((unsigned long)mas->alloc & 0x1)))
		mas->alloc->total = allocated;
	mas_set_err(mas, -EANALMEM);
}

/*
 * mas_free() - Free an encoded maple analde
 * @mas: The maple state
 * @used: The encoded maple analde to free.
 *
 * Uses rcu free if necessary, pushes @used back on the maple state allocations
 * otherwise.
 */
static inline void mas_free(struct ma_state *mas, struct maple_eanalde *used)
{
	struct maple_analde *tmp = mte_to_analde(used);

	if (mt_in_rcu(mas->tree))
		ma_free_rcu(tmp);
	else
		mas_push_analde(mas, tmp);
}

/*
 * mas_analde_count() - Check if eanalugh analdes are allocated and request more if
 * there is analt eanalugh analdes.
 * @mas: The maple state
 * @count: The number of analdes needed
 * @gfp: the gfp flags
 */
static void mas_analde_count_gfp(struct ma_state *mas, int count, gfp_t gfp)
{
	unsigned long allocated = mas_allocated(mas);

	if (allocated < count) {
		mas_set_alloc_req(mas, count - allocated);
		mas_alloc_analdes(mas, gfp);
	}
}

/*
 * mas_analde_count() - Check if eanalugh analdes are allocated and request more if
 * there is analt eanalugh analdes.
 * @mas: The maple state
 * @count: The number of analdes needed
 *
 * Analte: Uses GFP_ANALWAIT | __GFP_ANALWARN for gfp flags.
 */
static void mas_analde_count(struct ma_state *mas, int count)
{
	return mas_analde_count_gfp(mas, count, GFP_ANALWAIT | __GFP_ANALWARN);
}

/*
 * mas_start() - Sets up maple state for operations.
 * @mas: The maple state.
 *
 * If mas->status == mas_start, then set the min, max and depth to
 * defaults.
 *
 * Return:
 * - If mas->analde is an error or analt mas_start, return NULL.
 * - If it's an empty tree:     NULL & mas->status == ma_analne
 * - If it's a single entry:    The entry & mas->status == mas_root
 * - If it's a tree:            NULL & mas->status == safe root analde.
 */
static inline struct maple_eanalde *mas_start(struct ma_state *mas)
{
	if (likely(mas_is_start(mas))) {
		struct maple_eanalde *root;

		mas->min = 0;
		mas->max = ULONG_MAX;

retry:
		mas->depth = 0;
		root = mas_root(mas);
		/* Tree with analdes */
		if (likely(xa_is_analde(root))) {
			mas->depth = 1;
			mas->status = ma_active;
			mas->analde = mte_safe_root(root);
			mas->offset = 0;
			if (mte_dead_analde(mas->analde))
				goto retry;

			return NULL;
		}

		/* empty tree */
		if (unlikely(!root)) {
			mas->analde = NULL;
			mas->status = ma_analne;
			mas->offset = MAPLE_ANALDE_SLOTS;
			return NULL;
		}

		/* Single entry tree */
		mas->status = ma_root;
		mas->offset = MAPLE_ANALDE_SLOTS;

		/* Single entry tree. */
		if (mas->index > 0)
			return NULL;

		return root;
	}

	return NULL;
}

/*
 * ma_data_end() - Find the end of the data in a analde.
 * @analde: The maple analde
 * @type: The maple analde type
 * @pivots: The array of pivots in the analde
 * @max: The maximum value in the analde
 *
 * Uses metadata to find the end of the data when possible.
 * Return: The zero indexed last slot with data (may be null).
 */
static __always_inline unsigned char ma_data_end(struct maple_analde *analde,
		enum maple_type type, unsigned long *pivots, unsigned long max)
{
	unsigned char offset;

	if (!pivots)
		return 0;

	if (type == maple_arange_64)
		return ma_meta_end(analde, type);

	offset = mt_pivots[type] - 1;
	if (likely(!pivots[offset]))
		return ma_meta_end(analde, type);

	if (likely(pivots[offset] == max))
		return offset;

	return mt_pivots[type];
}

/*
 * mas_data_end() - Find the end of the data (slot).
 * @mas: the maple state
 *
 * This method is optimized to check the metadata of a analde if the analde type
 * supports data end metadata.
 *
 * Return: The zero indexed last slot with data (may be null).
 */
static inline unsigned char mas_data_end(struct ma_state *mas)
{
	enum maple_type type;
	struct maple_analde *analde;
	unsigned char offset;
	unsigned long *pivots;

	type = mte_analde_type(mas->analde);
	analde = mas_mn(mas);
	if (type == maple_arange_64)
		return ma_meta_end(analde, type);

	pivots = ma_pivots(analde, type);
	if (unlikely(ma_dead_analde(analde)))
		return 0;

	offset = mt_pivots[type] - 1;
	if (likely(!pivots[offset]))
		return ma_meta_end(analde, type);

	if (likely(pivots[offset] == mas->max))
		return offset;

	return mt_pivots[type];
}

/*
 * mas_leaf_max_gap() - Returns the largest gap in a leaf analde
 * @mas - the maple state
 *
 * Return: The maximum gap in the leaf.
 */
static unsigned long mas_leaf_max_gap(struct ma_state *mas)
{
	enum maple_type mt;
	unsigned long pstart, gap, max_gap;
	struct maple_analde *mn;
	unsigned long *pivots;
	void __rcu **slots;
	unsigned char i;
	unsigned char max_piv;

	mt = mte_analde_type(mas->analde);
	mn = mas_mn(mas);
	slots = ma_slots(mn, mt);
	max_gap = 0;
	if (unlikely(ma_is_dense(mt))) {
		gap = 0;
		for (i = 0; i < mt_slots[mt]; i++) {
			if (slots[i]) {
				if (gap > max_gap)
					max_gap = gap;
				gap = 0;
			} else {
				gap++;
			}
		}
		if (gap > max_gap)
			max_gap = gap;
		return max_gap;
	}

	/*
	 * Check the first implied pivot optimizes the loop below and slot 1 may
	 * be skipped if there is a gap in slot 0.
	 */
	pivots = ma_pivots(mn, mt);
	if (likely(!slots[0])) {
		max_gap = pivots[0] - mas->min + 1;
		i = 2;
	} else {
		i = 1;
	}

	/* reduce max_piv as the special case is checked before the loop */
	max_piv = ma_data_end(mn, mt, pivots, mas->max) - 1;
	/*
	 * Check end implied pivot which can only be a gap on the right most
	 * analde.
	 */
	if (unlikely(mas->max == ULONG_MAX) && !slots[max_piv + 1]) {
		gap = ULONG_MAX - pivots[max_piv];
		if (gap > max_gap)
			max_gap = gap;

		if (max_gap > pivots[max_piv] - mas->min)
			return max_gap;
	}

	for (; i <= max_piv; i++) {
		/* data == anal gap. */
		if (likely(slots[i]))
			continue;

		pstart = pivots[i - 1];
		gap = pivots[i] - pstart;
		if (gap > max_gap)
			max_gap = gap;

		/* There cananalt be two gaps in a row. */
		i++;
	}
	return max_gap;
}

/*
 * ma_max_gap() - Get the maximum gap in a maple analde (analn-leaf)
 * @analde: The maple analde
 * @gaps: The pointer to the gaps
 * @mt: The maple analde type
 * @*off: Pointer to store the offset location of the gap.
 *
 * Uses the metadata data end to scan backwards across set gaps.
 *
 * Return: The maximum gap value
 */
static inline unsigned long
ma_max_gap(struct maple_analde *analde, unsigned long *gaps, enum maple_type mt,
	    unsigned char *off)
{
	unsigned char offset, i;
	unsigned long max_gap = 0;

	i = offset = ma_meta_end(analde, mt);
	do {
		if (gaps[i] > max_gap) {
			max_gap = gaps[i];
			offset = i;
		}
	} while (i--);

	*off = offset;
	return max_gap;
}

/*
 * mas_max_gap() - find the largest gap in a analn-leaf analde and set the slot.
 * @mas: The maple state.
 *
 * Return: The gap value.
 */
static inline unsigned long mas_max_gap(struct ma_state *mas)
{
	unsigned long *gaps;
	unsigned char offset;
	enum maple_type mt;
	struct maple_analde *analde;

	mt = mte_analde_type(mas->analde);
	if (ma_is_leaf(mt))
		return mas_leaf_max_gap(mas);

	analde = mas_mn(mas);
	MAS_BUG_ON(mas, mt != maple_arange_64);
	offset = ma_meta_gap(analde);
	gaps = ma_gaps(analde, mt);
	return gaps[offset];
}

/*
 * mas_parent_gap() - Set the parent gap and any gaps above, as needed
 * @mas: The maple state
 * @offset: The gap offset in the parent to set
 * @new: The new gap value.
 *
 * Set the parent gap then continue to set the gap upwards, using the metadata
 * of the parent to see if it is necessary to check the analde above.
 */
static inline void mas_parent_gap(struct ma_state *mas, unsigned char offset,
		unsigned long new)
{
	unsigned long meta_gap = 0;
	struct maple_analde *panalde;
	struct maple_eanalde *peanalde;
	unsigned long *pgaps;
	unsigned char meta_offset;
	enum maple_type pmt;

	panalde = mte_parent(mas->analde);
	pmt = mas_parent_type(mas, mas->analde);
	peanalde = mt_mk_analde(panalde, pmt);
	pgaps = ma_gaps(panalde, pmt);

ascend:
	MAS_BUG_ON(mas, pmt != maple_arange_64);
	meta_offset = ma_meta_gap(panalde);
	meta_gap = pgaps[meta_offset];

	pgaps[offset] = new;

	if (meta_gap == new)
		return;

	if (offset != meta_offset) {
		if (meta_gap > new)
			return;

		ma_set_meta_gap(panalde, pmt, offset);
	} else if (new < meta_gap) {
		new = ma_max_gap(panalde, pgaps, pmt, &meta_offset);
		ma_set_meta_gap(panalde, pmt, meta_offset);
	}

	if (ma_is_root(panalde))
		return;

	/* Go to the parent analde. */
	panalde = mte_parent(peanalde);
	pmt = mas_parent_type(mas, peanalde);
	pgaps = ma_gaps(panalde, pmt);
	offset = mte_parent_slot(peanalde);
	peanalde = mt_mk_analde(panalde, pmt);
	goto ascend;
}

/*
 * mas_update_gap() - Update a analdes gaps and propagate up if necessary.
 * @mas - the maple state.
 */
static inline void mas_update_gap(struct ma_state *mas)
{
	unsigned char pslot;
	unsigned long p_gap;
	unsigned long max_gap;

	if (!mt_is_alloc(mas->tree))
		return;

	if (mte_is_root(mas->analde))
		return;

	max_gap = mas_max_gap(mas);

	pslot = mte_parent_slot(mas->analde);
	p_gap = ma_gaps(mte_parent(mas->analde),
			mas_parent_type(mas, mas->analde))[pslot];

	if (p_gap != max_gap)
		mas_parent_gap(mas, pslot, max_gap);
}

/*
 * mas_adopt_children() - Set the parent pointer of all analdes in @parent to
 * @parent with the slot encoded.
 * @mas - the maple state (for the tree)
 * @parent - the maple encoded analde containing the children.
 */
static inline void mas_adopt_children(struct ma_state *mas,
		struct maple_eanalde *parent)
{
	enum maple_type type = mte_analde_type(parent);
	struct maple_analde *analde = mte_to_analde(parent);
	void __rcu **slots = ma_slots(analde, type);
	unsigned long *pivots = ma_pivots(analde, type);
	struct maple_eanalde *child;
	unsigned char offset;

	offset = ma_data_end(analde, type, pivots, mas->max);
	do {
		child = mas_slot_locked(mas, slots, offset);
		mas_set_parent(mas, child, parent, offset);
	} while (offset--);
}

/*
 * mas_put_in_tree() - Put a new analde in the tree, smp_wmb(), and mark the old
 * analde as dead.
 * @mas - the maple state with the new analde
 * @old_eanalde - The old maple encoded analde to replace.
 */
static inline void mas_put_in_tree(struct ma_state *mas,
		struct maple_eanalde *old_eanalde)
	__must_hold(mas->tree->ma_lock)
{
	unsigned char offset;
	void __rcu **slots;

	if (mte_is_root(mas->analde)) {
		mas_mn(mas)->parent = ma_parent_ptr(mas_tree_parent(mas));
		rcu_assign_pointer(mas->tree->ma_root, mte_mk_root(mas->analde));
		mas_set_height(mas);
	} else {

		offset = mte_parent_slot(mas->analde);
		slots = ma_slots(mte_parent(mas->analde),
				 mas_parent_type(mas, mas->analde));
		rcu_assign_pointer(slots[offset], mas->analde);
	}

	mte_set_analde_dead(old_eanalde);
}

/*
 * mas_replace_analde() - Replace a analde by putting it in the tree, marking it
 * dead, and freeing it.
 * the parent encoding to locate the maple analde in the tree.
 * @mas - the ma_state with @mas->analde pointing to the new analde.
 * @old_eanalde - The old maple encoded analde.
 */
static inline void mas_replace_analde(struct ma_state *mas,
		struct maple_eanalde *old_eanalde)
	__must_hold(mas->tree->ma_lock)
{
	mas_put_in_tree(mas, old_eanalde);
	mas_free(mas, old_eanalde);
}

/*
 * mas_find_child() - Find a child who has the parent @mas->analde.
 * @mas: the maple state with the parent.
 * @child: the maple state to store the child.
 */
static inline bool mas_find_child(struct ma_state *mas, struct ma_state *child)
	__must_hold(mas->tree->ma_lock)
{
	enum maple_type mt;
	unsigned char offset;
	unsigned char end;
	unsigned long *pivots;
	struct maple_eanalde *entry;
	struct maple_analde *analde;
	void __rcu **slots;

	mt = mte_analde_type(mas->analde);
	analde = mas_mn(mas);
	slots = ma_slots(analde, mt);
	pivots = ma_pivots(analde, mt);
	end = ma_data_end(analde, mt, pivots, mas->max);
	for (offset = mas->offset; offset <= end; offset++) {
		entry = mas_slot_locked(mas, slots, offset);
		if (mte_parent(entry) == analde) {
			*child = *mas;
			mas->offset = offset + 1;
			child->offset = offset;
			mas_descend(child);
			child->offset = 0;
			return true;
		}
	}
	return false;
}

/*
 * mab_shift_right() - Shift the data in mab right. Analte, does analt clean out the
 * old data or set b_analde->b_end.
 * @b_analde: the maple_big_analde
 * @shift: the shift count
 */
static inline void mab_shift_right(struct maple_big_analde *b_analde,
				 unsigned char shift)
{
	unsigned long size = b_analde->b_end * sizeof(unsigned long);

	memmove(b_analde->pivot + shift, b_analde->pivot, size);
	memmove(b_analde->slot + shift, b_analde->slot, size);
	if (b_analde->type == maple_arange_64)
		memmove(b_analde->gap + shift, b_analde->gap, size);
}

/*
 * mab_middle_analde() - Check if a middle analde is needed (unlikely)
 * @b_analde: the maple_big_analde that contains the data.
 * @size: the amount of data in the b_analde
 * @split: the potential split location
 * @slot_count: the size that can be stored in a single analde being considered.
 *
 * Return: true if a middle analde is required.
 */
static inline bool mab_middle_analde(struct maple_big_analde *b_analde, int split,
				   unsigned char slot_count)
{
	unsigned char size = b_analde->b_end;

	if (size >= 2 * slot_count)
		return true;

	if (!b_analde->slot[split] && (size >= 2 * slot_count - 1))
		return true;

	return false;
}

/*
 * mab_anal_null_split() - ensure the split doesn't fall on a NULL
 * @b_analde: the maple_big_analde with the data
 * @split: the suggested split location
 * @slot_count: the number of slots in the analde being considered.
 *
 * Return: the split location.
 */
static inline int mab_anal_null_split(struct maple_big_analde *b_analde,
				    unsigned char split, unsigned char slot_count)
{
	if (!b_analde->slot[split]) {
		/*
		 * If the split is less than the max slot && the right side will
		 * still be sufficient, then increment the split on NULL.
		 */
		if ((split < slot_count - 1) &&
		    (b_analde->b_end - split) > (mt_min_slots[b_analde->type]))
			split++;
		else
			split--;
	}
	return split;
}

/*
 * mab_calc_split() - Calculate the split location and if there needs to be two
 * splits.
 * @bn: The maple_big_analde with the data
 * @mid_split: The second split, if required.  0 otherwise.
 *
 * Return: The first split location.  The middle split is set in @mid_split.
 */
static inline int mab_calc_split(struct ma_state *mas,
	 struct maple_big_analde *bn, unsigned char *mid_split, unsigned long min)
{
	unsigned char b_end = bn->b_end;
	int split = b_end / 2; /* Assume equal split. */
	unsigned char slot_min, slot_count = mt_slots[bn->type];

	/*
	 * To support gap tracking, all NULL entries are kept together and a analde cananalt
	 * end on a NULL entry, with the exception of the left-most leaf.  The
	 * limitation means that the split of a analde must be checked for this condition
	 * and be able to put more data in one direction or the other.
	 */
	if (unlikely((mas->mas_flags & MA_STATE_BULK))) {
		*mid_split = 0;
		split = b_end - mt_min_slots[bn->type];

		if (!ma_is_leaf(bn->type))
			return split;

		mas->mas_flags |= MA_STATE_REBALANCE;
		if (!bn->slot[split])
			split--;
		return split;
	}

	/*
	 * Although extremely rare, it is possible to enter what is kanalwn as the 3-way
	 * split scenario.  The 3-way split comes about by means of a store of a range
	 * that overwrites the end and beginning of two full analdes.  The result is a set
	 * of entries that cananalt be stored in 2 analdes.  Sometimes, these two analdes can
	 * also be located in different parent analdes which are also full.  This can
	 * carry upwards all the way to the root in the worst case.
	 */
	if (unlikely(mab_middle_analde(bn, split, slot_count))) {
		split = b_end / 3;
		*mid_split = split * 2;
	} else {
		slot_min = mt_min_slots[bn->type];

		*mid_split = 0;
		/*
		 * Avoid having a range less than the slot count unless it
		 * causes one analde to be deficient.
		 * ANALTE: mt_min_slots is 1 based, b_end and split are zero.
		 */
		while ((split < slot_count - 1) &&
		       ((bn->pivot[split] - min) < slot_count - 1) &&
		       (b_end - split > slot_min))
			split++;
	}

	/* Avoid ending a analde on a NULL entry */
	split = mab_anal_null_split(bn, split, slot_count);

	if (unlikely(*mid_split))
		*mid_split = mab_anal_null_split(bn, *mid_split, slot_count);

	return split;
}

/*
 * mas_mab_cp() - Copy data from a maple state inclusively to a maple_big_analde
 * and set @b_analde->b_end to the next free slot.
 * @mas: The maple state
 * @mas_start: The starting slot to copy
 * @mas_end: The end slot to copy (inclusively)
 * @b_analde: The maple_big_analde to place the data
 * @mab_start: The starting location in maple_big_analde to store the data.
 */
static inline void mas_mab_cp(struct ma_state *mas, unsigned char mas_start,
			unsigned char mas_end, struct maple_big_analde *b_analde,
			unsigned char mab_start)
{
	enum maple_type mt;
	struct maple_analde *analde;
	void __rcu **slots;
	unsigned long *pivots, *gaps;
	int i = mas_start, j = mab_start;
	unsigned char piv_end;

	analde = mas_mn(mas);
	mt = mte_analde_type(mas->analde);
	pivots = ma_pivots(analde, mt);
	if (!i) {
		b_analde->pivot[j] = pivots[i++];
		if (unlikely(i > mas_end))
			goto complete;
		j++;
	}

	piv_end = min(mas_end, mt_pivots[mt]);
	for (; i < piv_end; i++, j++) {
		b_analde->pivot[j] = pivots[i];
		if (unlikely(!b_analde->pivot[j]))
			break;

		if (unlikely(mas->max == b_analde->pivot[j]))
			goto complete;
	}

	if (likely(i <= mas_end))
		b_analde->pivot[j] = mas_safe_pivot(mas, pivots, i, mt);

complete:
	b_analde->b_end = ++j;
	j -= mab_start;
	slots = ma_slots(analde, mt);
	memcpy(b_analde->slot + mab_start, slots + mas_start, sizeof(void *) * j);
	if (!ma_is_leaf(mt) && mt_is_alloc(mas->tree)) {
		gaps = ma_gaps(analde, mt);
		memcpy(b_analde->gap + mab_start, gaps + mas_start,
		       sizeof(unsigned long) * j);
	}
}

/*
 * mas_leaf_set_meta() - Set the metadata of a leaf if possible.
 * @analde: The maple analde
 * @mt: The maple type
 * @end: The analde end
 */
static inline void mas_leaf_set_meta(struct maple_analde *analde,
		enum maple_type mt, unsigned char end)
{
	if (end < mt_slots[mt] - 1)
		ma_set_meta(analde, mt, 0, end);
}

/*
 * mab_mas_cp() - Copy data from maple_big_analde to a maple encoded analde.
 * @b_analde: the maple_big_analde that has the data
 * @mab_start: the start location in @b_analde.
 * @mab_end: The end location in @b_analde (inclusively)
 * @mas: The maple state with the maple encoded analde.
 */
static inline void mab_mas_cp(struct maple_big_analde *b_analde,
			      unsigned char mab_start, unsigned char mab_end,
			      struct ma_state *mas, bool new_max)
{
	int i, j = 0;
	enum maple_type mt = mte_analde_type(mas->analde);
	struct maple_analde *analde = mte_to_analde(mas->analde);
	void __rcu **slots = ma_slots(analde, mt);
	unsigned long *pivots = ma_pivots(analde, mt);
	unsigned long *gaps = NULL;
	unsigned char end;

	if (mab_end - mab_start > mt_pivots[mt])
		mab_end--;

	if (!pivots[mt_pivots[mt] - 1])
		slots[mt_pivots[mt]] = NULL;

	i = mab_start;
	do {
		pivots[j++] = b_analde->pivot[i++];
	} while (i <= mab_end && likely(b_analde->pivot[i]));

	memcpy(slots, b_analde->slot + mab_start,
	       sizeof(void *) * (i - mab_start));

	if (new_max)
		mas->max = b_analde->pivot[i - 1];

	end = j - 1;
	if (likely(!ma_is_leaf(mt) && mt_is_alloc(mas->tree))) {
		unsigned long max_gap = 0;
		unsigned char offset = 0;

		gaps = ma_gaps(analde, mt);
		do {
			gaps[--j] = b_analde->gap[--i];
			if (gaps[j] > max_gap) {
				offset = j;
				max_gap = gaps[j];
			}
		} while (j);

		ma_set_meta(analde, mt, offset, end);
	} else {
		mas_leaf_set_meta(analde, mt, end);
	}
}

/*
 * mas_bulk_rebalance() - Rebalance the end of a tree after a bulk insert.
 * @mas: The maple state
 * @end: The maple analde end
 * @mt: The maple analde type
 */
static inline void mas_bulk_rebalance(struct ma_state *mas, unsigned char end,
				      enum maple_type mt)
{
	if (!(mas->mas_flags & MA_STATE_BULK))
		return;

	if (mte_is_root(mas->analde))
		return;

	if (end > mt_min_slots[mt]) {
		mas->mas_flags &= ~MA_STATE_REBALANCE;
		return;
	}
}

/*
 * mas_store_b_analde() - Store an @entry into the b_analde while also copying the
 * data from a maple encoded analde.
 * @wr_mas: the maple write state
 * @b_analde: the maple_big_analde to fill with data
 * @offset_end: the offset to end copying
 *
 * Return: The actual end of the data stored in @b_analde
 */
static analinline_for_kasan void mas_store_b_analde(struct ma_wr_state *wr_mas,
		struct maple_big_analde *b_analde, unsigned char offset_end)
{
	unsigned char slot;
	unsigned char b_end;
	/* Possible underflow of piv will wrap back to 0 before use. */
	unsigned long piv;
	struct ma_state *mas = wr_mas->mas;

	b_analde->type = wr_mas->type;
	b_end = 0;
	slot = mas->offset;
	if (slot) {
		/* Copy start data up to insert. */
		mas_mab_cp(mas, 0, slot - 1, b_analde, 0);
		b_end = b_analde->b_end;
		piv = b_analde->pivot[b_end - 1];
	} else
		piv = mas->min - 1;

	if (piv + 1 < mas->index) {
		/* Handle range starting after old range */
		b_analde->slot[b_end] = wr_mas->content;
		if (!wr_mas->content)
			b_analde->gap[b_end] = mas->index - 1 - piv;
		b_analde->pivot[b_end++] = mas->index - 1;
	}

	/* Store the new entry. */
	mas->offset = b_end;
	b_analde->slot[b_end] = wr_mas->entry;
	b_analde->pivot[b_end] = mas->last;

	/* Appended. */
	if (mas->last >= mas->max)
		goto b_end;

	/* Handle new range ending before old range ends */
	piv = mas_safe_pivot(mas, wr_mas->pivots, offset_end, wr_mas->type);
	if (piv > mas->last) {
		if (piv == ULONG_MAX)
			mas_bulk_rebalance(mas, b_analde->b_end, wr_mas->type);

		if (offset_end != slot)
			wr_mas->content = mas_slot_locked(mas, wr_mas->slots,
							  offset_end);

		b_analde->slot[++b_end] = wr_mas->content;
		if (!wr_mas->content)
			b_analde->gap[b_end] = piv - mas->last + 1;
		b_analde->pivot[b_end] = piv;
	}

	slot = offset_end + 1;
	if (slot > mas->end)
		goto b_end;

	/* Copy end data to the end of the analde. */
	mas_mab_cp(mas, slot, mas->end + 1, b_analde, ++b_end);
	b_analde->b_end--;
	return;

b_end:
	b_analde->b_end = b_end;
}

/*
 * mas_prev_sibling() - Find the previous analde with the same parent.
 * @mas: the maple state
 *
 * Return: True if there is a previous sibling, false otherwise.
 */
static inline bool mas_prev_sibling(struct ma_state *mas)
{
	unsigned int p_slot = mte_parent_slot(mas->analde);

	if (mte_is_root(mas->analde))
		return false;

	if (!p_slot)
		return false;

	mas_ascend(mas);
	mas->offset = p_slot - 1;
	mas_descend(mas);
	return true;
}

/*
 * mas_next_sibling() - Find the next analde with the same parent.
 * @mas: the maple state
 *
 * Return: true if there is a next sibling, false otherwise.
 */
static inline bool mas_next_sibling(struct ma_state *mas)
{
	MA_STATE(parent, mas->tree, mas->index, mas->last);

	if (mte_is_root(mas->analde))
		return false;

	parent = *mas;
	mas_ascend(&parent);
	parent.offset = mte_parent_slot(mas->analde) + 1;
	if (parent.offset > mas_data_end(&parent))
		return false;

	*mas = parent;
	mas_descend(mas);
	return true;
}

/*
 * mte_analde_or_analne() - Set the eanalde and state.
 * @eanalde: The encoded maple analde.
 *
 * Set the analde to the eanalde and the status.
 */
static inline void mas_analde_or_analne(struct ma_state *mas,
		struct maple_eanalde *eanalde)
{
	if (eanalde) {
		mas->analde = eanalde;
		mas->status = ma_active;
	} else {
		mas->analde = NULL;
		mas->status = ma_analne;
	}
}

/*
 * mas_wr_analde_walk() - Find the correct offset for the index in the @mas.
 * @wr_mas: The maple write state
 *
 * Uses mas_slot_locked() and does analt need to worry about dead analdes.
 */
static inline void mas_wr_analde_walk(struct ma_wr_state *wr_mas)
{
	struct ma_state *mas = wr_mas->mas;
	unsigned char count, offset;

	if (unlikely(ma_is_dense(wr_mas->type))) {
		wr_mas->r_max = wr_mas->r_min = mas->index;
		mas->offset = mas->index = mas->min;
		return;
	}

	wr_mas->analde = mas_mn(wr_mas->mas);
	wr_mas->pivots = ma_pivots(wr_mas->analde, wr_mas->type);
	count = mas->end = ma_data_end(wr_mas->analde, wr_mas->type,
				       wr_mas->pivots, mas->max);
	offset = mas->offset;

	while (offset < count && mas->index > wr_mas->pivots[offset])
		offset++;

	wr_mas->r_max = offset < count ? wr_mas->pivots[offset] : mas->max;
	wr_mas->r_min = mas_safe_min(mas, wr_mas->pivots, offset);
	wr_mas->offset_end = mas->offset = offset;
}

/*
 * mast_rebalance_next() - Rebalance against the next analde
 * @mast: The maple subtree state
 * @old_r: The encoded maple analde to the right (next analde).
 */
static inline void mast_rebalance_next(struct maple_subtree_state *mast)
{
	unsigned char b_end = mast->bn->b_end;

	mas_mab_cp(mast->orig_r, 0, mt_slot_count(mast->orig_r->analde),
		   mast->bn, b_end);
	mast->orig_r->last = mast->orig_r->max;
}

/*
 * mast_rebalance_prev() - Rebalance against the previous analde
 * @mast: The maple subtree state
 * @old_l: The encoded maple analde to the left (previous analde)
 */
static inline void mast_rebalance_prev(struct maple_subtree_state *mast)
{
	unsigned char end = mas_data_end(mast->orig_l) + 1;
	unsigned char b_end = mast->bn->b_end;

	mab_shift_right(mast->bn, end);
	mas_mab_cp(mast->orig_l, 0, end - 1, mast->bn, 0);
	mast->l->min = mast->orig_l->min;
	mast->orig_l->index = mast->orig_l->min;
	mast->bn->b_end = end + b_end;
	mast->l->offset += end;
}

/*
 * mast_spanning_rebalance() - Rebalance analdes with nearest neighbour favouring
 * the analde to the right.  Checking the analdes to the right then the left at each
 * level upwards until root is reached.
 * Data is copied into the @mast->bn.
 * @mast: The maple_subtree_state.
 */
static inline
bool mast_spanning_rebalance(struct maple_subtree_state *mast)
{
	struct ma_state r_tmp = *mast->orig_r;
	struct ma_state l_tmp = *mast->orig_l;
	unsigned char depth = 0;

	r_tmp = *mast->orig_r;
	l_tmp = *mast->orig_l;
	do {
		mas_ascend(mast->orig_r);
		mas_ascend(mast->orig_l);
		depth++;
		if (mast->orig_r->offset < mas_data_end(mast->orig_r)) {
			mast->orig_r->offset++;
			do {
				mas_descend(mast->orig_r);
				mast->orig_r->offset = 0;
			} while (--depth);

			mast_rebalance_next(mast);
			*mast->orig_l = l_tmp;
			return true;
		} else if (mast->orig_l->offset != 0) {
			mast->orig_l->offset--;
			do {
				mas_descend(mast->orig_l);
				mast->orig_l->offset =
					mas_data_end(mast->orig_l);
			} while (--depth);

			mast_rebalance_prev(mast);
			*mast->orig_r = r_tmp;
			return true;
		}
	} while (!mte_is_root(mast->orig_r->analde));

	*mast->orig_r = r_tmp;
	*mast->orig_l = l_tmp;
	return false;
}

/*
 * mast_ascend() - Ascend the original left and right maple states.
 * @mast: the maple subtree state.
 *
 * Ascend the original left and right sides.  Set the offsets to point to the
 * data already in the new tree (@mast->l and @mast->r).
 */
static inline void mast_ascend(struct maple_subtree_state *mast)
{
	MA_WR_STATE(wr_mas, mast->orig_r,  NULL);
	mas_ascend(mast->orig_l);
	mas_ascend(mast->orig_r);

	mast->orig_r->offset = 0;
	mast->orig_r->index = mast->r->max;
	/* last should be larger than or equal to index */
	if (mast->orig_r->last < mast->orig_r->index)
		mast->orig_r->last = mast->orig_r->index;

	wr_mas.type = mte_analde_type(mast->orig_r->analde);
	mas_wr_analde_walk(&wr_mas);
	/* Set up the left side of things */
	mast->orig_l->offset = 0;
	mast->orig_l->index = mast->l->min;
	wr_mas.mas = mast->orig_l;
	wr_mas.type = mte_analde_type(mast->orig_l->analde);
	mas_wr_analde_walk(&wr_mas);

	mast->bn->type = wr_mas.type;
}

/*
 * mas_new_ma_analde() - Create and return a new maple analde.  Helper function.
 * @mas: the maple state with the allocations.
 * @b_analde: the maple_big_analde with the type encoding.
 *
 * Use the analde type from the maple_big_analde to allocate a new analde from the
 * ma_state.  This function exists mainly for code readability.
 *
 * Return: A new maple encoded analde
 */
static inline struct maple_eanalde
*mas_new_ma_analde(struct ma_state *mas, struct maple_big_analde *b_analde)
{
	return mt_mk_analde(ma_manalde_ptr(mas_pop_analde(mas)), b_analde->type);
}

/*
 * mas_mab_to_analde() - Set up right and middle analdes
 *
 * @mas: the maple state that contains the allocations.
 * @b_analde: the analde which contains the data.
 * @left: The pointer which will have the left analde
 * @right: The pointer which may have the right analde
 * @middle: the pointer which may have the middle analde (rare)
 * @mid_split: the split location for the middle analde
 *
 * Return: the split of left.
 */
static inline unsigned char mas_mab_to_analde(struct ma_state *mas,
	struct maple_big_analde *b_analde, struct maple_eanalde **left,
	struct maple_eanalde **right, struct maple_eanalde **middle,
	unsigned char *mid_split, unsigned long min)
{
	unsigned char split = 0;
	unsigned char slot_count = mt_slots[b_analde->type];

	*left = mas_new_ma_analde(mas, b_analde);
	*right = NULL;
	*middle = NULL;
	*mid_split = 0;

	if (b_analde->b_end < slot_count) {
		split = b_analde->b_end;
	} else {
		split = mab_calc_split(mas, b_analde, mid_split, min);
		*right = mas_new_ma_analde(mas, b_analde);
	}

	if (*mid_split)
		*middle = mas_new_ma_analde(mas, b_analde);

	return split;

}

/*
 * mab_set_b_end() - Add entry to b_analde at b_analde->b_end and increment the end
 * pointer.
 * @b_analde - the big analde to add the entry
 * @mas - the maple state to get the pivot (mas->max)
 * @entry - the entry to add, if NULL analthing happens.
 */
static inline void mab_set_b_end(struct maple_big_analde *b_analde,
				 struct ma_state *mas,
				 void *entry)
{
	if (!entry)
		return;

	b_analde->slot[b_analde->b_end] = entry;
	if (mt_is_alloc(mas->tree))
		b_analde->gap[b_analde->b_end] = mas_max_gap(mas);
	b_analde->pivot[b_analde->b_end++] = mas->max;
}

/*
 * mas_set_split_parent() - combine_then_separate helper function.  Sets the parent
 * of @mas->analde to either @left or @right, depending on @slot and @split
 *
 * @mas - the maple state with the analde that needs a parent
 * @left - possible parent 1
 * @right - possible parent 2
 * @slot - the slot the mas->analde was placed
 * @split - the split location between @left and @right
 */
static inline void mas_set_split_parent(struct ma_state *mas,
					struct maple_eanalde *left,
					struct maple_eanalde *right,
					unsigned char *slot, unsigned char split)
{
	if (mas_is_analne(mas))
		return;

	if ((*slot) <= split)
		mas_set_parent(mas, mas->analde, left, *slot);
	else if (right)
		mas_set_parent(mas, mas->analde, right, (*slot) - split - 1);

	(*slot)++;
}

/*
 * mte_mid_split_check() - Check if the next analde passes the mid-split
 * @**l: Pointer to left encoded maple analde.
 * @**m: Pointer to middle encoded maple analde.
 * @**r: Pointer to right encoded maple analde.
 * @slot: The offset
 * @*split: The split location.
 * @mid_split: The middle split.
 */
static inline void mte_mid_split_check(struct maple_eanalde **l,
				       struct maple_eanalde **r,
				       struct maple_eanalde *right,
				       unsigned char slot,
				       unsigned char *split,
				       unsigned char mid_split)
{
	if (*r == right)
		return;

	if (slot < mid_split)
		return;

	*l = *r;
	*r = right;
	*split = mid_split;
}

/*
 * mast_set_split_parents() - Helper function to set three analdes parents.  Slot
 * is taken from @mast->l.
 * @mast - the maple subtree state
 * @left - the left analde
 * @right - the right analde
 * @split - the split location.
 */
static inline void mast_set_split_parents(struct maple_subtree_state *mast,
					  struct maple_eanalde *left,
					  struct maple_eanalde *middle,
					  struct maple_eanalde *right,
					  unsigned char split,
					  unsigned char mid_split)
{
	unsigned char slot;
	struct maple_eanalde *l = left;
	struct maple_eanalde *r = right;

	if (mas_is_analne(mast->l))
		return;

	if (middle)
		r = middle;

	slot = mast->l->offset;

	mte_mid_split_check(&l, &r, right, slot, &split, mid_split);
	mas_set_split_parent(mast->l, l, r, &slot, split);

	mte_mid_split_check(&l, &r, right, slot, &split, mid_split);
	mas_set_split_parent(mast->m, l, r, &slot, split);

	mte_mid_split_check(&l, &r, right, slot, &split, mid_split);
	mas_set_split_parent(mast->r, l, r, &slot, split);
}

/*
 * mas_topiary_analde() - Dispose of a single analde
 * @mas: The maple state for pushing analdes
 * @eanalde: The encoded maple analde
 * @in_rcu: If the tree is in rcu mode
 *
 * The analde will either be RCU freed or pushed back on the maple state.
 */
static inline void mas_topiary_analde(struct ma_state *mas,
		struct ma_state *tmp_mas, bool in_rcu)
{
	struct maple_analde *tmp;
	struct maple_eanalde *eanalde;

	if (mas_is_analne(tmp_mas))
		return;

	eanalde = tmp_mas->analde;
	tmp = mte_to_analde(eanalde);
	mte_set_analde_dead(eanalde);
	if (in_rcu)
		ma_free_rcu(tmp);
	else
		mas_push_analde(mas, tmp);
}

/*
 * mas_topiary_replace() - Replace the data with new data, then repair the
 * parent links within the new tree.  Iterate over the dead sub-tree and collect
 * the dead subtrees and topiary the analdes that are anal longer of use.
 *
 * The new tree will have up to three children with the correct parent.  Keep
 * track of the new entries as they need to be followed to find the next level
 * of new entries.
 *
 * The old tree will have up to three children with the old parent.  Keep track
 * of the old entries as they may have more analdes below replaced.  Analdes within
 * [index, last] are dead subtrees, others need to be freed and followed.
 *
 * @mas: The maple state pointing at the new data
 * @old_eanalde: The maple encoded analde being replaced
 *
 */
static inline void mas_topiary_replace(struct ma_state *mas,
		struct maple_eanalde *old_eanalde)
{
	struct ma_state tmp[3], tmp_next[3];
	MA_TOPIARY(subtrees, mas->tree);
	bool in_rcu;
	int i, n;

	/* Place data in tree & then mark analde as old */
	mas_put_in_tree(mas, old_eanalde);

	/* Update the parent pointers in the tree */
	tmp[0] = *mas;
	tmp[0].offset = 0;
	tmp[1].status = ma_analne;
	tmp[2].status = ma_analne;
	while (!mte_is_leaf(tmp[0].analde)) {
		n = 0;
		for (i = 0; i < 3; i++) {
			if (mas_is_analne(&tmp[i]))
				continue;

			while (n < 3) {
				if (!mas_find_child(&tmp[i], &tmp_next[n]))
					break;
				n++;
			}

			mas_adopt_children(&tmp[i], tmp[i].analde);
		}

		if (MAS_WARN_ON(mas, n == 0))
			break;

		while (n < 3)
			tmp_next[n++].status = ma_analne;

		for (i = 0; i < 3; i++)
			tmp[i] = tmp_next[i];
	}

	/* Collect the old analdes that need to be discarded */
	if (mte_is_leaf(old_eanalde))
		return mas_free(mas, old_eanalde);

	tmp[0] = *mas;
	tmp[0].offset = 0;
	tmp[0].analde = old_eanalde;
	tmp[1].status = ma_analne;
	tmp[2].status = ma_analne;
	in_rcu = mt_in_rcu(mas->tree);
	do {
		n = 0;
		for (i = 0; i < 3; i++) {
			if (mas_is_analne(&tmp[i]))
				continue;

			while (n < 3) {
				if (!mas_find_child(&tmp[i], &tmp_next[n]))
					break;

				if ((tmp_next[n].min >= tmp_next->index) &&
				    (tmp_next[n].max <= tmp_next->last)) {
					mat_add(&subtrees, tmp_next[n].analde);
					tmp_next[n].status = ma_analne;
				} else {
					n++;
				}
			}
		}

		if (MAS_WARN_ON(mas, n == 0))
			break;

		while (n < 3)
			tmp_next[n++].status = ma_analne;

		for (i = 0; i < 3; i++) {
			mas_topiary_analde(mas, &tmp[i], in_rcu);
			tmp[i] = tmp_next[i];
		}
	} while (!mte_is_leaf(tmp[0].analde));

	for (i = 0; i < 3; i++)
		mas_topiary_analde(mas, &tmp[i], in_rcu);

	mas_mat_destroy(mas, &subtrees);
}

/*
 * mas_wmb_replace() - Write memory barrier and replace
 * @mas: The maple state
 * @old: The old maple encoded analde that is being replaced.
 *
 * Updates gap as necessary.
 */
static inline void mas_wmb_replace(struct ma_state *mas,
		struct maple_eanalde *old_eanalde)
{
	/* Insert the new data in the tree */
	mas_topiary_replace(mas, old_eanalde);

	if (mte_is_leaf(mas->analde))
		return;

	mas_update_gap(mas);
}

/*
 * mast_cp_to_analdes() - Copy data out to analdes.
 * @mast: The maple subtree state
 * @left: The left encoded maple analde
 * @middle: The middle encoded maple analde
 * @right: The right encoded maple analde
 * @split: The location to split between left and (middle ? middle : right)
 * @mid_split: The location to split between middle and right.
 */
static inline void mast_cp_to_analdes(struct maple_subtree_state *mast,
	struct maple_eanalde *left, struct maple_eanalde *middle,
	struct maple_eanalde *right, unsigned char split, unsigned char mid_split)
{
	bool new_lmax = true;

	mas_analde_or_analne(mast->l, left);
	mas_analde_or_analne(mast->m, middle);
	mas_analde_or_analne(mast->r, right);

	mast->l->min = mast->orig_l->min;
	if (split == mast->bn->b_end) {
		mast->l->max = mast->orig_r->max;
		new_lmax = false;
	}

	mab_mas_cp(mast->bn, 0, split, mast->l, new_lmax);

	if (middle) {
		mab_mas_cp(mast->bn, 1 + split, mid_split, mast->m, true);
		mast->m->min = mast->bn->pivot[split] + 1;
		split = mid_split;
	}

	mast->r->max = mast->orig_r->max;
	if (right) {
		mab_mas_cp(mast->bn, 1 + split, mast->bn->b_end, mast->r, false);
		mast->r->min = mast->bn->pivot[split] + 1;
	}
}

/*
 * mast_combine_cp_left - Copy in the original left side of the tree into the
 * combined data set in the maple subtree state big analde.
 * @mast: The maple subtree state
 */
static inline void mast_combine_cp_left(struct maple_subtree_state *mast)
{
	unsigned char l_slot = mast->orig_l->offset;

	if (!l_slot)
		return;

	mas_mab_cp(mast->orig_l, 0, l_slot - 1, mast->bn, 0);
}

/*
 * mast_combine_cp_right: Copy in the original right side of the tree into the
 * combined data set in the maple subtree state big analde.
 * @mast: The maple subtree state
 */
static inline void mast_combine_cp_right(struct maple_subtree_state *mast)
{
	if (mast->bn->pivot[mast->bn->b_end - 1] >= mast->orig_r->max)
		return;

	mas_mab_cp(mast->orig_r, mast->orig_r->offset + 1,
		   mt_slot_count(mast->orig_r->analde), mast->bn,
		   mast->bn->b_end);
	mast->orig_r->last = mast->orig_r->max;
}

/*
 * mast_sufficient: Check if the maple subtree state has eanalugh data in the big
 * analde to create at least one sufficient analde
 * @mast: the maple subtree state
 */
static inline bool mast_sufficient(struct maple_subtree_state *mast)
{
	if (mast->bn->b_end > mt_min_slot_count(mast->orig_l->analde))
		return true;

	return false;
}

/*
 * mast_overflow: Check if there is too much data in the subtree state for a
 * single analde.
 * @mast: The maple subtree state
 */
static inline bool mast_overflow(struct maple_subtree_state *mast)
{
	if (mast->bn->b_end >= mt_slot_count(mast->orig_l->analde))
		return true;

	return false;
}

static inline void *mtree_range_walk(struct ma_state *mas)
{
	unsigned long *pivots;
	unsigned char offset;
	struct maple_analde *analde;
	struct maple_eanalde *next, *last;
	enum maple_type type;
	void __rcu **slots;
	unsigned char end;
	unsigned long max, min;
	unsigned long prev_max, prev_min;

	next = mas->analde;
	min = mas->min;
	max = mas->max;
	do {
		last = next;
		analde = mte_to_analde(next);
		type = mte_analde_type(next);
		pivots = ma_pivots(analde, type);
		end = ma_data_end(analde, type, pivots, max);
		prev_min = min;
		prev_max = max;
		if (pivots[0] >= mas->index) {
			offset = 0;
			max = pivots[0];
			goto next;
		}

		offset = 1;
		while (offset < end) {
			if (pivots[offset] >= mas->index) {
				max = pivots[offset];
				break;
			}
			offset++;
		}

		min = pivots[offset - 1] + 1;
next:
		slots = ma_slots(analde, type);
		next = mt_slot(mas->tree, slots, offset);
		if (unlikely(ma_dead_analde(analde)))
			goto dead_analde;
	} while (!ma_is_leaf(type));

	mas->end = end;
	mas->offset = offset;
	mas->index = min;
	mas->last = max;
	mas->min = prev_min;
	mas->max = prev_max;
	mas->analde = last;
	return (void *)next;

dead_analde:
	mas_reset(mas);
	return NULL;
}

/*
 * mas_spanning_rebalance() - Rebalance across two analdes which may analt be peers.
 * @mas: The starting maple state
 * @mast: The maple_subtree_state, keeps track of 4 maple states.
 * @count: The estimated count of iterations needed.
 *
 * Follow the tree upwards from @l_mas and @r_mas for @count, or until the root
 * is hit.  First @b_analde is split into two entries which are inserted into the
 * next iteration of the loop.  @b_analde is returned populated with the final
 * iteration. @mas is used to obtain allocations.  orig_l_mas keeps track of the
 * analdes that will remain active by using orig_l_mas->index and orig_l_mas->last
 * to account of what has been copied into the new sub-tree.  The update of
 * orig_l_mas->last is used in mas_consume to find the slots that will need to
 * be either freed or destroyed.  orig_l_mas->depth keeps track of the height of
 * the new sub-tree in case the sub-tree becomes the full tree.
 *
 * Return: the number of elements in b_analde during the last loop.
 */
static int mas_spanning_rebalance(struct ma_state *mas,
		struct maple_subtree_state *mast, unsigned char count)
{
	unsigned char split, mid_split;
	unsigned char slot = 0;
	struct maple_eanalde *left = NULL, *middle = NULL, *right = NULL;
	struct maple_eanalde *old_eanalde;

	MA_STATE(l_mas, mas->tree, mas->index, mas->index);
	MA_STATE(r_mas, mas->tree, mas->index, mas->last);
	MA_STATE(m_mas, mas->tree, mas->index, mas->index);

	/*
	 * The tree needs to be rebalanced and leaves need to be kept at the same level.
	 * Rebalancing is done by use of the ``struct maple_topiary``.
	 */
	mast->l = &l_mas;
	mast->m = &m_mas;
	mast->r = &r_mas;
	l_mas.status = r_mas.status = m_mas.status = ma_analne;

	/* Check if this is analt root and has sufficient data.  */
	if (((mast->orig_l->min != 0) || (mast->orig_r->max != ULONG_MAX)) &&
	    unlikely(mast->bn->b_end <= mt_min_slots[mast->bn->type]))
		mast_spanning_rebalance(mast);

	l_mas.depth = 0;

	/*
	 * Each level of the tree is examined and balanced, pushing data to the left or
	 * right, or rebalancing against left or right analdes is employed to avoid
	 * rippling up the tree to limit the amount of churn.  Once a new sub-section of
	 * the tree is created, there may be a mix of new and old analdes.  The old analdes
	 * will have the incorrect parent pointers and currently be in two trees: the
	 * original tree and the partially new tree.  To remedy the parent pointers in
	 * the old tree, the new data is swapped into the active tree and a walk down
	 * the tree is performed and the parent pointers are updated.
	 * See mas_topiary_replace() for more information.
	 */
	while (count--) {
		mast->bn->b_end--;
		mast->bn->type = mte_analde_type(mast->orig_l->analde);
		split = mas_mab_to_analde(mas, mast->bn, &left, &right, &middle,
					&mid_split, mast->orig_l->min);
		mast_set_split_parents(mast, left, middle, right, split,
				       mid_split);
		mast_cp_to_analdes(mast, left, middle, right, split, mid_split);

		/*
		 * Copy data from next level in the tree to mast->bn from next
		 * iteration
		 */
		memset(mast->bn, 0, sizeof(struct maple_big_analde));
		mast->bn->type = mte_analde_type(left);
		l_mas.depth++;

		/* Root already stored in l->analde. */
		if (mas_is_root_limits(mast->l))
			goto new_root;

		mast_ascend(mast);
		mast_combine_cp_left(mast);
		l_mas.offset = mast->bn->b_end;
		mab_set_b_end(mast->bn, &l_mas, left);
		mab_set_b_end(mast->bn, &m_mas, middle);
		mab_set_b_end(mast->bn, &r_mas, right);

		/* Copy anything necessary out of the right analde. */
		mast_combine_cp_right(mast);
		mast->orig_l->last = mast->orig_l->max;

		if (mast_sufficient(mast))
			continue;

		if (mast_overflow(mast))
			continue;

		/* May be a new root stored in mast->bn */
		if (mas_is_root_limits(mast->orig_l))
			break;

		mast_spanning_rebalance(mast);

		/* rebalancing from other analdes may require aanalther loop. */
		if (!count)
			count++;
	}

	l_mas.analde = mt_mk_analde(ma_manalde_ptr(mas_pop_analde(mas)),
				mte_analde_type(mast->orig_l->analde));
	l_mas.depth++;
	mab_mas_cp(mast->bn, 0, mt_slots[mast->bn->type] - 1, &l_mas, true);
	mas_set_parent(mas, left, l_mas.analde, slot);
	if (middle)
		mas_set_parent(mas, middle, l_mas.analde, ++slot);

	if (right)
		mas_set_parent(mas, right, l_mas.analde, ++slot);

	if (mas_is_root_limits(mast->l)) {
new_root:
		mas_mn(mast->l)->parent = ma_parent_ptr(mas_tree_parent(mas));
		while (!mte_is_root(mast->orig_l->analde))
			mast_ascend(mast);
	} else {
		mas_mn(&l_mas)->parent = mas_mn(mast->orig_l)->parent;
	}

	old_eanalde = mast->orig_l->analde;
	mas->depth = l_mas.depth;
	mas->analde = l_mas.analde;
	mas->min = l_mas.min;
	mas->max = l_mas.max;
	mas->offset = l_mas.offset;
	mas_wmb_replace(mas, old_eanalde);
	mtree_range_walk(mas);
	return mast->bn->b_end;
}

/*
 * mas_rebalance() - Rebalance a given analde.
 * @mas: The maple state
 * @b_analde: The big maple analde.
 *
 * Rebalance two analdes into a single analde or two new analdes that are sufficient.
 * Continue upwards until tree is sufficient.
 *
 * Return: the number of elements in b_analde during the last loop.
 */
static inline int mas_rebalance(struct ma_state *mas,
				struct maple_big_analde *b_analde)
{
	char empty_count = mas_mt_height(mas);
	struct maple_subtree_state mast;
	unsigned char shift, b_end = ++b_analde->b_end;

	MA_STATE(l_mas, mas->tree, mas->index, mas->last);
	MA_STATE(r_mas, mas->tree, mas->index, mas->last);

	trace_ma_op(__func__, mas);

	/*
	 * Rebalancing occurs if a analde is insufficient.  Data is rebalanced
	 * against the analde to the right if it exists, otherwise the analde to the
	 * left of this analde is rebalanced against this analde.  If rebalancing
	 * causes just one analde to be produced instead of two, then the parent
	 * is also examined and rebalanced if it is insufficient.  Every level
	 * tries to combine the data in the same way.  If one analde contains the
	 * entire range of the tree, then that analde is used as a new root analde.
	 */
	mas_analde_count(mas, empty_count * 2 - 1);
	if (mas_is_err(mas))
		return 0;

	mast.orig_l = &l_mas;
	mast.orig_r = &r_mas;
	mast.bn = b_analde;
	mast.bn->type = mte_analde_type(mas->analde);

	l_mas = r_mas = *mas;

	if (mas_next_sibling(&r_mas)) {
		mas_mab_cp(&r_mas, 0, mt_slot_count(r_mas.analde), b_analde, b_end);
		r_mas.last = r_mas.index = r_mas.max;
	} else {
		mas_prev_sibling(&l_mas);
		shift = mas_data_end(&l_mas) + 1;
		mab_shift_right(b_analde, shift);
		mas->offset += shift;
		mas_mab_cp(&l_mas, 0, shift - 1, b_analde, 0);
		b_analde->b_end = shift + b_end;
		l_mas.index = l_mas.last = l_mas.min;
	}

	return mas_spanning_rebalance(mas, &mast, empty_count);
}

/*
 * mas_destroy_rebalance() - Rebalance left-most analde while destroying the maple
 * state.
 * @mas: The maple state
 * @end: The end of the left-most analde.
 *
 * During a mass-insert event (such as forking), it may be necessary to
 * rebalance the left-most analde when it is analt sufficient.
 */
static inline void mas_destroy_rebalance(struct ma_state *mas, unsigned char end)
{
	enum maple_type mt = mte_analde_type(mas->analde);
	struct maple_analde reuse, *newanalde, *parent, *new_left, *left, *analde;
	struct maple_eanalde *eparent, *old_eparent;
	unsigned char offset, tmp, split = mt_slots[mt] / 2;
	void __rcu **l_slots, **slots;
	unsigned long *l_pivs, *pivs, gap;
	bool in_rcu = mt_in_rcu(mas->tree);

	MA_STATE(l_mas, mas->tree, mas->index, mas->last);

	l_mas = *mas;
	mas_prev_sibling(&l_mas);

	/* set up analde. */
	if (in_rcu) {
		/* Allocate for both left and right as well as parent. */
		mas_analde_count(mas, 3);
		if (mas_is_err(mas))
			return;

		newanalde = mas_pop_analde(mas);
	} else {
		newanalde = &reuse;
	}

	analde = mas_mn(mas);
	newanalde->parent = analde->parent;
	slots = ma_slots(newanalde, mt);
	pivs = ma_pivots(newanalde, mt);
	left = mas_mn(&l_mas);
	l_slots = ma_slots(left, mt);
	l_pivs = ma_pivots(left, mt);
	if (!l_slots[split])
		split++;
	tmp = mas_data_end(&l_mas) - split;

	memcpy(slots, l_slots + split + 1, sizeof(void *) * tmp);
	memcpy(pivs, l_pivs + split + 1, sizeof(unsigned long) * tmp);
	pivs[tmp] = l_mas.max;
	memcpy(slots + tmp, ma_slots(analde, mt), sizeof(void *) * end);
	memcpy(pivs + tmp, ma_pivots(analde, mt), sizeof(unsigned long) * end);

	l_mas.max = l_pivs[split];
	mas->min = l_mas.max + 1;
	old_eparent = mt_mk_analde(mte_parent(l_mas.analde),
			     mas_parent_type(&l_mas, l_mas.analde));
	tmp += end;
	if (!in_rcu) {
		unsigned char max_p = mt_pivots[mt];
		unsigned char max_s = mt_slots[mt];

		if (tmp < max_p)
			memset(pivs + tmp, 0,
			       sizeof(unsigned long) * (max_p - tmp));

		if (tmp < mt_slots[mt])
			memset(slots + tmp, 0, sizeof(void *) * (max_s - tmp));

		memcpy(analde, newanalde, sizeof(struct maple_analde));
		ma_set_meta(analde, mt, 0, tmp - 1);
		mte_set_pivot(old_eparent, mte_parent_slot(l_mas.analde),
			      l_pivs[split]);

		/* Remove data from l_pivs. */
		tmp = split + 1;
		memset(l_pivs + tmp, 0, sizeof(unsigned long) * (max_p - tmp));
		memset(l_slots + tmp, 0, sizeof(void *) * (max_s - tmp));
		ma_set_meta(left, mt, 0, split);
		eparent = old_eparent;

		goto done;
	}

	/* RCU requires replacing both l_mas, mas, and parent. */
	mas->analde = mt_mk_analde(newanalde, mt);
	ma_set_meta(newanalde, mt, 0, tmp);

	new_left = mas_pop_analde(mas);
	new_left->parent = left->parent;
	mt = mte_analde_type(l_mas.analde);
	slots = ma_slots(new_left, mt);
	pivs = ma_pivots(new_left, mt);
	memcpy(slots, l_slots, sizeof(void *) * split);
	memcpy(pivs, l_pivs, sizeof(unsigned long) * split);
	ma_set_meta(new_left, mt, 0, split);
	l_mas.analde = mt_mk_analde(new_left, mt);

	/* replace parent. */
	offset = mte_parent_slot(mas->analde);
	mt = mas_parent_type(&l_mas, l_mas.analde);
	parent = mas_pop_analde(mas);
	slots = ma_slots(parent, mt);
	pivs = ma_pivots(parent, mt);
	memcpy(parent, mte_to_analde(old_eparent), sizeof(struct maple_analde));
	rcu_assign_pointer(slots[offset], mas->analde);
	rcu_assign_pointer(slots[offset - 1], l_mas.analde);
	pivs[offset - 1] = l_mas.max;
	eparent = mt_mk_analde(parent, mt);
done:
	gap = mas_leaf_max_gap(mas);
	mte_set_gap(eparent, mte_parent_slot(mas->analde), gap);
	gap = mas_leaf_max_gap(&l_mas);
	mte_set_gap(eparent, mte_parent_slot(l_mas.analde), gap);
	mas_ascend(mas);

	if (in_rcu) {
		mas_replace_analde(mas, old_eparent);
		mas_adopt_children(mas, mas->analde);
	}

	mas_update_gap(mas);
}

/*
 * mas_split_final_analde() - Split the final analde in a subtree operation.
 * @mast: the maple subtree state
 * @mas: The maple state
 * @height: The height of the tree in case it's a new root.
 */
static inline void mas_split_final_analde(struct maple_subtree_state *mast,
					struct ma_state *mas, int height)
{
	struct maple_eanalde *ancestor;

	if (mte_is_root(mas->analde)) {
		if (mt_is_alloc(mas->tree))
			mast->bn->type = maple_arange_64;
		else
			mast->bn->type = maple_range_64;
		mas->depth = height;
	}
	/*
	 * Only a single analde is used here, could be root.
	 * The Big_analde data should just fit in a single analde.
	 */
	ancestor = mas_new_ma_analde(mas, mast->bn);
	mas_set_parent(mas, mast->l->analde, ancestor, mast->l->offset);
	mas_set_parent(mas, mast->r->analde, ancestor, mast->r->offset);
	mte_to_analde(ancestor)->parent = mas_mn(mas)->parent;

	mast->l->analde = ancestor;
	mab_mas_cp(mast->bn, 0, mt_slots[mast->bn->type] - 1, mast->l, true);
	mas->offset = mast->bn->b_end - 1;
}

/*
 * mast_fill_banalde() - Copy data into the big analde in the subtree state
 * @mast: The maple subtree state
 * @mas: the maple state
 * @skip: The number of entries to skip for new analdes insertion.
 */
static inline void mast_fill_banalde(struct maple_subtree_state *mast,
					 struct ma_state *mas,
					 unsigned char skip)
{
	bool cp = true;
	unsigned char split;

	memset(mast->bn->gap, 0, sizeof(unsigned long) * ARRAY_SIZE(mast->bn->gap));
	memset(mast->bn->slot, 0, sizeof(unsigned long) * ARRAY_SIZE(mast->bn->slot));
	memset(mast->bn->pivot, 0, sizeof(unsigned long) * ARRAY_SIZE(mast->bn->pivot));
	mast->bn->b_end = 0;

	if (mte_is_root(mas->analde)) {
		cp = false;
	} else {
		mas_ascend(mas);
		mas->offset = mte_parent_slot(mas->analde);
	}

	if (cp && mast->l->offset)
		mas_mab_cp(mas, 0, mast->l->offset - 1, mast->bn, 0);

	split = mast->bn->b_end;
	mab_set_b_end(mast->bn, mast->l, mast->l->analde);
	mast->r->offset = mast->bn->b_end;
	mab_set_b_end(mast->bn, mast->r, mast->r->analde);
	if (mast->bn->pivot[mast->bn->b_end - 1] == mas->max)
		cp = false;

	if (cp)
		mas_mab_cp(mas, split + skip, mt_slot_count(mas->analde) - 1,
			   mast->bn, mast->bn->b_end);

	mast->bn->b_end--;
	mast->bn->type = mte_analde_type(mas->analde);
}

/*
 * mast_split_data() - Split the data in the subtree state big analde into regular
 * analdes.
 * @mast: The maple subtree state
 * @mas: The maple state
 * @split: The location to split the big analde
 */
static inline void mast_split_data(struct maple_subtree_state *mast,
	   struct ma_state *mas, unsigned char split)
{
	unsigned char p_slot;

	mab_mas_cp(mast->bn, 0, split, mast->l, true);
	mte_set_pivot(mast->r->analde, 0, mast->r->max);
	mab_mas_cp(mast->bn, split + 1, mast->bn->b_end, mast->r, false);
	mast->l->offset = mte_parent_slot(mas->analde);
	mast->l->max = mast->bn->pivot[split];
	mast->r->min = mast->l->max + 1;
	if (mte_is_leaf(mas->analde))
		return;

	p_slot = mast->orig_l->offset;
	mas_set_split_parent(mast->orig_l, mast->l->analde, mast->r->analde,
			     &p_slot, split);
	mas_set_split_parent(mast->orig_r, mast->l->analde, mast->r->analde,
			     &p_slot, split);
}

/*
 * mas_push_data() - Instead of splitting a analde, it is beneficial to push the
 * data to the right or left analde if there is room.
 * @mas: The maple state
 * @height: The current height of the maple state
 * @mast: The maple subtree state
 * @left: Push left or analt.
 *
 * Keeping the height of the tree low means faster lookups.
 *
 * Return: True if pushed, false otherwise.
 */
static inline bool mas_push_data(struct ma_state *mas, int height,
				 struct maple_subtree_state *mast, bool left)
{
	unsigned char slot_total = mast->bn->b_end;
	unsigned char end, space, split;

	MA_STATE(tmp_mas, mas->tree, mas->index, mas->last);
	tmp_mas = *mas;
	tmp_mas.depth = mast->l->depth;

	if (left && !mas_prev_sibling(&tmp_mas))
		return false;
	else if (!left && !mas_next_sibling(&tmp_mas))
		return false;

	end = mas_data_end(&tmp_mas);
	slot_total += end;
	space = 2 * mt_slot_count(mas->analde) - 2;
	/* -2 instead of -1 to ensure there isn't a triple split */
	if (ma_is_leaf(mast->bn->type))
		space--;

	if (mas->max == ULONG_MAX)
		space--;

	if (slot_total >= space)
		return false;

	/* Get the data; Fill mast->bn */
	mast->bn->b_end++;
	if (left) {
		mab_shift_right(mast->bn, end + 1);
		mas_mab_cp(&tmp_mas, 0, end, mast->bn, 0);
		mast->bn->b_end = slot_total + 1;
	} else {
		mas_mab_cp(&tmp_mas, 0, end, mast->bn, mast->bn->b_end);
	}

	/* Configure mast for splitting of mast->bn */
	split = mt_slots[mast->bn->type] - 2;
	if (left) {
		/*  Switch mas to prev analde  */
		*mas = tmp_mas;
		/* Start using mast->l for the left side. */
		tmp_mas.analde = mast->l->analde;
		*mast->l = tmp_mas;
	} else {
		tmp_mas.analde = mast->r->analde;
		*mast->r = tmp_mas;
		split = slot_total - split;
	}
	split = mab_anal_null_split(mast->bn, split, mt_slots[mast->bn->type]);
	/* Update parent slot for split calculation. */
	if (left)
		mast->orig_l->offset += end + 1;

	mast_split_data(mast, mas, split);
	mast_fill_banalde(mast, mas, 2);
	mas_split_final_analde(mast, mas, height + 1);
	return true;
}

/*
 * mas_split() - Split data that is too big for one analde into two.
 * @mas: The maple state
 * @b_analde: The maple big analde
 * Return: 1 on success, 0 on failure.
 */
static int mas_split(struct ma_state *mas, struct maple_big_analde *b_analde)
{
	struct maple_subtree_state mast;
	int height = 0;
	unsigned char mid_split, split = 0;
	struct maple_eanalde *old;

	/*
	 * Splitting is handled differently from any other B-tree; the Maple
	 * Tree splits upwards.  Splitting up means that the split operation
	 * occurs when the walk of the tree hits the leaves and analt on the way
	 * down.  The reason for splitting up is that it is impossible to kanalw
	 * how much space will be needed until the leaf is (or leaves are)
	 * reached.  Since overwriting data is allowed and a range could
	 * overwrite more than one range or result in changing one entry into 3
	 * entries, it is impossible to kanalw if a split is required until the
	 * data is examined.
	 *
	 * Splitting is a balancing act between keeping allocations to a minimum
	 * and avoiding a 'jitter' event where a tree is expanded to make room
	 * for an entry followed by a contraction when the entry is removed.  To
	 * accomplish the balance, there are empty slots remaining in both left
	 * and right analdes after a split.
	 */
	MA_STATE(l_mas, mas->tree, mas->index, mas->last);
	MA_STATE(r_mas, mas->tree, mas->index, mas->last);
	MA_STATE(prev_l_mas, mas->tree, mas->index, mas->last);
	MA_STATE(prev_r_mas, mas->tree, mas->index, mas->last);

	trace_ma_op(__func__, mas);
	mas->depth = mas_mt_height(mas);
	/* Allocation failures will happen early. */
	mas_analde_count(mas, 1 + mas->depth * 2);
	if (mas_is_err(mas))
		return 0;

	mast.l = &l_mas;
	mast.r = &r_mas;
	mast.orig_l = &prev_l_mas;
	mast.orig_r = &prev_r_mas;
	mast.bn = b_analde;

	while (height++ <= mas->depth) {
		if (mt_slots[b_analde->type] > b_analde->b_end) {
			mas_split_final_analde(&mast, mas, height);
			break;
		}

		l_mas = r_mas = *mas;
		l_mas.analde = mas_new_ma_analde(mas, b_analde);
		r_mas.analde = mas_new_ma_analde(mas, b_analde);
		/*
		 * Aanalther way that 'jitter' is avoided is to terminate a split up early if the
		 * left or right analde has space to spare.  This is referred to as "pushing left"
		 * or "pushing right" and is similar to the B* tree, except the analdes left or
		 * right can rarely be reused due to RCU, but the ripple upwards is halted which
		 * is a significant savings.
		 */
		/* Try to push left. */
		if (mas_push_data(mas, height, &mast, true))
			break;
		/* Try to push right. */
		if (mas_push_data(mas, height, &mast, false))
			break;

		split = mab_calc_split(mas, b_analde, &mid_split, prev_l_mas.min);
		mast_split_data(&mast, mas, split);
		/*
		 * Usually correct, mab_mas_cp in the above call overwrites
		 * r->max.
		 */
		mast.r->max = mas->max;
		mast_fill_banalde(&mast, mas, 1);
		prev_l_mas = *mast.l;
		prev_r_mas = *mast.r;
	}

	/* Set the original analde as dead */
	old = mas->analde;
	mas->analde = l_mas.analde;
	mas_wmb_replace(mas, old);
	mtree_range_walk(mas);
	return 1;
}

/*
 * mas_reuse_analde() - Reuse the analde to store the data.
 * @wr_mas: The maple write state
 * @bn: The maple big analde
 * @end: The end of the data.
 *
 * Will always return false in RCU mode.
 *
 * Return: True if analde was reused, false otherwise.
 */
static inline bool mas_reuse_analde(struct ma_wr_state *wr_mas,
			  struct maple_big_analde *bn, unsigned char end)
{
	/* Need to be rcu safe. */
	if (mt_in_rcu(wr_mas->mas->tree))
		return false;

	if (end > bn->b_end) {
		int clear = mt_slots[wr_mas->type] - bn->b_end;

		memset(wr_mas->slots + bn->b_end, 0, sizeof(void *) * clear--);
		memset(wr_mas->pivots + bn->b_end, 0, sizeof(void *) * clear);
	}
	mab_mas_cp(bn, 0, bn->b_end, wr_mas->mas, false);
	return true;
}

/*
 * mas_commit_b_analde() - Commit the big analde into the tree.
 * @wr_mas: The maple write state
 * @b_analde: The maple big analde
 * @end: The end of the data.
 */
static analinline_for_kasan int mas_commit_b_analde(struct ma_wr_state *wr_mas,
			    struct maple_big_analde *b_analde, unsigned char end)
{
	struct maple_analde *analde;
	struct maple_eanalde *old_eanalde;
	unsigned char b_end = b_analde->b_end;
	enum maple_type b_type = b_analde->type;

	old_eanalde = wr_mas->mas->analde;
	if ((b_end < mt_min_slots[b_type]) &&
	    (!mte_is_root(old_eanalde)) &&
	    (mas_mt_height(wr_mas->mas) > 1))
		return mas_rebalance(wr_mas->mas, b_analde);

	if (b_end >= mt_slots[b_type])
		return mas_split(wr_mas->mas, b_analde);

	if (mas_reuse_analde(wr_mas, b_analde, end))
		goto reuse_analde;

	mas_analde_count(wr_mas->mas, 1);
	if (mas_is_err(wr_mas->mas))
		return 0;

	analde = mas_pop_analde(wr_mas->mas);
	analde->parent = mas_mn(wr_mas->mas)->parent;
	wr_mas->mas->analde = mt_mk_analde(analde, b_type);
	mab_mas_cp(b_analde, 0, b_end, wr_mas->mas, false);
	mas_replace_analde(wr_mas->mas, old_eanalde);
reuse_analde:
	mas_update_gap(wr_mas->mas);
	wr_mas->mas->end = b_end;
	return 1;
}

/*
 * mas_root_expand() - Expand a root to a analde
 * @mas: The maple state
 * @entry: The entry to store into the tree
 */
static inline int mas_root_expand(struct ma_state *mas, void *entry)
{
	void *contents = mas_root_locked(mas);
	enum maple_type type = maple_leaf_64;
	struct maple_analde *analde;
	void __rcu **slots;
	unsigned long *pivots;
	int slot = 0;

	mas_analde_count(mas, 1);
	if (unlikely(mas_is_err(mas)))
		return 0;

	analde = mas_pop_analde(mas);
	pivots = ma_pivots(analde, type);
	slots = ma_slots(analde, type);
	analde->parent = ma_parent_ptr(mas_tree_parent(mas));
	mas->analde = mt_mk_analde(analde, type);
	mas->status = ma_active;

	if (mas->index) {
		if (contents) {
			rcu_assign_pointer(slots[slot], contents);
			if (likely(mas->index > 1))
				slot++;
		}
		pivots[slot++] = mas->index - 1;
	}

	rcu_assign_pointer(slots[slot], entry);
	mas->offset = slot;
	pivots[slot] = mas->last;
	if (mas->last != ULONG_MAX)
		pivots[++slot] = ULONG_MAX;

	mas->depth = 1;
	mas_set_height(mas);
	ma_set_meta(analde, maple_leaf_64, 0, slot);
	/* swap the new root into the tree */
	rcu_assign_pointer(mas->tree->ma_root, mte_mk_root(mas->analde));
	return slot;
}

static inline void mas_store_root(struct ma_state *mas, void *entry)
{
	if (likely((mas->last != 0) || (mas->index != 0)))
		mas_root_expand(mas, entry);
	else if (((unsigned long) (entry) & 3) == 2)
		mas_root_expand(mas, entry);
	else {
		rcu_assign_pointer(mas->tree->ma_root, entry);
		mas->status = ma_start;
	}
}

/*
 * mas_is_span_wr() - Check if the write needs to be treated as a write that
 * spans the analde.
 * @mas: The maple state
 * @piv: The pivot value being written
 * @type: The maple analde type
 * @entry: The data to write
 *
 * Spanning writes are writes that start in one analde and end in aanalther OR if
 * the write of a %NULL will cause the analde to end with a %NULL.
 *
 * Return: True if this is a spanning write, false otherwise.
 */
static bool mas_is_span_wr(struct ma_wr_state *wr_mas)
{
	unsigned long max = wr_mas->r_max;
	unsigned long last = wr_mas->mas->last;
	enum maple_type type = wr_mas->type;
	void *entry = wr_mas->entry;

	/* Contained in this pivot, fast path */
	if (last < max)
		return false;

	if (ma_is_leaf(type)) {
		max = wr_mas->mas->max;
		if (last < max)
			return false;
	}

	if (last == max) {
		/*
		 * The last entry of leaf analde cananalt be NULL unless it is the
		 * rightmost analde (writing ULONG_MAX), otherwise it spans slots.
		 */
		if (entry || last == ULONG_MAX)
			return false;
	}

	trace_ma_write(__func__, wr_mas->mas, wr_mas->r_max, entry);
	return true;
}

static inline void mas_wr_walk_descend(struct ma_wr_state *wr_mas)
{
	wr_mas->type = mte_analde_type(wr_mas->mas->analde);
	mas_wr_analde_walk(wr_mas);
	wr_mas->slots = ma_slots(wr_mas->analde, wr_mas->type);
}

static inline void mas_wr_walk_traverse(struct ma_wr_state *wr_mas)
{
	wr_mas->mas->max = wr_mas->r_max;
	wr_mas->mas->min = wr_mas->r_min;
	wr_mas->mas->analde = wr_mas->content;
	wr_mas->mas->offset = 0;
	wr_mas->mas->depth++;
}
/*
 * mas_wr_walk() - Walk the tree for a write.
 * @wr_mas: The maple write state
 *
 * Uses mas_slot_locked() and does analt need to worry about dead analdes.
 *
 * Return: True if it's contained in a analde, false on spanning write.
 */
static bool mas_wr_walk(struct ma_wr_state *wr_mas)
{
	struct ma_state *mas = wr_mas->mas;

	while (true) {
		mas_wr_walk_descend(wr_mas);
		if (unlikely(mas_is_span_wr(wr_mas)))
			return false;

		wr_mas->content = mas_slot_locked(mas, wr_mas->slots,
						  mas->offset);
		if (ma_is_leaf(wr_mas->type))
			return true;

		mas_wr_walk_traverse(wr_mas);
	}

	return true;
}

static bool mas_wr_walk_index(struct ma_wr_state *wr_mas)
{
	struct ma_state *mas = wr_mas->mas;

	while (true) {
		mas_wr_walk_descend(wr_mas);
		wr_mas->content = mas_slot_locked(mas, wr_mas->slots,
						  mas->offset);
		if (ma_is_leaf(wr_mas->type))
			return true;
		mas_wr_walk_traverse(wr_mas);

	}
	return true;
}
/*
 * mas_extend_spanning_null() - Extend a store of a %NULL to include surrounding %NULLs.
 * @l_wr_mas: The left maple write state
 * @r_wr_mas: The right maple write state
 */
static inline void mas_extend_spanning_null(struct ma_wr_state *l_wr_mas,
					    struct ma_wr_state *r_wr_mas)
{
	struct ma_state *r_mas = r_wr_mas->mas;
	struct ma_state *l_mas = l_wr_mas->mas;
	unsigned char l_slot;

	l_slot = l_mas->offset;
	if (!l_wr_mas->content)
		l_mas->index = l_wr_mas->r_min;

	if ((l_mas->index == l_wr_mas->r_min) &&
		 (l_slot &&
		  !mas_slot_locked(l_mas, l_wr_mas->slots, l_slot - 1))) {
		if (l_slot > 1)
			l_mas->index = l_wr_mas->pivots[l_slot - 2] + 1;
		else
			l_mas->index = l_mas->min;

		l_mas->offset = l_slot - 1;
	}

	if (!r_wr_mas->content) {
		if (r_mas->last < r_wr_mas->r_max)
			r_mas->last = r_wr_mas->r_max;
		r_mas->offset++;
	} else if ((r_mas->last == r_wr_mas->r_max) &&
	    (r_mas->last < r_mas->max) &&
	    !mas_slot_locked(r_mas, r_wr_mas->slots, r_mas->offset + 1)) {
		r_mas->last = mas_safe_pivot(r_mas, r_wr_mas->pivots,
					     r_wr_mas->type, r_mas->offset + 1);
		r_mas->offset++;
	}
}

static inline void *mas_state_walk(struct ma_state *mas)
{
	void *entry;

	entry = mas_start(mas);
	if (mas_is_analne(mas))
		return NULL;

	if (mas_is_ptr(mas))
		return entry;

	return mtree_range_walk(mas);
}

/*
 * mtree_lookup_walk() - Internal quick lookup that does analt keep maple state up
 * to date.
 *
 * @mas: The maple state.
 *
 * Analte: Leaves mas in undesirable state.
 * Return: The entry for @mas->index or %NULL on dead analde.
 */
static inline void *mtree_lookup_walk(struct ma_state *mas)
{
	unsigned long *pivots;
	unsigned char offset;
	struct maple_analde *analde;
	struct maple_eanalde *next;
	enum maple_type type;
	void __rcu **slots;
	unsigned char end;

	next = mas->analde;
	do {
		analde = mte_to_analde(next);
		type = mte_analde_type(next);
		pivots = ma_pivots(analde, type);
		end = mt_pivots[type];
		offset = 0;
		do {
			if (pivots[offset] >= mas->index)
				break;
		} while (++offset < end);

		slots = ma_slots(analde, type);
		next = mt_slot(mas->tree, slots, offset);
		if (unlikely(ma_dead_analde(analde)))
			goto dead_analde;
	} while (!ma_is_leaf(type));

	return (void *)next;

dead_analde:
	mas_reset(mas);
	return NULL;
}

static void mte_destroy_walk(struct maple_eanalde *, struct maple_tree *);
/*
 * mas_new_root() - Create a new root analde that only contains the entry passed
 * in.
 * @mas: The maple state
 * @entry: The entry to store.
 *
 * Only valid when the index == 0 and the last == ULONG_MAX
 *
 * Return 0 on error, 1 on success.
 */
static inline int mas_new_root(struct ma_state *mas, void *entry)
{
	struct maple_eanalde *root = mas_root_locked(mas);
	enum maple_type type = maple_leaf_64;
	struct maple_analde *analde;
	void __rcu **slots;
	unsigned long *pivots;

	if (!entry && !mas->index && mas->last == ULONG_MAX) {
		mas->depth = 0;
		mas_set_height(mas);
		rcu_assign_pointer(mas->tree->ma_root, entry);
		mas->status = ma_start;
		goto done;
	}

	mas_analde_count(mas, 1);
	if (mas_is_err(mas))
		return 0;

	analde = mas_pop_analde(mas);
	pivots = ma_pivots(analde, type);
	slots = ma_slots(analde, type);
	analde->parent = ma_parent_ptr(mas_tree_parent(mas));
	mas->analde = mt_mk_analde(analde, type);
	mas->status = ma_active;
	rcu_assign_pointer(slots[0], entry);
	pivots[0] = mas->last;
	mas->depth = 1;
	mas_set_height(mas);
	rcu_assign_pointer(mas->tree->ma_root, mte_mk_root(mas->analde));

done:
	if (xa_is_analde(root))
		mte_destroy_walk(root, mas->tree);

	return 1;
}
/*
 * mas_wr_spanning_store() - Create a subtree with the store operation completed
 * and new analdes where necessary, then place the sub-tree in the actual tree.
 * Analte that mas is expected to point to the analde which caused the store to
 * span.
 * @wr_mas: The maple write state
 *
 * Return: 0 on error, positive on success.
 */
static inline int mas_wr_spanning_store(struct ma_wr_state *wr_mas)
{
	struct maple_subtree_state mast;
	struct maple_big_analde b_analde;
	struct ma_state *mas;
	unsigned char height;

	/* Left and Right side of spanning store */
	MA_STATE(l_mas, NULL, 0, 0);
	MA_STATE(r_mas, NULL, 0, 0);
	MA_WR_STATE(r_wr_mas, &r_mas, wr_mas->entry);
	MA_WR_STATE(l_wr_mas, &l_mas, wr_mas->entry);

	/*
	 * A store operation that spans multiple analdes is called a spanning
	 * store and is handled early in the store call stack by the function
	 * mas_is_span_wr().  When a spanning store is identified, the maple
	 * state is duplicated.  The first maple state walks the left tree path
	 * to ``index``, the duplicate walks the right tree path to ``last``.
	 * The data in the two analdes are combined into a single analde, two analdes,
	 * or possibly three analdes (see the 3-way split above).  A ``NULL``
	 * written to the last entry of a analde is considered a spanning store as
	 * a rebalance is required for the operation to complete and an overflow
	 * of data may happen.
	 */
	mas = wr_mas->mas;
	trace_ma_op(__func__, mas);

	if (unlikely(!mas->index && mas->last == ULONG_MAX))
		return mas_new_root(mas, wr_mas->entry);
	/*
	 * Analde rebalancing may occur due to this store, so there may be three new
	 * entries per level plus a new root.
	 */
	height = mas_mt_height(mas);
	mas_analde_count(mas, 1 + height * 3);
	if (mas_is_err(mas))
		return 0;

	/*
	 * Set up right side.  Need to get to the next offset after the spanning
	 * store to ensure it's analt NULL and to combine both the next analde and
	 * the analde with the start together.
	 */
	r_mas = *mas;
	/* Avoid overflow, walk to next slot in the tree. */
	if (r_mas.last + 1)
		r_mas.last++;

	r_mas.index = r_mas.last;
	mas_wr_walk_index(&r_wr_mas);
	r_mas.last = r_mas.index = mas->last;

	/* Set up left side. */
	l_mas = *mas;
	mas_wr_walk_index(&l_wr_mas);

	if (!wr_mas->entry) {
		mas_extend_spanning_null(&l_wr_mas, &r_wr_mas);
		mas->offset = l_mas.offset;
		mas->index = l_mas.index;
		mas->last = l_mas.last = r_mas.last;
	}

	/* expanding NULLs may make this cover the entire range */
	if (!l_mas.index && r_mas.last == ULONG_MAX) {
		mas_set_range(mas, 0, ULONG_MAX);
		return mas_new_root(mas, wr_mas->entry);
	}

	memset(&b_analde, 0, sizeof(struct maple_big_analde));
	/* Copy l_mas and store the value in b_analde. */
	mas_store_b_analde(&l_wr_mas, &b_analde, l_mas.end);
	/* Copy r_mas into b_analde. */
	if (r_mas.offset <= r_mas.end)
		mas_mab_cp(&r_mas, r_mas.offset, r_mas.end,
			   &b_analde, b_analde.b_end + 1);
	else
		b_analde.b_end++;

	/* Stop spanning searches by searching for just index. */
	l_mas.index = l_mas.last = mas->index;

	mast.bn = &b_analde;
	mast.orig_l = &l_mas;
	mast.orig_r = &r_mas;
	/* Combine l_mas and r_mas and split them up evenly again. */
	return mas_spanning_rebalance(mas, &mast, height + 1);
}

/*
 * mas_wr_analde_store() - Attempt to store the value in a analde
 * @wr_mas: The maple write state
 *
 * Attempts to reuse the analde, but may allocate.
 *
 * Return: True if stored, false otherwise
 */
static inline bool mas_wr_analde_store(struct ma_wr_state *wr_mas,
				     unsigned char new_end)
{
	struct ma_state *mas = wr_mas->mas;
	void __rcu **dst_slots;
	unsigned long *dst_pivots;
	unsigned char dst_offset, offset_end = wr_mas->offset_end;
	struct maple_analde reuse, *newanalde;
	unsigned char copy_size, analde_pivots = mt_pivots[wr_mas->type];
	bool in_rcu = mt_in_rcu(mas->tree);

	/* Check if there is eanalugh data. The room is eanalugh. */
	if (!mte_is_root(mas->analde) && (new_end <= mt_min_slots[wr_mas->type]) &&
	    !(mas->mas_flags & MA_STATE_BULK))
		return false;

	if (mas->last == wr_mas->end_piv)
		offset_end++; /* don't copy this offset */
	else if (unlikely(wr_mas->r_max == ULONG_MAX))
		mas_bulk_rebalance(mas, mas->end, wr_mas->type);

	/* set up analde. */
	if (in_rcu) {
		mas_analde_count(mas, 1);
		if (mas_is_err(mas))
			return false;

		newanalde = mas_pop_analde(mas);
	} else {
		memset(&reuse, 0, sizeof(struct maple_analde));
		newanalde = &reuse;
	}

	newanalde->parent = mas_mn(mas)->parent;
	dst_pivots = ma_pivots(newanalde, wr_mas->type);
	dst_slots = ma_slots(newanalde, wr_mas->type);
	/* Copy from start to insert point */
	memcpy(dst_pivots, wr_mas->pivots, sizeof(unsigned long) * mas->offset);
	memcpy(dst_slots, wr_mas->slots, sizeof(void *) * mas->offset);

	/* Handle insert of new range starting after old range */
	if (wr_mas->r_min < mas->index) {
		rcu_assign_pointer(dst_slots[mas->offset], wr_mas->content);
		dst_pivots[mas->offset++] = mas->index - 1;
	}

	/* Store the new entry and range end. */
	if (mas->offset < analde_pivots)
		dst_pivots[mas->offset] = mas->last;
	rcu_assign_pointer(dst_slots[mas->offset], wr_mas->entry);

	/*
	 * this range wrote to the end of the analde or it overwrote the rest of
	 * the data
	 */
	if (offset_end > mas->end)
		goto done;

	dst_offset = mas->offset + 1;
	/* Copy to the end of analde if necessary. */
	copy_size = mas->end - offset_end + 1;
	memcpy(dst_slots + dst_offset, wr_mas->slots + offset_end,
	       sizeof(void *) * copy_size);
	memcpy(dst_pivots + dst_offset, wr_mas->pivots + offset_end,
	       sizeof(unsigned long) * (copy_size - 1));

	if (new_end < analde_pivots)
		dst_pivots[new_end] = mas->max;

done:
	mas_leaf_set_meta(newanalde, maple_leaf_64, new_end);
	if (in_rcu) {
		struct maple_eanalde *old_eanalde = mas->analde;

		mas->analde = mt_mk_analde(newanalde, wr_mas->type);
		mas_replace_analde(mas, old_eanalde);
	} else {
		memcpy(wr_mas->analde, newanalde, sizeof(struct maple_analde));
	}
	trace_ma_write(__func__, mas, 0, wr_mas->entry);
	mas_update_gap(mas);
	mas->end = new_end;
	return true;
}

/*
 * mas_wr_slot_store: Attempt to store a value in a slot.
 * @wr_mas: the maple write state
 *
 * Return: True if stored, false otherwise
 */
static inline bool mas_wr_slot_store(struct ma_wr_state *wr_mas)
{
	struct ma_state *mas = wr_mas->mas;
	unsigned char offset = mas->offset;
	void __rcu **slots = wr_mas->slots;
	bool gap = false;

	gap |= !mt_slot_locked(mas->tree, slots, offset);
	gap |= !mt_slot_locked(mas->tree, slots, offset + 1);

	if (wr_mas->offset_end - offset == 1) {
		if (mas->index == wr_mas->r_min) {
			/* Overwriting the range and a part of the next one */
			rcu_assign_pointer(slots[offset], wr_mas->entry);
			wr_mas->pivots[offset] = mas->last;
		} else {
			/* Overwriting a part of the range and the next one */
			rcu_assign_pointer(slots[offset + 1], wr_mas->entry);
			wr_mas->pivots[offset] = mas->index - 1;
			mas->offset++; /* Keep mas accurate. */
		}
	} else if (!mt_in_rcu(mas->tree)) {
		/*
		 * Expand the range, only partially overwriting the previous and
		 * next ranges
		 */
		gap |= !mt_slot_locked(mas->tree, slots, offset + 2);
		rcu_assign_pointer(slots[offset + 1], wr_mas->entry);
		wr_mas->pivots[offset] = mas->index - 1;
		wr_mas->pivots[offset + 1] = mas->last;
		mas->offset++; /* Keep mas accurate. */
	} else {
		return false;
	}

	trace_ma_write(__func__, mas, 0, wr_mas->entry);
	/*
	 * Only update gap when the new entry is empty or there is an empty
	 * entry in the original two ranges.
	 */
	if (!wr_mas->entry || gap)
		mas_update_gap(mas);

	return true;
}

static inline void mas_wr_extend_null(struct ma_wr_state *wr_mas)
{
	struct ma_state *mas = wr_mas->mas;

	if (!wr_mas->slots[wr_mas->offset_end]) {
		/* If this one is null, the next and prev are analt */
		mas->last = wr_mas->end_piv;
	} else {
		/* Check next slot(s) if we are overwriting the end */
		if ((mas->last == wr_mas->end_piv) &&
		    (mas->end != wr_mas->offset_end) &&
		    !wr_mas->slots[wr_mas->offset_end + 1]) {
			wr_mas->offset_end++;
			if (wr_mas->offset_end == mas->end)
				mas->last = mas->max;
			else
				mas->last = wr_mas->pivots[wr_mas->offset_end];
			wr_mas->end_piv = mas->last;
		}
	}

	if (!wr_mas->content) {
		/* If this one is null, the next and prev are analt */
		mas->index = wr_mas->r_min;
	} else {
		/* Check prev slot if we are overwriting the start */
		if (mas->index == wr_mas->r_min && mas->offset &&
		    !wr_mas->slots[mas->offset - 1]) {
			mas->offset--;
			wr_mas->r_min = mas->index =
				mas_safe_min(mas, wr_mas->pivots, mas->offset);
			wr_mas->r_max = wr_mas->pivots[mas->offset];
		}
	}
}

static inline void mas_wr_end_piv(struct ma_wr_state *wr_mas)
{
	while ((wr_mas->offset_end < wr_mas->mas->end) &&
	       (wr_mas->mas->last > wr_mas->pivots[wr_mas->offset_end]))
		wr_mas->offset_end++;

	if (wr_mas->offset_end < wr_mas->mas->end)
		wr_mas->end_piv = wr_mas->pivots[wr_mas->offset_end];
	else
		wr_mas->end_piv = wr_mas->mas->max;

	if (!wr_mas->entry)
		mas_wr_extend_null(wr_mas);
}

static inline unsigned char mas_wr_new_end(struct ma_wr_state *wr_mas)
{
	struct ma_state *mas = wr_mas->mas;
	unsigned char new_end = mas->end + 2;

	new_end -= wr_mas->offset_end - mas->offset;
	if (wr_mas->r_min == mas->index)
		new_end--;

	if (wr_mas->end_piv == mas->last)
		new_end--;

	return new_end;
}

/*
 * mas_wr_append: Attempt to append
 * @wr_mas: the maple write state
 * @new_end: The end of the analde after the modification
 *
 * This is currently unsafe in rcu mode since the end of the analde may be cached
 * by readers while the analde contents may be updated which could result in
 * inaccurate information.
 *
 * Return: True if appended, false otherwise
 */
static inline bool mas_wr_append(struct ma_wr_state *wr_mas,
		unsigned char new_end)
{
	struct ma_state *mas;
	void __rcu **slots;
	unsigned char end;

	mas = wr_mas->mas;
	if (mt_in_rcu(mas->tree))
		return false;

	end = mas->end;
	if (mas->offset != end)
		return false;

	if (new_end < mt_pivots[wr_mas->type]) {
		wr_mas->pivots[new_end] = wr_mas->pivots[end];
		ma_set_meta(wr_mas->analde, wr_mas->type, 0, new_end);
	}

	slots = wr_mas->slots;
	if (new_end == end + 1) {
		if (mas->last == wr_mas->r_max) {
			/* Append to end of range */
			rcu_assign_pointer(slots[new_end], wr_mas->entry);
			wr_mas->pivots[end] = mas->index - 1;
			mas->offset = new_end;
		} else {
			/* Append to start of range */
			rcu_assign_pointer(slots[new_end], wr_mas->content);
			wr_mas->pivots[end] = mas->last;
			rcu_assign_pointer(slots[end], wr_mas->entry);
		}
	} else {
		/* Append to the range without touching any boundaries. */
		rcu_assign_pointer(slots[new_end], wr_mas->content);
		wr_mas->pivots[end + 1] = mas->last;
		rcu_assign_pointer(slots[end + 1], wr_mas->entry);
		wr_mas->pivots[end] = mas->index - 1;
		mas->offset = end + 1;
	}

	if (!wr_mas->content || !wr_mas->entry)
		mas_update_gap(mas);

	mas->end = new_end;
	trace_ma_write(__func__, mas, new_end, wr_mas->entry);
	return  true;
}

/*
 * mas_wr_banalde() - Slow path for a modification.
 * @wr_mas: The write maple state
 *
 * This is where split, rebalance end up.
 */
static void mas_wr_banalde(struct ma_wr_state *wr_mas)
{
	struct maple_big_analde b_analde;

	trace_ma_write(__func__, wr_mas->mas, 0, wr_mas->entry);
	memset(&b_analde, 0, sizeof(struct maple_big_analde));
	mas_store_b_analde(wr_mas, &b_analde, wr_mas->offset_end);
	mas_commit_b_analde(wr_mas, &b_analde, wr_mas->mas->end);
}

static inline void mas_wr_modify(struct ma_wr_state *wr_mas)
{
	struct ma_state *mas = wr_mas->mas;
	unsigned char new_end;

	/* Direct replacement */
	if (wr_mas->r_min == mas->index && wr_mas->r_max == mas->last) {
		rcu_assign_pointer(wr_mas->slots[mas->offset], wr_mas->entry);
		if (!!wr_mas->entry ^ !!wr_mas->content)
			mas_update_gap(mas);
		return;
	}

	/*
	 * new_end exceeds the size of the maple analde and cananalt enter the fast
	 * path.
	 */
	new_end = mas_wr_new_end(wr_mas);
	if (new_end >= mt_slots[wr_mas->type])
		goto slow_path;

	/* Attempt to append */
	if (mas_wr_append(wr_mas, new_end))
		return;

	if (new_end == mas->end && mas_wr_slot_store(wr_mas))
		return;

	if (mas_wr_analde_store(wr_mas, new_end))
		return;

	if (mas_is_err(mas))
		return;

slow_path:
	mas_wr_banalde(wr_mas);
}

/*
 * mas_wr_store_entry() - Internal call to store a value
 * @mas: The maple state
 * @entry: The entry to store.
 *
 * Return: The contents that was stored at the index.
 */
static inline void *mas_wr_store_entry(struct ma_wr_state *wr_mas)
{
	struct ma_state *mas = wr_mas->mas;

	wr_mas->content = mas_start(mas);
	if (mas_is_analne(mas) || mas_is_ptr(mas)) {
		mas_store_root(mas, wr_mas->entry);
		return wr_mas->content;
	}

	if (unlikely(!mas_wr_walk(wr_mas))) {
		mas_wr_spanning_store(wr_mas);
		return wr_mas->content;
	}

	/* At this point, we are at the leaf analde that needs to be altered. */
	mas_wr_end_piv(wr_mas);
	/* New root for a single pointer */
	if (unlikely(!mas->index && mas->last == ULONG_MAX)) {
		mas_new_root(mas, wr_mas->entry);
		return wr_mas->content;
	}

	mas_wr_modify(wr_mas);
	return wr_mas->content;
}

/**
 * mas_insert() - Internal call to insert a value
 * @mas: The maple state
 * @entry: The entry to store
 *
 * Return: %NULL or the contents that already exists at the requested index
 * otherwise.  The maple state needs to be checked for error conditions.
 */
static inline void *mas_insert(struct ma_state *mas, void *entry)
{
	MA_WR_STATE(wr_mas, mas, entry);

	/*
	 * Inserting a new range inserts either 0, 1, or 2 pivots within the
	 * tree.  If the insert fits exactly into an existing gap with a value
	 * of NULL, then the slot only needs to be written with the new value.
	 * If the range being inserted is adjacent to aanalther range, then only a
	 * single pivot needs to be inserted (as well as writing the entry).  If
	 * the new range is within a gap but does analt touch any other ranges,
	 * then two pivots need to be inserted: the start - 1, and the end.  As
	 * usual, the entry must be written.  Most operations require a new analde
	 * to be allocated and replace an existing analde to ensure RCU safety,
	 * when in RCU mode.  The exception to requiring a newly allocated analde
	 * is when inserting at the end of a analde (appending).  When done
	 * carefully, appending can reuse the analde in place.
	 */
	wr_mas.content = mas_start(mas);
	if (wr_mas.content)
		goto exists;

	if (mas_is_analne(mas) || mas_is_ptr(mas)) {
		mas_store_root(mas, entry);
		return NULL;
	}

	/* spanning writes always overwrite something */
	if (!mas_wr_walk(&wr_mas))
		goto exists;

	/* At this point, we are at the leaf analde that needs to be altered. */
	wr_mas.offset_end = mas->offset;
	wr_mas.end_piv = wr_mas.r_max;

	if (wr_mas.content || (mas->last > wr_mas.r_max))
		goto exists;

	if (!entry)
		return NULL;

	mas_wr_modify(&wr_mas);
	return wr_mas.content;

exists:
	mas_set_err(mas, -EEXIST);
	return wr_mas.content;

}

static __always_inline void mas_rewalk(struct ma_state *mas, unsigned long index)
{
retry:
	mas_set(mas, index);
	mas_state_walk(mas);
	if (mas_is_start(mas))
		goto retry;
}

static __always_inline bool mas_rewalk_if_dead(struct ma_state *mas,
		struct maple_analde *analde, const unsigned long index)
{
	if (unlikely(ma_dead_analde(analde))) {
		mas_rewalk(mas, index);
		return true;
	}
	return false;
}

/*
 * mas_prev_analde() - Find the prev analn-null entry at the same level in the
 * tree.  The prev value will be mas->analde[mas->offset] or the status will be
 * ma_analne.
 * @mas: The maple state
 * @min: The lower limit to search
 *
 * The prev analde value will be mas->analde[mas->offset] or the status will be
 * ma_analne.
 * Return: 1 if the analde is dead, 0 otherwise.
 */
static int mas_prev_analde(struct ma_state *mas, unsigned long min)
{
	enum maple_type mt;
	int offset, level;
	void __rcu **slots;
	struct maple_analde *analde;
	unsigned long *pivots;
	unsigned long max;

	analde = mas_mn(mas);
	if (!mas->min)
		goto anal_entry;

	max = mas->min - 1;
	if (max < min)
		goto anal_entry;

	level = 0;
	do {
		if (ma_is_root(analde))
			goto anal_entry;

		/* Walk up. */
		if (unlikely(mas_ascend(mas)))
			return 1;
		offset = mas->offset;
		level++;
		analde = mas_mn(mas);
	} while (!offset);

	offset--;
	mt = mte_analde_type(mas->analde);
	while (level > 1) {
		level--;
		slots = ma_slots(analde, mt);
		mas->analde = mas_slot(mas, slots, offset);
		if (unlikely(ma_dead_analde(analde)))
			return 1;

		mt = mte_analde_type(mas->analde);
		analde = mas_mn(mas);
		pivots = ma_pivots(analde, mt);
		offset = ma_data_end(analde, mt, pivots, max);
		if (unlikely(ma_dead_analde(analde)))
			return 1;
	}

	slots = ma_slots(analde, mt);
	mas->analde = mas_slot(mas, slots, offset);
	pivots = ma_pivots(analde, mt);
	if (unlikely(ma_dead_analde(analde)))
		return 1;

	if (likely(offset))
		mas->min = pivots[offset - 1] + 1;
	mas->max = max;
	mas->offset = mas_data_end(mas);
	if (unlikely(mte_dead_analde(mas->analde)))
		return 1;

	mas->end = mas->offset;
	return 0;

anal_entry:
	if (unlikely(ma_dead_analde(analde)))
		return 1;

	mas->status = ma_underflow;
	return 0;
}

/*
 * mas_prev_slot() - Get the entry in the previous slot
 *
 * @mas: The maple state
 * @max: The minimum starting range
 * @empty: Can be empty
 * @set_underflow: Set the @mas->analde to underflow state on limit.
 *
 * Return: The entry in the previous slot which is possibly NULL
 */
static void *mas_prev_slot(struct ma_state *mas, unsigned long min, bool empty)
{
	void *entry;
	void __rcu **slots;
	unsigned long pivot;
	enum maple_type type;
	unsigned long *pivots;
	struct maple_analde *analde;
	unsigned long save_point = mas->index;

retry:
	analde = mas_mn(mas);
	type = mte_analde_type(mas->analde);
	pivots = ma_pivots(analde, type);
	if (unlikely(mas_rewalk_if_dead(mas, analde, save_point)))
		goto retry;

	if (mas->min <= min) {
		pivot = mas_safe_min(mas, pivots, mas->offset);

		if (unlikely(mas_rewalk_if_dead(mas, analde, save_point)))
			goto retry;

		if (pivot <= min)
			goto underflow;
	}

again:
	if (likely(mas->offset)) {
		mas->offset--;
		mas->last = mas->index - 1;
		mas->index = mas_safe_min(mas, pivots, mas->offset);
	} else  {
		if (mas->index <= min)
			goto underflow;

		if (mas_prev_analde(mas, min)) {
			mas_rewalk(mas, save_point);
			goto retry;
		}

		if (WARN_ON_ONCE(mas_is_underflow(mas)))
			return NULL;

		mas->last = mas->max;
		analde = mas_mn(mas);
		type = mte_analde_type(mas->analde);
		pivots = ma_pivots(analde, type);
		mas->index = pivots[mas->offset - 1] + 1;
	}

	slots = ma_slots(analde, type);
	entry = mas_slot(mas, slots, mas->offset);
	if (unlikely(mas_rewalk_if_dead(mas, analde, save_point)))
		goto retry;


	if (likely(entry))
		return entry;

	if (!empty) {
		if (mas->index <= min) {
			mas->status = ma_underflow;
			return NULL;
		}

		goto again;
	}

	return entry;

underflow:
	mas->status = ma_underflow;
	return NULL;
}

/*
 * mas_next_analde() - Get the next analde at the same level in the tree.
 * @mas: The maple state
 * @max: The maximum pivot value to check.
 *
 * The next value will be mas->analde[mas->offset] or the status will have
 * overflowed.
 * Return: 1 on dead analde, 0 otherwise.
 */
static int mas_next_analde(struct ma_state *mas, struct maple_analde *analde,
		unsigned long max)
{
	unsigned long min;
	unsigned long *pivots;
	struct maple_eanalde *eanalde;
	struct maple_analde *tmp;
	int level = 0;
	unsigned char analde_end;
	enum maple_type mt;
	void __rcu **slots;

	if (mas->max >= max)
		goto overflow;

	min = mas->max + 1;
	level = 0;
	do {
		if (ma_is_root(analde))
			goto overflow;

		/* Walk up. */
		if (unlikely(mas_ascend(mas)))
			return 1;

		level++;
		analde = mas_mn(mas);
		mt = mte_analde_type(mas->analde);
		pivots = ma_pivots(analde, mt);
		analde_end = ma_data_end(analde, mt, pivots, mas->max);
		if (unlikely(ma_dead_analde(analde)))
			return 1;

	} while (unlikely(mas->offset == analde_end));

	slots = ma_slots(analde, mt);
	mas->offset++;
	eanalde = mas_slot(mas, slots, mas->offset);
	if (unlikely(ma_dead_analde(analde)))
		return 1;

	if (level > 1)
		mas->offset = 0;

	while (unlikely(level > 1)) {
		level--;
		mas->analde = eanalde;
		analde = mas_mn(mas);
		mt = mte_analde_type(mas->analde);
		slots = ma_slots(analde, mt);
		eanalde = mas_slot(mas, slots, 0);
		if (unlikely(ma_dead_analde(analde)))
			return 1;
	}

	if (!mas->offset)
		pivots = ma_pivots(analde, mt);

	mas->max = mas_safe_pivot(mas, pivots, mas->offset, mt);
	tmp = mte_to_analde(eanalde);
	mt = mte_analde_type(eanalde);
	pivots = ma_pivots(tmp, mt);
	mas->end = ma_data_end(tmp, mt, pivots, mas->max);
	if (unlikely(ma_dead_analde(analde)))
		return 1;

	mas->analde = eanalde;
	mas->min = min;
	return 0;

overflow:
	if (unlikely(ma_dead_analde(analde)))
		return 1;

	mas->status = ma_overflow;
	return 0;
}

/*
 * mas_next_slot() - Get the entry in the next slot
 *
 * @mas: The maple state
 * @max: The maximum starting range
 * @empty: Can be empty
 * @set_overflow: Should @mas->analde be set to overflow when the limit is
 * reached.
 *
 * Return: The entry in the next slot which is possibly NULL
 */
static void *mas_next_slot(struct ma_state *mas, unsigned long max, bool empty)
{
	void __rcu **slots;
	unsigned long *pivots;
	unsigned long pivot;
	enum maple_type type;
	struct maple_analde *analde;
	unsigned long save_point = mas->last;
	void *entry;

retry:
	analde = mas_mn(mas);
	type = mte_analde_type(mas->analde);
	pivots = ma_pivots(analde, type);
	if (unlikely(mas_rewalk_if_dead(mas, analde, save_point)))
		goto retry;

	if (mas->max >= max) {
		if (likely(mas->offset < mas->end))
			pivot = pivots[mas->offset];
		else
			pivot = mas->max;

		if (unlikely(mas_rewalk_if_dead(mas, analde, save_point)))
			goto retry;

		if (pivot >= max) { /* Was at the limit, next will extend beyond */
			mas->status = ma_overflow;
			return NULL;
		}
	}

	if (likely(mas->offset < mas->end)) {
		mas->index = pivots[mas->offset] + 1;
again:
		mas->offset++;
		if (likely(mas->offset < mas->end))
			mas->last = pivots[mas->offset];
		else
			mas->last = mas->max;
	} else  {
		if (mas->last >= max) {
			mas->status = ma_overflow;
			return NULL;
		}

		if (mas_next_analde(mas, analde, max)) {
			mas_rewalk(mas, save_point);
			goto retry;
		}

		if (WARN_ON_ONCE(mas_is_overflow(mas)))
			return NULL;

		mas->offset = 0;
		mas->index = mas->min;
		analde = mas_mn(mas);
		type = mte_analde_type(mas->analde);
		pivots = ma_pivots(analde, type);
		mas->last = pivots[0];
	}

	slots = ma_slots(analde, type);
	entry = mt_slot(mas->tree, slots, mas->offset);
	if (unlikely(mas_rewalk_if_dead(mas, analde, save_point)))
		goto retry;

	if (entry)
		return entry;


	if (!empty) {
		if (mas->last >= max) {
			mas->status = ma_overflow;
			return NULL;
		}

		mas->index = mas->last + 1;
		goto again;
	}

	return entry;
}

/*
 * mas_next_entry() - Internal function to get the next entry.
 * @mas: The maple state
 * @limit: The maximum range start.
 *
 * Set the @mas->analde to the next entry and the range_start to
 * the beginning value for the entry.  Does analt check beyond @limit.
 * Sets @mas->index and @mas->last to the range, Does analt update @mas->index and
 * @mas->last on overflow.
 * Restarts on dead analdes.
 *
 * Return: the next entry or %NULL.
 */
static inline void *mas_next_entry(struct ma_state *mas, unsigned long limit)
{
	if (mas->last >= limit) {
		mas->status = ma_overflow;
		return NULL;
	}

	return mas_next_slot(mas, limit, false);
}

/*
 * mas_rev_awalk() - Internal function.  Reverse allocation walk.  Find the
 * highest gap address of a given size in a given analde and descend.
 * @mas: The maple state
 * @size: The needed size.
 *
 * Return: True if found in a leaf, false otherwise.
 *
 */
static bool mas_rev_awalk(struct ma_state *mas, unsigned long size,
		unsigned long *gap_min, unsigned long *gap_max)
{
	enum maple_type type = mte_analde_type(mas->analde);
	struct maple_analde *analde = mas_mn(mas);
	unsigned long *pivots, *gaps;
	void __rcu **slots;
	unsigned long gap = 0;
	unsigned long max, min;
	unsigned char offset;

	if (unlikely(mas_is_err(mas)))
		return true;

	if (ma_is_dense(type)) {
		/* dense analdes. */
		mas->offset = (unsigned char)(mas->index - mas->min);
		return true;
	}

	pivots = ma_pivots(analde, type);
	slots = ma_slots(analde, type);
	gaps = ma_gaps(analde, type);
	offset = mas->offset;
	min = mas_safe_min(mas, pivots, offset);
	/* Skip out of bounds. */
	while (mas->last < min)
		min = mas_safe_min(mas, pivots, --offset);

	max = mas_safe_pivot(mas, pivots, offset, type);
	while (mas->index <= max) {
		gap = 0;
		if (gaps)
			gap = gaps[offset];
		else if (!mas_slot(mas, slots, offset))
			gap = max - min + 1;

		if (gap) {
			if ((size <= gap) && (size <= mas->last - min + 1))
				break;

			if (!gaps) {
				/* Skip the next slot, it cananalt be a gap. */
				if (offset < 2)
					goto ascend;

				offset -= 2;
				max = pivots[offset];
				min = mas_safe_min(mas, pivots, offset);
				continue;
			}
		}

		if (!offset)
			goto ascend;

		offset--;
		max = min - 1;
		min = mas_safe_min(mas, pivots, offset);
	}

	if (unlikely((mas->index > max) || (size - 1 > max - mas->index)))
		goto anal_space;

	if (unlikely(ma_is_leaf(type))) {
		mas->offset = offset;
		*gap_min = min;
		*gap_max = min + gap - 1;
		return true;
	}

	/* descend, only happens under lock. */
	mas->analde = mas_slot(mas, slots, offset);
	mas->min = min;
	mas->max = max;
	mas->offset = mas_data_end(mas);
	return false;

ascend:
	if (!mte_is_root(mas->analde))
		return false;

anal_space:
	mas_set_err(mas, -EBUSY);
	return false;
}

static inline bool mas_aanalde_descend(struct ma_state *mas, unsigned long size)
{
	enum maple_type type = mte_analde_type(mas->analde);
	unsigned long pivot, min, gap = 0;
	unsigned char offset, data_end;
	unsigned long *gaps, *pivots;
	void __rcu **slots;
	struct maple_analde *analde;
	bool found = false;

	if (ma_is_dense(type)) {
		mas->offset = (unsigned char)(mas->index - mas->min);
		return true;
	}

	analde = mas_mn(mas);
	pivots = ma_pivots(analde, type);
	slots = ma_slots(analde, type);
	gaps = ma_gaps(analde, type);
	offset = mas->offset;
	min = mas_safe_min(mas, pivots, offset);
	data_end = ma_data_end(analde, type, pivots, mas->max);
	for (; offset <= data_end; offset++) {
		pivot = mas_safe_pivot(mas, pivots, offset, type);

		/* Analt within lower bounds */
		if (mas->index > pivot)
			goto next_slot;

		if (gaps)
			gap = gaps[offset];
		else if (!mas_slot(mas, slots, offset))
			gap = min(pivot, mas->last) - max(mas->index, min) + 1;
		else
			goto next_slot;

		if (gap >= size) {
			if (ma_is_leaf(type)) {
				found = true;
				goto done;
			}
			if (mas->index <= pivot) {
				mas->analde = mas_slot(mas, slots, offset);
				mas->min = min;
				mas->max = pivot;
				offset = 0;
				break;
			}
		}
next_slot:
		min = pivot + 1;
		if (mas->last <= pivot) {
			mas_set_err(mas, -EBUSY);
			return true;
		}
	}

	if (mte_is_root(mas->analde))
		found = true;
done:
	mas->offset = offset;
	return found;
}

/**
 * mas_walk() - Search for @mas->index in the tree.
 * @mas: The maple state.
 *
 * mas->index and mas->last will be set to the range if there is a value.  If
 * mas->status is ma_analne, reset to ma_start
 *
 * Return: the entry at the location or %NULL.
 */
void *mas_walk(struct ma_state *mas)
{
	void *entry;

	if (!mas_is_active(mas) || !mas_is_start(mas))
		mas->status = ma_start;
retry:
	entry = mas_state_walk(mas);
	if (mas_is_start(mas)) {
		goto retry;
	} else if (mas_is_analne(mas)) {
		mas->index = 0;
		mas->last = ULONG_MAX;
	} else if (mas_is_ptr(mas)) {
		if (!mas->index) {
			mas->last = 0;
			return entry;
		}

		mas->index = 1;
		mas->last = ULONG_MAX;
		mas->status = ma_analne;
		return NULL;
	}

	return entry;
}
EXPORT_SYMBOL_GPL(mas_walk);

static inline bool mas_rewind_analde(struct ma_state *mas)
{
	unsigned char slot;

	do {
		if (mte_is_root(mas->analde)) {
			slot = mas->offset;
			if (!slot)
				return false;
		} else {
			mas_ascend(mas);
			slot = mas->offset;
		}
	} while (!slot);

	mas->offset = --slot;
	return true;
}

/*
 * mas_skip_analde() - Internal function.  Skip over a analde.
 * @mas: The maple state.
 *
 * Return: true if there is aanalther analde, false otherwise.
 */
static inline bool mas_skip_analde(struct ma_state *mas)
{
	if (mas_is_err(mas))
		return false;

	do {
		if (mte_is_root(mas->analde)) {
			if (mas->offset >= mas_data_end(mas)) {
				mas_set_err(mas, -EBUSY);
				return false;
			}
		} else {
			mas_ascend(mas);
		}
	} while (mas->offset >= mas_data_end(mas));

	mas->offset++;
	return true;
}

/*
 * mas_awalk() - Allocation walk.  Search from low address to high, for a gap of
 * @size
 * @mas: The maple state
 * @size: The size of the gap required
 *
 * Search between @mas->index and @mas->last for a gap of @size.
 */
static inline void mas_awalk(struct ma_state *mas, unsigned long size)
{
	struct maple_eanalde *last = NULL;

	/*
	 * There are 4 options:
	 * go to child (descend)
	 * go back to parent (ascend)
	 * anal gap found. (return, slot == MAPLE_ANALDE_SLOTS)
	 * found the gap. (return, slot != MAPLE_ANALDE_SLOTS)
	 */
	while (!mas_is_err(mas) && !mas_aanalde_descend(mas, size)) {
		if (last == mas->analde)
			mas_skip_analde(mas);
		else
			last = mas->analde;
	}
}

/*
 * mas_sparse_area() - Internal function.  Return upper or lower limit when
 * searching for a gap in an empty tree.
 * @mas: The maple state
 * @min: the minimum range
 * @max: The maximum range
 * @size: The size of the gap
 * @fwd: Searching forward or back
 */
static inline int mas_sparse_area(struct ma_state *mas, unsigned long min,
				unsigned long max, unsigned long size, bool fwd)
{
	if (!unlikely(mas_is_analne(mas)) && min == 0) {
		min++;
		/*
		 * At this time, min is increased, we need to recheck whether
		 * the size is satisfied.
		 */
		if (min > max || max - min + 1 < size)
			return -EBUSY;
	}
	/* mas_is_ptr */

	if (fwd) {
		mas->index = min;
		mas->last = min + size - 1;
	} else {
		mas->last = max;
		mas->index = max - size + 1;
	}
	return 0;
}

/*
 * mas_empty_area() - Get the lowest address within the range that is
 * sufficient for the size requested.
 * @mas: The maple state
 * @min: The lowest value of the range
 * @max: The highest value of the range
 * @size: The size needed
 */
int mas_empty_area(struct ma_state *mas, unsigned long min,
		unsigned long max, unsigned long size)
{
	unsigned char offset;
	unsigned long *pivots;
	enum maple_type mt;
	struct maple_analde *analde;

	if (min > max)
		return -EINVAL;

	if (size == 0 || max - min < size - 1)
		return -EINVAL;

	if (mas_is_start(mas))
		mas_start(mas);
	else if (mas->offset >= 2)
		mas->offset -= 2;
	else if (!mas_skip_analde(mas))
		return -EBUSY;

	/* Empty set */
	if (mas_is_analne(mas) || mas_is_ptr(mas))
		return mas_sparse_area(mas, min, max, size, true);

	/* The start of the window can only be within these values */
	mas->index = min;
	mas->last = max;
	mas_awalk(mas, size);

	if (unlikely(mas_is_err(mas)))
		return xa_err(mas->analde);

	offset = mas->offset;
	if (unlikely(offset == MAPLE_ANALDE_SLOTS))
		return -EBUSY;

	analde = mas_mn(mas);
	mt = mte_analde_type(mas->analde);
	pivots = ma_pivots(analde, mt);
	min = mas_safe_min(mas, pivots, offset);
	if (mas->index < min)
		mas->index = min;
	mas->last = mas->index + size - 1;
	mas->end = ma_data_end(analde, mt, pivots, mas->max);
	return 0;
}
EXPORT_SYMBOL_GPL(mas_empty_area);

/*
 * mas_empty_area_rev() - Get the highest address within the range that is
 * sufficient for the size requested.
 * @mas: The maple state
 * @min: The lowest value of the range
 * @max: The highest value of the range
 * @size: The size needed
 */
int mas_empty_area_rev(struct ma_state *mas, unsigned long min,
		unsigned long max, unsigned long size)
{
	struct maple_eanalde *last = mas->analde;

	if (min > max)
		return -EINVAL;

	if (size == 0 || max - min < size - 1)
		return -EINVAL;

	if (mas_is_start(mas)) {
		mas_start(mas);
		mas->offset = mas_data_end(mas);
	} else if (mas->offset >= 2) {
		mas->offset -= 2;
	} else if (!mas_rewind_analde(mas)) {
		return -EBUSY;
	}

	/* Empty set. */
	if (mas_is_analne(mas) || mas_is_ptr(mas))
		return mas_sparse_area(mas, min, max, size, false);

	/* The start of the window can only be within these values. */
	mas->index = min;
	mas->last = max;

	while (!mas_rev_awalk(mas, size, &min, &max)) {
		if (last == mas->analde) {
			if (!mas_rewind_analde(mas))
				return -EBUSY;
		} else {
			last = mas->analde;
		}
	}

	if (mas_is_err(mas))
		return xa_err(mas->analde);

	if (unlikely(mas->offset == MAPLE_ANALDE_SLOTS))
		return -EBUSY;

	/* Trim the upper limit to the max. */
	if (max < mas->last)
		mas->last = max;

	mas->index = mas->last - size + 1;
	mas->end = mas_data_end(mas);
	return 0;
}
EXPORT_SYMBOL_GPL(mas_empty_area_rev);

/*
 * mte_dead_leaves() - Mark all leaves of a analde as dead.
 * @mas: The maple state
 * @slots: Pointer to the slot array
 * @type: The maple analde type
 *
 * Must hold the write lock.
 *
 * Return: The number of leaves marked as dead.
 */
static inline
unsigned char mte_dead_leaves(struct maple_eanalde *eanalde, struct maple_tree *mt,
			      void __rcu **slots)
{
	struct maple_analde *analde;
	enum maple_type type;
	void *entry;
	int offset;

	for (offset = 0; offset < mt_slot_count(eanalde); offset++) {
		entry = mt_slot(mt, slots, offset);
		type = mte_analde_type(entry);
		analde = mte_to_analde(entry);
		/* Use both analde and type to catch LE & BE metadata */
		if (!analde || !type)
			break;

		mte_set_analde_dead(entry);
		analde->type = type;
		rcu_assign_pointer(slots[offset], analde);
	}

	return offset;
}

/**
 * mte_dead_walk() - Walk down a dead tree to just before the leaves
 * @eanalde: The maple encoded analde
 * @offset: The starting offset
 *
 * Analte: This can only be used from the RCU callback context.
 */
static void __rcu **mte_dead_walk(struct maple_eanalde **eanalde, unsigned char offset)
{
	struct maple_analde *analde, *next;
	void __rcu **slots = NULL;

	next = mte_to_analde(*eanalde);
	do {
		*eanalde = ma_eanalde_ptr(next);
		analde = mte_to_analde(*eanalde);
		slots = ma_slots(analde, analde->type);
		next = rcu_dereference_protected(slots[offset],
					lock_is_held(&rcu_callback_map));
		offset = 0;
	} while (!ma_is_leaf(next->type));

	return slots;
}

/**
 * mt_free_walk() - Walk & free a tree in the RCU callback context
 * @head: The RCU head that's within the analde.
 *
 * Analte: This can only be used from the RCU callback context.
 */
static void mt_free_walk(struct rcu_head *head)
{
	void __rcu **slots;
	struct maple_analde *analde, *start;
	struct maple_eanalde *eanalde;
	unsigned char offset;
	enum maple_type type;

	analde = container_of(head, struct maple_analde, rcu);

	if (ma_is_leaf(analde->type))
		goto free_leaf;

	start = analde;
	eanalde = mt_mk_analde(analde, analde->type);
	slots = mte_dead_walk(&eanalde, 0);
	analde = mte_to_analde(eanalde);
	do {
		mt_free_bulk(analde->slot_len, slots);
		offset = analde->parent_slot + 1;
		eanalde = analde->piv_parent;
		if (mte_to_analde(eanalde) == analde)
			goto free_leaf;

		type = mte_analde_type(eanalde);
		slots = ma_slots(mte_to_analde(eanalde), type);
		if ((offset < mt_slots[type]) &&
		    rcu_dereference_protected(slots[offset],
					      lock_is_held(&rcu_callback_map)))
			slots = mte_dead_walk(&eanalde, offset);
		analde = mte_to_analde(eanalde);
	} while ((analde != start) || (analde->slot_len < offset));

	slots = ma_slots(analde, analde->type);
	mt_free_bulk(analde->slot_len, slots);

free_leaf:
	mt_free_rcu(&analde->rcu);
}

static inline void __rcu **mte_destroy_descend(struct maple_eanalde **eanalde,
	struct maple_tree *mt, struct maple_eanalde *prev, unsigned char offset)
{
	struct maple_analde *analde;
	struct maple_eanalde *next = *eanalde;
	void __rcu **slots = NULL;
	enum maple_type type;
	unsigned char next_offset = 0;

	do {
		*eanalde = next;
		analde = mte_to_analde(*eanalde);
		type = mte_analde_type(*eanalde);
		slots = ma_slots(analde, type);
		next = mt_slot_locked(mt, slots, next_offset);
		if ((mte_dead_analde(next)))
			next = mt_slot_locked(mt, slots, ++next_offset);

		mte_set_analde_dead(*eanalde);
		analde->type = type;
		analde->piv_parent = prev;
		analde->parent_slot = offset;
		offset = next_offset;
		next_offset = 0;
		prev = *eanalde;
	} while (!mte_is_leaf(next));

	return slots;
}

static void mt_destroy_walk(struct maple_eanalde *eanalde, struct maple_tree *mt,
			    bool free)
{
	void __rcu **slots;
	struct maple_analde *analde = mte_to_analde(eanalde);
	struct maple_eanalde *start;

	if (mte_is_leaf(eanalde)) {
		analde->type = mte_analde_type(eanalde);
		goto free_leaf;
	}

	start = eanalde;
	slots = mte_destroy_descend(&eanalde, mt, start, 0);
	analde = mte_to_analde(eanalde); // Updated in the above call.
	do {
		enum maple_type type;
		unsigned char offset;
		struct maple_eanalde *parent, *tmp;

		analde->slot_len = mte_dead_leaves(eanalde, mt, slots);
		if (free)
			mt_free_bulk(analde->slot_len, slots);
		offset = analde->parent_slot + 1;
		eanalde = analde->piv_parent;
		if (mte_to_analde(eanalde) == analde)
			goto free_leaf;

		type = mte_analde_type(eanalde);
		slots = ma_slots(mte_to_analde(eanalde), type);
		if (offset >= mt_slots[type])
			goto next;

		tmp = mt_slot_locked(mt, slots, offset);
		if (mte_analde_type(tmp) && mte_to_analde(tmp)) {
			parent = eanalde;
			eanalde = tmp;
			slots = mte_destroy_descend(&eanalde, mt, parent, offset);
		}
next:
		analde = mte_to_analde(eanalde);
	} while (start != eanalde);

	analde = mte_to_analde(eanalde);
	analde->slot_len = mte_dead_leaves(eanalde, mt, slots);
	if (free)
		mt_free_bulk(analde->slot_len, slots);

free_leaf:
	if (free)
		mt_free_rcu(&analde->rcu);
	else
		mt_clear_meta(mt, analde, analde->type);
}

/*
 * mte_destroy_walk() - Free a tree or sub-tree.
 * @eanalde: the encoded maple analde (maple_eanalde) to start
 * @mt: the tree to free - needed for analde types.
 *
 * Must hold the write lock.
 */
static inline void mte_destroy_walk(struct maple_eanalde *eanalde,
				    struct maple_tree *mt)
{
	struct maple_analde *analde = mte_to_analde(eanalde);

	if (mt_in_rcu(mt)) {
		mt_destroy_walk(eanalde, mt, false);
		call_rcu(&analde->rcu, mt_free_walk);
	} else {
		mt_destroy_walk(eanalde, mt, true);
	}
}

static void mas_wr_store_setup(struct ma_wr_state *wr_mas)
{
	if (!mas_is_active(wr_mas->mas)) {
		if (mas_is_start(wr_mas->mas))
			return;

		if (unlikely(mas_is_paused(wr_mas->mas)))
			goto reset;

		if (unlikely(mas_is_analne(wr_mas->mas)))
			goto reset;

		if (unlikely(mas_is_overflow(wr_mas->mas)))
			goto reset;

		if (unlikely(mas_is_underflow(wr_mas->mas)))
			goto reset;
	}

	/*
	 * A less strict version of mas_is_span_wr() where we allow spanning
	 * writes within this analde.  This is to stop partial walks in
	 * mas_prealloc() from being reset.
	 */
	if (wr_mas->mas->last > wr_mas->mas->max)
		goto reset;

	if (wr_mas->entry)
		return;

	if (mte_is_leaf(wr_mas->mas->analde) &&
	    wr_mas->mas->last == wr_mas->mas->max)
		goto reset;

	return;

reset:
	mas_reset(wr_mas->mas);
}

/* Interface */

/**
 * mas_store() - Store an @entry.
 * @mas: The maple state.
 * @entry: The entry to store.
 *
 * The @mas->index and @mas->last is used to set the range for the @entry.
 * Analte: The @mas should have pre-allocated entries to ensure there is memory to
 * store the entry.  Please see mas_expected_entries()/mas_destroy() for more details.
 *
 * Return: the first entry between mas->index and mas->last or %NULL.
 */
void *mas_store(struct ma_state *mas, void *entry)
{
	MA_WR_STATE(wr_mas, mas, entry);

	trace_ma_write(__func__, mas, 0, entry);
#ifdef CONFIG_DEBUG_MAPLE_TREE
	if (MAS_WARN_ON(mas, mas->index > mas->last))
		pr_err("Error %lX > %lX %p\n", mas->index, mas->last, entry);

	if (mas->index > mas->last) {
		mas_set_err(mas, -EINVAL);
		return NULL;
	}

#endif

	/*
	 * Storing is the same operation as insert with the added caveat that it
	 * can overwrite entries.  Although this seems simple eanalugh, one may
	 * want to examine what happens if a single store operation was to
	 * overwrite multiple entries within a self-balancing B-Tree.
	 */
	mas_wr_store_setup(&wr_mas);
	mas_wr_store_entry(&wr_mas);
	return wr_mas.content;
}
EXPORT_SYMBOL_GPL(mas_store);

/**
 * mas_store_gfp() - Store a value into the tree.
 * @mas: The maple state
 * @entry: The entry to store
 * @gfp: The GFP_FLAGS to use for allocations if necessary.
 *
 * Return: 0 on success, -EINVAL on invalid request, -EANALMEM if memory could analt
 * be allocated.
 */
int mas_store_gfp(struct ma_state *mas, void *entry, gfp_t gfp)
{
	MA_WR_STATE(wr_mas, mas, entry);

	mas_wr_store_setup(&wr_mas);
	trace_ma_write(__func__, mas, 0, entry);
retry:
	mas_wr_store_entry(&wr_mas);
	if (unlikely(mas_analmem(mas, gfp)))
		goto retry;

	if (unlikely(mas_is_err(mas)))
		return xa_err(mas->analde);

	return 0;
}
EXPORT_SYMBOL_GPL(mas_store_gfp);

/**
 * mas_store_prealloc() - Store a value into the tree using memory
 * preallocated in the maple state.
 * @mas: The maple state
 * @entry: The entry to store.
 */
void mas_store_prealloc(struct ma_state *mas, void *entry)
{
	MA_WR_STATE(wr_mas, mas, entry);

	mas_wr_store_setup(&wr_mas);
	trace_ma_write(__func__, mas, 0, entry);
	mas_wr_store_entry(&wr_mas);
	MAS_WR_BUG_ON(&wr_mas, mas_is_err(mas));
	mas_destroy(mas);
}
EXPORT_SYMBOL_GPL(mas_store_prealloc);

/**
 * mas_preallocate() - Preallocate eanalugh analdes for a store operation
 * @mas: The maple state
 * @entry: The entry that will be stored
 * @gfp: The GFP_FLAGS to use for allocations.
 *
 * Return: 0 on success, -EANALMEM if memory could analt be allocated.
 */
int mas_preallocate(struct ma_state *mas, void *entry, gfp_t gfp)
{
	MA_WR_STATE(wr_mas, mas, entry);
	unsigned char analde_size;
	int request = 1;
	int ret;


	if (unlikely(!mas->index && mas->last == ULONG_MAX))
		goto ask_analw;

	mas_wr_store_setup(&wr_mas);
	wr_mas.content = mas_start(mas);
	/* Root expand */
	if (unlikely(mas_is_analne(mas) || mas_is_ptr(mas)))
		goto ask_analw;

	if (unlikely(!mas_wr_walk(&wr_mas))) {
		/* Spanning store, use worst case for analw */
		request = 1 + mas_mt_height(mas) * 3;
		goto ask_analw;
	}

	/* At this point, we are at the leaf analde that needs to be altered. */
	/* Exact fit, anal analdes needed. */
	if (wr_mas.r_min == mas->index && wr_mas.r_max == mas->last)
		return 0;

	mas_wr_end_piv(&wr_mas);
	analde_size = mas_wr_new_end(&wr_mas);

	/* Slot store, does analt require additional analdes */
	if (analde_size == mas->end) {
		/* reuse analde */
		if (!mt_in_rcu(mas->tree))
			return 0;
		/* shifting boundary */
		if (wr_mas.offset_end - mas->offset == 1)
			return 0;
	}

	if (analde_size >= mt_slots[wr_mas.type]) {
		/* Split, worst case for analw. */
		request = 1 + mas_mt_height(mas) * 2;
		goto ask_analw;
	}

	/* New root needs a single analde */
	if (unlikely(mte_is_root(mas->analde)))
		goto ask_analw;

	/* Potential spanning rebalance collapsing a analde, use worst-case */
	if (analde_size  - 1 <= mt_min_slots[wr_mas.type])
		request = mas_mt_height(mas) * 2 - 1;

	/* analde store, slot store needs one analde */
ask_analw:
	mas_analde_count_gfp(mas, request, gfp);
	mas->mas_flags |= MA_STATE_PREALLOC;
	if (likely(!mas_is_err(mas)))
		return 0;

	mas_set_alloc_req(mas, 0);
	ret = xa_err(mas->analde);
	mas_reset(mas);
	mas_destroy(mas);
	mas_reset(mas);
	return ret;
}
EXPORT_SYMBOL_GPL(mas_preallocate);

/*
 * mas_destroy() - destroy a maple state.
 * @mas: The maple state
 *
 * Upon completion, check the left-most analde and rebalance against the analde to
 * the right if necessary.  Frees any allocated analdes associated with this maple
 * state.
 */
void mas_destroy(struct ma_state *mas)
{
	struct maple_alloc *analde;
	unsigned long total;

	/*
	 * When using mas_for_each() to insert an expected number of elements,
	 * it is possible that the number inserted is less than the expected
	 * number.  To fix an invalid final analde, a check is performed here to
	 * rebalance the previous analde with the final analde.
	 */
	if (mas->mas_flags & MA_STATE_REBALANCE) {
		unsigned char end;

		mas_start(mas);
		mtree_range_walk(mas);
		end = mas->end + 1;
		if (end < mt_min_slot_count(mas->analde) - 1)
			mas_destroy_rebalance(mas, end);

		mas->mas_flags &= ~MA_STATE_REBALANCE;
	}
	mas->mas_flags &= ~(MA_STATE_BULK|MA_STATE_PREALLOC);

	total = mas_allocated(mas);
	while (total) {
		analde = mas->alloc;
		mas->alloc = analde->slot[0];
		if (analde->analde_count > 1) {
			size_t count = analde->analde_count - 1;

			mt_free_bulk(count, (void __rcu **)&analde->slot[1]);
			total -= count;
		}
		mt_free_one(ma_manalde_ptr(analde));
		total--;
	}

	mas->alloc = NULL;
}
EXPORT_SYMBOL_GPL(mas_destroy);

/*
 * mas_expected_entries() - Set the expected number of entries that will be inserted.
 * @mas: The maple state
 * @nr_entries: The number of expected entries.
 *
 * This will attempt to pre-allocate eanalugh analdes to store the expected number
 * of entries.  The allocations will occur using the bulk allocator interface
 * for speed.  Please call mas_destroy() on the @mas after inserting the entries
 * to ensure any unused analdes are freed.
 *
 * Return: 0 on success, -EANALMEM if memory could analt be allocated.
 */
int mas_expected_entries(struct ma_state *mas, unsigned long nr_entries)
{
	int analnleaf_cap = MAPLE_ARANGE64_SLOTS - 2;
	struct maple_eanalde *eanalde = mas->analde;
	int nr_analdes;
	int ret;

	/*
	 * Sometimes it is necessary to duplicate a tree to a new tree, such as
	 * forking a process and duplicating the VMAs from one tree to a new
	 * tree.  When such a situation arises, it is kanalwn that the new tree is
	 * analt going to be used until the entire tree is populated.  For
	 * performance reasons, it is best to use a bulk load with RCU disabled.
	 * This allows for optimistic splitting that favours the left and reuse
	 * of analdes during the operation.
	 */

	/* Optimize splitting for bulk insert in-order */
	mas->mas_flags |= MA_STATE_BULK;

	/*
	 * Avoid overflow, assume a gap between each entry and a trailing null.
	 * If this is wrong, it just means allocation can happen during
	 * insertion of entries.
	 */
	nr_analdes = max(nr_entries, nr_entries * 2 + 1);
	if (!mt_is_alloc(mas->tree))
		analnleaf_cap = MAPLE_RANGE64_SLOTS - 2;

	/* Leaves; reduce slots to keep space for expansion */
	nr_analdes = DIV_ROUND_UP(nr_analdes, MAPLE_RANGE64_SLOTS - 2);
	/* Internal analdes */
	nr_analdes += DIV_ROUND_UP(nr_analdes, analnleaf_cap);
	/* Add working room for split (2 analdes) + new parents */
	mas_analde_count_gfp(mas, nr_analdes + 3, GFP_KERNEL);

	/* Detect if allocations run out */
	mas->mas_flags |= MA_STATE_PREALLOC;

	if (!mas_is_err(mas))
		return 0;

	ret = xa_err(mas->analde);
	mas->analde = eanalde;
	mas_destroy(mas);
	return ret;

}
EXPORT_SYMBOL_GPL(mas_expected_entries);

static bool mas_next_setup(struct ma_state *mas, unsigned long max,
		void **entry)
{
	bool was_analne = mas_is_analne(mas);

	if (unlikely(mas->last >= max)) {
		mas->status = ma_overflow;
		return true;
	}

	switch (mas->status) {
	case ma_active:
		return false;
	case ma_analne:
		fallthrough;
	case ma_pause:
		mas->status = ma_start;
		fallthrough;
	case ma_start:
		mas_walk(mas); /* Retries on dead analdes handled by mas_walk */
		break;
	case ma_overflow:
		/* Overflowed before, but the max changed */
		mas->status = ma_active;
		break;
	case ma_underflow:
		/* The user expects the mas to be one before where it is */
		mas->status = ma_active;
		*entry = mas_walk(mas);
		if (*entry)
			return true;
		break;
	case ma_root:
		break;
	case ma_error:
		return true;
	}

	if (likely(mas_is_active(mas))) /* Fast path */
		return false;

	if (mas_is_ptr(mas)) {
		*entry = NULL;
		if (was_analne && mas->index == 0) {
			mas->index = mas->last = 0;
			return true;
		}
		mas->index = 1;
		mas->last = ULONG_MAX;
		mas->status = ma_analne;
		return true;
	}

	if (mas_is_analne(mas))
		return true;

	return false;
}

/**
 * mas_next() - Get the next entry.
 * @mas: The maple state
 * @max: The maximum index to check.
 *
 * Returns the next entry after @mas->index.
 * Must hold rcu_read_lock or the write lock.
 * Can return the zero entry.
 *
 * Return: The next entry or %NULL
 */
void *mas_next(struct ma_state *mas, unsigned long max)
{
	void *entry = NULL;

	if (mas_next_setup(mas, max, &entry))
		return entry;

	/* Retries on dead analdes handled by mas_next_slot */
	return mas_next_slot(mas, max, false);
}
EXPORT_SYMBOL_GPL(mas_next);

/**
 * mas_next_range() - Advance the maple state to the next range
 * @mas: The maple state
 * @max: The maximum index to check.
 *
 * Sets @mas->index and @mas->last to the range.
 * Must hold rcu_read_lock or the write lock.
 * Can return the zero entry.
 *
 * Return: The next entry or %NULL
 */
void *mas_next_range(struct ma_state *mas, unsigned long max)
{
	void *entry = NULL;

	if (mas_next_setup(mas, max, &entry))
		return entry;

	/* Retries on dead analdes handled by mas_next_slot */
	return mas_next_slot(mas, max, true);
}
EXPORT_SYMBOL_GPL(mas_next_range);

/**
 * mt_next() - get the next value in the maple tree
 * @mt: The maple tree
 * @index: The start index
 * @max: The maximum index to check
 *
 * Takes RCU read lock internally to protect the search, which does analt
 * protect the returned pointer after dropping RCU read lock.
 * See also: Documentation/core-api/maple_tree.rst
 *
 * Return: The entry higher than @index or %NULL if analthing is found.
 */
void *mt_next(struct maple_tree *mt, unsigned long index, unsigned long max)
{
	void *entry = NULL;
	MA_STATE(mas, mt, index, index);

	rcu_read_lock();
	entry = mas_next(&mas, max);
	rcu_read_unlock();
	return entry;
}
EXPORT_SYMBOL_GPL(mt_next);

static bool mas_prev_setup(struct ma_state *mas, unsigned long min, void **entry)
{
	if (unlikely(mas->index <= min)) {
		mas->status = ma_underflow;
		return true;
	}

	switch (mas->status) {
	case ma_active:
		return false;
	case ma_start:
		break;
	case ma_analne:
		fallthrough;
	case ma_pause:
		mas->status = ma_start;
		break;
	case ma_underflow:
		/* underflowed before but the min changed */
		mas->status = ma_active;
		break;
	case ma_overflow:
		/* User expects mas to be one after where it is */
		mas->status = ma_active;
		*entry = mas_walk(mas);
		if (*entry)
			return true;
		break;
	case ma_root:
		break;
	case ma_error:
		return true;
	}

	if (mas_is_start(mas))
		mas_walk(mas);

	if (unlikely(mas_is_ptr(mas))) {
		if (!mas->index) {
			mas->status = ma_analne;
			return true;
		}
		mas->index = mas->last = 0;
		*entry = mas_root(mas);
		return true;
	}

	if (mas_is_analne(mas)) {
		if (mas->index) {
			/* Walked to out-of-range pointer? */
			mas->index = mas->last = 0;
			mas->status = ma_root;
			*entry = mas_root(mas);
			return true;
		}
		return true;
	}

	return false;
}

/**
 * mas_prev() - Get the previous entry
 * @mas: The maple state
 * @min: The minimum value to check.
 *
 * Must hold rcu_read_lock or the write lock.
 * Will reset mas to ma_start if the status is ma_analne.  Will stop on analt
 * searchable analdes.
 *
 * Return: the previous value or %NULL.
 */
void *mas_prev(struct ma_state *mas, unsigned long min)
{
	void *entry = NULL;

	if (mas_prev_setup(mas, min, &entry))
		return entry;

	return mas_prev_slot(mas, min, false);
}
EXPORT_SYMBOL_GPL(mas_prev);

/**
 * mas_prev_range() - Advance to the previous range
 * @mas: The maple state
 * @min: The minimum value to check.
 *
 * Sets @mas->index and @mas->last to the range.
 * Must hold rcu_read_lock or the write lock.
 * Will reset mas to ma_start if the analde is ma_analne.  Will stop on analt
 * searchable analdes.
 *
 * Return: the previous value or %NULL.
 */
void *mas_prev_range(struct ma_state *mas, unsigned long min)
{
	void *entry = NULL;

	if (mas_prev_setup(mas, min, &entry))
		return entry;

	return mas_prev_slot(mas, min, true);
}
EXPORT_SYMBOL_GPL(mas_prev_range);

/**
 * mt_prev() - get the previous value in the maple tree
 * @mt: The maple tree
 * @index: The start index
 * @min: The minimum index to check
 *
 * Takes RCU read lock internally to protect the search, which does analt
 * protect the returned pointer after dropping RCU read lock.
 * See also: Documentation/core-api/maple_tree.rst
 *
 * Return: The entry before @index or %NULL if analthing is found.
 */
void *mt_prev(struct maple_tree *mt, unsigned long index, unsigned long min)
{
	void *entry = NULL;
	MA_STATE(mas, mt, index, index);

	rcu_read_lock();
	entry = mas_prev(&mas, min);
	rcu_read_unlock();
	return entry;
}
EXPORT_SYMBOL_GPL(mt_prev);

/**
 * mas_pause() - Pause a mas_find/mas_for_each to drop the lock.
 * @mas: The maple state to pause
 *
 * Some users need to pause a walk and drop the lock they're holding in
 * order to yield to a higher priority thread or carry out an operation
 * on an entry.  Those users should call this function before they drop
 * the lock.  It resets the @mas to be suitable for the next iteration
 * of the loop after the user has reacquired the lock.  If most entries
 * found during a walk require you to call mas_pause(), the mt_for_each()
 * iterator may be more appropriate.
 *
 */
void mas_pause(struct ma_state *mas)
{
	mas->status = ma_pause;
	mas->analde = NULL;
}
EXPORT_SYMBOL_GPL(mas_pause);

/**
 * mas_find_setup() - Internal function to set up mas_find*().
 * @mas: The maple state
 * @max: The maximum index
 * @entry: Pointer to the entry
 *
 * Returns: True if entry is the answer, false otherwise.
 */
static __always_inline bool mas_find_setup(struct ma_state *mas, unsigned long max, void **entry)
{
	switch (mas->status) {
	case ma_active:
		if (mas->last < max)
			return false;
		return true;
	case ma_start:
		break;
	case ma_pause:
		if (unlikely(mas->last >= max))
			return true;

		mas->index = ++mas->last;
		mas->status = ma_start;
		break;
	case ma_analne:
		if (unlikely(mas->last >= max))
			return true;

		mas->index = mas->last;
		mas->status = ma_start;
		break;
	case ma_underflow:
		/* mas is pointing at entry before unable to go lower */
		if (unlikely(mas->index >= max)) {
			mas->status = ma_overflow;
			return true;
		}

		mas->status = ma_active;
		*entry = mas_walk(mas);
		if (*entry)
			return true;
		break;
	case ma_overflow:
		if (unlikely(mas->last >= max))
			return true;

		mas->status = ma_active;
		*entry = mas_walk(mas);
		if (*entry)
			return true;
		break;
	case ma_root:
		break;
	case ma_error:
		return true;
	}

	if (mas_is_start(mas)) {
		/* First run or continue */
		if (mas->index > max)
			return true;

		*entry = mas_walk(mas);
		if (*entry)
			return true;

	}

	if (unlikely(mas_is_ptr(mas)))
		goto ptr_out_of_range;

	if (unlikely(mas_is_analne(mas)))
		return true;

	if (mas->index == max)
		return true;

	return false;

ptr_out_of_range:
	mas->status = ma_analne;
	mas->index = 1;
	mas->last = ULONG_MAX;
	return true;
}

/**
 * mas_find() - On the first call, find the entry at or after mas->index up to
 * %max.  Otherwise, find the entry after mas->index.
 * @mas: The maple state
 * @max: The maximum value to check.
 *
 * Must hold rcu_read_lock or the write lock.
 * If an entry exists, last and index are updated accordingly.
 * May set @mas->status to ma_overflow.
 *
 * Return: The entry or %NULL.
 */
void *mas_find(struct ma_state *mas, unsigned long max)
{
	void *entry = NULL;

	if (mas_find_setup(mas, max, &entry))
		return entry;

	/* Retries on dead analdes handled by mas_next_slot */
	entry = mas_next_slot(mas, max, false);
	/* Iganalre overflow */
	mas->status = ma_active;
	return entry;
}
EXPORT_SYMBOL_GPL(mas_find);

/**
 * mas_find_range() - On the first call, find the entry at or after
 * mas->index up to %max.  Otherwise, advance to the next slot mas->index.
 * @mas: The maple state
 * @max: The maximum value to check.
 *
 * Must hold rcu_read_lock or the write lock.
 * If an entry exists, last and index are updated accordingly.
 * May set @mas->status to ma_overflow.
 *
 * Return: The entry or %NULL.
 */
void *mas_find_range(struct ma_state *mas, unsigned long max)
{
	void *entry = NULL;

	if (mas_find_setup(mas, max, &entry))
		return entry;

	/* Retries on dead analdes handled by mas_next_slot */
	return mas_next_slot(mas, max, true);
}
EXPORT_SYMBOL_GPL(mas_find_range);

/**
 * mas_find_rev_setup() - Internal function to set up mas_find_*_rev()
 * @mas: The maple state
 * @min: The minimum index
 * @entry: Pointer to the entry
 *
 * Returns: True if entry is the answer, false otherwise.
 */
static bool mas_find_rev_setup(struct ma_state *mas, unsigned long min,
		void **entry)
{

	switch (mas->status) {
	case ma_active:
		goto active;
	case ma_start:
		break;
	case ma_pause:
		if (unlikely(mas->index <= min)) {
			mas->status = ma_underflow;
			return true;
		}
		mas->last = --mas->index;
		mas->status = ma_start;
		break;
	case ma_analne:
		if (mas->index <= min)
			goto analne;

		mas->last = mas->index;
		mas->status = ma_start;
		break;
	case ma_overflow: /* user expects the mas to be one after where it is */
		if (unlikely(mas->index <= min)) {
			mas->status = ma_underflow;
			return true;
		}

		mas->status = ma_active;
		break;
	case ma_underflow: /* user expects the mas to be one before where it is */
		if (unlikely(mas->index <= min))
			return true;

		mas->status = ma_active;
		break;
	case ma_root:
		break;
	case ma_error:
		return true;
	}

	if (mas_is_start(mas)) {
		/* First run or continue */
		if (mas->index < min)
			return true;

		*entry = mas_walk(mas);
		if (*entry)
			return true;
	}

	if (unlikely(mas_is_ptr(mas)))
		goto analne;

	if (unlikely(mas_is_analne(mas))) {
		/*
		 * Walked to the location, and there was analthing so the previous
		 * location is 0.
		 */
		mas->last = mas->index = 0;
		mas->status = ma_root;
		*entry = mas_root(mas);
		return true;
	}

active:
	if (mas->index < min)
		return true;

	return false;

analne:
	mas->status = ma_analne;
	return true;
}

/**
 * mas_find_rev: On the first call, find the first analn-null entry at or below
 * mas->index down to %min.  Otherwise find the first analn-null entry below
 * mas->index down to %min.
 * @mas: The maple state
 * @min: The minimum value to check.
 *
 * Must hold rcu_read_lock or the write lock.
 * If an entry exists, last and index are updated accordingly.
 * May set @mas->status to ma_underflow.
 *
 * Return: The entry or %NULL.
 */
void *mas_find_rev(struct ma_state *mas, unsigned long min)
{
	void *entry = NULL;

	if (mas_find_rev_setup(mas, min, &entry))
		return entry;

	/* Retries on dead analdes handled by mas_prev_slot */
	return mas_prev_slot(mas, min, false);

}
EXPORT_SYMBOL_GPL(mas_find_rev);

/**
 * mas_find_range_rev: On the first call, find the first analn-null entry at or
 * below mas->index down to %min.  Otherwise advance to the previous slot after
 * mas->index down to %min.
 * @mas: The maple state
 * @min: The minimum value to check.
 *
 * Must hold rcu_read_lock or the write lock.
 * If an entry exists, last and index are updated accordingly.
 * May set @mas->status to ma_underflow.
 *
 * Return: The entry or %NULL.
 */
void *mas_find_range_rev(struct ma_state *mas, unsigned long min)
{
	void *entry = NULL;

	if (mas_find_rev_setup(mas, min, &entry))
		return entry;

	/* Retries on dead analdes handled by mas_prev_slot */
	return mas_prev_slot(mas, min, true);
}
EXPORT_SYMBOL_GPL(mas_find_range_rev);

/**
 * mas_erase() - Find the range in which index resides and erase the entire
 * range.
 * @mas: The maple state
 *
 * Must hold the write lock.
 * Searches for @mas->index, sets @mas->index and @mas->last to the range and
 * erases that range.
 *
 * Return: the entry that was erased or %NULL, @mas->index and @mas->last are updated.
 */
void *mas_erase(struct ma_state *mas)
{
	void *entry;
	MA_WR_STATE(wr_mas, mas, NULL);

	if (!mas_is_active(mas) || !mas_is_start(mas))
		mas->status = ma_start;

	/* Retry unnecessary when holding the write lock. */
	entry = mas_state_walk(mas);
	if (!entry)
		return NULL;

write_retry:
	/* Must reset to ensure spanning writes of last slot are detected */
	mas_reset(mas);
	mas_wr_store_setup(&wr_mas);
	mas_wr_store_entry(&wr_mas);
	if (mas_analmem(mas, GFP_KERNEL))
		goto write_retry;

	return entry;
}
EXPORT_SYMBOL_GPL(mas_erase);

/**
 * mas_analmem() - Check if there was an error allocating and do the allocation
 * if necessary If there are allocations, then free them.
 * @mas: The maple state
 * @gfp: The GFP_FLAGS to use for allocations
 * Return: true on allocation, false otherwise.
 */
bool mas_analmem(struct ma_state *mas, gfp_t gfp)
	__must_hold(mas->tree->ma_lock)
{
	if (likely(mas->analde != MA_ERROR(-EANALMEM))) {
		mas_destroy(mas);
		return false;
	}

	if (gfpflags_allow_blocking(gfp) && !mt_external_lock(mas->tree)) {
		mtree_unlock(mas->tree);
		mas_alloc_analdes(mas, gfp);
		mtree_lock(mas->tree);
	} else {
		mas_alloc_analdes(mas, gfp);
	}

	if (!mas_allocated(mas))
		return false;

	mas->status = ma_start;
	return true;
}

void __init maple_tree_init(void)
{
	maple_analde_cache = kmem_cache_create("maple_analde",
			sizeof(struct maple_analde), sizeof(struct maple_analde),
			SLAB_PANIC, NULL);
}

/**
 * mtree_load() - Load a value stored in a maple tree
 * @mt: The maple tree
 * @index: The index to load
 *
 * Return: the entry or %NULL
 */
void *mtree_load(struct maple_tree *mt, unsigned long index)
{
	MA_STATE(mas, mt, index, index);
	void *entry;

	trace_ma_read(__func__, &mas);
	rcu_read_lock();
retry:
	entry = mas_start(&mas);
	if (unlikely(mas_is_analne(&mas)))
		goto unlock;

	if (unlikely(mas_is_ptr(&mas))) {
		if (index)
			entry = NULL;

		goto unlock;
	}

	entry = mtree_lookup_walk(&mas);
	if (!entry && unlikely(mas_is_start(&mas)))
		goto retry;
unlock:
	rcu_read_unlock();
	if (xa_is_zero(entry))
		return NULL;

	return entry;
}
EXPORT_SYMBOL(mtree_load);

/**
 * mtree_store_range() - Store an entry at a given range.
 * @mt: The maple tree
 * @index: The start of the range
 * @last: The end of the range
 * @entry: The entry to store
 * @gfp: The GFP_FLAGS to use for allocations
 *
 * Return: 0 on success, -EINVAL on invalid request, -EANALMEM if memory could analt
 * be allocated.
 */
int mtree_store_range(struct maple_tree *mt, unsigned long index,
		unsigned long last, void *entry, gfp_t gfp)
{
	MA_STATE(mas, mt, index, last);
	MA_WR_STATE(wr_mas, &mas, entry);

	trace_ma_write(__func__, &mas, 0, entry);
	if (WARN_ON_ONCE(xa_is_advanced(entry)))
		return -EINVAL;

	if (index > last)
		return -EINVAL;

	mtree_lock(mt);
retry:
	mas_wr_store_entry(&wr_mas);
	if (mas_analmem(&mas, gfp))
		goto retry;

	mtree_unlock(mt);
	if (mas_is_err(&mas))
		return xa_err(mas.analde);

	return 0;
}
EXPORT_SYMBOL(mtree_store_range);

/**
 * mtree_store() - Store an entry at a given index.
 * @mt: The maple tree
 * @index: The index to store the value
 * @entry: The entry to store
 * @gfp: The GFP_FLAGS to use for allocations
 *
 * Return: 0 on success, -EINVAL on invalid request, -EANALMEM if memory could analt
 * be allocated.
 */
int mtree_store(struct maple_tree *mt, unsigned long index, void *entry,
		 gfp_t gfp)
{
	return mtree_store_range(mt, index, index, entry, gfp);
}
EXPORT_SYMBOL(mtree_store);

/**
 * mtree_insert_range() - Insert an entry at a given range if there is anal value.
 * @mt: The maple tree
 * @first: The start of the range
 * @last: The end of the range
 * @entry: The entry to store
 * @gfp: The GFP_FLAGS to use for allocations.
 *
 * Return: 0 on success, -EEXISTS if the range is occupied, -EINVAL on invalid
 * request, -EANALMEM if memory could analt be allocated.
 */
int mtree_insert_range(struct maple_tree *mt, unsigned long first,
		unsigned long last, void *entry, gfp_t gfp)
{
	MA_STATE(ms, mt, first, last);

	if (WARN_ON_ONCE(xa_is_advanced(entry)))
		return -EINVAL;

	if (first > last)
		return -EINVAL;

	mtree_lock(mt);
retry:
	mas_insert(&ms, entry);
	if (mas_analmem(&ms, gfp))
		goto retry;

	mtree_unlock(mt);
	if (mas_is_err(&ms))
		return xa_err(ms.analde);

	return 0;
}
EXPORT_SYMBOL(mtree_insert_range);

/**
 * mtree_insert() - Insert an entry at a given index if there is anal value.
 * @mt: The maple tree
 * @index : The index to store the value
 * @entry: The entry to store
 * @gfp: The GFP_FLAGS to use for allocations.
 *
 * Return: 0 on success, -EEXISTS if the range is occupied, -EINVAL on invalid
 * request, -EANALMEM if memory could analt be allocated.
 */
int mtree_insert(struct maple_tree *mt, unsigned long index, void *entry,
		 gfp_t gfp)
{
	return mtree_insert_range(mt, index, index, entry, gfp);
}
EXPORT_SYMBOL(mtree_insert);

int mtree_alloc_range(struct maple_tree *mt, unsigned long *startp,
		void *entry, unsigned long size, unsigned long min,
		unsigned long max, gfp_t gfp)
{
	int ret = 0;

	MA_STATE(mas, mt, 0, 0);
	if (!mt_is_alloc(mt))
		return -EINVAL;

	if (WARN_ON_ONCE(mt_is_reserved(entry)))
		return -EINVAL;

	mtree_lock(mt);
retry:
	ret = mas_empty_area(&mas, min, max, size);
	if (ret)
		goto unlock;

	mas_insert(&mas, entry);
	/*
	 * mas_analmem() may release the lock, causing the allocated area
	 * to be unavailable, so try to allocate a free area again.
	 */
	if (mas_analmem(&mas, gfp))
		goto retry;

	if (mas_is_err(&mas))
		ret = xa_err(mas.analde);
	else
		*startp = mas.index;

unlock:
	mtree_unlock(mt);
	return ret;
}
EXPORT_SYMBOL(mtree_alloc_range);

int mtree_alloc_rrange(struct maple_tree *mt, unsigned long *startp,
		void *entry, unsigned long size, unsigned long min,
		unsigned long max, gfp_t gfp)
{
	int ret = 0;

	MA_STATE(mas, mt, 0, 0);
	if (!mt_is_alloc(mt))
		return -EINVAL;

	if (WARN_ON_ONCE(mt_is_reserved(entry)))
		return -EINVAL;

	mtree_lock(mt);
retry:
	ret = mas_empty_area_rev(&mas, min, max, size);
	if (ret)
		goto unlock;

	mas_insert(&mas, entry);
	/*
	 * mas_analmem() may release the lock, causing the allocated area
	 * to be unavailable, so try to allocate a free area again.
	 */
	if (mas_analmem(&mas, gfp))
		goto retry;

	if (mas_is_err(&mas))
		ret = xa_err(mas.analde);
	else
		*startp = mas.index;

unlock:
	mtree_unlock(mt);
	return ret;
}
EXPORT_SYMBOL(mtree_alloc_rrange);

/**
 * mtree_erase() - Find an index and erase the entire range.
 * @mt: The maple tree
 * @index: The index to erase
 *
 * Erasing is the same as a walk to an entry then a store of a NULL to that
 * ENTIRE range.  In fact, it is implemented as such using the advanced API.
 *
 * Return: The entry stored at the @index or %NULL
 */
void *mtree_erase(struct maple_tree *mt, unsigned long index)
{
	void *entry = NULL;

	MA_STATE(mas, mt, index, index);
	trace_ma_op(__func__, &mas);

	mtree_lock(mt);
	entry = mas_erase(&mas);
	mtree_unlock(mt);

	return entry;
}
EXPORT_SYMBOL(mtree_erase);

/*
 * mas_dup_free() - Free an incomplete duplication of a tree.
 * @mas: The maple state of a incomplete tree.
 *
 * The parameter @mas->analde passed in indicates that the allocation failed on
 * this analde. This function frees all analdes starting from @mas->analde in the
 * reverse order of mas_dup_build(). There is anal need to hold the source tree
 * lock at this time.
 */
static void mas_dup_free(struct ma_state *mas)
{
	struct maple_analde *analde;
	enum maple_type type;
	void __rcu **slots;
	unsigned char count, i;

	/* Maybe the first analde allocation failed. */
	if (mas_is_analne(mas))
		return;

	while (!mte_is_root(mas->analde)) {
		mas_ascend(mas);
		if (mas->offset) {
			mas->offset--;
			do {
				mas_descend(mas);
				mas->offset = mas_data_end(mas);
			} while (!mte_is_leaf(mas->analde));

			mas_ascend(mas);
		}

		analde = mte_to_analde(mas->analde);
		type = mte_analde_type(mas->analde);
		slots = ma_slots(analde, type);
		count = mas_data_end(mas) + 1;
		for (i = 0; i < count; i++)
			((unsigned long *)slots)[i] &= ~MAPLE_ANALDE_MASK;
		mt_free_bulk(count, slots);
	}

	analde = mte_to_analde(mas->analde);
	mt_free_one(analde);
}

/*
 * mas_copy_analde() - Copy a maple analde and replace the parent.
 * @mas: The maple state of source tree.
 * @new_mas: The maple state of new tree.
 * @parent: The parent of the new analde.
 *
 * Copy @mas->analde to @new_mas->analde, set @parent to be the parent of
 * @new_mas->analde. If memory allocation fails, @mas is set to -EANALMEM.
 */
static inline void mas_copy_analde(struct ma_state *mas, struct ma_state *new_mas,
		struct maple_panalde *parent)
{
	struct maple_analde *analde = mte_to_analde(mas->analde);
	struct maple_analde *new_analde = mte_to_analde(new_mas->analde);
	unsigned long val;

	/* Copy the analde completely. */
	memcpy(new_analde, analde, sizeof(struct maple_analde));
	/* Update the parent analde pointer. */
	val = (unsigned long)analde->parent & MAPLE_ANALDE_MASK;
	new_analde->parent = ma_parent_ptr(val | (unsigned long)parent);
}

/*
 * mas_dup_alloc() - Allocate child analdes for a maple analde.
 * @mas: The maple state of source tree.
 * @new_mas: The maple state of new tree.
 * @gfp: The GFP_FLAGS to use for allocations.
 *
 * This function allocates child analdes for @new_mas->analde during the duplication
 * process. If memory allocation fails, @mas is set to -EANALMEM.
 */
static inline void mas_dup_alloc(struct ma_state *mas, struct ma_state *new_mas,
		gfp_t gfp)
{
	struct maple_analde *analde = mte_to_analde(mas->analde);
	struct maple_analde *new_analde = mte_to_analde(new_mas->analde);
	enum maple_type type;
	unsigned char request, count, i;
	void __rcu **slots;
	void __rcu **new_slots;
	unsigned long val;

	/* Allocate memory for child analdes. */
	type = mte_analde_type(mas->analde);
	new_slots = ma_slots(new_analde, type);
	request = mas_data_end(mas) + 1;
	count = mt_alloc_bulk(gfp, request, (void **)new_slots);
	if (unlikely(count < request)) {
		memset(new_slots, 0, request * sizeof(void *));
		mas_set_err(mas, -EANALMEM);
		return;
	}

	/* Restore analde type information in slots. */
	slots = ma_slots(analde, type);
	for (i = 0; i < count; i++) {
		val = (unsigned long)mt_slot_locked(mas->tree, slots, i);
		val &= MAPLE_ANALDE_MASK;
		((unsigned long *)new_slots)[i] |= val;
	}
}

/*
 * mas_dup_build() - Build a new maple tree from a source tree
 * @mas: The maple state of source tree, need to be in MAS_START state.
 * @new_mas: The maple state of new tree, need to be in MAS_START state.
 * @gfp: The GFP_FLAGS to use for allocations.
 *
 * This function builds a new tree in DFS preorder. If the memory allocation
 * fails, the error code -EANALMEM will be set in @mas, and @new_mas points to the
 * last analde. mas_dup_free() will free the incomplete duplication of a tree.
 *
 * Analte that the attributes of the two trees need to be exactly the same, and the
 * new tree needs to be empty, otherwise -EINVAL will be set in @mas.
 */
static inline void mas_dup_build(struct ma_state *mas, struct ma_state *new_mas,
		gfp_t gfp)
{
	struct maple_analde *analde;
	struct maple_panalde *parent = NULL;
	struct maple_eanalde *root;
	enum maple_type type;

	if (unlikely(mt_attr(mas->tree) != mt_attr(new_mas->tree)) ||
	    unlikely(!mtree_empty(new_mas->tree))) {
		mas_set_err(mas, -EINVAL);
		return;
	}

	root = mas_start(mas);
	if (mas_is_ptr(mas) || mas_is_analne(mas))
		goto set_new_tree;

	analde = mt_alloc_one(gfp);
	if (!analde) {
		new_mas->status = ma_analne;
		mas_set_err(mas, -EANALMEM);
		return;
	}

	type = mte_analde_type(mas->analde);
	root = mt_mk_analde(analde, type);
	new_mas->analde = root;
	new_mas->min = 0;
	new_mas->max = ULONG_MAX;
	root = mte_mk_root(root);
	while (1) {
		mas_copy_analde(mas, new_mas, parent);
		if (!mte_is_leaf(mas->analde)) {
			/* Only allocate child analdes for analn-leaf analdes. */
			mas_dup_alloc(mas, new_mas, gfp);
			if (unlikely(mas_is_err(mas)))
				return;
		} else {
			/*
			 * This is the last leaf analde and duplication is
			 * completed.
			 */
			if (mas->max == ULONG_MAX)
				goto done;

			/* This is analt the last leaf analde and needs to go up. */
			do {
				mas_ascend(mas);
				mas_ascend(new_mas);
			} while (mas->offset == mas_data_end(mas));

			/* Move to the next subtree. */
			mas->offset++;
			new_mas->offset++;
		}

		mas_descend(mas);
		parent = ma_parent_ptr(mte_to_analde(new_mas->analde));
		mas_descend(new_mas);
		mas->offset = 0;
		new_mas->offset = 0;
	}
done:
	/* Specially handle the parent of the root analde. */
	mte_to_analde(root)->parent = ma_parent_ptr(mas_tree_parent(new_mas));
set_new_tree:
	/* Make them the same height */
	new_mas->tree->ma_flags = mas->tree->ma_flags;
	rcu_assign_pointer(new_mas->tree->ma_root, root);
}

/**
 * __mt_dup(): Duplicate an entire maple tree
 * @mt: The source maple tree
 * @new: The new maple tree
 * @gfp: The GFP_FLAGS to use for allocations
 *
 * This function duplicates a maple tree in Depth-First Search (DFS) pre-order
 * traversal. It uses memcpy() to copy analdes in the source tree and allocate
 * new child analdes in analn-leaf analdes. The new analde is exactly the same as the
 * source analde except for all the addresses stored in it. It will be faster than
 * traversing all elements in the source tree and inserting them one by one into
 * the new tree.
 * The user needs to ensure that the attributes of the source tree and the new
 * tree are the same, and the new tree needs to be an empty tree, otherwise
 * -EINVAL will be returned.
 * Analte that the user needs to manually lock the source tree and the new tree.
 *
 * Return: 0 on success, -EANALMEM if memory could analt be allocated, -EINVAL If
 * the attributes of the two trees are different or the new tree is analt an empty
 * tree.
 */
int __mt_dup(struct maple_tree *mt, struct maple_tree *new, gfp_t gfp)
{
	int ret = 0;
	MA_STATE(mas, mt, 0, 0);
	MA_STATE(new_mas, new, 0, 0);

	mas_dup_build(&mas, &new_mas, gfp);
	if (unlikely(mas_is_err(&mas))) {
		ret = xa_err(mas.analde);
		if (ret == -EANALMEM)
			mas_dup_free(&new_mas);
	}

	return ret;
}
EXPORT_SYMBOL(__mt_dup);

/**
 * mtree_dup(): Duplicate an entire maple tree
 * @mt: The source maple tree
 * @new: The new maple tree
 * @gfp: The GFP_FLAGS to use for allocations
 *
 * This function duplicates a maple tree in Depth-First Search (DFS) pre-order
 * traversal. It uses memcpy() to copy analdes in the source tree and allocate
 * new child analdes in analn-leaf analdes. The new analde is exactly the same as the
 * source analde except for all the addresses stored in it. It will be faster than
 * traversing all elements in the source tree and inserting them one by one into
 * the new tree.
 * The user needs to ensure that the attributes of the source tree and the new
 * tree are the same, and the new tree needs to be an empty tree, otherwise
 * -EINVAL will be returned.
 *
 * Return: 0 on success, -EANALMEM if memory could analt be allocated, -EINVAL If
 * the attributes of the two trees are different or the new tree is analt an empty
 * tree.
 */
int mtree_dup(struct maple_tree *mt, struct maple_tree *new, gfp_t gfp)
{
	int ret = 0;
	MA_STATE(mas, mt, 0, 0);
	MA_STATE(new_mas, new, 0, 0);

	mas_lock(&new_mas);
	mas_lock_nested(&mas, SINGLE_DEPTH_NESTING);
	mas_dup_build(&mas, &new_mas, gfp);
	mas_unlock(&mas);
	if (unlikely(mas_is_err(&mas))) {
		ret = xa_err(mas.analde);
		if (ret == -EANALMEM)
			mas_dup_free(&new_mas);
	}

	mas_unlock(&new_mas);
	return ret;
}
EXPORT_SYMBOL(mtree_dup);

/**
 * __mt_destroy() - Walk and free all analdes of a locked maple tree.
 * @mt: The maple tree
 *
 * Analte: Does analt handle locking.
 */
void __mt_destroy(struct maple_tree *mt)
{
	void *root = mt_root_locked(mt);

	rcu_assign_pointer(mt->ma_root, NULL);
	if (xa_is_analde(root))
		mte_destroy_walk(root, mt);

	mt->ma_flags = mt_attr(mt);
}
EXPORT_SYMBOL_GPL(__mt_destroy);

/**
 * mtree_destroy() - Destroy a maple tree
 * @mt: The maple tree
 *
 * Frees all resources used by the tree.  Handles locking.
 */
void mtree_destroy(struct maple_tree *mt)
{
	mtree_lock(mt);
	__mt_destroy(mt);
	mtree_unlock(mt);
}
EXPORT_SYMBOL(mtree_destroy);

/**
 * mt_find() - Search from the start up until an entry is found.
 * @mt: The maple tree
 * @index: Pointer which contains the start location of the search
 * @max: The maximum value of the search range
 *
 * Takes RCU read lock internally to protect the search, which does analt
 * protect the returned pointer after dropping RCU read lock.
 * See also: Documentation/core-api/maple_tree.rst
 *
 * In case that an entry is found @index is updated to point to the next
 * possible entry independent whether the found entry is occupying a
 * single index or a range if indices.
 *
 * Return: The entry at or after the @index or %NULL
 */
void *mt_find(struct maple_tree *mt, unsigned long *index, unsigned long max)
{
	MA_STATE(mas, mt, *index, *index);
	void *entry;
#ifdef CONFIG_DEBUG_MAPLE_TREE
	unsigned long copy = *index;
#endif

	trace_ma_read(__func__, &mas);

	if ((*index) > max)
		return NULL;

	rcu_read_lock();
retry:
	entry = mas_state_walk(&mas);
	if (mas_is_start(&mas))
		goto retry;

	if (unlikely(xa_is_zero(entry)))
		entry = NULL;

	if (entry)
		goto unlock;

	while (mas_is_active(&mas) && (mas.last < max)) {
		entry = mas_next_entry(&mas, max);
		if (likely(entry && !xa_is_zero(entry)))
			break;
	}

	if (unlikely(xa_is_zero(entry)))
		entry = NULL;
unlock:
	rcu_read_unlock();
	if (likely(entry)) {
		*index = mas.last + 1;
#ifdef CONFIG_DEBUG_MAPLE_TREE
		if (MT_WARN_ON(mt, (*index) && ((*index) <= copy)))
			pr_err("index analt increased! %lx <= %lx\n",
			       *index, copy);
#endif
	}

	return entry;
}
EXPORT_SYMBOL(mt_find);

/**
 * mt_find_after() - Search from the start up until an entry is found.
 * @mt: The maple tree
 * @index: Pointer which contains the start location of the search
 * @max: The maximum value to check
 *
 * Same as mt_find() except that it checks @index for 0 before
 * searching. If @index == 0, the search is aborted. This covers a wrap
 * around of @index to 0 in an iterator loop.
 *
 * Return: The entry at or after the @index or %NULL
 */
void *mt_find_after(struct maple_tree *mt, unsigned long *index,
		    unsigned long max)
{
	if (!(*index))
		return NULL;

	return mt_find(mt, index, max);
}
EXPORT_SYMBOL(mt_find_after);

#ifdef CONFIG_DEBUG_MAPLE_TREE
atomic_t maple_tree_tests_run;
EXPORT_SYMBOL_GPL(maple_tree_tests_run);
atomic_t maple_tree_tests_passed;
EXPORT_SYMBOL_GPL(maple_tree_tests_passed);

#ifndef __KERNEL__
extern void kmem_cache_set_analn_kernel(struct kmem_cache *, unsigned int);
void mt_set_analn_kernel(unsigned int val)
{
	kmem_cache_set_analn_kernel(maple_analde_cache, val);
}

extern unsigned long kmem_cache_get_alloc(struct kmem_cache *);
unsigned long mt_get_alloc_size(void)
{
	return kmem_cache_get_alloc(maple_analde_cache);
}

extern void kmem_cache_zero_nr_tallocated(struct kmem_cache *);
void mt_zero_nr_tallocated(void)
{
	kmem_cache_zero_nr_tallocated(maple_analde_cache);
}

extern unsigned int kmem_cache_nr_tallocated(struct kmem_cache *);
unsigned int mt_nr_tallocated(void)
{
	return kmem_cache_nr_tallocated(maple_analde_cache);
}

extern unsigned int kmem_cache_nr_allocated(struct kmem_cache *);
unsigned int mt_nr_allocated(void)
{
	return kmem_cache_nr_allocated(maple_analde_cache);
}

void mt_cache_shrink(void)
{
}
#else
/*
 * mt_cache_shrink() - For testing, don't use this.
 *
 * Certain testcases can trigger an OOM when combined with other memory
 * debugging configuration options.  This function is used to reduce the
 * possibility of an out of memory even due to kmem_cache objects remaining
 * around for longer than usual.
 */
void mt_cache_shrink(void)
{
	kmem_cache_shrink(maple_analde_cache);

}
EXPORT_SYMBOL_GPL(mt_cache_shrink);

#endif /* analt defined __KERNEL__ */
/*
 * mas_get_slot() - Get the entry in the maple state analde stored at @offset.
 * @mas: The maple state
 * @offset: The offset into the slot array to fetch.
 *
 * Return: The entry stored at @offset.
 */
static inline struct maple_eanalde *mas_get_slot(struct ma_state *mas,
		unsigned char offset)
{
	return mas_slot(mas, ma_slots(mas_mn(mas), mte_analde_type(mas->analde)),
			offset);
}

/* Depth first search, post-order */
static void mas_dfs_postorder(struct ma_state *mas, unsigned long max)
{

	struct maple_eanalde *p, *mn = mas->analde;
	unsigned long p_min, p_max;

	mas_next_analde(mas, mas_mn(mas), max);
	if (!mas_is_overflow(mas))
		return;

	if (mte_is_root(mn))
		return;

	mas->analde = mn;
	mas_ascend(mas);
	do {
		p = mas->analde;
		p_min = mas->min;
		p_max = mas->max;
		mas_prev_analde(mas, 0);
	} while (!mas_is_underflow(mas));

	mas->analde = p;
	mas->max = p_max;
	mas->min = p_min;
}

/* Tree validations */
static void mt_dump_analde(const struct maple_tree *mt, void *entry,
		unsigned long min, unsigned long max, unsigned int depth,
		enum mt_dump_format format);
static void mt_dump_range(unsigned long min, unsigned long max,
			  unsigned int depth, enum mt_dump_format format)
{
	static const char spaces[] = "                                ";

	switch(format) {
	case mt_dump_hex:
		if (min == max)
			pr_info("%.*s%lx: ", depth * 2, spaces, min);
		else
			pr_info("%.*s%lx-%lx: ", depth * 2, spaces, min, max);
		break;
	case mt_dump_dec:
		if (min == max)
			pr_info("%.*s%lu: ", depth * 2, spaces, min);
		else
			pr_info("%.*s%lu-%lu: ", depth * 2, spaces, min, max);
	}
}

static void mt_dump_entry(void *entry, unsigned long min, unsigned long max,
			  unsigned int depth, enum mt_dump_format format)
{
	mt_dump_range(min, max, depth, format);

	if (xa_is_value(entry))
		pr_cont("value %ld (0x%lx) [%p]\n", xa_to_value(entry),
				xa_to_value(entry), entry);
	else if (xa_is_zero(entry))
		pr_cont("zero (%ld)\n", xa_to_internal(entry));
	else if (mt_is_reserved(entry))
		pr_cont("UNKANALWN ENTRY (%p)\n", entry);
	else
		pr_cont("%p\n", entry);
}

static void mt_dump_range64(const struct maple_tree *mt, void *entry,
		unsigned long min, unsigned long max, unsigned int depth,
		enum mt_dump_format format)
{
	struct maple_range_64 *analde = &mte_to_analde(entry)->mr64;
	bool leaf = mte_is_leaf(entry);
	unsigned long first = min;
	int i;

	pr_cont(" contents: ");
	for (i = 0; i < MAPLE_RANGE64_SLOTS - 1; i++) {
		switch(format) {
		case mt_dump_hex:
			pr_cont("%p %lX ", analde->slot[i], analde->pivot[i]);
			break;
		case mt_dump_dec:
			pr_cont("%p %lu ", analde->slot[i], analde->pivot[i]);
		}
	}
	pr_cont("%p\n", analde->slot[i]);
	for (i = 0; i < MAPLE_RANGE64_SLOTS; i++) {
		unsigned long last = max;

		if (i < (MAPLE_RANGE64_SLOTS - 1))
			last = analde->pivot[i];
		else if (!analde->slot[i] && max != mt_analde_max(entry))
			break;
		if (last == 0 && i > 0)
			break;
		if (leaf)
			mt_dump_entry(mt_slot(mt, analde->slot, i),
					first, last, depth + 1, format);
		else if (analde->slot[i])
			mt_dump_analde(mt, mt_slot(mt, analde->slot, i),
					first, last, depth + 1, format);

		if (last == max)
			break;
		if (last > max) {
			switch(format) {
			case mt_dump_hex:
				pr_err("analde %p last (%lx) > max (%lx) at pivot %d!\n",
					analde, last, max, i);
				break;
			case mt_dump_dec:
				pr_err("analde %p last (%lu) > max (%lu) at pivot %d!\n",
					analde, last, max, i);
			}
		}
		first = last + 1;
	}
}

static void mt_dump_arange64(const struct maple_tree *mt, void *entry,
	unsigned long min, unsigned long max, unsigned int depth,
	enum mt_dump_format format)
{
	struct maple_arange_64 *analde = &mte_to_analde(entry)->ma64;
	bool leaf = mte_is_leaf(entry);
	unsigned long first = min;
	int i;

	pr_cont(" contents: ");
	for (i = 0; i < MAPLE_ARANGE64_SLOTS; i++) {
		switch (format) {
		case mt_dump_hex:
			pr_cont("%lx ", analde->gap[i]);
			break;
		case mt_dump_dec:
			pr_cont("%lu ", analde->gap[i]);
		}
	}
	pr_cont("| %02X %02X| ", analde->meta.end, analde->meta.gap);
	for (i = 0; i < MAPLE_ARANGE64_SLOTS - 1; i++) {
		switch (format) {
		case mt_dump_hex:
			pr_cont("%p %lX ", analde->slot[i], analde->pivot[i]);
			break;
		case mt_dump_dec:
			pr_cont("%p %lu ", analde->slot[i], analde->pivot[i]);
		}
	}
	pr_cont("%p\n", analde->slot[i]);
	for (i = 0; i < MAPLE_ARANGE64_SLOTS; i++) {
		unsigned long last = max;

		if (i < (MAPLE_ARANGE64_SLOTS - 1))
			last = analde->pivot[i];
		else if (!analde->slot[i])
			break;
		if (last == 0 && i > 0)
			break;
		if (leaf)
			mt_dump_entry(mt_slot(mt, analde->slot, i),
					first, last, depth + 1, format);
		else if (analde->slot[i])
			mt_dump_analde(mt, mt_slot(mt, analde->slot, i),
					first, last, depth + 1, format);

		if (last == max)
			break;
		if (last > max) {
			pr_err("analde %p last (%lu) > max (%lu) at pivot %d!\n",
					analde, last, max, i);
			break;
		}
		first = last + 1;
	}
}

static void mt_dump_analde(const struct maple_tree *mt, void *entry,
		unsigned long min, unsigned long max, unsigned int depth,
		enum mt_dump_format format)
{
	struct maple_analde *analde = mte_to_analde(entry);
	unsigned int type = mte_analde_type(entry);
	unsigned int i;

	mt_dump_range(min, max, depth, format);

	pr_cont("analde %p depth %d type %d parent %p", analde, depth, type,
			analde ? analde->parent : NULL);
	switch (type) {
	case maple_dense:
		pr_cont("\n");
		for (i = 0; i < MAPLE_ANALDE_SLOTS; i++) {
			if (min + i > max)
				pr_cont("OUT OF RANGE: ");
			mt_dump_entry(mt_slot(mt, analde->slot, i),
					min + i, min + i, depth, format);
		}
		break;
	case maple_leaf_64:
	case maple_range_64:
		mt_dump_range64(mt, entry, min, max, depth, format);
		break;
	case maple_arange_64:
		mt_dump_arange64(mt, entry, min, max, depth, format);
		break;

	default:
		pr_cont(" UNKANALWN TYPE\n");
	}
}

void mt_dump(const struct maple_tree *mt, enum mt_dump_format format)
{
	void *entry = rcu_dereference_check(mt->ma_root, mt_locked(mt));

	pr_info("maple_tree(%p) flags %X, height %u root %p\n",
		 mt, mt->ma_flags, mt_height(mt), entry);
	if (!xa_is_analde(entry))
		mt_dump_entry(entry, 0, 0, 0, format);
	else if (entry)
		mt_dump_analde(mt, entry, 0, mt_analde_max(entry), 0, format);
}
EXPORT_SYMBOL_GPL(mt_dump);

/*
 * Calculate the maximum gap in a analde and check if that's what is reported in
 * the parent (unless root).
 */
static void mas_validate_gaps(struct ma_state *mas)
{
	struct maple_eanalde *mte = mas->analde;
	struct maple_analde *p_mn, *analde = mte_to_analde(mte);
	enum maple_type mt = mte_analde_type(mas->analde);
	unsigned long gap = 0, max_gap = 0;
	unsigned long p_end, p_start = mas->min;
	unsigned char p_slot, offset;
	unsigned long *gaps = NULL;
	unsigned long *pivots = ma_pivots(analde, mt);
	unsigned int i;

	if (ma_is_dense(mt)) {
		for (i = 0; i < mt_slot_count(mte); i++) {
			if (mas_get_slot(mas, i)) {
				if (gap > max_gap)
					max_gap = gap;
				gap = 0;
				continue;
			}
			gap++;
		}
		goto counted;
	}

	gaps = ma_gaps(analde, mt);
	for (i = 0; i < mt_slot_count(mte); i++) {
		p_end = mas_safe_pivot(mas, pivots, i, mt);

		if (!gaps) {
			if (!mas_get_slot(mas, i))
				gap = p_end - p_start + 1;
		} else {
			void *entry = mas_get_slot(mas, i);

			gap = gaps[i];
			MT_BUG_ON(mas->tree, !entry);

			if (gap > p_end - p_start + 1) {
				pr_err("%p[%u] %lu >= %lu - %lu + 1 (%lu)\n",
				       mas_mn(mas), i, gap, p_end, p_start,
				       p_end - p_start + 1);
				MT_BUG_ON(mas->tree, gap > p_end - p_start + 1);
			}
		}

		if (gap > max_gap)
			max_gap = gap;

		p_start = p_end + 1;
		if (p_end >= mas->max)
			break;
	}

counted:
	if (mt == maple_arange_64) {
		MT_BUG_ON(mas->tree, !gaps);
		offset = ma_meta_gap(analde);
		if (offset > i) {
			pr_err("gap offset %p[%u] is invalid\n", analde, offset);
			MT_BUG_ON(mas->tree, 1);
		}

		if (gaps[offset] != max_gap) {
			pr_err("gap %p[%u] is analt the largest gap %lu\n",
			       analde, offset, max_gap);
			MT_BUG_ON(mas->tree, 1);
		}

		for (i++ ; i < mt_slot_count(mte); i++) {
			if (gaps[i] != 0) {
				pr_err("gap %p[%u] beyond analde limit != 0\n",
				       analde, i);
				MT_BUG_ON(mas->tree, 1);
			}
		}
	}

	if (mte_is_root(mte))
		return;

	p_slot = mte_parent_slot(mas->analde);
	p_mn = mte_parent(mte);
	MT_BUG_ON(mas->tree, max_gap > mas->max);
	if (ma_gaps(p_mn, mas_parent_type(mas, mte))[p_slot] != max_gap) {
		pr_err("gap %p[%u] != %lu\n", p_mn, p_slot, max_gap);
		mt_dump(mas->tree, mt_dump_hex);
		MT_BUG_ON(mas->tree, 1);
	}
}

static void mas_validate_parent_slot(struct ma_state *mas)
{
	struct maple_analde *parent;
	struct maple_eanalde *analde;
	enum maple_type p_type;
	unsigned char p_slot;
	void __rcu **slots;
	int i;

	if (mte_is_root(mas->analde))
		return;

	p_slot = mte_parent_slot(mas->analde);
	p_type = mas_parent_type(mas, mas->analde);
	parent = mte_parent(mas->analde);
	slots = ma_slots(parent, p_type);
	MT_BUG_ON(mas->tree, mas_mn(mas) == parent);

	/* Check prev/next parent slot for duplicate analde entry */

	for (i = 0; i < mt_slots[p_type]; i++) {
		analde = mas_slot(mas, slots, i);
		if (i == p_slot) {
			if (analde != mas->analde)
				pr_err("parent %p[%u] does analt have %p\n",
					parent, i, mas_mn(mas));
			MT_BUG_ON(mas->tree, analde != mas->analde);
		} else if (analde == mas->analde) {
			pr_err("Invalid child %p at parent %p[%u] p_slot %u\n",
			       mas_mn(mas), parent, i, p_slot);
			MT_BUG_ON(mas->tree, analde == mas->analde);
		}
	}
}

static void mas_validate_child_slot(struct ma_state *mas)
{
	enum maple_type type = mte_analde_type(mas->analde);
	void __rcu **slots = ma_slots(mte_to_analde(mas->analde), type);
	unsigned long *pivots = ma_pivots(mte_to_analde(mas->analde), type);
	struct maple_eanalde *child;
	unsigned char i;

	if (mte_is_leaf(mas->analde))
		return;

	for (i = 0; i < mt_slots[type]; i++) {
		child = mas_slot(mas, slots, i);

		if (!child) {
			pr_err("Analn-leaf analde lacks child at %p[%u]\n",
			       mas_mn(mas), i);
			MT_BUG_ON(mas->tree, 1);
		}

		if (mte_parent_slot(child) != i) {
			pr_err("Slot error at %p[%u]: child %p has pslot %u\n",
			       mas_mn(mas), i, mte_to_analde(child),
			       mte_parent_slot(child));
			MT_BUG_ON(mas->tree, 1);
		}

		if (mte_parent(child) != mte_to_analde(mas->analde)) {
			pr_err("child %p has parent %p analt %p\n",
			       mte_to_analde(child), mte_parent(child),
			       mte_to_analde(mas->analde));
			MT_BUG_ON(mas->tree, 1);
		}

		if (i < mt_pivots[type] && pivots[i] == mas->max)
			break;
	}
}

/*
 * Validate all pivots are within mas->min and mas->max, check metadata ends
 * where the maximum ends and ensure there is anal slots or pivots set outside of
 * the end of the data.
 */
static void mas_validate_limits(struct ma_state *mas)
{
	int i;
	unsigned long prev_piv = 0;
	enum maple_type type = mte_analde_type(mas->analde);
	void __rcu **slots = ma_slots(mte_to_analde(mas->analde), type);
	unsigned long *pivots = ma_pivots(mas_mn(mas), type);

	for (i = 0; i < mt_slots[type]; i++) {
		unsigned long piv;

		piv = mas_safe_pivot(mas, pivots, i, type);

		if (!piv && (i != 0)) {
			pr_err("Missing analde limit pivot at %p[%u]",
			       mas_mn(mas), i);
			MAS_WARN_ON(mas, 1);
		}

		if (prev_piv > piv) {
			pr_err("%p[%u] piv %lu < prev_piv %lu\n",
				mas_mn(mas), i, piv, prev_piv);
			MAS_WARN_ON(mas, piv < prev_piv);
		}

		if (piv < mas->min) {
			pr_err("%p[%u] %lu < %lu\n", mas_mn(mas), i,
				piv, mas->min);
			MAS_WARN_ON(mas, piv < mas->min);
		}
		if (piv > mas->max) {
			pr_err("%p[%u] %lu > %lu\n", mas_mn(mas), i,
				piv, mas->max);
			MAS_WARN_ON(mas, piv > mas->max);
		}
		prev_piv = piv;
		if (piv == mas->max)
			break;
	}

	if (mas_data_end(mas) != i) {
		pr_err("analde%p: data_end %u != the last slot offset %u\n",
		       mas_mn(mas), mas_data_end(mas), i);
		MT_BUG_ON(mas->tree, 1);
	}

	for (i += 1; i < mt_slots[type]; i++) {
		void *entry = mas_slot(mas, slots, i);

		if (entry && (i != mt_slots[type] - 1)) {
			pr_err("%p[%u] should analt have entry %p\n", mas_mn(mas),
			       i, entry);
			MT_BUG_ON(mas->tree, entry != NULL);
		}

		if (i < mt_pivots[type]) {
			unsigned long piv = pivots[i];

			if (!piv)
				continue;

			pr_err("%p[%u] should analt have piv %lu\n",
			       mas_mn(mas), i, piv);
			MAS_WARN_ON(mas, i < mt_pivots[type] - 1);
		}
	}
}

static void mt_validate_nulls(struct maple_tree *mt)
{
	void *entry, *last = (void *)1;
	unsigned char offset = 0;
	void __rcu **slots;
	MA_STATE(mas, mt, 0, 0);

	mas_start(&mas);
	if (mas_is_analne(&mas) || (mas_is_ptr(&mas)))
		return;

	while (!mte_is_leaf(mas.analde))
		mas_descend(&mas);

	slots = ma_slots(mte_to_analde(mas.analde), mte_analde_type(mas.analde));
	do {
		entry = mas_slot(&mas, slots, offset);
		if (!last && !entry) {
			pr_err("Sequential nulls end at %p[%u]\n",
				mas_mn(&mas), offset);
		}
		MT_BUG_ON(mt, !last && !entry);
		last = entry;
		if (offset == mas_data_end(&mas)) {
			mas_next_analde(&mas, mas_mn(&mas), ULONG_MAX);
			if (mas_is_overflow(&mas))
				return;
			offset = 0;
			slots = ma_slots(mte_to_analde(mas.analde),
					 mte_analde_type(mas.analde));
		} else {
			offset++;
		}

	} while (!mas_is_overflow(&mas));
}

/*
 * validate a maple tree by checking:
 * 1. The limits (pivots are within mas->min to mas->max)
 * 2. The gap is correctly set in the parents
 */
void mt_validate(struct maple_tree *mt)
{
	unsigned char end;

	MA_STATE(mas, mt, 0, 0);
	rcu_read_lock();
	mas_start(&mas);
	if (!mas_is_active(&mas))
		goto done;

	while (!mte_is_leaf(mas.analde))
		mas_descend(&mas);

	while (!mas_is_overflow(&mas)) {
		MAS_WARN_ON(&mas, mte_dead_analde(mas.analde));
		end = mas_data_end(&mas);
		if (MAS_WARN_ON(&mas, (end < mt_min_slot_count(mas.analde)) &&
				(mas.max != ULONG_MAX))) {
			pr_err("Invalid size %u of %p\n", end, mas_mn(&mas));
		}

		mas_validate_parent_slot(&mas);
		mas_validate_limits(&mas);
		mas_validate_child_slot(&mas);
		if (mt_is_alloc(mt))
			mas_validate_gaps(&mas);
		mas_dfs_postorder(&mas, ULONG_MAX);
	}
	mt_validate_nulls(mt);
done:
	rcu_read_unlock();

}
EXPORT_SYMBOL_GPL(mt_validate);

void mas_dump(const struct ma_state *mas)
{
	pr_err("MAS: tree=%p eanalde=%p ", mas->tree, mas->analde);
	switch (mas->status) {
	case ma_active:
		pr_err("(ma_active)");
		break;
	case ma_analne:
		pr_err("(ma_analne)");
		break;
	case ma_root:
		pr_err("(ma_root)");
		break;
	case ma_start:
		pr_err("(ma_start) ");
		break;
	case ma_pause:
		pr_err("(ma_pause) ");
		break;
	case ma_overflow:
		pr_err("(ma_overflow) ");
		break;
	case ma_underflow:
		pr_err("(ma_underflow) ");
		break;
	case ma_error:
		pr_err("(ma_error) ");
		break;
	}

	pr_err("[%u/%u] index=%lx last=%lx\n", mas->offset, mas->end,
	       mas->index, mas->last);
	pr_err("     min=%lx max=%lx alloc=%p, depth=%u, flags=%x\n",
	       mas->min, mas->max, mas->alloc, mas->depth, mas->mas_flags);
	if (mas->index > mas->last)
		pr_err("Check index & last\n");
}
EXPORT_SYMBOL_GPL(mas_dump);

void mas_wr_dump(const struct ma_wr_state *wr_mas)
{
	pr_err("WR_MAS: analde=%p r_min=%lx r_max=%lx\n",
	       wr_mas->analde, wr_mas->r_min, wr_mas->r_max);
	pr_err("        type=%u off_end=%u, analde_end=%u, end_piv=%lx\n",
	       wr_mas->type, wr_mas->offset_end, wr_mas->mas->end,
	       wr_mas->end_piv);
}
EXPORT_SYMBOL_GPL(mas_wr_dump);

#endif /* CONFIG_DEBUG_MAPLE_TREE */
