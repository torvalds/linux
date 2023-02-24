// SPDX-License-Identifier: GPL-2.0+
/*
 * Maple Tree implementation
 * Copyright (c) 2018-2022 Oracle Corporation
 * Authors: Liam R. Howlett <Liam.Howlett@oracle.com>
 *	    Matthew Wilcox <willy@infradead.org>
 */

/*
 * DOC: Interesting implementation details of the Maple Tree
 *
 * Each node type has a number of slots for entries and a number of slots for
 * pivots.  In the case of dense nodes, the pivots are implied by the position
 * and are simply the slot index + the minimum of the node.
 *
 * In regular B-Tree terms, pivots are called keys.  The term pivot is used to
 * indicate that the tree is specifying ranges,  Pivots may appear in the
 * subtree with an entry attached to the value where as keys are unique to a
 * specific position of a B-tree.  Pivot values are inclusive of the slot with
 * the same index.
 *
 *
 * The following illustrates the layout of a range64 nodes slots and pivots.
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
 *  Internal (non-leaf) nodes contain pointers to other nodes.
 *  Leaf nodes contain entries.
 *
 * The location of interest is often referred to as an offset.  All offsets have
 * a slot, but the last offset has an implied pivot from the node above (or
 * UINT_MAX for the root node.
 *
 * Ranges complicate certain write activities.  When modifying any of
 * the B-tree variants, it is known that one entry will either be added or
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
 * * MA_STATE_PREALLOC		- Preallocated nodes, WARN_ON allocation
 */
#define MA_STATE_BULK		1
#define MA_STATE_REBALANCE	2
#define MA_STATE_PREALLOC	4

#define ma_parent_ptr(x) ((struct maple_pnode *)(x))
#define ma_mnode_ptr(x) ((struct maple_node *)(x))
#define ma_enode_ptr(x) ((struct maple_enode *)(x))
static struct kmem_cache *maple_node_cache;

#ifdef CONFIG_DEBUG_MAPLE_TREE
static const unsigned long mt_max[] = {
	[maple_dense]		= MAPLE_NODE_SLOTS,
	[maple_leaf_64]		= ULONG_MAX,
	[maple_range_64]	= ULONG_MAX,
	[maple_arange_64]	= ULONG_MAX,
};
#define mt_node_max(x) mt_max[mte_node_type(x)]
#endif

static const unsigned char mt_slots[] = {
	[maple_dense]		= MAPLE_NODE_SLOTS,
	[maple_leaf_64]		= MAPLE_RANGE64_SLOTS,
	[maple_range_64]	= MAPLE_RANGE64_SLOTS,
	[maple_arange_64]	= MAPLE_ARANGE64_SLOTS,
};
#define mt_slot_count(x) mt_slots[mte_node_type(x)]

static const unsigned char mt_pivots[] = {
	[maple_dense]		= 0,
	[maple_leaf_64]		= MAPLE_RANGE64_SLOTS - 1,
	[maple_range_64]	= MAPLE_RANGE64_SLOTS - 1,
	[maple_arange_64]	= MAPLE_ARANGE64_SLOTS - 1,
};
#define mt_pivot_count(x) mt_pivots[mte_node_type(x)]

static const unsigned char mt_min_slots[] = {
	[maple_dense]		= MAPLE_NODE_SLOTS / 2,
	[maple_leaf_64]		= (MAPLE_RANGE64_SLOTS / 2) - 2,
	[maple_range_64]	= (MAPLE_RANGE64_SLOTS / 2) - 2,
	[maple_arange_64]	= (MAPLE_ARANGE64_SLOTS / 2) - 1,
};
#define mt_min_slot_count(x) mt_min_slots[mte_node_type(x)]

#define MAPLE_BIG_NODE_SLOTS	(MAPLE_RANGE64_SLOTS * 2 + 2)
#define MAPLE_BIG_NODE_GAPS	(MAPLE_ARANGE64_SLOTS * 2 + 1)

struct maple_big_node {
	struct maple_pnode *parent;
	unsigned long pivot[MAPLE_BIG_NODE_SLOTS - 1];
	union {
		struct maple_enode *slot[MAPLE_BIG_NODE_SLOTS];
		struct {
			unsigned long padding[MAPLE_BIG_NODE_GAPS];
			unsigned long gap[MAPLE_BIG_NODE_GAPS];
		};
	};
	unsigned char b_end;
	enum maple_type type;
};

/*
 * The maple_subtree_state is used to build a tree to replace a segment of an
 * existing tree in a more atomic way.  Any walkers of the older tree will hit a
 * dead node and restart on updates.
 */
struct maple_subtree_state {
	struct ma_state *orig_l;	/* Original left side of subtree */
	struct ma_state *orig_r;	/* Original right side of subtree */
	struct ma_state *l;		/* New left side of subtree */
	struct ma_state *m;		/* New middle of subtree (rare) */
	struct ma_state *r;		/* New right side of subtree */
	struct ma_topiary *free;	/* nodes to be freed */
	struct ma_topiary *destroy;	/* Nodes to be destroyed (walked and freed) */
	struct maple_big_node *bn;
};

#ifdef CONFIG_KASAN_STACK
/* Prevent mas_wr_bnode() from exceeding the stack frame limit */
#define noinline_for_kasan noinline_for_stack
#else
#define noinline_for_kasan inline
#endif

/* Functions */
static inline struct maple_node *mt_alloc_one(gfp_t gfp)
{
	return kmem_cache_alloc(maple_node_cache, gfp);
}

static inline int mt_alloc_bulk(gfp_t gfp, size_t size, void **nodes)
{
	return kmem_cache_alloc_bulk(maple_node_cache, gfp, size, nodes);
}

static inline void mt_free_bulk(size_t size, void __rcu **nodes)
{
	kmem_cache_free_bulk(maple_node_cache, size, (void **)nodes);
}

static void mt_free_rcu(struct rcu_head *head)
{
	struct maple_node *node = container_of(head, struct maple_node, rcu);

	kmem_cache_free(maple_node_cache, node);
}

/*
 * ma_free_rcu() - Use rcu callback to free a maple node
 * @node: The node to free
 *
 * The maple tree uses the parent pointer to indicate this node is no longer in
 * use and will be freed.
 */
static void ma_free_rcu(struct maple_node *node)
{
	node->parent = ma_parent_ptr(node);
	call_rcu(&node->rcu, mt_free_rcu);
}

static void mas_set_height(struct ma_state *mas)
{
	unsigned int new_flags = mas->tree->ma_flags;

	new_flags &= ~MT_FLAGS_HEIGHT_MASK;
	BUG_ON(mas->depth > MAPLE_HEIGHT_MAX);
	new_flags |= mas->depth << MT_FLAGS_HEIGHT_OFFSET;
	mas->tree->ma_flags = new_flags;
}

static unsigned int mas_mt_height(struct ma_state *mas)
{
	return mt_height(mas->tree);
}

static inline enum maple_type mte_node_type(const struct maple_enode *entry)
{
	return ((unsigned long)entry >> MAPLE_NODE_TYPE_SHIFT) &
		MAPLE_NODE_TYPE_MASK;
}

static inline bool ma_is_dense(const enum maple_type type)
{
	return type < maple_leaf_64;
}

static inline bool ma_is_leaf(const enum maple_type type)
{
	return type < maple_range_64;
}

static inline bool mte_is_leaf(const struct maple_enode *entry)
{
	return ma_is_leaf(mte_node_type(entry));
}

/*
 * We also reserve values with the bottom two bits set to '10' which are
 * below 4096
 */
static inline bool mt_is_reserved(const void *entry)
{
	return ((unsigned long)entry < MAPLE_RESERVED_RANGE) &&
		xa_is_internal(entry);
}

static inline void mas_set_err(struct ma_state *mas, long err)
{
	mas->node = MA_ERROR(err);
}

static inline bool mas_is_ptr(struct ma_state *mas)
{
	return mas->node == MAS_ROOT;
}

static inline bool mas_is_start(struct ma_state *mas)
{
	return mas->node == MAS_START;
}

bool mas_is_err(struct ma_state *mas)
{
	return xa_is_err(mas->node);
}

static inline bool mas_searchable(struct ma_state *mas)
{
	if (mas_is_none(mas))
		return false;

	if (mas_is_ptr(mas))
		return false;

	return true;
}

static inline struct maple_node *mte_to_node(const struct maple_enode *entry)
{
	return (struct maple_node *)((unsigned long)entry & ~MAPLE_NODE_MASK);
}

/*
 * mte_to_mat() - Convert a maple encoded node to a maple topiary node.
 * @entry: The maple encoded node
 *
 * Return: a maple topiary pointer
 */
static inline struct maple_topiary *mte_to_mat(const struct maple_enode *entry)
{
	return (struct maple_topiary *)
		((unsigned long)entry & ~MAPLE_NODE_MASK);
}

/*
 * mas_mn() - Get the maple state node.
 * @mas: The maple state
 *
 * Return: the maple node (not encoded - bare pointer).
 */
static inline struct maple_node *mas_mn(const struct ma_state *mas)
{
	return mte_to_node(mas->node);
}

/*
 * mte_set_node_dead() - Set a maple encoded node as dead.
 * @mn: The maple encoded node.
 */
static inline void mte_set_node_dead(struct maple_enode *mn)
{
	mte_to_node(mn)->parent = ma_parent_ptr(mte_to_node(mn));
	smp_wmb(); /* Needed for RCU */
}

/* Bit 1 indicates the root is a node */
#define MAPLE_ROOT_NODE			0x02
/* maple_type stored bit 3-6 */
#define MAPLE_ENODE_TYPE_SHIFT		0x03
/* Bit 2 means a NULL somewhere below */
#define MAPLE_ENODE_NULL		0x04

static inline struct maple_enode *mt_mk_node(const struct maple_node *node,
					     enum maple_type type)
{
	return (void *)((unsigned long)node |
			(type << MAPLE_ENODE_TYPE_SHIFT) | MAPLE_ENODE_NULL);
}

static inline void *mte_mk_root(const struct maple_enode *node)
{
	return (void *)((unsigned long)node | MAPLE_ROOT_NODE);
}

static inline void *mte_safe_root(const struct maple_enode *node)
{
	return (void *)((unsigned long)node & ~MAPLE_ROOT_NODE);
}

static inline void *mte_set_full(const struct maple_enode *node)
{
	return (void *)((unsigned long)node & ~MAPLE_ENODE_NULL);
}

static inline void *mte_clear_full(const struct maple_enode *node)
{
	return (void *)((unsigned long)node | MAPLE_ENODE_NULL);
}

static inline bool mte_has_null(const struct maple_enode *node)
{
	return (unsigned long)node & MAPLE_ENODE_NULL;
}

static inline bool ma_is_root(struct maple_node *node)
{
	return ((unsigned long)node->parent & MA_ROOT_PARENT);
}

static inline bool mte_is_root(const struct maple_enode *node)
{
	return ma_is_root(mte_to_node(node));
}

static inline bool mas_is_root_limits(const struct ma_state *mas)
{
	return !mas->min && mas->max == ULONG_MAX;
}

static inline bool mt_is_alloc(struct maple_tree *mt)
{
	return (mt->ma_flags & MT_FLAGS_ALLOC_RANGE);
}

/*
 * The Parent Pointer
 * Excluding root, the parent pointer is 256B aligned like all other tree nodes.
 * When storing a 32 or 64 bit values, the offset can fit into 5 bits.  The 16
 * bit values need an extra bit to store the offset.  This extra bit comes from
 * a reuse of the last bit in the node type.  This is possible by using bit 1 to
 * indicate if bit 2 is part of the type or the slot.
 *
 * Note types:
 *  0x??1 = Root
 *  0x?00 = 16 bit nodes
 *  0x010 = 32 bit nodes
 *  0x110 = 64 bit nodes
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
#define MAPLE_PARENT_NOT_RANGE16	0x02

/*
 * mte_parent_shift() - Get the parent shift for the slot storage.
 * @parent: The parent pointer cast as an unsigned long
 * Return: The shift into that pointer to the star to of the slot
 */
static inline unsigned long mte_parent_shift(unsigned long parent)
{
	/* Note bit 1 == 0 means 16B */
	if (likely(parent & MAPLE_PARENT_NOT_RANGE16))
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
	/* Note bit 1 == 0 means 16B */
	if (likely(parent & MAPLE_PARENT_NOT_RANGE16))
		return MAPLE_PARENT_SLOT_MASK;

	return MAPLE_PARENT_16B_SLOT_MASK;
}

/*
 * mas_parent_enum() - Return the maple_type of the parent from the stored
 * parent type.
 * @mas: The maple state
 * @node: The maple_enode to extract the parent's enum
 * Return: The node->parent maple_type
 */
static inline
enum maple_type mte_parent_enum(struct maple_enode *p_enode,
				struct maple_tree *mt)
{
	unsigned long p_type;

	p_type = (unsigned long)p_enode;
	if (p_type & MAPLE_PARENT_ROOT)
		return 0; /* Validated in the caller. */

	p_type &= MAPLE_NODE_MASK;
	p_type = p_type & ~(MAPLE_PARENT_ROOT | mte_parent_slot_mask(p_type));

	switch (p_type) {
	case MAPLE_PARENT_RANGE64: /* or MAPLE_PARENT_ARANGE64 */
		if (mt_is_alloc(mt))
			return maple_arange_64;
		return maple_range_64;
	}

	return 0;
}

static inline
enum maple_type mas_parent_enum(struct ma_state *mas, struct maple_enode *enode)
{
	return mte_parent_enum(ma_enode_ptr(mte_to_node(enode)->parent), mas->tree);
}

/*
 * mte_set_parent() - Set the parent node and encode the slot
 * @enode: The encoded maple node.
 * @parent: The encoded maple node that is the parent of @enode.
 * @slot: The slot that @enode resides in @parent.
 *
 * Slot number is encoded in the enode->parent bit 3-6 or 2-6, depending on the
 * parent type.
 */
static inline
void mte_set_parent(struct maple_enode *enode, const struct maple_enode *parent,
		    unsigned char slot)
{
	unsigned long val = (unsigned long)parent;
	unsigned long shift;
	unsigned long type;
	enum maple_type p_type = mte_node_type(parent);

	BUG_ON(p_type == maple_dense);
	BUG_ON(p_type == maple_leaf_64);

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

	val &= ~MAPLE_NODE_MASK; /* Clear all node metadata in parent */
	val |= (slot << shift) | type;
	mte_to_node(enode)->parent = ma_parent_ptr(val);
}

/*
 * mte_parent_slot() - get the parent slot of @enode.
 * @enode: The encoded maple node.
 *
 * Return: The slot in the parent node where @enode resides.
 */
static inline unsigned int mte_parent_slot(const struct maple_enode *enode)
{
	unsigned long val = (unsigned long)mte_to_node(enode)->parent;

	if (val & MA_ROOT_PARENT)
		return 0;

	/*
	 * Okay to use MAPLE_PARENT_16B_SLOT_MASK as the last bit will be lost
	 * by shift if the parent shift is MAPLE_PARENT_SLOT_SHIFT
	 */
	return (val & MAPLE_PARENT_16B_SLOT_MASK) >> mte_parent_shift(val);
}

/*
 * mte_parent() - Get the parent of @node.
 * @node: The encoded maple node.
 *
 * Return: The parent maple node.
 */
static inline struct maple_node *mte_parent(const struct maple_enode *enode)
{
	return (void *)((unsigned long)
			(mte_to_node(enode)->parent) & ~MAPLE_NODE_MASK);
}

/*
 * ma_dead_node() - check if the @enode is dead.
 * @enode: The encoded maple node
 *
 * Return: true if dead, false otherwise.
 */
static inline bool ma_dead_node(const struct maple_node *node)
{
	struct maple_node *parent = (void *)((unsigned long)
					     node->parent & ~MAPLE_NODE_MASK);

	return (parent == node);
}
/*
 * mte_dead_node() - check if the @enode is dead.
 * @enode: The encoded maple node
 *
 * Return: true if dead, false otherwise.
 */
static inline bool mte_dead_node(const struct maple_enode *enode)
{
	struct maple_node *parent, *node;

	node = mte_to_node(enode);
	parent = mte_parent(enode);
	return (parent == node);
}

/*
 * mas_allocated() - Get the number of nodes allocated in a maple state.
 * @mas: The maple state
 *
 * The ma_state alloc member is overloaded to hold a pointer to the first
 * allocated node or to the number of requested nodes to allocate.  If bit 0 is
 * set, then the alloc contains the number of requested nodes.  If there is an
 * allocated node, then the total allocated nodes is in that node.
 *
 * Return: The total number of nodes allocated
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
 * The requested number of allocations is either in the first allocated node,
 * located in @mas->alloc->request_count, or directly in @mas->alloc if there is
 * no allocated node.  Set the request either in the node or do the necessary
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
 * @mas->alloc->request_count if there is at least one node allocated.  Decode
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
 * ma_pivots() - Get a pointer to the maple node pivots.
 * @node - the maple node
 * @type - the node type
 *
 * Return: A pointer to the maple node pivots
 */
static inline unsigned long *ma_pivots(struct maple_node *node,
					   enum maple_type type)
{
	switch (type) {
	case maple_arange_64:
		return node->ma64.pivot;
	case maple_range_64:
	case maple_leaf_64:
		return node->mr64.pivot;
	case maple_dense:
		return NULL;
	}
	return NULL;
}

/*
 * ma_gaps() - Get a pointer to the maple node gaps.
 * @node - the maple node
 * @type - the node type
 *
 * Return: A pointer to the maple node gaps
 */
static inline unsigned long *ma_gaps(struct maple_node *node,
				     enum maple_type type)
{
	switch (type) {
	case maple_arange_64:
		return node->ma64.gap;
	case maple_range_64:
	case maple_leaf_64:
	case maple_dense:
		return NULL;
	}
	return NULL;
}

/*
 * mte_pivot() - Get the pivot at @piv of the maple encoded node.
 * @mn: The maple encoded node.
 * @piv: The pivot.
 *
 * Return: the pivot at @piv of @mn.
 */
static inline unsigned long mte_pivot(const struct maple_enode *mn,
				 unsigned char piv)
{
	struct maple_node *node = mte_to_node(mn);
	enum maple_type type = mte_node_type(mn);

	if (piv >= mt_pivots[type]) {
		WARN_ON(1);
		return 0;
	}
	switch (type) {
	case maple_arange_64:
		return node->ma64.pivot[piv];
	case maple_range_64:
	case maple_leaf_64:
		return node->mr64.pivot[piv];
	case maple_dense:
		return 0;
	}
	return 0;
}

/*
 * mas_safe_pivot() - get the pivot at @piv or mas->max.
 * @mas: The maple state
 * @pivots: The pointer to the maple node pivots
 * @piv: The pivot to fetch
 * @type: The maple node type
 *
 * Return: The pivot at @piv within the limit of the @pivots array, @mas->max
 * otherwise.
 */
static inline unsigned long
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
 * @pivots: The pointer to the maple node pivots
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
 * mas_logical_pivot() - Get the logical pivot of a given offset.
 * @mas: The maple state
 * @pivots: The pointer to the maple node pivots
 * @offset: The offset into the pivot array
 * @type: The maple node type
 *
 * When there is no value at a pivot (beyond the end of the data), then the
 * pivot is actually @mas->max.
 *
 * Return: the logical pivot of a given @offset.
 */
static inline unsigned long
mas_logical_pivot(struct ma_state *mas, unsigned long *pivots,
		  unsigned char offset, enum maple_type type)
{
	unsigned long lpiv = mas_safe_pivot(mas, pivots, offset, type);

	if (likely(lpiv))
		return lpiv;

	if (likely(offset))
		return mas->max;

	return lpiv;
}

/*
 * mte_set_pivot() - Set a pivot to a value in an encoded maple node.
 * @mn: The encoded maple node
 * @piv: The pivot offset
 * @val: The value of the pivot
 */
static inline void mte_set_pivot(struct maple_enode *mn, unsigned char piv,
				unsigned long val)
{
	struct maple_node *node = mte_to_node(mn);
	enum maple_type type = mte_node_type(mn);

	BUG_ON(piv >= mt_pivots[type]);
	switch (type) {
	default:
	case maple_range_64:
	case maple_leaf_64:
		node->mr64.pivot[piv] = val;
		break;
	case maple_arange_64:
		node->ma64.pivot[piv] = val;
		break;
	case maple_dense:
		break;
	}

}

/*
 * ma_slots() - Get a pointer to the maple node slots.
 * @mn: The maple node
 * @mt: The maple node type
 *
 * Return: A pointer to the maple node slots
 */
static inline void __rcu **ma_slots(struct maple_node *mn, enum maple_type mt)
{
	switch (mt) {
	default:
	case maple_arange_64:
		return mn->ma64.slot;
	case maple_range_64:
	case maple_leaf_64:
		return mn->mr64.slot;
	case maple_dense:
		return mn->slot;
	}
}

static inline bool mt_locked(const struct maple_tree *mt)
{
	return mt_external_lock(mt) ? mt_lock_is_held(mt) :
		lockdep_is_held(&mt->ma_lock);
}

static inline void *mt_slot(const struct maple_tree *mt,
		void __rcu **slots, unsigned char offset)
{
	return rcu_dereference_check(slots[offset], mt_locked(mt));
}

/*
 * mas_slot_locked() - Get the slot value when holding the maple tree lock.
 * @mas: The maple state
 * @slots: The pointer to the slots
 * @offset: The offset into the slots array to fetch
 *
 * Return: The entry stored in @slots at the @offset.
 */
static inline void *mas_slot_locked(struct ma_state *mas, void __rcu **slots,
				       unsigned char offset)
{
	return rcu_dereference_protected(slots[offset], mt_locked(mas->tree));
}

/*
 * mas_slot() - Get the slot value when not holding the maple tree lock.
 * @mas: The maple state
 * @slots: The pointer to the slots
 * @offset: The offset into the slots array to fetch
 *
 * Return: The entry stored in @slots at the @offset
 */
static inline void *mas_slot(struct ma_state *mas, void __rcu **slots,
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
static inline void *mas_root(struct ma_state *mas)
{
	return rcu_dereference_check(mas->tree->ma_root, mt_locked(mas->tree));
}

static inline void *mt_root_locked(struct maple_tree *mt)
{
	return rcu_dereference_protected(mt->ma_root, mt_locked(mt));
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

static inline struct maple_metadata *ma_meta(struct maple_node *mn,
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
 * ma_set_meta() - Set the metadata information of a node.
 * @mn: The maple node
 * @mt: The maple node type
 * @offset: The offset of the highest sub-gap in this node.
 * @end: The end of the data in this node.
 */
static inline void ma_set_meta(struct maple_node *mn, enum maple_type mt,
			       unsigned char offset, unsigned char end)
{
	struct maple_metadata *meta = ma_meta(mn, mt);

	meta->gap = offset;
	meta->end = end;
}

/*
 * ma_meta_end() - Get the data end of a node from the metadata
 * @mn: The maple node
 * @mt: The maple node type
 */
static inline unsigned char ma_meta_end(struct maple_node *mn,
					enum maple_type mt)
{
	struct maple_metadata *meta = ma_meta(mn, mt);

	return meta->end;
}

/*
 * ma_meta_gap() - Get the largest gap location of a node from the metadata
 * @mn: The maple node
 * @mt: The maple node type
 */
static inline unsigned char ma_meta_gap(struct maple_node *mn,
					enum maple_type mt)
{
	BUG_ON(mt != maple_arange_64);

	return mn->ma64.meta.gap;
}

/*
 * ma_set_meta_gap() - Set the largest gap location in a nodes metadata
 * @mn: The maple node
 * @mn: The maple node type
 * @offset: The location of the largest gap.
 */
static inline void ma_set_meta_gap(struct maple_node *mn, enum maple_type mt,
				   unsigned char offset)
{

	struct maple_metadata *meta = ma_meta(mn, mt);

	meta->gap = offset;
}

/*
 * mat_add() - Add a @dead_enode to the ma_topiary of a list of dead nodes.
 * @mat - the ma_topiary, a linked list of dead nodes.
 * @dead_enode - the node to be marked as dead and added to the tail of the list
 *
 * Add the @dead_enode to the linked list in @mat.
 */
static inline void mat_add(struct ma_topiary *mat,
			   struct maple_enode *dead_enode)
{
	mte_set_node_dead(dead_enode);
	mte_to_mat(dead_enode)->next = NULL;
	if (!mat->tail) {
		mat->tail = mat->head = dead_enode;
		return;
	}

	mte_to_mat(mat->tail)->next = dead_enode;
	mat->tail = dead_enode;
}

static void mte_destroy_walk(struct maple_enode *, struct maple_tree *);
static inline void mas_free(struct ma_state *mas, struct maple_enode *used);

/*
 * mas_mat_free() - Free all nodes in a dead list.
 * @mas - the maple state
 * @mat - the ma_topiary linked list of dead nodes to free.
 *
 * Free walk a dead list.
 */
static void mas_mat_free(struct ma_state *mas, struct ma_topiary *mat)
{
	struct maple_enode *next;

	while (mat->head) {
		next = mte_to_mat(mat->head)->next;
		mas_free(mas, mat->head);
		mat->head = next;
	}
}

/*
 * mas_mat_destroy() - Free all nodes and subtrees in a dead list.
 * @mas - the maple state
 * @mat - the ma_topiary linked list of dead nodes to free.
 *
 * Destroy walk a dead list.
 */
static void mas_mat_destroy(struct ma_state *mas, struct ma_topiary *mat)
{
	struct maple_enode *next;

	while (mat->head) {
		next = mte_to_mat(mat->head)->next;
		mte_destroy_walk(mat->head, mat->mtree);
		mat->head = next;
	}
}
/*
 * mas_descend() - Descend into the slot stored in the ma_state.
 * @mas - the maple state.
 *
 * Note: Not RCU safe, only use in write side or debug code.
 */
static inline void mas_descend(struct ma_state *mas)
{
	enum maple_type type;
	unsigned long *pivots;
	struct maple_node *node;
	void __rcu **slots;

	node = mas_mn(mas);
	type = mte_node_type(mas->node);
	pivots = ma_pivots(node, type);
	slots = ma_slots(node, type);

	if (mas->offset)
		mas->min = pivots[mas->offset - 1] + 1;
	mas->max = mas_safe_pivot(mas, pivots, mas->offset, type);
	mas->node = mas_slot(mas, slots, mas->offset);
}

/*
 * mte_set_gap() - Set a maple node gap.
 * @mn: The encoded maple node
 * @gap: The offset of the gap to set
 * @val: The gap value
 */
static inline void mte_set_gap(const struct maple_enode *mn,
				 unsigned char gap, unsigned long val)
{
	switch (mte_node_type(mn)) {
	default:
		break;
	case maple_arange_64:
		mte_to_node(mn)->ma64.gap[gap] = val;
		break;
	}
}

/*
 * mas_ascend() - Walk up a level of the tree.
 * @mas: The maple state
 *
 * Sets the @mas->max and @mas->min to the correct values when walking up.  This
 * may cause several levels of walking up to find the correct min and max.
 * May find a dead node which will cause a premature return.
 * Return: 1 on dead node, 0 otherwise
 */
static int mas_ascend(struct ma_state *mas)
{
	struct maple_enode *p_enode; /* parent enode. */
	struct maple_enode *a_enode; /* ancestor enode. */
	struct maple_node *a_node; /* ancestor node. */
	struct maple_node *p_node; /* parent node. */
	unsigned char a_slot;
	enum maple_type a_type;
	unsigned long min, max;
	unsigned long *pivots;
	unsigned char offset;
	bool set_max = false, set_min = false;

	a_node = mas_mn(mas);
	if (ma_is_root(a_node)) {
		mas->offset = 0;
		return 0;
	}

	p_node = mte_parent(mas->node);
	if (unlikely(a_node == p_node))
		return 1;
	a_type = mas_parent_enum(mas, mas->node);
	offset = mte_parent_slot(mas->node);
	a_enode = mt_mk_node(p_node, a_type);

	/* Check to make sure all parent information is still accurate */
	if (p_node != mte_parent(mas->node))
		return 1;

	mas->node = a_enode;
	mas->offset = offset;

	if (mte_is_root(a_enode)) {
		mas->max = ULONG_MAX;
		mas->min = 0;
		return 0;
	}

	min = 0;
	max = ULONG_MAX;
	do {
		p_enode = a_enode;
		a_type = mas_parent_enum(mas, p_enode);
		a_node = mte_parent(p_enode);
		a_slot = mte_parent_slot(p_enode);
		pivots = ma_pivots(a_node, a_type);
		a_enode = mt_mk_node(a_node, a_type);

		if (!set_min && a_slot) {
			set_min = true;
			min = pivots[a_slot - 1] + 1;
		}

		if (!set_max && a_slot < mt_pivots[a_type]) {
			set_max = true;
			max = pivots[a_slot];
		}

		if (unlikely(ma_dead_node(a_node)))
			return 1;

		if (unlikely(ma_is_root(a_node)))
			break;

	} while (!set_min || !set_max);

	mas->max = max;
	mas->min = min;
	return 0;
}

/*
 * mas_pop_node() - Get a previously allocated maple node from the maple state.
 * @mas: The maple state
 *
 * Return: A pointer to a maple node.
 */
static inline struct maple_node *mas_pop_node(struct ma_state *mas)
{
	struct maple_alloc *ret, *node = mas->alloc;
	unsigned long total = mas_allocated(mas);
	unsigned int req = mas_alloc_req(mas);

	/* nothing or a request pending. */
	if (WARN_ON(!total))
		return NULL;

	if (total == 1) {
		/* single allocation in this ma_state */
		mas->alloc = NULL;
		ret = node;
		goto single_node;
	}

	if (node->node_count == 1) {
		/* Single allocation in this node. */
		mas->alloc = node->slot[0];
		mas->alloc->total = node->total - 1;
		ret = node;
		goto new_head;
	}
	node->total--;
	ret = node->slot[--node->node_count];
	node->slot[node->node_count] = NULL;

single_node:
new_head:
	if (req) {
		req++;
		mas_set_alloc_req(mas, req);
	}

	memset(ret, 0, sizeof(*ret));
	return (struct maple_node *)ret;
}

/*
 * mas_push_node() - Push a node back on the maple state allocation.
 * @mas: The maple state
 * @used: The used maple node
 *
 * Stores the maple node back into @mas->alloc for reuse.  Updates allocated and
 * requested node count as necessary.
 */
static inline void mas_push_node(struct ma_state *mas, struct maple_node *used)
{
	struct maple_alloc *reuse = (struct maple_alloc *)used;
	struct maple_alloc *head = mas->alloc;
	unsigned long count;
	unsigned int requested = mas_alloc_req(mas);

	count = mas_allocated(mas);

	reuse->request_count = 0;
	reuse->node_count = 0;
	if (count && (head->node_count < MAPLE_ALLOC_SLOTS)) {
		head->slot[head->node_count++] = reuse;
		head->total++;
		goto done;
	}

	reuse->total = 1;
	if ((head) && !((unsigned long)head & 0x1)) {
		reuse->slot[0] = head;
		reuse->node_count = 1;
		reuse->total += head->total;
	}

	mas->alloc = reuse;
done:
	if (requested > 1)
		mas_set_alloc_req(mas, requested - 1);
}

/*
 * mas_alloc_nodes() - Allocate nodes into a maple state
 * @mas: The maple state
 * @gfp: The GFP Flags
 */
static inline void mas_alloc_nodes(struct ma_state *mas, gfp_t gfp)
{
	struct maple_alloc *node;
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
		WARN_ON(!allocated);
	}

	if (!allocated || mas->alloc->node_count == MAPLE_ALLOC_SLOTS) {
		node = (struct maple_alloc *)mt_alloc_one(gfp);
		if (!node)
			goto nomem_one;

		if (allocated) {
			node->slot[0] = mas->alloc;
			node->node_count = 1;
		} else {
			node->node_count = 0;
		}

		mas->alloc = node;
		node->total = ++allocated;
		requested--;
	}

	node = mas->alloc;
	node->request_count = 0;
	while (requested) {
		max_req = MAPLE_ALLOC_SLOTS;
		if (node->node_count) {
			unsigned int offset = node->node_count;

			slots = (void **)&node->slot[offset];
			max_req -= offset;
		} else {
			slots = (void **)&node->slot;
		}

		max_req = min(requested, max_req);
		count = mt_alloc_bulk(gfp, max_req, slots);
		if (!count)
			goto nomem_bulk;

		node->node_count += count;
		allocated += count;
		node = node->slot[0];
		node->node_count = 0;
		node->request_count = 0;
		requested -= count;
	}
	mas->alloc->total = allocated;
	return;

nomem_bulk:
	/* Clean up potential freed allocations on bulk failure */
	memset(slots, 0, max_req * sizeof(unsigned long));
nomem_one:
	mas_set_alloc_req(mas, requested);
	if (mas->alloc && !(((unsigned long)mas->alloc & 0x1)))
		mas->alloc->total = allocated;
	mas_set_err(mas, -ENOMEM);
}

/*
 * mas_free() - Free an encoded maple node
 * @mas: The maple state
 * @used: The encoded maple node to free.
 *
 * Uses rcu free if necessary, pushes @used back on the maple state allocations
 * otherwise.
 */
static inline void mas_free(struct ma_state *mas, struct maple_enode *used)
{
	struct maple_node *tmp = mte_to_node(used);

	if (mt_in_rcu(mas->tree))
		ma_free_rcu(tmp);
	else
		mas_push_node(mas, tmp);
}

/*
 * mas_node_count() - Check if enough nodes are allocated and request more if
 * there is not enough nodes.
 * @mas: The maple state
 * @count: The number of nodes needed
 * @gfp: the gfp flags
 */
static void mas_node_count_gfp(struct ma_state *mas, int count, gfp_t gfp)
{
	unsigned long allocated = mas_allocated(mas);

	if (allocated < count) {
		mas_set_alloc_req(mas, count - allocated);
		mas_alloc_nodes(mas, gfp);
	}
}

/*
 * mas_node_count() - Check if enough nodes are allocated and request more if
 * there is not enough nodes.
 * @mas: The maple state
 * @count: The number of nodes needed
 *
 * Note: Uses GFP_NOWAIT | __GFP_NOWARN for gfp flags.
 */
static void mas_node_count(struct ma_state *mas, int count)
{
	return mas_node_count_gfp(mas, count, GFP_NOWAIT | __GFP_NOWARN);
}

/*
 * mas_start() - Sets up maple state for operations.
 * @mas: The maple state.
 *
 * If mas->node == MAS_START, then set the min, max and depth to
 * defaults.
 *
 * Return:
 * - If mas->node is an error or not MAS_START, return NULL.
 * - If it's an empty tree:     NULL & mas->node == MAS_NONE
 * - If it's a single entry:    The entry & mas->node == MAS_ROOT
 * - If it's a tree:            NULL & mas->node == safe root node.
 */
static inline struct maple_enode *mas_start(struct ma_state *mas)
{
	if (likely(mas_is_start(mas))) {
		struct maple_enode *root;

		mas->min = 0;
		mas->max = ULONG_MAX;
		mas->depth = 0;

		root = mas_root(mas);
		/* Tree with nodes */
		if (likely(xa_is_node(root))) {
			mas->depth = 1;
			mas->node = mte_safe_root(root);
			mas->offset = 0;
			return NULL;
		}

		/* empty tree */
		if (unlikely(!root)) {
			mas->node = MAS_NONE;
			mas->offset = MAPLE_NODE_SLOTS;
			return NULL;
		}

		/* Single entry tree */
		mas->node = MAS_ROOT;
		mas->offset = MAPLE_NODE_SLOTS;

		/* Single entry tree. */
		if (mas->index > 0)
			return NULL;

		return root;
	}

	return NULL;
}

/*
 * ma_data_end() - Find the end of the data in a node.
 * @node: The maple node
 * @type: The maple node type
 * @pivots: The array of pivots in the node
 * @max: The maximum value in the node
 *
 * Uses metadata to find the end of the data when possible.
 * Return: The zero indexed last slot with data (may be null).
 */
static inline unsigned char ma_data_end(struct maple_node *node,
					enum maple_type type,
					unsigned long *pivots,
					unsigned long max)
{
	unsigned char offset;

	if (type == maple_arange_64)
		return ma_meta_end(node, type);

	offset = mt_pivots[type] - 1;
	if (likely(!pivots[offset]))
		return ma_meta_end(node, type);

	if (likely(pivots[offset] == max))
		return offset;

	return mt_pivots[type];
}

/*
 * mas_data_end() - Find the end of the data (slot).
 * @mas: the maple state
 *
 * This method is optimized to check the metadata of a node if the node type
 * supports data end metadata.
 *
 * Return: The zero indexed last slot with data (may be null).
 */
static inline unsigned char mas_data_end(struct ma_state *mas)
{
	enum maple_type type;
	struct maple_node *node;
	unsigned char offset;
	unsigned long *pivots;

	type = mte_node_type(mas->node);
	node = mas_mn(mas);
	if (type == maple_arange_64)
		return ma_meta_end(node, type);

	pivots = ma_pivots(node, type);
	offset = mt_pivots[type] - 1;
	if (likely(!pivots[offset]))
		return ma_meta_end(node, type);

	if (likely(pivots[offset] == mas->max))
		return offset;

	return mt_pivots[type];
}

/*
 * mas_leaf_max_gap() - Returns the largest gap in a leaf node
 * @mas - the maple state
 *
 * Return: The maximum gap in the leaf.
 */
static unsigned long mas_leaf_max_gap(struct ma_state *mas)
{
	enum maple_type mt;
	unsigned long pstart, gap, max_gap;
	struct maple_node *mn;
	unsigned long *pivots;
	void __rcu **slots;
	unsigned char i;
	unsigned char max_piv;

	mt = mte_node_type(mas->node);
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
	 * node.
	 */
	if (unlikely(mas->max == ULONG_MAX) && !slots[max_piv + 1]) {
		gap = ULONG_MAX - pivots[max_piv];
		if (gap > max_gap)
			max_gap = gap;
	}

	for (; i <= max_piv; i++) {
		/* data == no gap. */
		if (likely(slots[i]))
			continue;

		pstart = pivots[i - 1];
		gap = pivots[i] - pstart;
		if (gap > max_gap)
			max_gap = gap;

		/* There cannot be two gaps in a row. */
		i++;
	}
	return max_gap;
}

/*
 * ma_max_gap() - Get the maximum gap in a maple node (non-leaf)
 * @node: The maple node
 * @gaps: The pointer to the gaps
 * @mt: The maple node type
 * @*off: Pointer to store the offset location of the gap.
 *
 * Uses the metadata data end to scan backwards across set gaps.
 *
 * Return: The maximum gap value
 */
static inline unsigned long
ma_max_gap(struct maple_node *node, unsigned long *gaps, enum maple_type mt,
	    unsigned char *off)
{
	unsigned char offset, i;
	unsigned long max_gap = 0;

	i = offset = ma_meta_end(node, mt);
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
 * mas_max_gap() - find the largest gap in a non-leaf node and set the slot.
 * @mas: The maple state.
 *
 * If the metadata gap is set to MAPLE_ARANGE64_META_MAX, there is no gap.
 *
 * Return: The gap value.
 */
static inline unsigned long mas_max_gap(struct ma_state *mas)
{
	unsigned long *gaps;
	unsigned char offset;
	enum maple_type mt;
	struct maple_node *node;

	mt = mte_node_type(mas->node);
	if (ma_is_leaf(mt))
		return mas_leaf_max_gap(mas);

	node = mas_mn(mas);
	offset = ma_meta_gap(node, mt);
	if (offset == MAPLE_ARANGE64_META_MAX)
		return 0;

	gaps = ma_gaps(node, mt);
	return gaps[offset];
}

/*
 * mas_parent_gap() - Set the parent gap and any gaps above, as needed
 * @mas: The maple state
 * @offset: The gap offset in the parent to set
 * @new: The new gap value.
 *
 * Set the parent gap then continue to set the gap upwards, using the metadata
 * of the parent to see if it is necessary to check the node above.
 */
static inline void mas_parent_gap(struct ma_state *mas, unsigned char offset,
		unsigned long new)
{
	unsigned long meta_gap = 0;
	struct maple_node *pnode;
	struct maple_enode *penode;
	unsigned long *pgaps;
	unsigned char meta_offset;
	enum maple_type pmt;

	pnode = mte_parent(mas->node);
	pmt = mas_parent_enum(mas, mas->node);
	penode = mt_mk_node(pnode, pmt);
	pgaps = ma_gaps(pnode, pmt);

ascend:
	meta_offset = ma_meta_gap(pnode, pmt);
	if (meta_offset == MAPLE_ARANGE64_META_MAX)
		meta_gap = 0;
	else
		meta_gap = pgaps[meta_offset];

	pgaps[offset] = new;

	if (meta_gap == new)
		return;

	if (offset != meta_offset) {
		if (meta_gap > new)
			return;

		ma_set_meta_gap(pnode, pmt, offset);
	} else if (new < meta_gap) {
		meta_offset = 15;
		new = ma_max_gap(pnode, pgaps, pmt, &meta_offset);
		ma_set_meta_gap(pnode, pmt, meta_offset);
	}

	if (ma_is_root(pnode))
		return;

	/* Go to the parent node. */
	pnode = mte_parent(penode);
	pmt = mas_parent_enum(mas, penode);
	pgaps = ma_gaps(pnode, pmt);
	offset = mte_parent_slot(penode);
	penode = mt_mk_node(pnode, pmt);
	goto ascend;
}

/*
 * mas_update_gap() - Update a nodes gaps and propagate up if necessary.
 * @mas - the maple state.
 */
static inline void mas_update_gap(struct ma_state *mas)
{
	unsigned char pslot;
	unsigned long p_gap;
	unsigned long max_gap;

	if (!mt_is_alloc(mas->tree))
		return;

	if (mte_is_root(mas->node))
		return;

	max_gap = mas_max_gap(mas);

	pslot = mte_parent_slot(mas->node);
	p_gap = ma_gaps(mte_parent(mas->node),
			mas_parent_enum(mas, mas->node))[pslot];

	if (p_gap != max_gap)
		mas_parent_gap(mas, pslot, max_gap);
}

/*
 * mas_adopt_children() - Set the parent pointer of all nodes in @parent to
 * @parent with the slot encoded.
 * @mas - the maple state (for the tree)
 * @parent - the maple encoded node containing the children.
 */
static inline void mas_adopt_children(struct ma_state *mas,
		struct maple_enode *parent)
{
	enum maple_type type = mte_node_type(parent);
	struct maple_node *node = mas_mn(mas);
	void __rcu **slots = ma_slots(node, type);
	unsigned long *pivots = ma_pivots(node, type);
	struct maple_enode *child;
	unsigned char offset;

	offset = ma_data_end(node, type, pivots, mas->max);
	do {
		child = mas_slot_locked(mas, slots, offset);
		mte_set_parent(child, parent, offset);
	} while (offset--);
}

/*
 * mas_replace() - Replace a maple node in the tree with mas->node.  Uses the
 * parent encoding to locate the maple node in the tree.
 * @mas - the ma_state to use for operations.
 * @advanced - boolean to adopt the child nodes and free the old node (false) or
 * leave the node (true) and handle the adoption and free elsewhere.
 */
static inline void mas_replace(struct ma_state *mas, bool advanced)
	__must_hold(mas->tree->lock)
{
	struct maple_node *mn = mas_mn(mas);
	struct maple_enode *old_enode;
	unsigned char offset = 0;
	void __rcu **slots = NULL;

	if (ma_is_root(mn)) {
		old_enode = mas_root_locked(mas);
	} else {
		offset = mte_parent_slot(mas->node);
		slots = ma_slots(mte_parent(mas->node),
				 mas_parent_enum(mas, mas->node));
		old_enode = mas_slot_locked(mas, slots, offset);
	}

	if (!advanced && !mte_is_leaf(mas->node))
		mas_adopt_children(mas, mas->node);

	if (mte_is_root(mas->node)) {
		mn->parent = ma_parent_ptr(
			      ((unsigned long)mas->tree | MA_ROOT_PARENT));
		rcu_assign_pointer(mas->tree->ma_root, mte_mk_root(mas->node));
		mas_set_height(mas);
	} else {
		rcu_assign_pointer(slots[offset], mas->node);
	}

	if (!advanced)
		mas_free(mas, old_enode);
}

/*
 * mas_new_child() - Find the new child of a node.
 * @mas: the maple state
 * @child: the maple state to store the child.
 */
static inline bool mas_new_child(struct ma_state *mas, struct ma_state *child)
	__must_hold(mas->tree->lock)
{
	enum maple_type mt;
	unsigned char offset;
	unsigned char end;
	unsigned long *pivots;
	struct maple_enode *entry;
	struct maple_node *node;
	void __rcu **slots;

	mt = mte_node_type(mas->node);
	node = mas_mn(mas);
	slots = ma_slots(node, mt);
	pivots = ma_pivots(node, mt);
	end = ma_data_end(node, mt, pivots, mas->max);
	for (offset = mas->offset; offset <= end; offset++) {
		entry = mas_slot_locked(mas, slots, offset);
		if (mte_parent(entry) == node) {
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
 * mab_shift_right() - Shift the data in mab right. Note, does not clean out the
 * old data or set b_node->b_end.
 * @b_node: the maple_big_node
 * @shift: the shift count
 */
static inline void mab_shift_right(struct maple_big_node *b_node,
				 unsigned char shift)
{
	unsigned long size = b_node->b_end * sizeof(unsigned long);

	memmove(b_node->pivot + shift, b_node->pivot, size);
	memmove(b_node->slot + shift, b_node->slot, size);
	if (b_node->type == maple_arange_64)
		memmove(b_node->gap + shift, b_node->gap, size);
}

/*
 * mab_middle_node() - Check if a middle node is needed (unlikely)
 * @b_node: the maple_big_node that contains the data.
 * @size: the amount of data in the b_node
 * @split: the potential split location
 * @slot_count: the size that can be stored in a single node being considered.
 *
 * Return: true if a middle node is required.
 */
static inline bool mab_middle_node(struct maple_big_node *b_node, int split,
				   unsigned char slot_count)
{
	unsigned char size = b_node->b_end;

	if (size >= 2 * slot_count)
		return true;

	if (!b_node->slot[split] && (size >= 2 * slot_count - 1))
		return true;

	return false;
}

/*
 * mab_no_null_split() - ensure the split doesn't fall on a NULL
 * @b_node: the maple_big_node with the data
 * @split: the suggested split location
 * @slot_count: the number of slots in the node being considered.
 *
 * Return: the split location.
 */
static inline int mab_no_null_split(struct maple_big_node *b_node,
				    unsigned char split, unsigned char slot_count)
{
	if (!b_node->slot[split]) {
		/*
		 * If the split is less than the max slot && the right side will
		 * still be sufficient, then increment the split on NULL.
		 */
		if ((split < slot_count - 1) &&
		    (b_node->b_end - split) > (mt_min_slots[b_node->type]))
			split++;
		else
			split--;
	}
	return split;
}

/*
 * mab_calc_split() - Calculate the split location and if there needs to be two
 * splits.
 * @bn: The maple_big_node with the data
 * @mid_split: The second split, if required.  0 otherwise.
 *
 * Return: The first split location.  The middle split is set in @mid_split.
 */
static inline int mab_calc_split(struct ma_state *mas,
	 struct maple_big_node *bn, unsigned char *mid_split, unsigned long min)
{
	unsigned char b_end = bn->b_end;
	int split = b_end / 2; /* Assume equal split. */
	unsigned char slot_min, slot_count = mt_slots[bn->type];

	/*
	 * To support gap tracking, all NULL entries are kept together and a node cannot
	 * end on a NULL entry, with the exception of the left-most leaf.  The
	 * limitation means that the split of a node must be checked for this condition
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
	 * Although extremely rare, it is possible to enter what is known as the 3-way
	 * split scenario.  The 3-way split comes about by means of a store of a range
	 * that overwrites the end and beginning of two full nodes.  The result is a set
	 * of entries that cannot be stored in 2 nodes.  Sometimes, these two nodes can
	 * also be located in different parent nodes which are also full.  This can
	 * carry upwards all the way to the root in the worst case.
	 */
	if (unlikely(mab_middle_node(bn, split, slot_count))) {
		split = b_end / 3;
		*mid_split = split * 2;
	} else {
		slot_min = mt_min_slots[bn->type];

		*mid_split = 0;
		/*
		 * Avoid having a range less than the slot count unless it
		 * causes one node to be deficient.
		 * NOTE: mt_min_slots is 1 based, b_end and split are zero.
		 */
		while (((bn->pivot[split] - min) < slot_count - 1) &&
		       (split < slot_count - 1) && (b_end - split > slot_min))
			split++;
	}

	/* Avoid ending a node on a NULL entry */
	split = mab_no_null_split(bn, split, slot_count);

	if (unlikely(*mid_split))
		*mid_split = mab_no_null_split(bn, *mid_split, slot_count);

	return split;
}

/*
 * mas_mab_cp() - Copy data from a maple state inclusively to a maple_big_node
 * and set @b_node->b_end to the next free slot.
 * @mas: The maple state
 * @mas_start: The starting slot to copy
 * @mas_end: The end slot to copy (inclusively)
 * @b_node: The maple_big_node to place the data
 * @mab_start: The starting location in maple_big_node to store the data.
 */
static inline void mas_mab_cp(struct ma_state *mas, unsigned char mas_start,
			unsigned char mas_end, struct maple_big_node *b_node,
			unsigned char mab_start)
{
	enum maple_type mt;
	struct maple_node *node;
	void __rcu **slots;
	unsigned long *pivots, *gaps;
	int i = mas_start, j = mab_start;
	unsigned char piv_end;

	node = mas_mn(mas);
	mt = mte_node_type(mas->node);
	pivots = ma_pivots(node, mt);
	if (!i) {
		b_node->pivot[j] = pivots[i++];
		if (unlikely(i > mas_end))
			goto complete;
		j++;
	}

	piv_end = min(mas_end, mt_pivots[mt]);
	for (; i < piv_end; i++, j++) {
		b_node->pivot[j] = pivots[i];
		if (unlikely(!b_node->pivot[j]))
			break;

		if (unlikely(mas->max == b_node->pivot[j]))
			goto complete;
	}

	if (likely(i <= mas_end))
		b_node->pivot[j] = mas_safe_pivot(mas, pivots, i, mt);

complete:
	b_node->b_end = ++j;
	j -= mab_start;
	slots = ma_slots(node, mt);
	memcpy(b_node->slot + mab_start, slots + mas_start, sizeof(void *) * j);
	if (!ma_is_leaf(mt) && mt_is_alloc(mas->tree)) {
		gaps = ma_gaps(node, mt);
		memcpy(b_node->gap + mab_start, gaps + mas_start,
		       sizeof(unsigned long) * j);
	}
}

/*
 * mas_leaf_set_meta() - Set the metadata of a leaf if possible.
 * @mas: The maple state
 * @node: The maple node
 * @pivots: pointer to the maple node pivots
 * @mt: The maple type
 * @end: The assumed end
 *
 * Note, end may be incremented within this function but not modified at the
 * source.  This is fine since the metadata is the last thing to be stored in a
 * node during a write.
 */
static inline void mas_leaf_set_meta(struct ma_state *mas,
		struct maple_node *node, unsigned long *pivots,
		enum maple_type mt, unsigned char end)
{
	/* There is no room for metadata already */
	if (mt_pivots[mt] <= end)
		return;

	if (pivots[end] && pivots[end] < mas->max)
		end++;

	if (end < mt_slots[mt] - 1)
		ma_set_meta(node, mt, 0, end);
}

/*
 * mab_mas_cp() - Copy data from maple_big_node to a maple encoded node.
 * @b_node: the maple_big_node that has the data
 * @mab_start: the start location in @b_node.
 * @mab_end: The end location in @b_node (inclusively)
 * @mas: The maple state with the maple encoded node.
 */
static inline void mab_mas_cp(struct maple_big_node *b_node,
			      unsigned char mab_start, unsigned char mab_end,
			      struct ma_state *mas, bool new_max)
{
	int i, j = 0;
	enum maple_type mt = mte_node_type(mas->node);
	struct maple_node *node = mte_to_node(mas->node);
	void __rcu **slots = ma_slots(node, mt);
	unsigned long *pivots = ma_pivots(node, mt);
	unsigned long *gaps = NULL;
	unsigned char end;

	if (mab_end - mab_start > mt_pivots[mt])
		mab_end--;

	if (!pivots[mt_pivots[mt] - 1])
		slots[mt_pivots[mt]] = NULL;

	i = mab_start;
	do {
		pivots[j++] = b_node->pivot[i++];
	} while (i <= mab_end && likely(b_node->pivot[i]));

	memcpy(slots, b_node->slot + mab_start,
	       sizeof(void *) * (i - mab_start));

	if (new_max)
		mas->max = b_node->pivot[i - 1];

	end = j - 1;
	if (likely(!ma_is_leaf(mt) && mt_is_alloc(mas->tree))) {
		unsigned long max_gap = 0;
		unsigned char offset = 15;

		gaps = ma_gaps(node, mt);
		do {
			gaps[--j] = b_node->gap[--i];
			if (gaps[j] > max_gap) {
				offset = j;
				max_gap = gaps[j];
			}
		} while (j);

		ma_set_meta(node, mt, offset, end);
	} else {
		mas_leaf_set_meta(mas, node, pivots, mt, end);
	}
}

/*
 * mas_descend_adopt() - Descend through a sub-tree and adopt children.
 * @mas: the maple state with the maple encoded node of the sub-tree.
 *
 * Descend through a sub-tree and adopt children who do not have the correct
 * parents set.  Follow the parents which have the correct parents as they are
 * the new entries which need to be followed to find other incorrectly set
 * parents.
 */
static inline void mas_descend_adopt(struct ma_state *mas)
{
	struct ma_state list[3], next[3];
	int i, n;

	/*
	 * At each level there may be up to 3 correct parent pointers which indicates
	 * the new nodes which need to be walked to find any new nodes at a lower level.
	 */

	for (i = 0; i < 3; i++) {
		list[i] = *mas;
		list[i].offset = 0;
		next[i].offset = 0;
	}
	next[0] = *mas;

	while (!mte_is_leaf(list[0].node)) {
		n = 0;
		for (i = 0; i < 3; i++) {
			if (mas_is_none(&list[i]))
				continue;

			if (i && list[i-1].node == list[i].node)
				continue;

			while ((n < 3) && (mas_new_child(&list[i], &next[n])))
				n++;

			mas_adopt_children(&list[i], list[i].node);
		}

		while (n < 3)
			next[n++].node = MAS_NONE;

		/* descend by setting the list to the children */
		for (i = 0; i < 3; i++)
			list[i] = next[i];
	}
}

/*
 * mas_bulk_rebalance() - Rebalance the end of a tree after a bulk insert.
 * @mas: The maple state
 * @end: The maple node end
 * @mt: The maple node type
 */
static inline void mas_bulk_rebalance(struct ma_state *mas, unsigned char end,
				      enum maple_type mt)
{
	if (!(mas->mas_flags & MA_STATE_BULK))
		return;

	if (mte_is_root(mas->node))
		return;

	if (end > mt_min_slots[mt]) {
		mas->mas_flags &= ~MA_STATE_REBALANCE;
		return;
	}
}

/*
 * mas_store_b_node() - Store an @entry into the b_node while also copying the
 * data from a maple encoded node.
 * @wr_mas: the maple write state
 * @b_node: the maple_big_node to fill with data
 * @offset_end: the offset to end copying
 *
 * Return: The actual end of the data stored in @b_node
 */
static noinline_for_kasan void mas_store_b_node(struct ma_wr_state *wr_mas,
		struct maple_big_node *b_node, unsigned char offset_end)
{
	unsigned char slot;
	unsigned char b_end;
	/* Possible underflow of piv will wrap back to 0 before use. */
	unsigned long piv;
	struct ma_state *mas = wr_mas->mas;

	b_node->type = wr_mas->type;
	b_end = 0;
	slot = mas->offset;
	if (slot) {
		/* Copy start data up to insert. */
		mas_mab_cp(mas, 0, slot - 1, b_node, 0);
		b_end = b_node->b_end;
		piv = b_node->pivot[b_end - 1];
	} else
		piv = mas->min - 1;

	if (piv + 1 < mas->index) {
		/* Handle range starting after old range */
		b_node->slot[b_end] = wr_mas->content;
		if (!wr_mas->content)
			b_node->gap[b_end] = mas->index - 1 - piv;
		b_node->pivot[b_end++] = mas->index - 1;
	}

	/* Store the new entry. */
	mas->offset = b_end;
	b_node->slot[b_end] = wr_mas->entry;
	b_node->pivot[b_end] = mas->last;

	/* Appended. */
	if (mas->last >= mas->max)
		goto b_end;

	/* Handle new range ending before old range ends */
	piv = mas_logical_pivot(mas, wr_mas->pivots, offset_end, wr_mas->type);
	if (piv > mas->last) {
		if (piv == ULONG_MAX)
			mas_bulk_rebalance(mas, b_node->b_end, wr_mas->type);

		if (offset_end != slot)
			wr_mas->content = mas_slot_locked(mas, wr_mas->slots,
							  offset_end);

		b_node->slot[++b_end] = wr_mas->content;
		if (!wr_mas->content)
			b_node->gap[b_end] = piv - mas->last + 1;
		b_node->pivot[b_end] = piv;
	}

	slot = offset_end + 1;
	if (slot > wr_mas->node_end)
		goto b_end;

	/* Copy end data to the end of the node. */
	mas_mab_cp(mas, slot, wr_mas->node_end + 1, b_node, ++b_end);
	b_node->b_end--;
	return;

b_end:
	b_node->b_end = b_end;
}

/*
 * mas_prev_sibling() - Find the previous node with the same parent.
 * @mas: the maple state
 *
 * Return: True if there is a previous sibling, false otherwise.
 */
static inline bool mas_prev_sibling(struct ma_state *mas)
{
	unsigned int p_slot = mte_parent_slot(mas->node);

	if (mte_is_root(mas->node))
		return false;

	if (!p_slot)
		return false;

	mas_ascend(mas);
	mas->offset = p_slot - 1;
	mas_descend(mas);
	return true;
}

/*
 * mas_next_sibling() - Find the next node with the same parent.
 * @mas: the maple state
 *
 * Return: true if there is a next sibling, false otherwise.
 */
static inline bool mas_next_sibling(struct ma_state *mas)
{
	MA_STATE(parent, mas->tree, mas->index, mas->last);

	if (mte_is_root(mas->node))
		return false;

	parent = *mas;
	mas_ascend(&parent);
	parent.offset = mte_parent_slot(mas->node) + 1;
	if (parent.offset > mas_data_end(&parent))
		return false;

	*mas = parent;
	mas_descend(mas);
	return true;
}

/*
 * mte_node_or_node() - Return the encoded node or MAS_NONE.
 * @enode: The encoded maple node.
 *
 * Shorthand to avoid setting %NULLs in the tree or maple_subtree_state.
 *
 * Return: @enode or MAS_NONE
 */
static inline struct maple_enode *mte_node_or_none(struct maple_enode *enode)
{
	if (enode)
		return enode;

	return ma_enode_ptr(MAS_NONE);
}

/*
 * mas_wr_node_walk() - Find the correct offset for the index in the @mas.
 * @wr_mas: The maple write state
 *
 * Uses mas_slot_locked() and does not need to worry about dead nodes.
 */
static inline void mas_wr_node_walk(struct ma_wr_state *wr_mas)
{
	struct ma_state *mas = wr_mas->mas;
	unsigned char count;
	unsigned char offset;
	unsigned long index, min, max;

	if (unlikely(ma_is_dense(wr_mas->type))) {
		wr_mas->r_max = wr_mas->r_min = mas->index;
		mas->offset = mas->index = mas->min;
		return;
	}

	wr_mas->node = mas_mn(wr_mas->mas);
	wr_mas->pivots = ma_pivots(wr_mas->node, wr_mas->type);
	count = wr_mas->node_end = ma_data_end(wr_mas->node, wr_mas->type,
					       wr_mas->pivots, mas->max);
	offset = mas->offset;
	min = mas_safe_min(mas, wr_mas->pivots, offset);
	if (unlikely(offset == count))
		goto max;

	max = wr_mas->pivots[offset];
	index = mas->index;
	if (unlikely(index <= max))
		goto done;

	if (unlikely(!max && offset))
		goto max;

	min = max + 1;
	while (++offset < count) {
		max = wr_mas->pivots[offset];
		if (index <= max)
			goto done;
		else if (unlikely(!max))
			break;

		min = max + 1;
	}

max:
	max = mas->max;
done:
	wr_mas->r_max = max;
	wr_mas->r_min = min;
	wr_mas->offset_end = mas->offset = offset;
}

/*
 * mas_topiary_range() - Add a range of slots to the topiary.
 * @mas: The maple state
 * @destroy: The topiary to add the slots (usually destroy)
 * @start: The starting slot inclusively
 * @end: The end slot inclusively
 */
static inline void mas_topiary_range(struct ma_state *mas,
	struct ma_topiary *destroy, unsigned char start, unsigned char end)
{
	void __rcu **slots;
	unsigned char offset;

	MT_BUG_ON(mas->tree, mte_is_leaf(mas->node));
	slots = ma_slots(mas_mn(mas), mte_node_type(mas->node));
	for (offset = start; offset <= end; offset++) {
		struct maple_enode *enode = mas_slot_locked(mas, slots, offset);

		if (mte_dead_node(enode))
			continue;

		mat_add(destroy, enode);
	}
}

/*
 * mast_topiary() - Add the portions of the tree to the removal list; either to
 * be freed or discarded (destroy walk).
 * @mast: The maple_subtree_state.
 */
static inline void mast_topiary(struct maple_subtree_state *mast)
{
	MA_WR_STATE(wr_mas, mast->orig_l, NULL);
	unsigned char r_start, r_end;
	unsigned char l_start, l_end;
	void __rcu **l_slots, **r_slots;

	wr_mas.type = mte_node_type(mast->orig_l->node);
	mast->orig_l->index = mast->orig_l->last;
	mas_wr_node_walk(&wr_mas);
	l_start = mast->orig_l->offset + 1;
	l_end = mas_data_end(mast->orig_l);
	r_start = 0;
	r_end = mast->orig_r->offset;

	if (r_end)
		r_end--;

	l_slots = ma_slots(mas_mn(mast->orig_l),
			   mte_node_type(mast->orig_l->node));

	r_slots = ma_slots(mas_mn(mast->orig_r),
			   mte_node_type(mast->orig_r->node));

	if ((l_start < l_end) &&
	    mte_dead_node(mas_slot_locked(mast->orig_l, l_slots, l_start))) {
		l_start++;
	}

	if (mte_dead_node(mas_slot_locked(mast->orig_r, r_slots, r_end))) {
		if (r_end)
			r_end--;
	}

	if ((l_start > r_end) && (mast->orig_l->node == mast->orig_r->node))
		return;

	/* At the node where left and right sides meet, add the parts between */
	if (mast->orig_l->node == mast->orig_r->node) {
		return mas_topiary_range(mast->orig_l, mast->destroy,
					     l_start, r_end);
	}

	/* mast->orig_r is different and consumed. */
	if (mte_is_leaf(mast->orig_r->node))
		return;

	if (mte_dead_node(mas_slot_locked(mast->orig_l, l_slots, l_end)))
		l_end--;


	if (l_start <= l_end)
		mas_topiary_range(mast->orig_l, mast->destroy, l_start, l_end);

	if (mte_dead_node(mas_slot_locked(mast->orig_r, r_slots, r_start)))
		r_start++;

	if (r_start <= r_end)
		mas_topiary_range(mast->orig_r, mast->destroy, 0, r_end);
}

/*
 * mast_rebalance_next() - Rebalance against the next node
 * @mast: The maple subtree state
 * @old_r: The encoded maple node to the right (next node).
 */
static inline void mast_rebalance_next(struct maple_subtree_state *mast)
{
	unsigned char b_end = mast->bn->b_end;

	mas_mab_cp(mast->orig_r, 0, mt_slot_count(mast->orig_r->node),
		   mast->bn, b_end);
	mast->orig_r->last = mast->orig_r->max;
}

/*
 * mast_rebalance_prev() - Rebalance against the previous node
 * @mast: The maple subtree state
 * @old_l: The encoded maple node to the left (previous node)
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
 * mast_spanning_rebalance() - Rebalance nodes with nearest neighbour favouring
 * the node to the right.  Checking the nodes to the right then the left at each
 * level upwards until root is reached.  Free and destroy as needed.
 * Data is copied into the @mast->bn.
 * @mast: The maple_subtree_state.
 */
static inline
bool mast_spanning_rebalance(struct maple_subtree_state *mast)
{
	struct ma_state r_tmp = *mast->orig_r;
	struct ma_state l_tmp = *mast->orig_l;
	struct maple_enode *ancestor = NULL;
	unsigned char start, end;
	unsigned char depth = 0;

	r_tmp = *mast->orig_r;
	l_tmp = *mast->orig_l;
	do {
		mas_ascend(mast->orig_r);
		mas_ascend(mast->orig_l);
		depth++;
		if (!ancestor &&
		    (mast->orig_r->node == mast->orig_l->node)) {
			ancestor = mast->orig_r->node;
			end = mast->orig_r->offset - 1;
			start = mast->orig_l->offset + 1;
		}

		if (mast->orig_r->offset < mas_data_end(mast->orig_r)) {
			if (!ancestor) {
				ancestor = mast->orig_r->node;
				start = 0;
			}

			mast->orig_r->offset++;
			do {
				mas_descend(mast->orig_r);
				mast->orig_r->offset = 0;
				depth--;
			} while (depth);

			mast_rebalance_next(mast);
			do {
				unsigned char l_off = 0;
				struct maple_enode *child = r_tmp.node;

				mas_ascend(&r_tmp);
				if (ancestor == r_tmp.node)
					l_off = start;

				if (r_tmp.offset)
					r_tmp.offset--;

				if (l_off < r_tmp.offset)
					mas_topiary_range(&r_tmp, mast->destroy,
							  l_off, r_tmp.offset);

				if (l_tmp.node != child)
					mat_add(mast->free, child);

			} while (r_tmp.node != ancestor);

			*mast->orig_l = l_tmp;
			return true;

		} else if (mast->orig_l->offset != 0) {
			if (!ancestor) {
				ancestor = mast->orig_l->node;
				end = mas_data_end(mast->orig_l);
			}

			mast->orig_l->offset--;
			do {
				mas_descend(mast->orig_l);
				mast->orig_l->offset =
					mas_data_end(mast->orig_l);
				depth--;
			} while (depth);

			mast_rebalance_prev(mast);
			do {
				unsigned char r_off;
				struct maple_enode *child = l_tmp.node;

				mas_ascend(&l_tmp);
				if (ancestor == l_tmp.node)
					r_off = end;
				else
					r_off = mas_data_end(&l_tmp);

				if (l_tmp.offset < r_off)
					l_tmp.offset++;

				if (l_tmp.offset < r_off)
					mas_topiary_range(&l_tmp, mast->destroy,
							  l_tmp.offset, r_off);

				if (r_tmp.node != child)
					mat_add(mast->free, child);

			} while (l_tmp.node != ancestor);

			*mast->orig_r = r_tmp;
			return true;
		}
	} while (!mte_is_root(mast->orig_r->node));

	*mast->orig_r = r_tmp;
	*mast->orig_l = l_tmp;
	return false;
}

/*
 * mast_ascend_free() - Add current original maple state nodes to the free list
 * and ascend.
 * @mast: the maple subtree state.
 *
 * Ascend the original left and right sides and add the previous nodes to the
 * free list.  Set the slots to point to the correct location in the new nodes.
 */
static inline void
mast_ascend_free(struct maple_subtree_state *mast)
{
	MA_WR_STATE(wr_mas, mast->orig_r,  NULL);
	struct maple_enode *left = mast->orig_l->node;
	struct maple_enode *right = mast->orig_r->node;

	mas_ascend(mast->orig_l);
	mas_ascend(mast->orig_r);
	mat_add(mast->free, left);

	if (left != right)
		mat_add(mast->free, right);

	mast->orig_r->offset = 0;
	mast->orig_r->index = mast->r->max;
	/* last should be larger than or equal to index */
	if (mast->orig_r->last < mast->orig_r->index)
		mast->orig_r->last = mast->orig_r->index;
	/*
	 * The node may not contain the value so set slot to ensure all
	 * of the nodes contents are freed or destroyed.
	 */
	wr_mas.type = mte_node_type(mast->orig_r->node);
	mas_wr_node_walk(&wr_mas);
	/* Set up the left side of things */
	mast->orig_l->offset = 0;
	mast->orig_l->index = mast->l->min;
	wr_mas.mas = mast->orig_l;
	wr_mas.type = mte_node_type(mast->orig_l->node);
	mas_wr_node_walk(&wr_mas);

	mast->bn->type = wr_mas.type;
}

/*
 * mas_new_ma_node() - Create and return a new maple node.  Helper function.
 * @mas: the maple state with the allocations.
 * @b_node: the maple_big_node with the type encoding.
 *
 * Use the node type from the maple_big_node to allocate a new node from the
 * ma_state.  This function exists mainly for code readability.
 *
 * Return: A new maple encoded node
 */
static inline struct maple_enode
*mas_new_ma_node(struct ma_state *mas, struct maple_big_node *b_node)
{
	return mt_mk_node(ma_mnode_ptr(mas_pop_node(mas)), b_node->type);
}

/*
 * mas_mab_to_node() - Set up right and middle nodes
 *
 * @mas: the maple state that contains the allocations.
 * @b_node: the node which contains the data.
 * @left: The pointer which will have the left node
 * @right: The pointer which may have the right node
 * @middle: the pointer which may have the middle node (rare)
 * @mid_split: the split location for the middle node
 *
 * Return: the split of left.
 */
static inline unsigned char mas_mab_to_node(struct ma_state *mas,
	struct maple_big_node *b_node, struct maple_enode **left,
	struct maple_enode **right, struct maple_enode **middle,
	unsigned char *mid_split, unsigned long min)
{
	unsigned char split = 0;
	unsigned char slot_count = mt_slots[b_node->type];

	*left = mas_new_ma_node(mas, b_node);
	*right = NULL;
	*middle = NULL;
	*mid_split = 0;

	if (b_node->b_end < slot_count) {
		split = b_node->b_end;
	} else {
		split = mab_calc_split(mas, b_node, mid_split, min);
		*right = mas_new_ma_node(mas, b_node);
	}

	if (*mid_split)
		*middle = mas_new_ma_node(mas, b_node);

	return split;

}

/*
 * mab_set_b_end() - Add entry to b_node at b_node->b_end and increment the end
 * pointer.
 * @b_node - the big node to add the entry
 * @mas - the maple state to get the pivot (mas->max)
 * @entry - the entry to add, if NULL nothing happens.
 */
static inline void mab_set_b_end(struct maple_big_node *b_node,
				 struct ma_state *mas,
				 void *entry)
{
	if (!entry)
		return;

	b_node->slot[b_node->b_end] = entry;
	if (mt_is_alloc(mas->tree))
		b_node->gap[b_node->b_end] = mas_max_gap(mas);
	b_node->pivot[b_node->b_end++] = mas->max;
}

/*
 * mas_set_split_parent() - combine_then_separate helper function.  Sets the parent
 * of @mas->node to either @left or @right, depending on @slot and @split
 *
 * @mas - the maple state with the node that needs a parent
 * @left - possible parent 1
 * @right - possible parent 2
 * @slot - the slot the mas->node was placed
 * @split - the split location between @left and @right
 */
static inline void mas_set_split_parent(struct ma_state *mas,
					struct maple_enode *left,
					struct maple_enode *right,
					unsigned char *slot, unsigned char split)
{
	if (mas_is_none(mas))
		return;

	if ((*slot) <= split)
		mte_set_parent(mas->node, left, *slot);
	else if (right)
		mte_set_parent(mas->node, right, (*slot) - split - 1);

	(*slot)++;
}

/*
 * mte_mid_split_check() - Check if the next node passes the mid-split
 * @**l: Pointer to left encoded maple node.
 * @**m: Pointer to middle encoded maple node.
 * @**r: Pointer to right encoded maple node.
 * @slot: The offset
 * @*split: The split location.
 * @mid_split: The middle split.
 */
static inline void mte_mid_split_check(struct maple_enode **l,
				       struct maple_enode **r,
				       struct maple_enode *right,
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
 * mast_set_split_parents() - Helper function to set three nodes parents.  Slot
 * is taken from @mast->l.
 * @mast - the maple subtree state
 * @left - the left node
 * @right - the right node
 * @split - the split location.
 */
static inline void mast_set_split_parents(struct maple_subtree_state *mast,
					  struct maple_enode *left,
					  struct maple_enode *middle,
					  struct maple_enode *right,
					  unsigned char split,
					  unsigned char mid_split)
{
	unsigned char slot;
	struct maple_enode *l = left;
	struct maple_enode *r = right;

	if (mas_is_none(mast->l))
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
 * mas_wmb_replace() - Write memory barrier and replace
 * @mas: The maple state
 * @free: the maple topiary list of nodes to free
 * @destroy: The maple topiary list of nodes to destroy (walk and free)
 *
 * Updates gap as necessary.
 */
static inline void mas_wmb_replace(struct ma_state *mas,
				   struct ma_topiary *free,
				   struct ma_topiary *destroy)
{
	/* All nodes must see old data as dead prior to replacing that data */
	smp_wmb(); /* Needed for RCU */

	/* Insert the new data in the tree */
	mas_replace(mas, true);

	if (!mte_is_leaf(mas->node))
		mas_descend_adopt(mas);

	mas_mat_free(mas, free);

	if (destroy)
		mas_mat_destroy(mas, destroy);

	if (mte_is_leaf(mas->node))
		return;

	mas_update_gap(mas);
}

/*
 * mast_new_root() - Set a new tree root during subtree creation
 * @mast: The maple subtree state
 * @mas: The maple state
 */
static inline void mast_new_root(struct maple_subtree_state *mast,
				 struct ma_state *mas)
{
	mas_mn(mast->l)->parent =
		ma_parent_ptr(((unsigned long)mas->tree | MA_ROOT_PARENT));
	if (!mte_dead_node(mast->orig_l->node) &&
	    !mte_is_root(mast->orig_l->node)) {
		do {
			mast_ascend_free(mast);
			mast_topiary(mast);
		} while (!mte_is_root(mast->orig_l->node));
	}
	if ((mast->orig_l->node != mas->node) &&
		   (mast->l->depth > mas_mt_height(mas))) {
		mat_add(mast->free, mas->node);
	}
}

/*
 * mast_cp_to_nodes() - Copy data out to nodes.
 * @mast: The maple subtree state
 * @left: The left encoded maple node
 * @middle: The middle encoded maple node
 * @right: The right encoded maple node
 * @split: The location to split between left and (middle ? middle : right)
 * @mid_split: The location to split between middle and right.
 */
static inline void mast_cp_to_nodes(struct maple_subtree_state *mast,
	struct maple_enode *left, struct maple_enode *middle,
	struct maple_enode *right, unsigned char split, unsigned char mid_split)
{
	bool new_lmax = true;

	mast->l->node = mte_node_or_none(left);
	mast->m->node = mte_node_or_none(middle);
	mast->r->node = mte_node_or_none(right);

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
 * combined data set in the maple subtree state big node.
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
 * combined data set in the maple subtree state big node.
 * @mast: The maple subtree state
 */
static inline void mast_combine_cp_right(struct maple_subtree_state *mast)
{
	if (mast->bn->pivot[mast->bn->b_end - 1] >= mast->orig_r->max)
		return;

	mas_mab_cp(mast->orig_r, mast->orig_r->offset + 1,
		   mt_slot_count(mast->orig_r->node), mast->bn,
		   mast->bn->b_end);
	mast->orig_r->last = mast->orig_r->max;
}

/*
 * mast_sufficient: Check if the maple subtree state has enough data in the big
 * node to create at least one sufficient node
 * @mast: the maple subtree state
 */
static inline bool mast_sufficient(struct maple_subtree_state *mast)
{
	if (mast->bn->b_end > mt_min_slot_count(mast->orig_l->node))
		return true;

	return false;
}

/*
 * mast_overflow: Check if there is too much data in the subtree state for a
 * single node.
 * @mast: The maple subtree state
 */
static inline bool mast_overflow(struct maple_subtree_state *mast)
{
	if (mast->bn->b_end >= mt_slot_count(mast->orig_l->node))
		return true;

	return false;
}

static inline void *mtree_range_walk(struct ma_state *mas)
{
	unsigned long *pivots;
	unsigned char offset;
	struct maple_node *node;
	struct maple_enode *next, *last;
	enum maple_type type;
	void __rcu **slots;
	unsigned char end;
	unsigned long max, min;
	unsigned long prev_max, prev_min;

	next = mas->node;
	min = mas->min;
	max = mas->max;
	do {
		offset = 0;
		last = next;
		node = mte_to_node(next);
		type = mte_node_type(next);
		pivots = ma_pivots(node, type);
		end = ma_data_end(node, type, pivots, max);
		if (unlikely(ma_dead_node(node)))
			goto dead_node;

		if (pivots[offset] >= mas->index) {
			prev_max = max;
			prev_min = min;
			max = pivots[offset];
			goto next;
		}

		do {
			offset++;
		} while ((offset < end) && (pivots[offset] < mas->index));

		prev_min = min;
		min = pivots[offset - 1] + 1;
		prev_max = max;
		if (likely(offset < end && pivots[offset]))
			max = pivots[offset];

next:
		slots = ma_slots(node, type);
		next = mt_slot(mas->tree, slots, offset);
		if (unlikely(ma_dead_node(node)))
			goto dead_node;
	} while (!ma_is_leaf(type));

	mas->offset = offset;
	mas->index = min;
	mas->last = max;
	mas->min = prev_min;
	mas->max = prev_max;
	mas->node = last;
	return (void *)next;

dead_node:
	mas_reset(mas);
	return NULL;
}

/*
 * mas_spanning_rebalance() - Rebalance across two nodes which may not be peers.
 * @mas: The starting maple state
 * @mast: The maple_subtree_state, keeps track of 4 maple states.
 * @count: The estimated count of iterations needed.
 *
 * Follow the tree upwards from @l_mas and @r_mas for @count, or until the root
 * is hit.  First @b_node is split into two entries which are inserted into the
 * next iteration of the loop.  @b_node is returned populated with the final
 * iteration. @mas is used to obtain allocations.  orig_l_mas keeps track of the
 * nodes that will remain active by using orig_l_mas->index and orig_l_mas->last
 * to account of what has been copied into the new sub-tree.  The update of
 * orig_l_mas->last is used in mas_consume to find the slots that will need to
 * be either freed or destroyed.  orig_l_mas->depth keeps track of the height of
 * the new sub-tree in case the sub-tree becomes the full tree.
 *
 * Return: the number of elements in b_node during the last loop.
 */
static int mas_spanning_rebalance(struct ma_state *mas,
		struct maple_subtree_state *mast, unsigned char count)
{
	unsigned char split, mid_split;
	unsigned char slot = 0;
	struct maple_enode *left = NULL, *middle = NULL, *right = NULL;

	MA_STATE(l_mas, mas->tree, mas->index, mas->index);
	MA_STATE(r_mas, mas->tree, mas->index, mas->last);
	MA_STATE(m_mas, mas->tree, mas->index, mas->index);
	MA_TOPIARY(free, mas->tree);
	MA_TOPIARY(destroy, mas->tree);

	/*
	 * The tree needs to be rebalanced and leaves need to be kept at the same level.
	 * Rebalancing is done by use of the ``struct maple_topiary``.
	 */
	mast->l = &l_mas;
	mast->m = &m_mas;
	mast->r = &r_mas;
	mast->free = &free;
	mast->destroy = &destroy;
	l_mas.node = r_mas.node = m_mas.node = MAS_NONE;

	/* Check if this is not root and has sufficient data.  */
	if (((mast->orig_l->min != 0) || (mast->orig_r->max != ULONG_MAX)) &&
	    unlikely(mast->bn->b_end <= mt_min_slots[mast->bn->type]))
		mast_spanning_rebalance(mast);

	mast->orig_l->depth = 0;

	/*
	 * Each level of the tree is examined and balanced, pushing data to the left or
	 * right, or rebalancing against left or right nodes is employed to avoid
	 * rippling up the tree to limit the amount of churn.  Once a new sub-section of
	 * the tree is created, there may be a mix of new and old nodes.  The old nodes
	 * will have the incorrect parent pointers and currently be in two trees: the
	 * original tree and the partially new tree.  To remedy the parent pointers in
	 * the old tree, the new data is swapped into the active tree and a walk down
	 * the tree is performed and the parent pointers are updated.
	 * See mas_descend_adopt() for more information..
	 */
	while (count--) {
		mast->bn->b_end--;
		mast->bn->type = mte_node_type(mast->orig_l->node);
		split = mas_mab_to_node(mas, mast->bn, &left, &right, &middle,
					&mid_split, mast->orig_l->min);
		mast_set_split_parents(mast, left, middle, right, split,
				       mid_split);
		mast_cp_to_nodes(mast, left, middle, right, split, mid_split);

		/*
		 * Copy data from next level in the tree to mast->bn from next
		 * iteration
		 */
		memset(mast->bn, 0, sizeof(struct maple_big_node));
		mast->bn->type = mte_node_type(left);
		mast->orig_l->depth++;

		/* Root already stored in l->node. */
		if (mas_is_root_limits(mast->l))
			goto new_root;

		mast_ascend_free(mast);
		mast_combine_cp_left(mast);
		l_mas.offset = mast->bn->b_end;
		mab_set_b_end(mast->bn, &l_mas, left);
		mab_set_b_end(mast->bn, &m_mas, middle);
		mab_set_b_end(mast->bn, &r_mas, right);

		/* Copy anything necessary out of the right node. */
		mast_combine_cp_right(mast);
		mast_topiary(mast);
		mast->orig_l->last = mast->orig_l->max;

		if (mast_sufficient(mast))
			continue;

		if (mast_overflow(mast))
			continue;

		/* May be a new root stored in mast->bn */
		if (mas_is_root_limits(mast->orig_l))
			break;

		mast_spanning_rebalance(mast);

		/* rebalancing from other nodes may require another loop. */
		if (!count)
			count++;
	}

	l_mas.node = mt_mk_node(ma_mnode_ptr(mas_pop_node(mas)),
				mte_node_type(mast->orig_l->node));
	mast->orig_l->depth++;
	mab_mas_cp(mast->bn, 0, mt_slots[mast->bn->type] - 1, &l_mas, true);
	mte_set_parent(left, l_mas.node, slot);
	if (middle)
		mte_set_parent(middle, l_mas.node, ++slot);

	if (right)
		mte_set_parent(right, l_mas.node, ++slot);

	if (mas_is_root_limits(mast->l)) {
new_root:
		mast_new_root(mast, mas);
	} else {
		mas_mn(&l_mas)->parent = mas_mn(mast->orig_l)->parent;
	}

	if (!mte_dead_node(mast->orig_l->node))
		mat_add(&free, mast->orig_l->node);

	mas->depth = mast->orig_l->depth;
	*mast->orig_l = l_mas;
	mte_set_node_dead(mas->node);

	/* Set up mas for insertion. */
	mast->orig_l->depth = mas->depth;
	mast->orig_l->alloc = mas->alloc;
	*mas = *mast->orig_l;
	mas_wmb_replace(mas, &free, &destroy);
	mtree_range_walk(mas);
	return mast->bn->b_end;
}

/*
 * mas_rebalance() - Rebalance a given node.
 * @mas: The maple state
 * @b_node: The big maple node.
 *
 * Rebalance two nodes into a single node or two new nodes that are sufficient.
 * Continue upwards until tree is sufficient.
 *
 * Return: the number of elements in b_node during the last loop.
 */
static inline int mas_rebalance(struct ma_state *mas,
				struct maple_big_node *b_node)
{
	char empty_count = mas_mt_height(mas);
	struct maple_subtree_state mast;
	unsigned char shift, b_end = ++b_node->b_end;

	MA_STATE(l_mas, mas->tree, mas->index, mas->last);
	MA_STATE(r_mas, mas->tree, mas->index, mas->last);

	trace_ma_op(__func__, mas);

	/*
	 * Rebalancing occurs if a node is insufficient.  Data is rebalanced
	 * against the node to the right if it exists, otherwise the node to the
	 * left of this node is rebalanced against this node.  If rebalancing
	 * causes just one node to be produced instead of two, then the parent
	 * is also examined and rebalanced if it is insufficient.  Every level
	 * tries to combine the data in the same way.  If one node contains the
	 * entire range of the tree, then that node is used as a new root node.
	 */
	mas_node_count(mas, 1 + empty_count * 3);
	if (mas_is_err(mas))
		return 0;

	mast.orig_l = &l_mas;
	mast.orig_r = &r_mas;
	mast.bn = b_node;
	mast.bn->type = mte_node_type(mas->node);

	l_mas = r_mas = *mas;

	if (mas_next_sibling(&r_mas)) {
		mas_mab_cp(&r_mas, 0, mt_slot_count(r_mas.node), b_node, b_end);
		r_mas.last = r_mas.index = r_mas.max;
	} else {
		mas_prev_sibling(&l_mas);
		shift = mas_data_end(&l_mas) + 1;
		mab_shift_right(b_node, shift);
		mas->offset += shift;
		mas_mab_cp(&l_mas, 0, shift - 1, b_node, 0);
		b_node->b_end = shift + b_end;
		l_mas.index = l_mas.last = l_mas.min;
	}

	return mas_spanning_rebalance(mas, &mast, empty_count);
}

/*
 * mas_destroy_rebalance() - Rebalance left-most node while destroying the maple
 * state.
 * @mas: The maple state
 * @end: The end of the left-most node.
 *
 * During a mass-insert event (such as forking), it may be necessary to
 * rebalance the left-most node when it is not sufficient.
 */
static inline void mas_destroy_rebalance(struct ma_state *mas, unsigned char end)
{
	enum maple_type mt = mte_node_type(mas->node);
	struct maple_node reuse, *newnode, *parent, *new_left, *left, *node;
	struct maple_enode *eparent;
	unsigned char offset, tmp, split = mt_slots[mt] / 2;
	void __rcu **l_slots, **slots;
	unsigned long *l_pivs, *pivs, gap;
	bool in_rcu = mt_in_rcu(mas->tree);

	MA_STATE(l_mas, mas->tree, mas->index, mas->last);

	l_mas = *mas;
	mas_prev_sibling(&l_mas);

	/* set up node. */
	if (in_rcu) {
		/* Allocate for both left and right as well as parent. */
		mas_node_count(mas, 3);
		if (mas_is_err(mas))
			return;

		newnode = mas_pop_node(mas);
	} else {
		newnode = &reuse;
	}

	node = mas_mn(mas);
	newnode->parent = node->parent;
	slots = ma_slots(newnode, mt);
	pivs = ma_pivots(newnode, mt);
	left = mas_mn(&l_mas);
	l_slots = ma_slots(left, mt);
	l_pivs = ma_pivots(left, mt);
	if (!l_slots[split])
		split++;
	tmp = mas_data_end(&l_mas) - split;

	memcpy(slots, l_slots + split + 1, sizeof(void *) * tmp);
	memcpy(pivs, l_pivs + split + 1, sizeof(unsigned long) * tmp);
	pivs[tmp] = l_mas.max;
	memcpy(slots + tmp, ma_slots(node, mt), sizeof(void *) * end);
	memcpy(pivs + tmp, ma_pivots(node, mt), sizeof(unsigned long) * end);

	l_mas.max = l_pivs[split];
	mas->min = l_mas.max + 1;
	eparent = mt_mk_node(mte_parent(l_mas.node),
			     mas_parent_enum(&l_mas, l_mas.node));
	tmp += end;
	if (!in_rcu) {
		unsigned char max_p = mt_pivots[mt];
		unsigned char max_s = mt_slots[mt];

		if (tmp < max_p)
			memset(pivs + tmp, 0,
			       sizeof(unsigned long *) * (max_p - tmp));

		if (tmp < mt_slots[mt])
			memset(slots + tmp, 0, sizeof(void *) * (max_s - tmp));

		memcpy(node, newnode, sizeof(struct maple_node));
		ma_set_meta(node, mt, 0, tmp - 1);
		mte_set_pivot(eparent, mte_parent_slot(l_mas.node),
			      l_pivs[split]);

		/* Remove data from l_pivs. */
		tmp = split + 1;
		memset(l_pivs + tmp, 0, sizeof(unsigned long) * (max_p - tmp));
		memset(l_slots + tmp, 0, sizeof(void *) * (max_s - tmp));
		ma_set_meta(left, mt, 0, split);

		goto done;
	}

	/* RCU requires replacing both l_mas, mas, and parent. */
	mas->node = mt_mk_node(newnode, mt);
	ma_set_meta(newnode, mt, 0, tmp);

	new_left = mas_pop_node(mas);
	new_left->parent = left->parent;
	mt = mte_node_type(l_mas.node);
	slots = ma_slots(new_left, mt);
	pivs = ma_pivots(new_left, mt);
	memcpy(slots, l_slots, sizeof(void *) * split);
	memcpy(pivs, l_pivs, sizeof(unsigned long) * split);
	ma_set_meta(new_left, mt, 0, split);
	l_mas.node = mt_mk_node(new_left, mt);

	/* replace parent. */
	offset = mte_parent_slot(mas->node);
	mt = mas_parent_enum(&l_mas, l_mas.node);
	parent = mas_pop_node(mas);
	slots = ma_slots(parent, mt);
	pivs = ma_pivots(parent, mt);
	memcpy(parent, mte_to_node(eparent), sizeof(struct maple_node));
	rcu_assign_pointer(slots[offset], mas->node);
	rcu_assign_pointer(slots[offset - 1], l_mas.node);
	pivs[offset - 1] = l_mas.max;
	eparent = mt_mk_node(parent, mt);
done:
	gap = mas_leaf_max_gap(mas);
	mte_set_gap(eparent, mte_parent_slot(mas->node), gap);
	gap = mas_leaf_max_gap(&l_mas);
	mte_set_gap(eparent, mte_parent_slot(l_mas.node), gap);
	mas_ascend(mas);

	if (in_rcu)
		mas_replace(mas, false);

	mas_update_gap(mas);
}

/*
 * mas_split_final_node() - Split the final node in a subtree operation.
 * @mast: the maple subtree state
 * @mas: The maple state
 * @height: The height of the tree in case it's a new root.
 */
static inline bool mas_split_final_node(struct maple_subtree_state *mast,
					struct ma_state *mas, int height)
{
	struct maple_enode *ancestor;

	if (mte_is_root(mas->node)) {
		if (mt_is_alloc(mas->tree))
			mast->bn->type = maple_arange_64;
		else
			mast->bn->type = maple_range_64;
		mas->depth = height;
	}
	/*
	 * Only a single node is used here, could be root.
	 * The Big_node data should just fit in a single node.
	 */
	ancestor = mas_new_ma_node(mas, mast->bn);
	mte_set_parent(mast->l->node, ancestor, mast->l->offset);
	mte_set_parent(mast->r->node, ancestor, mast->r->offset);
	mte_to_node(ancestor)->parent = mas_mn(mas)->parent;

	mast->l->node = ancestor;
	mab_mas_cp(mast->bn, 0, mt_slots[mast->bn->type] - 1, mast->l, true);
	mas->offset = mast->bn->b_end - 1;
	return true;
}

/*
 * mast_fill_bnode() - Copy data into the big node in the subtree state
 * @mast: The maple subtree state
 * @mas: the maple state
 * @skip: The number of entries to skip for new nodes insertion.
 */
static inline void mast_fill_bnode(struct maple_subtree_state *mast,
					 struct ma_state *mas,
					 unsigned char skip)
{
	bool cp = true;
	struct maple_enode *old = mas->node;
	unsigned char split;

	memset(mast->bn->gap, 0, sizeof(unsigned long) * ARRAY_SIZE(mast->bn->gap));
	memset(mast->bn->slot, 0, sizeof(unsigned long) * ARRAY_SIZE(mast->bn->slot));
	memset(mast->bn->pivot, 0, sizeof(unsigned long) * ARRAY_SIZE(mast->bn->pivot));
	mast->bn->b_end = 0;

	if (mte_is_root(mas->node)) {
		cp = false;
	} else {
		mas_ascend(mas);
		mat_add(mast->free, old);
		mas->offset = mte_parent_slot(mas->node);
	}

	if (cp && mast->l->offset)
		mas_mab_cp(mas, 0, mast->l->offset - 1, mast->bn, 0);

	split = mast->bn->b_end;
	mab_set_b_end(mast->bn, mast->l, mast->l->node);
	mast->r->offset = mast->bn->b_end;
	mab_set_b_end(mast->bn, mast->r, mast->r->node);
	if (mast->bn->pivot[mast->bn->b_end - 1] == mas->max)
		cp = false;

	if (cp)
		mas_mab_cp(mas, split + skip, mt_slot_count(mas->node) - 1,
			   mast->bn, mast->bn->b_end);

	mast->bn->b_end--;
	mast->bn->type = mte_node_type(mas->node);
}

/*
 * mast_split_data() - Split the data in the subtree state big node into regular
 * nodes.
 * @mast: The maple subtree state
 * @mas: The maple state
 * @split: The location to split the big node
 */
static inline void mast_split_data(struct maple_subtree_state *mast,
	   struct ma_state *mas, unsigned char split)
{
	unsigned char p_slot;

	mab_mas_cp(mast->bn, 0, split, mast->l, true);
	mte_set_pivot(mast->r->node, 0, mast->r->max);
	mab_mas_cp(mast->bn, split + 1, mast->bn->b_end, mast->r, false);
	mast->l->offset = mte_parent_slot(mas->node);
	mast->l->max = mast->bn->pivot[split];
	mast->r->min = mast->l->max + 1;
	if (mte_is_leaf(mas->node))
		return;

	p_slot = mast->orig_l->offset;
	mas_set_split_parent(mast->orig_l, mast->l->node, mast->r->node,
			     &p_slot, split);
	mas_set_split_parent(mast->orig_r, mast->l->node, mast->r->node,
			     &p_slot, split);
}

/*
 * mas_push_data() - Instead of splitting a node, it is beneficial to push the
 * data to the right or left node if there is room.
 * @mas: The maple state
 * @height: The current height of the maple state
 * @mast: The maple subtree state
 * @left: Push left or not.
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
	space = 2 * mt_slot_count(mas->node) - 2;
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
		/*  Switch mas to prev node  */
		mat_add(mast->free, mas->node);
		*mas = tmp_mas;
		/* Start using mast->l for the left side. */
		tmp_mas.node = mast->l->node;
		*mast->l = tmp_mas;
	} else {
		mat_add(mast->free, tmp_mas.node);
		tmp_mas.node = mast->r->node;
		*mast->r = tmp_mas;
		split = slot_total - split;
	}
	split = mab_no_null_split(mast->bn, split, mt_slots[mast->bn->type]);
	/* Update parent slot for split calculation. */
	if (left)
		mast->orig_l->offset += end + 1;

	mast_split_data(mast, mas, split);
	mast_fill_bnode(mast, mas, 2);
	mas_split_final_node(mast, mas, height + 1);
	return true;
}

/*
 * mas_split() - Split data that is too big for one node into two.
 * @mas: The maple state
 * @b_node: The maple big node
 * Return: 1 on success, 0 on failure.
 */
static int mas_split(struct ma_state *mas, struct maple_big_node *b_node)
{
	struct maple_subtree_state mast;
	int height = 0;
	unsigned char mid_split, split = 0;

	/*
	 * Splitting is handled differently from any other B-tree; the Maple
	 * Tree splits upwards.  Splitting up means that the split operation
	 * occurs when the walk of the tree hits the leaves and not on the way
	 * down.  The reason for splitting up is that it is impossible to know
	 * how much space will be needed until the leaf is (or leaves are)
	 * reached.  Since overwriting data is allowed and a range could
	 * overwrite more than one range or result in changing one entry into 3
	 * entries, it is impossible to know if a split is required until the
	 * data is examined.
	 *
	 * Splitting is a balancing act between keeping allocations to a minimum
	 * and avoiding a 'jitter' event where a tree is expanded to make room
	 * for an entry followed by a contraction when the entry is removed.  To
	 * accomplish the balance, there are empty slots remaining in both left
	 * and right nodes after a split.
	 */
	MA_STATE(l_mas, mas->tree, mas->index, mas->last);
	MA_STATE(r_mas, mas->tree, mas->index, mas->last);
	MA_STATE(prev_l_mas, mas->tree, mas->index, mas->last);
	MA_STATE(prev_r_mas, mas->tree, mas->index, mas->last);
	MA_TOPIARY(mat, mas->tree);

	trace_ma_op(__func__, mas);
	mas->depth = mas_mt_height(mas);
	/* Allocation failures will happen early. */
	mas_node_count(mas, 1 + mas->depth * 2);
	if (mas_is_err(mas))
		return 0;

	mast.l = &l_mas;
	mast.r = &r_mas;
	mast.orig_l = &prev_l_mas;
	mast.orig_r = &prev_r_mas;
	mast.free = &mat;
	mast.bn = b_node;

	while (height++ <= mas->depth) {
		if (mt_slots[b_node->type] > b_node->b_end) {
			mas_split_final_node(&mast, mas, height);
			break;
		}

		l_mas = r_mas = *mas;
		l_mas.node = mas_new_ma_node(mas, b_node);
		r_mas.node = mas_new_ma_node(mas, b_node);
		/*
		 * Another way that 'jitter' is avoided is to terminate a split up early if the
		 * left or right node has space to spare.  This is referred to as "pushing left"
		 * or "pushing right" and is similar to the B* tree, except the nodes left or
		 * right can rarely be reused due to RCU, but the ripple upwards is halted which
		 * is a significant savings.
		 */
		/* Try to push left. */
		if (mas_push_data(mas, height, &mast, true))
			break;

		/* Try to push right. */
		if (mas_push_data(mas, height, &mast, false))
			break;

		split = mab_calc_split(mas, b_node, &mid_split, prev_l_mas.min);
		mast_split_data(&mast, mas, split);
		/*
		 * Usually correct, mab_mas_cp in the above call overwrites
		 * r->max.
		 */
		mast.r->max = mas->max;
		mast_fill_bnode(&mast, mas, 1);
		prev_l_mas = *mast.l;
		prev_r_mas = *mast.r;
	}

	/* Set the original node as dead */
	mat_add(mast.free, mas->node);
	mas->node = l_mas.node;
	mas_wmb_replace(mas, mast.free, NULL);
	mtree_range_walk(mas);
	return 1;
}

/*
 * mas_reuse_node() - Reuse the node to store the data.
 * @wr_mas: The maple write state
 * @bn: The maple big node
 * @end: The end of the data.
 *
 * Will always return false in RCU mode.
 *
 * Return: True if node was reused, false otherwise.
 */
static inline bool mas_reuse_node(struct ma_wr_state *wr_mas,
			  struct maple_big_node *bn, unsigned char end)
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
 * mas_commit_b_node() - Commit the big node into the tree.
 * @wr_mas: The maple write state
 * @b_node: The maple big node
 * @end: The end of the data.
 */
static noinline_for_kasan int mas_commit_b_node(struct ma_wr_state *wr_mas,
			    struct maple_big_node *b_node, unsigned char end)
{
	struct maple_node *node;
	unsigned char b_end = b_node->b_end;
	enum maple_type b_type = b_node->type;

	if ((b_end < mt_min_slots[b_type]) &&
	    (!mte_is_root(wr_mas->mas->node)) &&
	    (mas_mt_height(wr_mas->mas) > 1))
		return mas_rebalance(wr_mas->mas, b_node);

	if (b_end >= mt_slots[b_type])
		return mas_split(wr_mas->mas, b_node);

	if (mas_reuse_node(wr_mas, b_node, end))
		goto reuse_node;

	mas_node_count(wr_mas->mas, 1);
	if (mas_is_err(wr_mas->mas))
		return 0;

	node = mas_pop_node(wr_mas->mas);
	node->parent = mas_mn(wr_mas->mas)->parent;
	wr_mas->mas->node = mt_mk_node(node, b_type);
	mab_mas_cp(b_node, 0, b_end, wr_mas->mas, false);
	mas_replace(wr_mas->mas, false);
reuse_node:
	mas_update_gap(wr_mas->mas);
	return 1;
}

/*
 * mas_root_expand() - Expand a root to a node
 * @mas: The maple state
 * @entry: The entry to store into the tree
 */
static inline int mas_root_expand(struct ma_state *mas, void *entry)
{
	void *contents = mas_root_locked(mas);
	enum maple_type type = maple_leaf_64;
	struct maple_node *node;
	void __rcu **slots;
	unsigned long *pivots;
	int slot = 0;

	mas_node_count(mas, 1);
	if (unlikely(mas_is_err(mas)))
		return 0;

	node = mas_pop_node(mas);
	pivots = ma_pivots(node, type);
	slots = ma_slots(node, type);
	node->parent = ma_parent_ptr(
		      ((unsigned long)mas->tree | MA_ROOT_PARENT));
	mas->node = mt_mk_node(node, type);

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
		slot++;
	mas->depth = 1;
	mas_set_height(mas);

	/* swap the new root into the tree */
	rcu_assign_pointer(mas->tree->ma_root, mte_mk_root(mas->node));
	ma_set_meta(node, maple_leaf_64, 0, slot);
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
		mas->node = MAS_START;
	}
}

/*
 * mas_is_span_wr() - Check if the write needs to be treated as a write that
 * spans the node.
 * @mas: The maple state
 * @piv: The pivot value being written
 * @type: The maple node type
 * @entry: The data to write
 *
 * Spanning writes are writes that start in one node and end in another OR if
 * the write of a %NULL will cause the node to end with a %NULL.
 *
 * Return: True if this is a spanning write, false otherwise.
 */
static bool mas_is_span_wr(struct ma_wr_state *wr_mas)
{
	unsigned long max;
	unsigned long last = wr_mas->mas->last;
	unsigned long piv = wr_mas->r_max;
	enum maple_type type = wr_mas->type;
	void *entry = wr_mas->entry;

	/* Contained in this pivot */
	if (piv > last)
		return false;

	max = wr_mas->mas->max;
	if (unlikely(ma_is_leaf(type))) {
		/* Fits in the node, but may span slots. */
		if (last < max)
			return false;

		/* Writes to the end of the node but not null. */
		if ((last == max) && entry)
			return false;

		/*
		 * Writing ULONG_MAX is not a spanning write regardless of the
		 * value being written as long as the range fits in the node.
		 */
		if ((last == ULONG_MAX) && (last == max))
			return false;
	} else if (piv == last) {
		if (entry)
			return false;

		/* Detect spanning store wr walk */
		if (last == ULONG_MAX)
			return false;
	}

	trace_ma_write(__func__, wr_mas->mas, piv, entry);

	return true;
}

static inline void mas_wr_walk_descend(struct ma_wr_state *wr_mas)
{
	wr_mas->type = mte_node_type(wr_mas->mas->node);
	mas_wr_node_walk(wr_mas);
	wr_mas->slots = ma_slots(wr_mas->node, wr_mas->type);
}

static inline void mas_wr_walk_traverse(struct ma_wr_state *wr_mas)
{
	wr_mas->mas->max = wr_mas->r_max;
	wr_mas->mas->min = wr_mas->r_min;
	wr_mas->mas->node = wr_mas->content;
	wr_mas->mas->offset = 0;
	wr_mas->mas->depth++;
}
/*
 * mas_wr_walk() - Walk the tree for a write.
 * @wr_mas: The maple write state
 *
 * Uses mas_slot_locked() and does not need to worry about dead nodes.
 *
 * Return: True if it's contained in a node, false on spanning write.
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
	if (mas_is_none(mas))
		return NULL;

	if (mas_is_ptr(mas))
		return entry;

	return mtree_range_walk(mas);
}

/*
 * mtree_lookup_walk() - Internal quick lookup that does not keep maple state up
 * to date.
 *
 * @mas: The maple state.
 *
 * Note: Leaves mas in undesirable state.
 * Return: The entry for @mas->index or %NULL on dead node.
 */
static inline void *mtree_lookup_walk(struct ma_state *mas)
{
	unsigned long *pivots;
	unsigned char offset;
	struct maple_node *node;
	struct maple_enode *next;
	enum maple_type type;
	void __rcu **slots;
	unsigned char end;
	unsigned long max;

	next = mas->node;
	max = ULONG_MAX;
	do {
		offset = 0;
		node = mte_to_node(next);
		type = mte_node_type(next);
		pivots = ma_pivots(node, type);
		end = ma_data_end(node, type, pivots, max);
		if (unlikely(ma_dead_node(node)))
			goto dead_node;

		if (pivots[offset] >= mas->index)
			goto next;

		do {
			offset++;
		} while ((offset < end) && (pivots[offset] < mas->index));

		if (likely(offset > end))
			max = pivots[offset];

next:
		slots = ma_slots(node, type);
		next = mt_slot(mas->tree, slots, offset);
		if (unlikely(ma_dead_node(node)))
			goto dead_node;
	} while (!ma_is_leaf(type));

	return (void *)next;

dead_node:
	mas_reset(mas);
	return NULL;
}

/*
 * mas_new_root() - Create a new root node that only contains the entry passed
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
	struct maple_enode *root = mas_root_locked(mas);
	enum maple_type type = maple_leaf_64;
	struct maple_node *node;
	void __rcu **slots;
	unsigned long *pivots;

	if (!entry && !mas->index && mas->last == ULONG_MAX) {
		mas->depth = 0;
		mas_set_height(mas);
		rcu_assign_pointer(mas->tree->ma_root, entry);
		mas->node = MAS_START;
		goto done;
	}

	mas_node_count(mas, 1);
	if (mas_is_err(mas))
		return 0;

	node = mas_pop_node(mas);
	pivots = ma_pivots(node, type);
	slots = ma_slots(node, type);
	node->parent = ma_parent_ptr(
		      ((unsigned long)mas->tree | MA_ROOT_PARENT));
	mas->node = mt_mk_node(node, type);
	rcu_assign_pointer(slots[0], entry);
	pivots[0] = mas->last;
	mas->depth = 1;
	mas_set_height(mas);
	rcu_assign_pointer(mas->tree->ma_root, mte_mk_root(mas->node));

done:
	if (xa_is_node(root))
		mte_destroy_walk(root, mas->tree);

	return 1;
}
/*
 * mas_wr_spanning_store() - Create a subtree with the store operation completed
 * and new nodes where necessary, then place the sub-tree in the actual tree.
 * Note that mas is expected to point to the node which caused the store to
 * span.
 * @wr_mas: The maple write state
 *
 * Return: 0 on error, positive on success.
 */
static inline int mas_wr_spanning_store(struct ma_wr_state *wr_mas)
{
	struct maple_subtree_state mast;
	struct maple_big_node b_node;
	struct ma_state *mas;
	unsigned char height;

	/* Left and Right side of spanning store */
	MA_STATE(l_mas, NULL, 0, 0);
	MA_STATE(r_mas, NULL, 0, 0);

	MA_WR_STATE(r_wr_mas, &r_mas, wr_mas->entry);
	MA_WR_STATE(l_wr_mas, &l_mas, wr_mas->entry);

	/*
	 * A store operation that spans multiple nodes is called a spanning
	 * store and is handled early in the store call stack by the function
	 * mas_is_span_wr().  When a spanning store is identified, the maple
	 * state is duplicated.  The first maple state walks the left tree path
	 * to ``index``, the duplicate walks the right tree path to ``last``.
	 * The data in the two nodes are combined into a single node, two nodes,
	 * or possibly three nodes (see the 3-way split above).  A ``NULL``
	 * written to the last entry of a node is considered a spanning store as
	 * a rebalance is required for the operation to complete and an overflow
	 * of data may happen.
	 */
	mas = wr_mas->mas;
	trace_ma_op(__func__, mas);

	if (unlikely(!mas->index && mas->last == ULONG_MAX))
		return mas_new_root(mas, wr_mas->entry);
	/*
	 * Node rebalancing may occur due to this store, so there may be three new
	 * entries per level plus a new root.
	 */
	height = mas_mt_height(mas);
	mas_node_count(mas, 1 + height * 3);
	if (mas_is_err(mas))
		return 0;

	/*
	 * Set up right side.  Need to get to the next offset after the spanning
	 * store to ensure it's not NULL and to combine both the next node and
	 * the node with the start together.
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

	memset(&b_node, 0, sizeof(struct maple_big_node));
	/* Copy l_mas and store the value in b_node. */
	mas_store_b_node(&l_wr_mas, &b_node, l_wr_mas.node_end);
	/* Copy r_mas into b_node. */
	if (r_mas.offset <= r_wr_mas.node_end)
		mas_mab_cp(&r_mas, r_mas.offset, r_wr_mas.node_end,
			   &b_node, b_node.b_end + 1);
	else
		b_node.b_end++;

	/* Stop spanning searches by searching for just index. */
	l_mas.index = l_mas.last = mas->index;

	mast.bn = &b_node;
	mast.orig_l = &l_mas;
	mast.orig_r = &r_mas;
	/* Combine l_mas and r_mas and split them up evenly again. */
	return mas_spanning_rebalance(mas, &mast, height + 1);
}

/*
 * mas_wr_node_store() - Attempt to store the value in a node
 * @wr_mas: The maple write state
 *
 * Attempts to reuse the node, but may allocate.
 *
 * Return: True if stored, false otherwise
 */
static inline bool mas_wr_node_store(struct ma_wr_state *wr_mas)
{
	struct ma_state *mas = wr_mas->mas;
	void __rcu **dst_slots;
	unsigned long *dst_pivots;
	unsigned char dst_offset;
	unsigned char new_end = wr_mas->node_end;
	unsigned char offset;
	unsigned char node_slots = mt_slots[wr_mas->type];
	struct maple_node reuse, *newnode;
	unsigned char copy_size, max_piv = mt_pivots[wr_mas->type];
	bool in_rcu = mt_in_rcu(mas->tree);

	offset = mas->offset;
	if (mas->last == wr_mas->r_max) {
		/* runs right to the end of the node */
		if (mas->last == mas->max)
			new_end = offset;
		/* don't copy this offset */
		wr_mas->offset_end++;
	} else if (mas->last < wr_mas->r_max) {
		/* new range ends in this range */
		if (unlikely(wr_mas->r_max == ULONG_MAX))
			mas_bulk_rebalance(mas, wr_mas->node_end, wr_mas->type);

		new_end++;
	} else {
		if (wr_mas->end_piv == mas->last)
			wr_mas->offset_end++;

		new_end -= wr_mas->offset_end - offset - 1;
	}

	/* new range starts within a range */
	if (wr_mas->r_min < mas->index)
		new_end++;

	/* Not enough room */
	if (new_end >= node_slots)
		return false;

	/* Not enough data. */
	if (!mte_is_root(mas->node) && (new_end <= mt_min_slots[wr_mas->type]) &&
	    !(mas->mas_flags & MA_STATE_BULK))
		return false;

	/* set up node. */
	if (in_rcu) {
		mas_node_count(mas, 1);
		if (mas_is_err(mas))
			return false;

		newnode = mas_pop_node(mas);
	} else {
		memset(&reuse, 0, sizeof(struct maple_node));
		newnode = &reuse;
	}

	newnode->parent = mas_mn(mas)->parent;
	dst_pivots = ma_pivots(newnode, wr_mas->type);
	dst_slots = ma_slots(newnode, wr_mas->type);
	/* Copy from start to insert point */
	memcpy(dst_pivots, wr_mas->pivots, sizeof(unsigned long) * (offset + 1));
	memcpy(dst_slots, wr_mas->slots, sizeof(void *) * (offset + 1));
	dst_offset = offset;

	/* Handle insert of new range starting after old range */
	if (wr_mas->r_min < mas->index) {
		mas->offset++;
		rcu_assign_pointer(dst_slots[dst_offset], wr_mas->content);
		dst_pivots[dst_offset++] = mas->index - 1;
	}

	/* Store the new entry and range end. */
	if (dst_offset < max_piv)
		dst_pivots[dst_offset] = mas->last;
	mas->offset = dst_offset;
	rcu_assign_pointer(dst_slots[dst_offset], wr_mas->entry);

	/*
	 * this range wrote to the end of the node or it overwrote the rest of
	 * the data
	 */
	if (wr_mas->offset_end > wr_mas->node_end || mas->last >= mas->max) {
		new_end = dst_offset;
		goto done;
	}

	dst_offset++;
	/* Copy to the end of node if necessary. */
	copy_size = wr_mas->node_end - wr_mas->offset_end + 1;
	memcpy(dst_slots + dst_offset, wr_mas->slots + wr_mas->offset_end,
	       sizeof(void *) * copy_size);
	if (dst_offset < max_piv) {
		if (copy_size > max_piv - dst_offset)
			copy_size = max_piv - dst_offset;

		memcpy(dst_pivots + dst_offset,
		       wr_mas->pivots + wr_mas->offset_end,
		       sizeof(unsigned long) * copy_size);
	}

	if ((wr_mas->node_end == node_slots - 1) && (new_end < node_slots - 1))
		dst_pivots[new_end] = mas->max;

done:
	mas_leaf_set_meta(mas, newnode, dst_pivots, maple_leaf_64, new_end);
	if (in_rcu) {
		mas->node = mt_mk_node(newnode, wr_mas->type);
		mas_replace(mas, false);
	} else {
		memcpy(wr_mas->node, newnode, sizeof(struct maple_node));
	}
	trace_ma_write(__func__, mas, 0, wr_mas->entry);
	mas_update_gap(mas);
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
	unsigned long lmax; /* Logical max. */
	unsigned char offset = mas->offset;

	if ((wr_mas->r_max > mas->last) && ((wr_mas->r_min != mas->index) ||
				  (offset != wr_mas->node_end)))
		return false;

	if (offset == wr_mas->node_end - 1)
		lmax = mas->max;
	else
		lmax = wr_mas->pivots[offset + 1];

	/* going to overwrite too many slots. */
	if (lmax < mas->last)
		return false;

	if (wr_mas->r_min == mas->index) {
		/* overwriting two or more ranges with one. */
		if (lmax == mas->last)
			return false;

		/* Overwriting all of offset and a portion of offset + 1. */
		rcu_assign_pointer(wr_mas->slots[offset], wr_mas->entry);
		wr_mas->pivots[offset] = mas->last;
		goto done;
	}

	/* Doesn't end on the next range end. */
	if (lmax != mas->last)
		return false;

	/* Overwriting a portion of offset and all of offset + 1 */
	if ((offset + 1 < mt_pivots[wr_mas->type]) &&
	    (wr_mas->entry || wr_mas->pivots[offset + 1]))
		wr_mas->pivots[offset + 1] = mas->last;

	rcu_assign_pointer(wr_mas->slots[offset + 1], wr_mas->entry);
	wr_mas->pivots[offset] = mas->index - 1;
	mas->offset++; /* Keep mas accurate. */

done:
	trace_ma_write(__func__, mas, 0, wr_mas->entry);
	mas_update_gap(mas);
	return true;
}

static inline void mas_wr_end_piv(struct ma_wr_state *wr_mas)
{
	while ((wr_mas->mas->last > wr_mas->end_piv) &&
	       (wr_mas->offset_end < wr_mas->node_end))
		wr_mas->end_piv = wr_mas->pivots[++wr_mas->offset_end];

	if (wr_mas->mas->last > wr_mas->end_piv)
		wr_mas->end_piv = wr_mas->mas->max;
}

static inline void mas_wr_extend_null(struct ma_wr_state *wr_mas)
{
	struct ma_state *mas = wr_mas->mas;

	if (mas->last < wr_mas->end_piv && !wr_mas->slots[wr_mas->offset_end])
		mas->last = wr_mas->end_piv;

	/* Check next slot(s) if we are overwriting the end */
	if ((mas->last == wr_mas->end_piv) &&
	    (wr_mas->node_end != wr_mas->offset_end) &&
	    !wr_mas->slots[wr_mas->offset_end + 1]) {
		wr_mas->offset_end++;
		if (wr_mas->offset_end == wr_mas->node_end)
			mas->last = mas->max;
		else
			mas->last = wr_mas->pivots[wr_mas->offset_end];
		wr_mas->end_piv = mas->last;
	}

	if (!wr_mas->content) {
		/* If this one is null, the next and prev are not */
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

static inline bool mas_wr_append(struct ma_wr_state *wr_mas)
{
	unsigned char end = wr_mas->node_end;
	unsigned char new_end = end + 1;
	struct ma_state *mas = wr_mas->mas;
	unsigned char node_pivots = mt_pivots[wr_mas->type];

	if ((mas->index != wr_mas->r_min) && (mas->last == wr_mas->r_max)) {
		if (new_end < node_pivots)
			wr_mas->pivots[new_end] = wr_mas->pivots[end];

		if (new_end < node_pivots)
			ma_set_meta(wr_mas->node, maple_leaf_64, 0, new_end);

		rcu_assign_pointer(wr_mas->slots[new_end], wr_mas->entry);
		mas->offset = new_end;
		wr_mas->pivots[end] = mas->index - 1;

		return true;
	}

	if ((mas->index == wr_mas->r_min) && (mas->last < wr_mas->r_max)) {
		if (new_end < node_pivots)
			wr_mas->pivots[new_end] = wr_mas->pivots[end];

		rcu_assign_pointer(wr_mas->slots[new_end], wr_mas->content);
		if (new_end < node_pivots)
			ma_set_meta(wr_mas->node, maple_leaf_64, 0, new_end);

		wr_mas->pivots[end] = mas->last;
		rcu_assign_pointer(wr_mas->slots[end], wr_mas->entry);
		return true;
	}

	return false;
}

/*
 * mas_wr_bnode() - Slow path for a modification.
 * @wr_mas: The write maple state
 *
 * This is where split, rebalance end up.
 */
static void mas_wr_bnode(struct ma_wr_state *wr_mas)
{
	struct maple_big_node b_node;

	trace_ma_write(__func__, wr_mas->mas, 0, wr_mas->entry);
	memset(&b_node, 0, sizeof(struct maple_big_node));
	mas_store_b_node(wr_mas, &b_node, wr_mas->offset_end);
	mas_commit_b_node(wr_mas, &b_node, wr_mas->node_end);
}

static inline void mas_wr_modify(struct ma_wr_state *wr_mas)
{
	unsigned char node_slots;
	unsigned char node_size;
	struct ma_state *mas = wr_mas->mas;

	/* Direct replacement */
	if (wr_mas->r_min == mas->index && wr_mas->r_max == mas->last) {
		rcu_assign_pointer(wr_mas->slots[mas->offset], wr_mas->entry);
		if (!!wr_mas->entry ^ !!wr_mas->content)
			mas_update_gap(mas);
		return;
	}

	/* Attempt to append */
	node_slots = mt_slots[wr_mas->type];
	node_size = wr_mas->node_end - wr_mas->offset_end + mas->offset + 2;
	if (mas->max == ULONG_MAX)
		node_size++;

	/* slot and node store will not fit, go to the slow path */
	if (unlikely(node_size >= node_slots))
		goto slow_path;

	if (wr_mas->entry && (wr_mas->node_end < node_slots - 1) &&
	    (mas->offset == wr_mas->node_end) && mas_wr_append(wr_mas)) {
		if (!wr_mas->content || !wr_mas->entry)
			mas_update_gap(mas);
		return;
	}

	if ((wr_mas->offset_end - mas->offset <= 1) && mas_wr_slot_store(wr_mas))
		return;
	else if (mas_wr_node_store(wr_mas))
		return;

	if (mas_is_err(mas))
		return;

slow_path:
	mas_wr_bnode(wr_mas);
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
	if (mas_is_none(mas) || mas_is_ptr(mas)) {
		mas_store_root(mas, wr_mas->entry);
		return wr_mas->content;
	}

	if (unlikely(!mas_wr_walk(wr_mas))) {
		mas_wr_spanning_store(wr_mas);
		return wr_mas->content;
	}

	/* At this point, we are at the leaf node that needs to be altered. */
	wr_mas->end_piv = wr_mas->r_max;
	mas_wr_end_piv(wr_mas);

	if (!wr_mas->entry)
		mas_wr_extend_null(wr_mas);

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
	 * If the range being inserted is adjacent to another range, then only a
	 * single pivot needs to be inserted (as well as writing the entry).  If
	 * the new range is within a gap but does not touch any other ranges,
	 * then two pivots need to be inserted: the start - 1, and the end.  As
	 * usual, the entry must be written.  Most operations require a new node
	 * to be allocated and replace an existing node to ensure RCU safety,
	 * when in RCU mode.  The exception to requiring a newly allocated node
	 * is when inserting at the end of a node (appending).  When done
	 * carefully, appending can reuse the node in place.
	 */
	wr_mas.content = mas_start(mas);
	if (wr_mas.content)
		goto exists;

	if (mas_is_none(mas) || mas_is_ptr(mas)) {
		mas_store_root(mas, entry);
		return NULL;
	}

	/* spanning writes always overwrite something */
	if (!mas_wr_walk(&wr_mas))
		goto exists;

	/* At this point, we are at the leaf node that needs to be altered. */
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

/*
 * mas_prev_node() - Find the prev non-null entry at the same level in the
 * tree.  The prev value will be mas->node[mas->offset] or MAS_NONE.
 * @mas: The maple state
 * @min: The lower limit to search
 *
 * The prev node value will be mas->node[mas->offset] or MAS_NONE.
 * Return: 1 if the node is dead, 0 otherwise.
 */
static inline int mas_prev_node(struct ma_state *mas, unsigned long min)
{
	enum maple_type mt;
	int offset, level;
	void __rcu **slots;
	struct maple_node *node;
	struct maple_enode *enode;
	unsigned long *pivots;

	if (mas_is_none(mas))
		return 0;

	level = 0;
	do {
		node = mas_mn(mas);
		if (ma_is_root(node))
			goto no_entry;

		/* Walk up. */
		if (unlikely(mas_ascend(mas)))
			return 1;
		offset = mas->offset;
		level++;
	} while (!offset);

	offset--;
	mt = mte_node_type(mas->node);
	node = mas_mn(mas);
	slots = ma_slots(node, mt);
	pivots = ma_pivots(node, mt);
	mas->max = pivots[offset];
	if (offset)
		mas->min = pivots[offset - 1] + 1;
	if (unlikely(ma_dead_node(node)))
		return 1;

	if (mas->max < min)
		goto no_entry_min;

	while (level > 1) {
		level--;
		enode = mas_slot(mas, slots, offset);
		if (unlikely(ma_dead_node(node)))
			return 1;

		mas->node = enode;
		mt = mte_node_type(mas->node);
		node = mas_mn(mas);
		slots = ma_slots(node, mt);
		pivots = ma_pivots(node, mt);
		offset = ma_data_end(node, mt, pivots, mas->max);
		if (offset)
			mas->min = pivots[offset - 1] + 1;

		if (offset < mt_pivots[mt])
			mas->max = pivots[offset];

		if (mas->max < min)
			goto no_entry;
	}

	mas->node = mas_slot(mas, slots, offset);
	if (unlikely(ma_dead_node(node)))
		return 1;

	mas->offset = mas_data_end(mas);
	if (unlikely(mte_dead_node(mas->node)))
		return 1;

	return 0;

no_entry_min:
	mas->offset = offset;
	if (offset)
		mas->min = pivots[offset - 1] + 1;
no_entry:
	if (unlikely(ma_dead_node(node)))
		return 1;

	mas->node = MAS_NONE;
	return 0;
}

/*
 * mas_next_node() - Get the next node at the same level in the tree.
 * @mas: The maple state
 * @max: The maximum pivot value to check.
 *
 * The next value will be mas->node[mas->offset] or MAS_NONE.
 * Return: 1 on dead node, 0 otherwise.
 */
static inline int mas_next_node(struct ma_state *mas, struct maple_node *node,
				unsigned long max)
{
	unsigned long min, pivot;
	unsigned long *pivots;
	struct maple_enode *enode;
	int level = 0;
	unsigned char offset;
	enum maple_type mt;
	void __rcu **slots;

	if (mas->max >= max)
		goto no_entry;

	level = 0;
	do {
		if (ma_is_root(node))
			goto no_entry;

		min = mas->max + 1;
		if (min > max)
			goto no_entry;

		if (unlikely(mas_ascend(mas)))
			return 1;

		offset = mas->offset;
		level++;
		node = mas_mn(mas);
		mt = mte_node_type(mas->node);
		pivots = ma_pivots(node, mt);
	} while (unlikely(offset == ma_data_end(node, mt, pivots, mas->max)));

	slots = ma_slots(node, mt);
	pivot = mas_safe_pivot(mas, pivots, ++offset, mt);
	while (unlikely(level > 1)) {
		/* Descend, if necessary */
		enode = mas_slot(mas, slots, offset);
		if (unlikely(ma_dead_node(node)))
			return 1;

		mas->node = enode;
		level--;
		node = mas_mn(mas);
		mt = mte_node_type(mas->node);
		slots = ma_slots(node, mt);
		pivots = ma_pivots(node, mt);
		offset = 0;
		pivot = pivots[0];
	}

	enode = mas_slot(mas, slots, offset);
	if (unlikely(ma_dead_node(node)))
		return 1;

	mas->node = enode;
	mas->min = min;
	mas->max = pivot;
	return 0;

no_entry:
	if (unlikely(ma_dead_node(node)))
		return 1;

	mas->node = MAS_NONE;
	return 0;
}

/*
 * mas_next_nentry() - Get the next node entry
 * @mas: The maple state
 * @max: The maximum value to check
 * @*range_start: Pointer to store the start of the range.
 *
 * Sets @mas->offset to the offset of the next node entry, @mas->last to the
 * pivot of the entry.
 *
 * Return: The next entry, %NULL otherwise
 */
static inline void *mas_next_nentry(struct ma_state *mas,
	    struct maple_node *node, unsigned long max, enum maple_type type)
{
	unsigned char count;
	unsigned long pivot;
	unsigned long *pivots;
	void __rcu **slots;
	void *entry;

	if (mas->last == mas->max) {
		mas->index = mas->max;
		return NULL;
	}

	pivots = ma_pivots(node, type);
	slots = ma_slots(node, type);
	mas->index = mas_safe_min(mas, pivots, mas->offset);
	count = ma_data_end(node, type, pivots, mas->max);
	if (ma_dead_node(node))
		return NULL;

	if (mas->index > max)
		return NULL;

	if (mas->offset > count)
		return NULL;

	while (mas->offset < count) {
		pivot = pivots[mas->offset];
		entry = mas_slot(mas, slots, mas->offset);
		if (ma_dead_node(node))
			return NULL;

		if (entry)
			goto found;

		if (pivot >= max)
			return NULL;

		mas->index = pivot + 1;
		mas->offset++;
	}

	if (mas->index > mas->max) {
		mas->index = mas->last;
		return NULL;
	}

	pivot = mas_safe_pivot(mas, pivots, mas->offset, type);
	entry = mas_slot(mas, slots, mas->offset);
	if (ma_dead_node(node))
		return NULL;

	if (!pivot)
		return NULL;

	if (!entry)
		return NULL;

found:
	mas->last = pivot;
	return entry;
}

static inline void mas_rewalk(struct ma_state *mas, unsigned long index)
{
retry:
	mas_set(mas, index);
	mas_state_walk(mas);
	if (mas_is_start(mas))
		goto retry;
}

/*
 * mas_next_entry() - Internal function to get the next entry.
 * @mas: The maple state
 * @limit: The maximum range start.
 *
 * Set the @mas->node to the next entry and the range_start to
 * the beginning value for the entry.  Does not check beyond @limit.
 * Sets @mas->index and @mas->last to the limit if it is hit.
 * Restarts on dead nodes.
 *
 * Return: the next entry or %NULL.
 */
static inline void *mas_next_entry(struct ma_state *mas, unsigned long limit)
{
	void *entry = NULL;
	struct maple_enode *prev_node;
	struct maple_node *node;
	unsigned char offset;
	unsigned long last;
	enum maple_type mt;

	if (mas->index > limit) {
		mas->index = mas->last = limit;
		mas_pause(mas);
		return NULL;
	}
	last = mas->last;
retry:
	offset = mas->offset;
	prev_node = mas->node;
	node = mas_mn(mas);
	mt = mte_node_type(mas->node);
	mas->offset++;
	if (unlikely(mas->offset >= mt_slots[mt])) {
		mas->offset = mt_slots[mt] - 1;
		goto next_node;
	}

	while (!mas_is_none(mas)) {
		entry = mas_next_nentry(mas, node, limit, mt);
		if (unlikely(ma_dead_node(node))) {
			mas_rewalk(mas, last);
			goto retry;
		}

		if (likely(entry))
			return entry;

		if (unlikely((mas->index > limit)))
			break;

next_node:
		prev_node = mas->node;
		offset = mas->offset;
		if (unlikely(mas_next_node(mas, node, limit))) {
			mas_rewalk(mas, last);
			goto retry;
		}
		mas->offset = 0;
		node = mas_mn(mas);
		mt = mte_node_type(mas->node);
	}

	mas->index = mas->last = limit;
	mas->offset = offset;
	mas->node = prev_node;
	return NULL;
}

/*
 * mas_prev_nentry() - Get the previous node entry.
 * @mas: The maple state.
 * @limit: The lower limit to check for a value.
 *
 * Return: the entry, %NULL otherwise.
 */
static inline void *mas_prev_nentry(struct ma_state *mas, unsigned long limit,
				    unsigned long index)
{
	unsigned long pivot, min;
	unsigned char offset;
	struct maple_node *mn;
	enum maple_type mt;
	unsigned long *pivots;
	void __rcu **slots;
	void *entry;

retry:
	if (!mas->offset)
		return NULL;

	mn = mas_mn(mas);
	mt = mte_node_type(mas->node);
	offset = mas->offset - 1;
	if (offset >= mt_slots[mt])
		offset = mt_slots[mt] - 1;

	slots = ma_slots(mn, mt);
	pivots = ma_pivots(mn, mt);
	if (offset == mt_pivots[mt])
		pivot = mas->max;
	else
		pivot = pivots[offset];

	if (unlikely(ma_dead_node(mn))) {
		mas_rewalk(mas, index);
		goto retry;
	}

	while (offset && ((!mas_slot(mas, slots, offset) && pivot >= limit) ||
	       !pivot))
		pivot = pivots[--offset];

	min = mas_safe_min(mas, pivots, offset);
	entry = mas_slot(mas, slots, offset);
	if (unlikely(ma_dead_node(mn))) {
		mas_rewalk(mas, index);
		goto retry;
	}

	if (likely(entry)) {
		mas->offset = offset;
		mas->last = pivot;
		mas->index = min;
	}
	return entry;
}

static inline void *mas_prev_entry(struct ma_state *mas, unsigned long min)
{
	void *entry;

	if (mas->index < min) {
		mas->index = mas->last = min;
		mas->node = MAS_NONE;
		return NULL;
	}
retry:
	while (likely(!mas_is_none(mas))) {
		entry = mas_prev_nentry(mas, min, mas->index);
		if (unlikely(mas->last < min))
			goto not_found;

		if (likely(entry))
			return entry;

		if (unlikely(mas_prev_node(mas, min))) {
			mas_rewalk(mas, mas->index);
			goto retry;
		}

		mas->offset++;
	}

	mas->offset--;
not_found:
	mas->index = mas->last = min;
	return NULL;
}

/*
 * mas_rev_awalk() - Internal function.  Reverse allocation walk.  Find the
 * highest gap address of a given size in a given node and descend.
 * @mas: The maple state
 * @size: The needed size.
 *
 * Return: True if found in a leaf, false otherwise.
 *
 */
static bool mas_rev_awalk(struct ma_state *mas, unsigned long size)
{
	enum maple_type type = mte_node_type(mas->node);
	struct maple_node *node = mas_mn(mas);
	unsigned long *pivots, *gaps;
	void __rcu **slots;
	unsigned long gap = 0;
	unsigned long max, min;
	unsigned char offset;

	if (unlikely(mas_is_err(mas)))
		return true;

	if (ma_is_dense(type)) {
		/* dense nodes. */
		mas->offset = (unsigned char)(mas->index - mas->min);
		return true;
	}

	pivots = ma_pivots(node, type);
	slots = ma_slots(node, type);
	gaps = ma_gaps(node, type);
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
				/* Skip the next slot, it cannot be a gap. */
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
		goto no_space;

	if (unlikely(ma_is_leaf(type))) {
		mas->offset = offset;
		mas->min = min;
		mas->max = min + gap - 1;
		return true;
	}

	/* descend, only happens under lock. */
	mas->node = mas_slot(mas, slots, offset);
	mas->min = min;
	mas->max = max;
	mas->offset = mas_data_end(mas);
	return false;

ascend:
	if (!mte_is_root(mas->node))
		return false;

no_space:
	mas_set_err(mas, -EBUSY);
	return false;
}

static inline bool mas_anode_descend(struct ma_state *mas, unsigned long size)
{
	enum maple_type type = mte_node_type(mas->node);
	unsigned long pivot, min, gap = 0;
	unsigned char offset;
	unsigned long *gaps;
	unsigned long *pivots = ma_pivots(mas_mn(mas), type);
	void __rcu **slots = ma_slots(mas_mn(mas), type);
	bool found = false;

	if (ma_is_dense(type)) {
		mas->offset = (unsigned char)(mas->index - mas->min);
		return true;
	}

	gaps = ma_gaps(mte_to_node(mas->node), type);
	offset = mas->offset;
	min = mas_safe_min(mas, pivots, offset);
	for (; offset < mt_slots[type]; offset++) {
		pivot = mas_safe_pivot(mas, pivots, offset, type);
		if (offset && !pivot)
			break;

		/* Not within lower bounds */
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
				mas->node = mas_slot(mas, slots, offset);
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

	if (mte_is_root(mas->node))
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
 * mas->node is MAS_NONE, reset to MAS_START.
 *
 * Return: the entry at the location or %NULL.
 */
void *mas_walk(struct ma_state *mas)
{
	void *entry;

retry:
	entry = mas_state_walk(mas);
	if (mas_is_start(mas))
		goto retry;

	if (mas_is_ptr(mas)) {
		if (!mas->index) {
			mas->last = 0;
		} else {
			mas->index = 1;
			mas->last = ULONG_MAX;
		}
		return entry;
	}

	if (mas_is_none(mas)) {
		mas->index = 0;
		mas->last = ULONG_MAX;
	}

	return entry;
}
EXPORT_SYMBOL_GPL(mas_walk);

static inline bool mas_rewind_node(struct ma_state *mas)
{
	unsigned char slot;

	do {
		if (mte_is_root(mas->node)) {
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
 * mas_skip_node() - Internal function.  Skip over a node.
 * @mas: The maple state.
 *
 * Return: true if there is another node, false otherwise.
 */
static inline bool mas_skip_node(struct ma_state *mas)
{
	unsigned char slot, slot_count;
	unsigned long *pivots;
	enum maple_type mt;

	mt = mte_node_type(mas->node);
	slot_count = mt_slots[mt] - 1;
	do {
		if (mte_is_root(mas->node)) {
			slot = mas->offset;
			if (slot > slot_count) {
				mas_set_err(mas, -EBUSY);
				return false;
			}
		} else {
			mas_ascend(mas);
			slot = mas->offset;
			mt = mte_node_type(mas->node);
			slot_count = mt_slots[mt] - 1;
		}
	} while (slot > slot_count);

	mas->offset = ++slot;
	pivots = ma_pivots(mas_mn(mas), mt);
	if (slot > 0)
		mas->min = pivots[slot - 1] + 1;

	if (slot <= slot_count)
		mas->max = pivots[slot];

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
	struct maple_enode *last = NULL;

	/*
	 * There are 4 options:
	 * go to child (descend)
	 * go back to parent (ascend)
	 * no gap found. (return, slot == MAPLE_NODE_SLOTS)
	 * found the gap. (return, slot != MAPLE_NODE_SLOTS)
	 */
	while (!mas_is_err(mas) && !mas_anode_descend(mas, size)) {
		if (last == mas->node)
			mas_skip_node(mas);
		else
			last = mas->node;
	}
}

/*
 * mas_fill_gap() - Fill a located gap with @entry.
 * @mas: The maple state
 * @entry: The value to store
 * @slot: The offset into the node to store the @entry
 * @size: The size of the entry
 * @index: The start location
 */
static inline void mas_fill_gap(struct ma_state *mas, void *entry,
		unsigned char slot, unsigned long size, unsigned long *index)
{
	MA_WR_STATE(wr_mas, mas, entry);
	unsigned char pslot = mte_parent_slot(mas->node);
	struct maple_enode *mn = mas->node;
	unsigned long *pivots;
	enum maple_type ptype;
	/*
	 * mas->index is the start address for the search
	 *  which may no longer be needed.
	 * mas->last is the end address for the search
	 */

	*index = mas->index;
	mas->last = mas->index + size - 1;

	/*
	 * It is possible that using mas->max and mas->min to correctly
	 * calculate the index and last will cause an issue in the gap
	 * calculation, so fix the ma_state here
	 */
	mas_ascend(mas);
	ptype = mte_node_type(mas->node);
	pivots = ma_pivots(mas_mn(mas), ptype);
	mas->max = mas_safe_pivot(mas, pivots, pslot, ptype);
	mas->min = mas_safe_min(mas, pivots, pslot);
	mas->node = mn;
	mas->offset = slot;
	mas_wr_store_entry(&wr_mas);
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
static inline void mas_sparse_area(struct ma_state *mas, unsigned long min,
				unsigned long max, unsigned long size, bool fwd)
{
	unsigned long start = 0;

	if (!unlikely(mas_is_none(mas)))
		start++;
	/* mas_is_ptr */

	if (start < min)
		start = min;

	if (fwd) {
		mas->index = start;
		mas->last = start + size - 1;
		return;
	}

	mas->index = max;
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

	if (mas_is_start(mas))
		mas_start(mas);
	else if (mas->offset >= 2)
		mas->offset -= 2;
	else if (!mas_skip_node(mas))
		return -EBUSY;

	/* Empty set */
	if (mas_is_none(mas) || mas_is_ptr(mas)) {
		mas_sparse_area(mas, min, max, size, true);
		return 0;
	}

	/* The start of the window can only be within these values */
	mas->index = min;
	mas->last = max;
	mas_awalk(mas, size);

	if (unlikely(mas_is_err(mas)))
		return xa_err(mas->node);

	offset = mas->offset;
	if (unlikely(offset == MAPLE_NODE_SLOTS))
		return -EBUSY;

	mt = mte_node_type(mas->node);
	pivots = ma_pivots(mas_mn(mas), mt);
	if (offset)
		mas->min = pivots[offset - 1] + 1;

	if (offset < mt_pivots[mt])
		mas->max = pivots[offset];

	if (mas->index < mas->min)
		mas->index = mas->min;

	mas->last = mas->index + size - 1;
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
	struct maple_enode *last = mas->node;

	if (mas_is_start(mas)) {
		mas_start(mas);
		mas->offset = mas_data_end(mas);
	} else if (mas->offset >= 2) {
		mas->offset -= 2;
	} else if (!mas_rewind_node(mas)) {
		return -EBUSY;
	}

	/* Empty set. */
	if (mas_is_none(mas) || mas_is_ptr(mas)) {
		mas_sparse_area(mas, min, max, size, false);
		return 0;
	}

	/* The start of the window can only be within these values. */
	mas->index = min;
	mas->last = max;

	while (!mas_rev_awalk(mas, size)) {
		if (last == mas->node) {
			if (!mas_rewind_node(mas))
				return -EBUSY;
		} else {
			last = mas->node;
		}
	}

	if (mas_is_err(mas))
		return xa_err(mas->node);

	if (unlikely(mas->offset == MAPLE_NODE_SLOTS))
		return -EBUSY;

	/*
	 * mas_rev_awalk() has set mas->min and mas->max to the gap values.  If
	 * the maximum is outside the window we are searching, then use the last
	 * location in the search.
	 * mas->max and mas->min is the range of the gap.
	 * mas->index and mas->last are currently set to the search range.
	 */

	/* Trim the upper limit to the max. */
	if (mas->max <= mas->last)
		mas->last = mas->max;

	mas->index = mas->last - size + 1;
	return 0;
}
EXPORT_SYMBOL_GPL(mas_empty_area_rev);

static inline int mas_alloc(struct ma_state *mas, void *entry,
		unsigned long size, unsigned long *index)
{
	unsigned long min;

	mas_start(mas);
	if (mas_is_none(mas) || mas_is_ptr(mas)) {
		mas_root_expand(mas, entry);
		if (mas_is_err(mas))
			return xa_err(mas->node);

		if (!mas->index)
			return mte_pivot(mas->node, 0);
		return mte_pivot(mas->node, 1);
	}

	/* Must be walking a tree. */
	mas_awalk(mas, size);
	if (mas_is_err(mas))
		return xa_err(mas->node);

	if (mas->offset == MAPLE_NODE_SLOTS)
		goto no_gap;

	/*
	 * At this point, mas->node points to the right node and we have an
	 * offset that has a sufficient gap.
	 */
	min = mas->min;
	if (mas->offset)
		min = mte_pivot(mas->node, mas->offset - 1) + 1;

	if (mas->index < min)
		mas->index = min;

	mas_fill_gap(mas, entry, mas->offset, size, index);
	return 0;

no_gap:
	return -EBUSY;
}

static inline int mas_rev_alloc(struct ma_state *mas, unsigned long min,
				unsigned long max, void *entry,
				unsigned long size, unsigned long *index)
{
	int ret = 0;

	ret = mas_empty_area_rev(mas, min, max, size);
	if (ret)
		return ret;

	if (mas_is_err(mas))
		return xa_err(mas->node);

	if (mas->offset == MAPLE_NODE_SLOTS)
		goto no_gap;

	mas_fill_gap(mas, entry, mas->offset, size, index);
	return 0;

no_gap:
	return -EBUSY;
}

/*
 * mas_dead_leaves() - Mark all leaves of a node as dead.
 * @mas: The maple state
 * @slots: Pointer to the slot array
 *
 * Must hold the write lock.
 *
 * Return: The number of leaves marked as dead.
 */
static inline
unsigned char mas_dead_leaves(struct ma_state *mas, void __rcu **slots)
{
	struct maple_node *node;
	enum maple_type type;
	void *entry;
	int offset;

	for (offset = 0; offset < mt_slot_count(mas->node); offset++) {
		entry = mas_slot_locked(mas, slots, offset);
		type = mte_node_type(entry);
		node = mte_to_node(entry);
		/* Use both node and type to catch LE & BE metadata */
		if (!node || !type)
			break;

		mte_set_node_dead(entry);
		smp_wmb(); /* Needed for RCU */
		node->type = type;
		rcu_assign_pointer(slots[offset], node);
	}

	return offset;
}

static void __rcu **mas_dead_walk(struct ma_state *mas, unsigned char offset)
{
	struct maple_node *node, *next;
	void __rcu **slots = NULL;

	next = mas_mn(mas);
	do {
		mas->node = ma_enode_ptr(next);
		node = mas_mn(mas);
		slots = ma_slots(node, node->type);
		next = mas_slot_locked(mas, slots, offset);
		offset = 0;
	} while (!ma_is_leaf(next->type));

	return slots;
}

static void mt_free_walk(struct rcu_head *head)
{
	void __rcu **slots;
	struct maple_node *node, *start;
	struct maple_tree mt;
	unsigned char offset;
	enum maple_type type;
	MA_STATE(mas, &mt, 0, 0);

	node = container_of(head, struct maple_node, rcu);

	if (ma_is_leaf(node->type))
		goto free_leaf;

	mt_init_flags(&mt, node->ma_flags);
	mas_lock(&mas);
	start = node;
	mas.node = mt_mk_node(node, node->type);
	slots = mas_dead_walk(&mas, 0);
	node = mas_mn(&mas);
	do {
		mt_free_bulk(node->slot_len, slots);
		offset = node->parent_slot + 1;
		mas.node = node->piv_parent;
		if (mas_mn(&mas) == node)
			goto start_slots_free;

		type = mte_node_type(mas.node);
		slots = ma_slots(mte_to_node(mas.node), type);
		if ((offset < mt_slots[type]) && (slots[offset]))
			slots = mas_dead_walk(&mas, offset);

		node = mas_mn(&mas);
	} while ((node != start) || (node->slot_len < offset));

	slots = ma_slots(node, node->type);
	mt_free_bulk(node->slot_len, slots);

start_slots_free:
	mas_unlock(&mas);
free_leaf:
	mt_free_rcu(&node->rcu);
}

static inline void __rcu **mas_destroy_descend(struct ma_state *mas,
			struct maple_enode *prev, unsigned char offset)
{
	struct maple_node *node;
	struct maple_enode *next = mas->node;
	void __rcu **slots = NULL;

	do {
		mas->node = next;
		node = mas_mn(mas);
		slots = ma_slots(node, mte_node_type(mas->node));
		next = mas_slot_locked(mas, slots, 0);
		if ((mte_dead_node(next)))
			next = mas_slot_locked(mas, slots, 1);

		mte_set_node_dead(mas->node);
		node->type = mte_node_type(mas->node);
		node->piv_parent = prev;
		node->parent_slot = offset;
		offset = 0;
		prev = mas->node;
	} while (!mte_is_leaf(next));

	return slots;
}

static void mt_destroy_walk(struct maple_enode *enode, unsigned char ma_flags,
			    bool free)
{
	void __rcu **slots;
	struct maple_node *node = mte_to_node(enode);
	struct maple_enode *start;
	struct maple_tree mt;

	MA_STATE(mas, &mt, 0, 0);

	if (mte_is_leaf(enode))
		goto free_leaf;

	mt_init_flags(&mt, ma_flags);
	mas_lock(&mas);

	mas.node = start = enode;
	slots = mas_destroy_descend(&mas, start, 0);
	node = mas_mn(&mas);
	do {
		enum maple_type type;
		unsigned char offset;
		struct maple_enode *parent, *tmp;

		node->slot_len = mas_dead_leaves(&mas, slots);
		if (free)
			mt_free_bulk(node->slot_len, slots);
		offset = node->parent_slot + 1;
		mas.node = node->piv_parent;
		if (mas_mn(&mas) == node)
			goto start_slots_free;

		type = mte_node_type(mas.node);
		slots = ma_slots(mte_to_node(mas.node), type);
		if (offset >= mt_slots[type])
			goto next;

		tmp = mas_slot_locked(&mas, slots, offset);
		if (mte_node_type(tmp) && mte_to_node(tmp)) {
			parent = mas.node;
			mas.node = tmp;
			slots = mas_destroy_descend(&mas, parent, offset);
		}
next:
		node = mas_mn(&mas);
	} while (start != mas.node);

	node = mas_mn(&mas);
	node->slot_len = mas_dead_leaves(&mas, slots);
	if (free)
		mt_free_bulk(node->slot_len, slots);

start_slots_free:
	mas_unlock(&mas);

free_leaf:
	if (free)
		mt_free_rcu(&node->rcu);
}

/*
 * mte_destroy_walk() - Free a tree or sub-tree.
 * @enode: the encoded maple node (maple_enode) to start
 * @mt: the tree to free - needed for node types.
 *
 * Must hold the write lock.
 */
static inline void mte_destroy_walk(struct maple_enode *enode,
				    struct maple_tree *mt)
{
	struct maple_node *node = mte_to_node(enode);

	if (mt_in_rcu(mt)) {
		mt_destroy_walk(enode, mt->ma_flags, false);
		call_rcu(&node->rcu, mt_free_walk);
	} else {
		mt_destroy_walk(enode, mt->ma_flags, true);
	}
}

static void mas_wr_store_setup(struct ma_wr_state *wr_mas)
{
	if (unlikely(mas_is_paused(wr_mas->mas)))
		mas_reset(wr_mas->mas);

	if (!mas_is_start(wr_mas->mas)) {
		if (mas_is_none(wr_mas->mas)) {
			mas_reset(wr_mas->mas);
		} else {
			wr_mas->r_max = wr_mas->mas->max;
			wr_mas->type = mte_node_type(wr_mas->mas->node);
			if (mas_is_span_wr(wr_mas))
				mas_reset(wr_mas->mas);
		}
	}
}

/* Interface */

/**
 * mas_store() - Store an @entry.
 * @mas: The maple state.
 * @entry: The entry to store.
 *
 * The @mas->index and @mas->last is used to set the range for the @entry.
 * Note: The @mas should have pre-allocated entries to ensure there is memory to
 * store the entry.  Please see mas_expected_entries()/mas_destroy() for more details.
 *
 * Return: the first entry between mas->index and mas->last or %NULL.
 */
void *mas_store(struct ma_state *mas, void *entry)
{
	MA_WR_STATE(wr_mas, mas, entry);

	trace_ma_write(__func__, mas, 0, entry);
#ifdef CONFIG_DEBUG_MAPLE_TREE
	if (mas->index > mas->last)
		pr_err("Error %lu > %lu %p\n", mas->index, mas->last, entry);
	MT_BUG_ON(mas->tree, mas->index > mas->last);
	if (mas->index > mas->last) {
		mas_set_err(mas, -EINVAL);
		return NULL;
	}

#endif

	/*
	 * Storing is the same operation as insert with the added caveat that it
	 * can overwrite entries.  Although this seems simple enough, one may
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
 * Return: 0 on success, -EINVAL on invalid request, -ENOMEM if memory could not
 * be allocated.
 */
int mas_store_gfp(struct ma_state *mas, void *entry, gfp_t gfp)
{
	MA_WR_STATE(wr_mas, mas, entry);

	mas_wr_store_setup(&wr_mas);
	trace_ma_write(__func__, mas, 0, entry);
retry:
	mas_wr_store_entry(&wr_mas);
	if (unlikely(mas_nomem(mas, gfp)))
		goto retry;

	if (unlikely(mas_is_err(mas)))
		return xa_err(mas->node);

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
	BUG_ON(mas_is_err(mas));
	mas_destroy(mas);
}
EXPORT_SYMBOL_GPL(mas_store_prealloc);

/**
 * mas_preallocate() - Preallocate enough nodes for a store operation
 * @mas: The maple state
 * @gfp: The GFP_FLAGS to use for allocations.
 *
 * Return: 0 on success, -ENOMEM if memory could not be allocated.
 */
int mas_preallocate(struct ma_state *mas, gfp_t gfp)
{
	int ret;

	mas_node_count_gfp(mas, 1 + mas_mt_height(mas) * 3, gfp);
	mas->mas_flags |= MA_STATE_PREALLOC;
	if (likely(!mas_is_err(mas)))
		return 0;

	mas_set_alloc_req(mas, 0);
	ret = xa_err(mas->node);
	mas_reset(mas);
	mas_destroy(mas);
	mas_reset(mas);
	return ret;
}

/*
 * mas_destroy() - destroy a maple state.
 * @mas: The maple state
 *
 * Upon completion, check the left-most node and rebalance against the node to
 * the right if necessary.  Frees any allocated nodes associated with this maple
 * state.
 */
void mas_destroy(struct ma_state *mas)
{
	struct maple_alloc *node;
	unsigned long total;

	/*
	 * When using mas_for_each() to insert an expected number of elements,
	 * it is possible that the number inserted is less than the expected
	 * number.  To fix an invalid final node, a check is performed here to
	 * rebalance the previous node with the final node.
	 */
	if (mas->mas_flags & MA_STATE_REBALANCE) {
		unsigned char end;

		if (mas_is_start(mas))
			mas_start(mas);

		mtree_range_walk(mas);
		end = mas_data_end(mas) + 1;
		if (end < mt_min_slot_count(mas->node) - 1)
			mas_destroy_rebalance(mas, end);

		mas->mas_flags &= ~MA_STATE_REBALANCE;
	}
	mas->mas_flags &= ~(MA_STATE_BULK|MA_STATE_PREALLOC);

	total = mas_allocated(mas);
	while (total) {
		node = mas->alloc;
		mas->alloc = node->slot[0];
		if (node->node_count > 1) {
			size_t count = node->node_count - 1;

			mt_free_bulk(count, (void __rcu **)&node->slot[1]);
			total -= count;
		}
		kmem_cache_free(maple_node_cache, node);
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
 * This will attempt to pre-allocate enough nodes to store the expected number
 * of entries.  The allocations will occur using the bulk allocator interface
 * for speed.  Please call mas_destroy() on the @mas after inserting the entries
 * to ensure any unused nodes are freed.
 *
 * Return: 0 on success, -ENOMEM if memory could not be allocated.
 */
int mas_expected_entries(struct ma_state *mas, unsigned long nr_entries)
{
	int nonleaf_cap = MAPLE_ARANGE64_SLOTS - 2;
	struct maple_enode *enode = mas->node;
	int nr_nodes;
	int ret;

	/*
	 * Sometimes it is necessary to duplicate a tree to a new tree, such as
	 * forking a process and duplicating the VMAs from one tree to a new
	 * tree.  When such a situation arises, it is known that the new tree is
	 * not going to be used until the entire tree is populated.  For
	 * performance reasons, it is best to use a bulk load with RCU disabled.
	 * This allows for optimistic splitting that favours the left and reuse
	 * of nodes during the operation.
	 */

	/* Optimize splitting for bulk insert in-order */
	mas->mas_flags |= MA_STATE_BULK;

	/*
	 * Avoid overflow, assume a gap between each entry and a trailing null.
	 * If this is wrong, it just means allocation can happen during
	 * insertion of entries.
	 */
	nr_nodes = max(nr_entries, nr_entries * 2 + 1);
	if (!mt_is_alloc(mas->tree))
		nonleaf_cap = MAPLE_RANGE64_SLOTS - 2;

	/* Leaves; reduce slots to keep space for expansion */
	nr_nodes = DIV_ROUND_UP(nr_nodes, MAPLE_RANGE64_SLOTS - 2);
	/* Internal nodes */
	nr_nodes += DIV_ROUND_UP(nr_nodes, nonleaf_cap);
	/* Add working room for split (2 nodes) + new parents */
	mas_node_count(mas, nr_nodes + 3);

	/* Detect if allocations run out */
	mas->mas_flags |= MA_STATE_PREALLOC;

	if (!mas_is_err(mas))
		return 0;

	ret = xa_err(mas->node);
	mas->node = enode;
	mas_destroy(mas);
	return ret;

}
EXPORT_SYMBOL_GPL(mas_expected_entries);

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
	if (mas_is_none(mas) || mas_is_paused(mas))
		mas->node = MAS_START;

	if (mas_is_start(mas))
		mas_walk(mas); /* Retries on dead nodes handled by mas_walk */

	if (mas_is_ptr(mas)) {
		if (!mas->index) {
			mas->index = 1;
			mas->last = ULONG_MAX;
		}
		return NULL;
	}

	if (mas->last == ULONG_MAX)
		return NULL;

	/* Retries on dead nodes handled by mas_next_entry */
	return mas_next_entry(mas, max);
}
EXPORT_SYMBOL_GPL(mas_next);

/**
 * mt_next() - get the next value in the maple tree
 * @mt: The maple tree
 * @index: The start index
 * @max: The maximum index to check
 *
 * Return: The entry at @index or higher, or %NULL if nothing is found.
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

/**
 * mas_prev() - Get the previous entry
 * @mas: The maple state
 * @min: The minimum value to check.
 *
 * Must hold rcu_read_lock or the write lock.
 * Will reset mas to MAS_START if the node is MAS_NONE.  Will stop on not
 * searchable nodes.
 *
 * Return: the previous value or %NULL.
 */
void *mas_prev(struct ma_state *mas, unsigned long min)
{
	if (!mas->index) {
		/* Nothing comes before 0 */
		mas->last = 0;
		mas->node = MAS_NONE;
		return NULL;
	}

	if (unlikely(mas_is_ptr(mas)))
		return NULL;

	if (mas_is_none(mas) || mas_is_paused(mas))
		mas->node = MAS_START;

	if (mas_is_start(mas)) {
		mas_walk(mas);
		if (!mas->index)
			return NULL;
	}

	if (mas_is_ptr(mas)) {
		if (!mas->index) {
			mas->last = 0;
			return NULL;
		}

		mas->index = mas->last = 0;
		return mas_root_locked(mas);
	}
	return mas_prev_entry(mas, min);
}
EXPORT_SYMBOL_GPL(mas_prev);

/**
 * mt_prev() - get the previous value in the maple tree
 * @mt: The maple tree
 * @index: The start index
 * @min: The minimum index to check
 *
 * Return: The entry at @index or lower, or %NULL if nothing is found.
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
	mas->node = MAS_PAUSE;
}
EXPORT_SYMBOL_GPL(mas_pause);

/**
 * mas_find() - On the first call, find the entry at or after mas->index up to
 * %max.  Otherwise, find the entry after mas->index.
 * @mas: The maple state
 * @max: The maximum value to check.
 *
 * Must hold rcu_read_lock or the write lock.
 * If an entry exists, last and index are updated accordingly.
 * May set @mas->node to MAS_NONE.
 *
 * Return: The entry or %NULL.
 */
void *mas_find(struct ma_state *mas, unsigned long max)
{
	if (unlikely(mas_is_paused(mas))) {
		if (unlikely(mas->last == ULONG_MAX)) {
			mas->node = MAS_NONE;
			return NULL;
		}
		mas->node = MAS_START;
		mas->index = ++mas->last;
	}

	if (unlikely(mas_is_none(mas)))
		mas->node = MAS_START;

	if (unlikely(mas_is_start(mas))) {
		/* First run or continue */
		void *entry;

		if (mas->index > max)
			return NULL;

		entry = mas_walk(mas);
		if (entry)
			return entry;
	}

	if (unlikely(!mas_searchable(mas)))
		return NULL;

	/* Retries on dead nodes handled by mas_next_entry */
	return mas_next_entry(mas, max);
}
EXPORT_SYMBOL_GPL(mas_find);

/**
 * mas_find_rev: On the first call, find the first non-null entry at or below
 * mas->index down to %min.  Otherwise find the first non-null entry below
 * mas->index down to %min.
 * @mas: The maple state
 * @min: The minimum value to check.
 *
 * Must hold rcu_read_lock or the write lock.
 * If an entry exists, last and index are updated accordingly.
 * May set @mas->node to MAS_NONE.
 *
 * Return: The entry or %NULL.
 */
void *mas_find_rev(struct ma_state *mas, unsigned long min)
{
	if (unlikely(mas_is_paused(mas))) {
		if (unlikely(mas->last == ULONG_MAX)) {
			mas->node = MAS_NONE;
			return NULL;
		}
		mas->node = MAS_START;
		mas->last = --mas->index;
	}

	if (unlikely(mas_is_start(mas))) {
		/* First run or continue */
		void *entry;

		if (mas->index < min)
			return NULL;

		entry = mas_walk(mas);
		if (entry)
			return entry;
	}

	if (unlikely(!mas_searchable(mas)))
		return NULL;

	if (mas->index < min)
		return NULL;

	/* Retries on dead nodes handled by mas_prev_entry */
	return mas_prev_entry(mas, min);
}
EXPORT_SYMBOL_GPL(mas_find_rev);

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

	if (mas_is_none(mas) || mas_is_paused(mas))
		mas->node = MAS_START;

	/* Retry unnecessary when holding the write lock. */
	entry = mas_state_walk(mas);
	if (!entry)
		return NULL;

write_retry:
	/* Must reset to ensure spanning writes of last slot are detected */
	mas_reset(mas);
	mas_wr_store_setup(&wr_mas);
	mas_wr_store_entry(&wr_mas);
	if (mas_nomem(mas, GFP_KERNEL))
		goto write_retry;

	return entry;
}
EXPORT_SYMBOL_GPL(mas_erase);

/**
 * mas_nomem() - Check if there was an error allocating and do the allocation
 * if necessary If there are allocations, then free them.
 * @mas: The maple state
 * @gfp: The GFP_FLAGS to use for allocations
 * Return: true on allocation, false otherwise.
 */
bool mas_nomem(struct ma_state *mas, gfp_t gfp)
	__must_hold(mas->tree->lock)
{
	if (likely(mas->node != MA_ERROR(-ENOMEM))) {
		mas_destroy(mas);
		return false;
	}

	if (gfpflags_allow_blocking(gfp) && !mt_external_lock(mas->tree)) {
		mtree_unlock(mas->tree);
		mas_alloc_nodes(mas, gfp);
		mtree_lock(mas->tree);
	} else {
		mas_alloc_nodes(mas, gfp);
	}

	if (!mas_allocated(mas))
		return false;

	mas->node = MAS_START;
	return true;
}

void __init maple_tree_init(void)
{
	maple_node_cache = kmem_cache_create("maple_node",
			sizeof(struct maple_node), sizeof(struct maple_node),
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
	if (unlikely(mas_is_none(&mas)))
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
 * Return: 0 on success, -EINVAL on invalid request, -ENOMEM if memory could not
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
	if (mas_nomem(&mas, gfp))
		goto retry;

	mtree_unlock(mt);
	if (mas_is_err(&mas))
		return xa_err(mas.node);

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
 * Return: 0 on success, -EINVAL on invalid request, -ENOMEM if memory could not
 * be allocated.
 */
int mtree_store(struct maple_tree *mt, unsigned long index, void *entry,
		 gfp_t gfp)
{
	return mtree_store_range(mt, index, index, entry, gfp);
}
EXPORT_SYMBOL(mtree_store);

/**
 * mtree_insert_range() - Insert an entry at a give range if there is no value.
 * @mt: The maple tree
 * @first: The start of the range
 * @last: The end of the range
 * @entry: The entry to store
 * @gfp: The GFP_FLAGS to use for allocations.
 *
 * Return: 0 on success, -EEXISTS if the range is occupied, -EINVAL on invalid
 * request, -ENOMEM if memory could not be allocated.
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
	if (mas_nomem(&ms, gfp))
		goto retry;

	mtree_unlock(mt);
	if (mas_is_err(&ms))
		return xa_err(ms.node);

	return 0;
}
EXPORT_SYMBOL(mtree_insert_range);

/**
 * mtree_insert() - Insert an entry at a give index if there is no value.
 * @mt: The maple tree
 * @index : The index to store the value
 * @entry: The entry to store
 * @gfp: The FGP_FLAGS to use for allocations.
 *
 * Return: 0 on success, -EEXISTS if the range is occupied, -EINVAL on invalid
 * request, -ENOMEM if memory could not be allocated.
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

	MA_STATE(mas, mt, min, max - size);
	if (!mt_is_alloc(mt))
		return -EINVAL;

	if (WARN_ON_ONCE(mt_is_reserved(entry)))
		return -EINVAL;

	if (min > max)
		return -EINVAL;

	if (max < size)
		return -EINVAL;

	if (!size)
		return -EINVAL;

	mtree_lock(mt);
retry:
	mas.offset = 0;
	mas.index = min;
	mas.last = max - size;
	ret = mas_alloc(&mas, entry, size, startp);
	if (mas_nomem(&mas, gfp))
		goto retry;

	mtree_unlock(mt);
	return ret;
}
EXPORT_SYMBOL(mtree_alloc_range);

int mtree_alloc_rrange(struct maple_tree *mt, unsigned long *startp,
		void *entry, unsigned long size, unsigned long min,
		unsigned long max, gfp_t gfp)
{
	int ret = 0;

	MA_STATE(mas, mt, min, max - size);
	if (!mt_is_alloc(mt))
		return -EINVAL;

	if (WARN_ON_ONCE(mt_is_reserved(entry)))
		return -EINVAL;

	if (min >= max)
		return -EINVAL;

	if (max < size - 1)
		return -EINVAL;

	if (!size)
		return -EINVAL;

	mtree_lock(mt);
retry:
	ret = mas_rev_alloc(&mas, min, max, entry, size, startp);
	if (mas_nomem(&mas, gfp))
		goto retry;

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

/**
 * __mt_destroy() - Walk and free all nodes of a locked maple tree.
 * @mt: The maple tree
 *
 * Note: Does not handle locking.
 */
void __mt_destroy(struct maple_tree *mt)
{
	void *root = mt_root_locked(mt);

	rcu_assign_pointer(mt->ma_root, NULL);
	if (xa_is_node(root))
		mte_destroy_walk(root, mt);

	mt->ma_flags = 0;
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
 * @max: The maximum value to check
 *
 * Handles locking.  @index will be incremented to one beyond the range.
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

	while (mas_searchable(&mas) && (mas.index < max)) {
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
		if ((*index) && (*index) <= copy)
			pr_err("index not increased! %lx <= %lx\n",
			       *index, copy);
		MT_BUG_ON(mt, (*index) && ((*index) <= copy));
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
 * Handles locking, detects wrapping on index == 0
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
extern void kmem_cache_set_non_kernel(struct kmem_cache *, unsigned int);
void mt_set_non_kernel(unsigned int val)
{
	kmem_cache_set_non_kernel(maple_node_cache, val);
}

extern unsigned long kmem_cache_get_alloc(struct kmem_cache *);
unsigned long mt_get_alloc_size(void)
{
	return kmem_cache_get_alloc(maple_node_cache);
}

extern void kmem_cache_zero_nr_tallocated(struct kmem_cache *);
void mt_zero_nr_tallocated(void)
{
	kmem_cache_zero_nr_tallocated(maple_node_cache);
}

extern unsigned int kmem_cache_nr_tallocated(struct kmem_cache *);
unsigned int mt_nr_tallocated(void)
{
	return kmem_cache_nr_tallocated(maple_node_cache);
}

extern unsigned int kmem_cache_nr_allocated(struct kmem_cache *);
unsigned int mt_nr_allocated(void)
{
	return kmem_cache_nr_allocated(maple_node_cache);
}

/*
 * mas_dead_node() - Check if the maple state is pointing to a dead node.
 * @mas: The maple state
 * @index: The index to restore in @mas.
 *
 * Used in test code.
 * Return: 1 if @mas has been reset to MAS_START, 0 otherwise.
 */
static inline int mas_dead_node(struct ma_state *mas, unsigned long index)
{
	if (unlikely(!mas_searchable(mas) || mas_is_start(mas)))
		return 0;

	if (likely(!mte_dead_node(mas->node)))
		return 0;

	mas_rewalk(mas, index);
	return 1;
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
	kmem_cache_shrink(maple_node_cache);

}
EXPORT_SYMBOL_GPL(mt_cache_shrink);

#endif /* not defined __KERNEL__ */
/*
 * mas_get_slot() - Get the entry in the maple state node stored at @offset.
 * @mas: The maple state
 * @offset: The offset into the slot array to fetch.
 *
 * Return: The entry stored at @offset.
 */
static inline struct maple_enode *mas_get_slot(struct ma_state *mas,
		unsigned char offset)
{
	return mas_slot(mas, ma_slots(mas_mn(mas), mte_node_type(mas->node)),
			offset);
}


/*
 * mas_first_entry() - Go the first leaf and find the first entry.
 * @mas: the maple state.
 * @limit: the maximum index to check.
 * @*r_start: Pointer to set to the range start.
 *
 * Sets mas->offset to the offset of the entry, r_start to the range minimum.
 *
 * Return: The first entry or MAS_NONE.
 */
static inline void *mas_first_entry(struct ma_state *mas, struct maple_node *mn,
		unsigned long limit, enum maple_type mt)

{
	unsigned long max;
	unsigned long *pivots;
	void __rcu **slots;
	void *entry = NULL;

	mas->index = mas->min;
	if (mas->index > limit)
		goto none;

	max = mas->max;
	mas->offset = 0;
	while (likely(!ma_is_leaf(mt))) {
		MT_BUG_ON(mas->tree, mte_dead_node(mas->node));
		slots = ma_slots(mn, mt);
		pivots = ma_pivots(mn, mt);
		max = pivots[0];
		entry = mas_slot(mas, slots, 0);
		if (unlikely(ma_dead_node(mn)))
			return NULL;
		mas->node = entry;
		mn = mas_mn(mas);
		mt = mte_node_type(mas->node);
	}
	MT_BUG_ON(mas->tree, mte_dead_node(mas->node));

	mas->max = max;
	slots = ma_slots(mn, mt);
	entry = mas_slot(mas, slots, 0);
	if (unlikely(ma_dead_node(mn)))
		return NULL;

	/* Slot 0 or 1 must be set */
	if (mas->index > limit)
		goto none;

	if (likely(entry))
		return entry;

	pivots = ma_pivots(mn, mt);
	mas->index = pivots[0] + 1;
	mas->offset = 1;
	entry = mas_slot(mas, slots, 1);
	if (unlikely(ma_dead_node(mn)))
		return NULL;

	if (mas->index > limit)
		goto none;

	if (likely(entry))
		return entry;

none:
	if (likely(!ma_dead_node(mn)))
		mas->node = MAS_NONE;
	return NULL;
}

/* Depth first search, post-order */
static void mas_dfs_postorder(struct ma_state *mas, unsigned long max)
{

	struct maple_enode *p = MAS_NONE, *mn = mas->node;
	unsigned long p_min, p_max;

	mas_next_node(mas, mas_mn(mas), max);
	if (!mas_is_none(mas))
		return;

	if (mte_is_root(mn))
		return;

	mas->node = mn;
	mas_ascend(mas);
	while (mas->node != MAS_NONE) {
		p = mas->node;
		p_min = mas->min;
		p_max = mas->max;
		mas_prev_node(mas, 0);
	}

	if (p == MAS_NONE)
		return;

	mas->node = p;
	mas->max = p_max;
	mas->min = p_min;
}

/* Tree validations */
static void mt_dump_node(const struct maple_tree *mt, void *entry,
		unsigned long min, unsigned long max, unsigned int depth);
static void mt_dump_range(unsigned long min, unsigned long max,
			  unsigned int depth)
{
	static const char spaces[] = "                                ";

	if (min == max)
		pr_info("%.*s%lu: ", depth * 2, spaces, min);
	else
		pr_info("%.*s%lu-%lu: ", depth * 2, spaces, min, max);
}

static void mt_dump_entry(void *entry, unsigned long min, unsigned long max,
			  unsigned int depth)
{
	mt_dump_range(min, max, depth);

	if (xa_is_value(entry))
		pr_cont("value %ld (0x%lx) [%p]\n", xa_to_value(entry),
				xa_to_value(entry), entry);
	else if (xa_is_zero(entry))
		pr_cont("zero (%ld)\n", xa_to_internal(entry));
	else if (mt_is_reserved(entry))
		pr_cont("UNKNOWN ENTRY (%p)\n", entry);
	else
		pr_cont("%p\n", entry);
}

static void mt_dump_range64(const struct maple_tree *mt, void *entry,
			unsigned long min, unsigned long max, unsigned int depth)
{
	struct maple_range_64 *node = &mte_to_node(entry)->mr64;
	bool leaf = mte_is_leaf(entry);
	unsigned long first = min;
	int i;

	pr_cont(" contents: ");
	for (i = 0; i < MAPLE_RANGE64_SLOTS - 1; i++)
		pr_cont("%p %lu ", node->slot[i], node->pivot[i]);
	pr_cont("%p\n", node->slot[i]);
	for (i = 0; i < MAPLE_RANGE64_SLOTS; i++) {
		unsigned long last = max;

		if (i < (MAPLE_RANGE64_SLOTS - 1))
			last = node->pivot[i];
		else if (!node->slot[i] && max != mt_node_max(entry))
			break;
		if (last == 0 && i > 0)
			break;
		if (leaf)
			mt_dump_entry(mt_slot(mt, node->slot, i),
					first, last, depth + 1);
		else if (node->slot[i])
			mt_dump_node(mt, mt_slot(mt, node->slot, i),
					first, last, depth + 1);

		if (last == max)
			break;
		if (last > max) {
			pr_err("node %p last (%lu) > max (%lu) at pivot %d!\n",
					node, last, max, i);
			break;
		}
		first = last + 1;
	}
}

static void mt_dump_arange64(const struct maple_tree *mt, void *entry,
			unsigned long min, unsigned long max, unsigned int depth)
{
	struct maple_arange_64 *node = &mte_to_node(entry)->ma64;
	bool leaf = mte_is_leaf(entry);
	unsigned long first = min;
	int i;

	pr_cont(" contents: ");
	for (i = 0; i < MAPLE_ARANGE64_SLOTS; i++)
		pr_cont("%lu ", node->gap[i]);
	pr_cont("| %02X %02X| ", node->meta.end, node->meta.gap);
	for (i = 0; i < MAPLE_ARANGE64_SLOTS - 1; i++)
		pr_cont("%p %lu ", node->slot[i], node->pivot[i]);
	pr_cont("%p\n", node->slot[i]);
	for (i = 0; i < MAPLE_ARANGE64_SLOTS; i++) {
		unsigned long last = max;

		if (i < (MAPLE_ARANGE64_SLOTS - 1))
			last = node->pivot[i];
		else if (!node->slot[i])
			break;
		if (last == 0 && i > 0)
			break;
		if (leaf)
			mt_dump_entry(mt_slot(mt, node->slot, i),
					first, last, depth + 1);
		else if (node->slot[i])
			mt_dump_node(mt, mt_slot(mt, node->slot, i),
					first, last, depth + 1);

		if (last == max)
			break;
		if (last > max) {
			pr_err("node %p last (%lu) > max (%lu) at pivot %d!\n",
					node, last, max, i);
			break;
		}
		first = last + 1;
	}
}

static void mt_dump_node(const struct maple_tree *mt, void *entry,
		unsigned long min, unsigned long max, unsigned int depth)
{
	struct maple_node *node = mte_to_node(entry);
	unsigned int type = mte_node_type(entry);
	unsigned int i;

	mt_dump_range(min, max, depth);

	pr_cont("node %p depth %d type %d parent %p", node, depth, type,
			node ? node->parent : NULL);
	switch (type) {
	case maple_dense:
		pr_cont("\n");
		for (i = 0; i < MAPLE_NODE_SLOTS; i++) {
			if (min + i > max)
				pr_cont("OUT OF RANGE: ");
			mt_dump_entry(mt_slot(mt, node->slot, i),
					min + i, min + i, depth);
		}
		break;
	case maple_leaf_64:
	case maple_range_64:
		mt_dump_range64(mt, entry, min, max, depth);
		break;
	case maple_arange_64:
		mt_dump_arange64(mt, entry, min, max, depth);
		break;

	default:
		pr_cont(" UNKNOWN TYPE\n");
	}
}

void mt_dump(const struct maple_tree *mt)
{
	void *entry = rcu_dereference_check(mt->ma_root, mt_locked(mt));

	pr_info("maple_tree(%p) flags %X, height %u root %p\n",
		 mt, mt->ma_flags, mt_height(mt), entry);
	if (!xa_is_node(entry))
		mt_dump_entry(entry, 0, 0, 0);
	else if (entry)
		mt_dump_node(mt, entry, 0, mt_node_max(entry), 0);
}
EXPORT_SYMBOL_GPL(mt_dump);

/*
 * Calculate the maximum gap in a node and check if that's what is reported in
 * the parent (unless root).
 */
static void mas_validate_gaps(struct ma_state *mas)
{
	struct maple_enode *mte = mas->node;
	struct maple_node *p_mn;
	unsigned long gap = 0, max_gap = 0;
	unsigned long p_end, p_start = mas->min;
	unsigned char p_slot;
	unsigned long *gaps = NULL;
	unsigned long *pivots = ma_pivots(mte_to_node(mte), mte_node_type(mte));
	int i;

	if (ma_is_dense(mte_node_type(mte))) {
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

	gaps = ma_gaps(mte_to_node(mte), mte_node_type(mte));
	for (i = 0; i < mt_slot_count(mte); i++) {
		p_end = mas_logical_pivot(mas, pivots, i, mte_node_type(mte));

		if (!gaps) {
			if (mas_get_slot(mas, i)) {
				gap = 0;
				goto not_empty;
			}

			gap += p_end - p_start + 1;
		} else {
			void *entry = mas_get_slot(mas, i);

			gap = gaps[i];
			if (!entry) {
				if (gap != p_end - p_start + 1) {
					pr_err("%p[%u] -> %p %lu != %lu - %lu + 1\n",
						mas_mn(mas), i,
						mas_get_slot(mas, i), gap,
						p_end, p_start);
					mt_dump(mas->tree);

					MT_BUG_ON(mas->tree,
						gap != p_end - p_start + 1);
				}
			} else {
				if (gap > p_end - p_start + 1) {
					pr_err("%p[%u] %lu >= %lu - %lu + 1 (%lu)\n",
					mas_mn(mas), i, gap, p_end, p_start,
					p_end - p_start + 1);
					MT_BUG_ON(mas->tree,
						gap > p_end - p_start + 1);
				}
			}
		}

		if (gap > max_gap)
			max_gap = gap;
not_empty:
		p_start = p_end + 1;
		if (p_end >= mas->max)
			break;
	}

counted:
	if (mte_is_root(mte))
		return;

	p_slot = mte_parent_slot(mas->node);
	p_mn = mte_parent(mte);
	MT_BUG_ON(mas->tree, max_gap > mas->max);
	if (ma_gaps(p_mn, mas_parent_enum(mas, mte))[p_slot] != max_gap) {
		pr_err("gap %p[%u] != %lu\n", p_mn, p_slot, max_gap);
		mt_dump(mas->tree);
	}

	MT_BUG_ON(mas->tree,
		  ma_gaps(p_mn, mas_parent_enum(mas, mte))[p_slot] != max_gap);
}

static void mas_validate_parent_slot(struct ma_state *mas)
{
	struct maple_node *parent;
	struct maple_enode *node;
	enum maple_type p_type = mas_parent_enum(mas, mas->node);
	unsigned char p_slot = mte_parent_slot(mas->node);
	void __rcu **slots;
	int i;

	if (mte_is_root(mas->node))
		return;

	parent = mte_parent(mas->node);
	slots = ma_slots(parent, p_type);
	MT_BUG_ON(mas->tree, mas_mn(mas) == parent);

	/* Check prev/next parent slot for duplicate node entry */

	for (i = 0; i < mt_slots[p_type]; i++) {
		node = mas_slot(mas, slots, i);
		if (i == p_slot) {
			if (node != mas->node)
				pr_err("parent %p[%u] does not have %p\n",
					parent, i, mas_mn(mas));
			MT_BUG_ON(mas->tree, node != mas->node);
		} else if (node == mas->node) {
			pr_err("Invalid child %p at parent %p[%u] p_slot %u\n",
			       mas_mn(mas), parent, i, p_slot);
			MT_BUG_ON(mas->tree, node == mas->node);
		}
	}
}

static void mas_validate_child_slot(struct ma_state *mas)
{
	enum maple_type type = mte_node_type(mas->node);
	void __rcu **slots = ma_slots(mte_to_node(mas->node), type);
	unsigned long *pivots = ma_pivots(mte_to_node(mas->node), type);
	struct maple_enode *child;
	unsigned char i;

	if (mte_is_leaf(mas->node))
		return;

	for (i = 0; i < mt_slots[type]; i++) {
		child = mas_slot(mas, slots, i);
		if (!pivots[i] || pivots[i] == mas->max)
			break;

		if (!child)
			break;

		if (mte_parent_slot(child) != i) {
			pr_err("Slot error at %p[%u]: child %p has pslot %u\n",
			       mas_mn(mas), i, mte_to_node(child),
			       mte_parent_slot(child));
			MT_BUG_ON(mas->tree, 1);
		}

		if (mte_parent(child) != mte_to_node(mas->node)) {
			pr_err("child %p has parent %p not %p\n",
			       mte_to_node(child), mte_parent(child),
			       mte_to_node(mas->node));
			MT_BUG_ON(mas->tree, 1);
		}
	}
}

/*
 * Validate all pivots are within mas->min and mas->max.
 */
static void mas_validate_limits(struct ma_state *mas)
{
	int i;
	unsigned long prev_piv = 0;
	enum maple_type type = mte_node_type(mas->node);
	void __rcu **slots = ma_slots(mte_to_node(mas->node), type);
	unsigned long *pivots = ma_pivots(mas_mn(mas), type);

	/* all limits are fine here. */
	if (mte_is_root(mas->node))
		return;

	for (i = 0; i < mt_slots[type]; i++) {
		unsigned long piv;

		piv = mas_safe_pivot(mas, pivots, i, type);

		if (!piv && (i != 0))
			break;

		if (!mte_is_leaf(mas->node)) {
			void *entry = mas_slot(mas, slots, i);

			if (!entry)
				pr_err("%p[%u] cannot be null\n",
				       mas_mn(mas), i);

			MT_BUG_ON(mas->tree, !entry);
		}

		if (prev_piv > piv) {
			pr_err("%p[%u] piv %lu < prev_piv %lu\n",
				mas_mn(mas), i, piv, prev_piv);
			MT_BUG_ON(mas->tree, piv < prev_piv);
		}

		if (piv < mas->min) {
			pr_err("%p[%u] %lu < %lu\n", mas_mn(mas), i,
				piv, mas->min);
			MT_BUG_ON(mas->tree, piv < mas->min);
		}
		if (piv > mas->max) {
			pr_err("%p[%u] %lu > %lu\n", mas_mn(mas), i,
				piv, mas->max);
			MT_BUG_ON(mas->tree, piv > mas->max);
		}
		prev_piv = piv;
		if (piv == mas->max)
			break;
	}
	for (i += 1; i < mt_slots[type]; i++) {
		void *entry = mas_slot(mas, slots, i);

		if (entry && (i != mt_slots[type] - 1)) {
			pr_err("%p[%u] should not have entry %p\n", mas_mn(mas),
			       i, entry);
			MT_BUG_ON(mas->tree, entry != NULL);
		}

		if (i < mt_pivots[type]) {
			unsigned long piv = pivots[i];

			if (!piv)
				continue;

			pr_err("%p[%u] should not have piv %lu\n",
			       mas_mn(mas), i, piv);
			MT_BUG_ON(mas->tree, i < mt_pivots[type] - 1);
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
	if (mas_is_none(&mas) || (mas.node == MAS_ROOT))
		return;

	while (!mte_is_leaf(mas.node))
		mas_descend(&mas);

	slots = ma_slots(mte_to_node(mas.node), mte_node_type(mas.node));
	do {
		entry = mas_slot(&mas, slots, offset);
		if (!last && !entry) {
			pr_err("Sequential nulls end at %p[%u]\n",
				mas_mn(&mas), offset);
		}
		MT_BUG_ON(mt, !last && !entry);
		last = entry;
		if (offset == mas_data_end(&mas)) {
			mas_next_node(&mas, mas_mn(&mas), ULONG_MAX);
			if (mas_is_none(&mas))
				return;
			offset = 0;
			slots = ma_slots(mte_to_node(mas.node),
					 mte_node_type(mas.node));
		} else {
			offset++;
		}

	} while (!mas_is_none(&mas));
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
	if (!mas_searchable(&mas))
		goto done;

	mas_first_entry(&mas, mas_mn(&mas), ULONG_MAX, mte_node_type(mas.node));
	while (!mas_is_none(&mas)) {
		MT_BUG_ON(mas.tree, mte_dead_node(mas.node));
		if (!mte_is_root(mas.node)) {
			end = mas_data_end(&mas);
			if ((end < mt_min_slot_count(mas.node)) &&
			    (mas.max != ULONG_MAX)) {
				pr_err("Invalid size %u of %p\n", end,
				mas_mn(&mas));
				MT_BUG_ON(mas.tree, 1);
			}

		}
		mas_validate_parent_slot(&mas);
		mas_validate_child_slot(&mas);
		mas_validate_limits(&mas);
		if (mt_is_alloc(mt))
			mas_validate_gaps(&mas);
		mas_dfs_postorder(&mas, ULONG_MAX);
	}
	mt_validate_nulls(mt);
done:
	rcu_read_unlock();

}
EXPORT_SYMBOL_GPL(mt_validate);

#endif /* CONFIG_DEBUG_MAPLE_TREE */
