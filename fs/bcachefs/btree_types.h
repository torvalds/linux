/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_TYPES_H
#define _BCACHEFS_BTREE_TYPES_H

#include <linux/list.h>
#include <linux/rhashtable.h>

#include "bkey_methods.h"
#include "journal_types.h"
#include "six.h"

struct open_bucket;
struct btree_update;

#define MAX_BSETS		3U

struct btree_nr_keys {

	/*
	 * Amount of live metadata (i.e. size of node after a compaction) in
	 * units of u64s
	 */
	u16			live_u64s;
	u16			bset_u64s[MAX_BSETS];

	/* live keys only: */
	u16			packed_keys;
	u16			unpacked_keys;
};

struct bset_tree {
	/*
	 * We construct a binary tree in an array as if the array
	 * started at 1, so that things line up on the same cachelines
	 * better: see comments in bset.c at cacheline_to_bkey() for
	 * details
	 */

	/* size of the binary tree and prev array */
	u16			size;

	/* function of size - precalculated for to_inorder() */
	u16			extra;

	u16			data_offset;
	u16			aux_data_offset;
	u16			end_offset;

	struct bpos		max_key;
};

struct btree_write {
	struct journal_entry_pin	journal;
	struct closure_waitlist		wait;
};

struct btree_alloc {
	struct open_buckets	ob;
	BKEY_PADDED(k);
};

struct btree {
	/* Hottest entries first */
	struct rhash_head	hash;

	/* Key/pointer for this btree node */
	__BKEY_PADDED(key, BKEY_BTREE_PTR_VAL_U64s_MAX);

	struct six_lock		lock;

	unsigned long		flags;
	u16			written;
	u8			level;
	u8			btree_id;
	u8			nsets;
	u8			nr_key_bits;

	struct bkey_format	format;

	struct btree_node	*data;
	void			*aux_data;

	/*
	 * Sets of sorted keys - the real btree node - plus a binary search tree
	 *
	 * set[0] is special; set[0]->tree, set[0]->prev and set[0]->data point
	 * to the memory we have allocated for this btree node. Additionally,
	 * set[0]->data points to the entire btree node as it exists on disk.
	 */
	struct bset_tree	set[MAX_BSETS];

	struct btree_nr_keys	nr;
	u16			sib_u64s[2];
	u16			whiteout_u64s;
	u16			uncompacted_whiteout_u64s;
	u8			page_order;
	u8			unpack_fn_len;

	/*
	 * XXX: add a delete sequence number, so when bch2_btree_node_relock()
	 * fails because the lock sequence number has changed - i.e. the
	 * contents were modified - we can still relock the node if it's still
	 * the one we want, without redoing the traversal
	 */

	/*
	 * For asynchronous splits/interior node updates:
	 * When we do a split, we allocate new child nodes and update the parent
	 * node to point to them: we update the parent in memory immediately,
	 * but then we must wait until the children have been written out before
	 * the update to the parent can be written - this is a list of the
	 * btree_updates that are blocking this node from being
	 * written:
	 */
	struct list_head	write_blocked;

	/*
	 * Also for asynchronous splits/interior node updates:
	 * If a btree node isn't reachable yet, we don't want to kick off
	 * another write - because that write also won't yet be reachable and
	 * marking it as completed before it's reachable would be incorrect:
	 */
	unsigned long		will_make_reachable;

	struct open_buckets	ob;

	/* lru list */
	struct list_head	list;

	struct btree_write	writes[2];

#ifdef CONFIG_BCACHEFS_DEBUG
	bool			*expensive_debug_checks;
#endif
};

struct btree_cache {
	struct rhashtable	table;
	bool			table_init_done;
	/*
	 * We never free a struct btree, except on shutdown - we just put it on
	 * the btree_cache_freed list and reuse it later. This simplifies the
	 * code, and it doesn't cost us much memory as the memory usage is
	 * dominated by buffers that hold the actual btree node data and those
	 * can be freed - and the number of struct btrees allocated is
	 * effectively bounded.
	 *
	 * btree_cache_freeable effectively is a small cache - we use it because
	 * high order page allocations can be rather expensive, and it's quite
	 * common to delete and allocate btree nodes in quick succession. It
	 * should never grow past ~2-3 nodes in practice.
	 */
	struct mutex		lock;
	struct list_head	live;
	struct list_head	freeable;
	struct list_head	freed;

	/* Number of elements in live + freeable lists */
	unsigned		used;
	unsigned		reserve;
	struct shrinker		shrink;

	/*
	 * If we need to allocate memory for a new btree node and that
	 * allocation fails, we can cannibalize another node in the btree cache
	 * to satisfy the allocation - lock to guarantee only one thread does
	 * this at a time:
	 */
	struct task_struct	*alloc_lock;
	struct closure_waitlist	alloc_wait;
};

struct btree_node_iter {
	struct btree_node_iter_set {
		u16	k, end;
	} data[MAX_BSETS];
};

enum btree_iter_type {
	BTREE_ITER_KEYS,
	BTREE_ITER_SLOTS,
	BTREE_ITER_NODES,
};

#define BTREE_ITER_TYPE			((1 << 2) - 1)

#define BTREE_ITER_INTENT		(1 << 2)
#define BTREE_ITER_PREFETCH		(1 << 3)
/*
 * Used in bch2_btree_iter_traverse(), to indicate whether we're searching for
 * @pos or the first key strictly greater than @pos
 */
#define BTREE_ITER_IS_EXTENTS		(1 << 4)
#define BTREE_ITER_ERROR		(1 << 5)

enum btree_iter_uptodate {
	BTREE_ITER_UPTODATE		= 0,
	BTREE_ITER_NEED_PEEK		= 1,
	BTREE_ITER_NEED_RELOCK		= 2,
	BTREE_ITER_NEED_TRAVERSE	= 3,
};

/*
 * @pos			- iterator's current position
 * @level		- current btree depth
 * @locks_want		- btree level below which we start taking intent locks
 * @nodes_locked	- bitmask indicating which nodes in @nodes are locked
 * @nodes_intent_locked	- bitmask indicating which locks are intent locks
 */
struct btree_iter {
	struct bch_fs		*c;
	struct bpos		pos;

	u8			flags;
	enum btree_iter_uptodate uptodate:4;
	enum btree_id		btree_id:4;
	unsigned		level:4,
				locks_want:4,
				nodes_locked:4,
				nodes_intent_locked:4;

	struct btree_iter_level {
		struct btree	*b;
		struct btree_node_iter iter;
		u32		lock_seq;
	}			l[BTREE_MAX_DEPTH];

	/*
	 * Current unpacked key - so that bch2_btree_iter_next()/
	 * bch2_btree_iter_next_slot() can correctly advance pos.
	 */
	struct bkey		k;

	/*
	 * Circular linked list of linked iterators: linked iterators share
	 * locks (e.g. two linked iterators may have the same node intent
	 * locked, or read and write locked, at the same time), and insertions
	 * through one iterator won't invalidate the other linked iterators.
	 */

	/* Must come last: */
	struct btree_iter	*next;
};

#define BTREE_ITER_MAX		8

struct btree_insert_entry {
	struct btree_iter *iter;
	struct bkey_i	*k;
};

struct btree_trans {
	struct bch_fs		*c;
	size_t			nr_restarts;

	u8			nr_iters;
	u8			iters_live;
	u8			iters_linked;
	u8			nr_updates;

	unsigned		mem_top;
	unsigned		mem_bytes;
	void			*mem;

	struct btree_iter	*iters;
	u64			iter_ids[BTREE_ITER_MAX];

	struct btree_insert_entry updates[BTREE_ITER_MAX];

	struct btree_iter	iters_onstack[2];
};

#define BTREE_FLAG(flag)						\
static inline bool btree_node_ ## flag(struct btree *b)			\
{	return test_bit(BTREE_NODE_ ## flag, &b->flags); }		\
									\
static inline void set_btree_node_ ## flag(struct btree *b)		\
{	set_bit(BTREE_NODE_ ## flag, &b->flags); }			\
									\
static inline void clear_btree_node_ ## flag(struct btree *b)		\
{	clear_bit(BTREE_NODE_ ## flag, &b->flags); }

enum btree_flags {
	BTREE_NODE_read_in_flight,
	BTREE_NODE_read_error,
	BTREE_NODE_dirty,
	BTREE_NODE_need_write,
	BTREE_NODE_noevict,
	BTREE_NODE_write_idx,
	BTREE_NODE_accessed,
	BTREE_NODE_write_in_flight,
	BTREE_NODE_just_written,
	BTREE_NODE_dying,
	BTREE_NODE_fake,
};

BTREE_FLAG(read_in_flight);
BTREE_FLAG(read_error);
BTREE_FLAG(dirty);
BTREE_FLAG(need_write);
BTREE_FLAG(noevict);
BTREE_FLAG(write_idx);
BTREE_FLAG(accessed);
BTREE_FLAG(write_in_flight);
BTREE_FLAG(just_written);
BTREE_FLAG(dying);
BTREE_FLAG(fake);

static inline struct btree_write *btree_current_write(struct btree *b)
{
	return b->writes + btree_node_write_idx(b);
}

static inline struct btree_write *btree_prev_write(struct btree *b)
{
	return b->writes + (btree_node_write_idx(b) ^ 1);
}

static inline struct bset_tree *bset_tree_last(struct btree *b)
{
	EBUG_ON(!b->nsets);
	return b->set + b->nsets - 1;
}

static inline void *
__btree_node_offset_to_ptr(const struct btree *b, u16 offset)
{
	return (void *) ((u64 *) b->data + 1 + offset);
}

static inline u16
__btree_node_ptr_to_offset(const struct btree *b, const void *p)
{
	u16 ret = (u64 *) p - 1 - (u64 *) b->data;

	EBUG_ON(__btree_node_offset_to_ptr(b, ret) != p);
	return ret;
}

static inline struct bset *bset(const struct btree *b,
				const struct bset_tree *t)
{
	return __btree_node_offset_to_ptr(b, t->data_offset);
}

static inline void set_btree_bset_end(struct btree *b, struct bset_tree *t)
{
	t->end_offset =
		__btree_node_ptr_to_offset(b, vstruct_last(bset(b, t)));
}

static inline void set_btree_bset(struct btree *b, struct bset_tree *t,
				  const struct bset *i)
{
	t->data_offset = __btree_node_ptr_to_offset(b, i);
	set_btree_bset_end(b, t);
}

static inline struct bset *btree_bset_first(struct btree *b)
{
	return bset(b, b->set);
}

static inline struct bset *btree_bset_last(struct btree *b)
{
	return bset(b, bset_tree_last(b));
}

static inline u16
__btree_node_key_to_offset(const struct btree *b, const struct bkey_packed *k)
{
	return __btree_node_ptr_to_offset(b, k);
}

static inline struct bkey_packed *
__btree_node_offset_to_key(const struct btree *b, u16 k)
{
	return __btree_node_offset_to_ptr(b, k);
}

static inline unsigned btree_bkey_first_offset(const struct bset_tree *t)
{
	return t->data_offset + offsetof(struct bset, _data) / sizeof(u64);
}

#define btree_bkey_first(_b, _t)					\
({									\
	EBUG_ON(bset(_b, _t)->start !=					\
		__btree_node_offset_to_key(_b, btree_bkey_first_offset(_t)));\
									\
	bset(_b, _t)->start;						\
})

#define btree_bkey_last(_b, _t)						\
({									\
	EBUG_ON(__btree_node_offset_to_key(_b, (_t)->end_offset) !=	\
		vstruct_last(bset(_b, _t)));				\
									\
	__btree_node_offset_to_key(_b, (_t)->end_offset);		\
})

static inline unsigned bset_byte_offset(struct btree *b, void *i)
{
	return i - (void *) b->data;
}

/* Type of keys @b contains: */
static inline enum bkey_type btree_node_type(struct btree *b)
{
	return b->level ? BKEY_TYPE_BTREE : b->btree_id;
}

static inline const struct bkey_ops *btree_node_ops(struct btree *b)
{
	return &bch2_bkey_ops[btree_node_type(b)];
}

static inline bool btree_node_has_ptrs(struct btree *b)
{
	return btree_type_has_ptrs(btree_node_type(b));
}

static inline bool btree_node_is_extents(struct btree *b)
{
	return btree_node_type(b) == BKEY_TYPE_EXTENTS;
}

struct btree_root {
	struct btree		*b;

	struct btree_update	*as;

	/* On disk root - see async splits: */
	__BKEY_PADDED(key, BKEY_BTREE_PTR_VAL_U64s_MAX);
	u8			level;
	u8			alive;
};

/*
 * Optional hook that will be called just prior to a btree node update, when
 * we're holding the write lock and we know what key is about to be overwritten:
 */

enum btree_insert_ret {
	BTREE_INSERT_OK,
	/* extent spanned multiple leaf nodes: have to traverse to next node: */
	BTREE_INSERT_NEED_TRAVERSE,
	/* write lock held for too long */
	/* leaf node needs to be split */
	BTREE_INSERT_BTREE_NODE_FULL,
	BTREE_INSERT_ENOSPC,
	BTREE_INSERT_NEED_GC_LOCK,
};

enum btree_gc_coalesce_fail_reason {
	BTREE_GC_COALESCE_FAIL_RESERVE_GET,
	BTREE_GC_COALESCE_FAIL_KEYLIST_REALLOC,
	BTREE_GC_COALESCE_FAIL_FORMAT_FITS,
};

enum btree_node_sibling {
	btree_prev_sib,
	btree_next_sib,
};

typedef struct btree_nr_keys (*sort_fix_overlapping_fn)(struct bset *,
							struct btree *,
							struct btree_node_iter *);

#endif /* _BCACHEFS_BTREE_TYPES_H */
