/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_TYPES_H
#define _BCACHEFS_BTREE_TYPES_H

#include <linux/list.h>
#include <linux/rhashtable.h>

//#include "bkey_methods.h"
#include "buckets_types.h"
#include "darray.h"
#include "errcode.h"
#include "journal_types.h"
#include "replicas_types.h"
#include "six.h"

struct open_bucket;
struct btree_update;
struct btree_trans;

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
};

struct btree_write {
	struct journal_entry_pin	journal;
};

struct btree_alloc {
	struct open_buckets	ob;
	__BKEY_PADDED(k, BKEY_BTREE_PTR_VAL_U64s_MAX);
};

struct btree_bkey_cached_common {
	struct six_lock		lock;
	u8			level;
	u8			btree_id;
	bool			cached;
};

struct btree {
	struct btree_bkey_cached_common c;

	struct rhash_head	hash;
	u64			hash_val;

	unsigned long		flags;
	u16			written;
	u8			nsets;
	u8			nr_key_bits;
	u16			version_ondisk;

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
	u8			byte_order;
	u8			unpack_fn_len;

	struct btree_write	writes[2];

	/* Key/pointer for this btree node */
	__BKEY_PADDED(key, BKEY_BTREE_PTR_VAL_U64s_MAX);

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
	struct list_head	freed_pcpu;
	struct list_head	freed_nonpcpu;

	/* Number of elements in live + freeable lists */
	unsigned		used;
	unsigned		reserve;
	atomic_t		dirty;
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

/*
 * Iterate over all possible positions, synthesizing deleted keys for holes:
 */
static const u16 BTREE_ITER_SLOTS		= 1 << 0;
static const u16 BTREE_ITER_ALL_LEVELS		= 1 << 1;
/*
 * Indicates that intent locks should be taken on leaf nodes, because we expect
 * to be doing updates:
 */
static const u16 BTREE_ITER_INTENT		= 1 << 2;
/*
 * Causes the btree iterator code to prefetch additional btree nodes from disk:
 */
static const u16 BTREE_ITER_PREFETCH		= 1 << 3;
/*
 * Used in bch2_btree_iter_traverse(), to indicate whether we're searching for
 * @pos or the first key strictly greater than @pos
 */
static const u16 BTREE_ITER_IS_EXTENTS		= 1 << 4;
static const u16 BTREE_ITER_NOT_EXTENTS		= 1 << 5;
static const u16 BTREE_ITER_CACHED		= 1 << 6;
static const u16 BTREE_ITER_WITH_KEY_CACHE	= 1 << 7;
static const u16 BTREE_ITER_WITH_UPDATES	= 1 << 8;
static const u16 BTREE_ITER_WITH_JOURNAL	= 1 << 9;
static const u16 __BTREE_ITER_ALL_SNAPSHOTS	= 1 << 10;
static const u16 BTREE_ITER_ALL_SNAPSHOTS	= 1 << 11;
static const u16 BTREE_ITER_FILTER_SNAPSHOTS	= 1 << 12;
static const u16 BTREE_ITER_NOPRESERVE		= 1 << 13;
static const u16 BTREE_ITER_CACHED_NOFILL	= 1 << 14;
static const u16 BTREE_ITER_KEY_CACHE_FILL	= 1 << 15;
#define __BTREE_ITER_FLAGS_END			       16

enum btree_path_uptodate {
	BTREE_ITER_UPTODATE		= 0,
	BTREE_ITER_NEED_RELOCK		= 1,
	BTREE_ITER_NEED_TRAVERSE	= 2,
};

#if defined(CONFIG_BCACHEFS_LOCK_TIME_STATS) || defined(CONFIG_BCACHEFS_DEBUG)
#define TRACK_PATH_ALLOCATED
#endif

struct btree_path {
	u8			idx;
	u8			sorted_idx;
	u8			ref;
	u8			intent_ref;

	/* btree_iter_copy starts here: */
	struct bpos		pos;

	enum btree_id		btree_id:5;
	bool			cached:1;
	bool			preserve:1;
	enum btree_path_uptodate uptodate:2;
	/*
	 * When true, failing to relock this path will cause the transaction to
	 * restart:
	 */
	bool			should_be_locked:1;
	unsigned		level:3,
				locks_want:3;
	u8			nodes_locked;

	struct btree_path_level {
		struct btree	*b;
		struct btree_node_iter iter;
		u32		lock_seq;
#ifdef CONFIG_BCACHEFS_LOCK_TIME_STATS
		u64             lock_taken_time;
#endif
	}			l[BTREE_MAX_DEPTH];
#ifdef TRACK_PATH_ALLOCATED
	unsigned long		ip_allocated;
#endif
};

static inline struct btree_path_level *path_l(struct btree_path *path)
{
	return path->l + path->level;
}

static inline unsigned long btree_path_ip_allocated(struct btree_path *path)
{
#ifdef TRACK_PATH_ALLOCATED
	return path->ip_allocated;
#else
	return _THIS_IP_;
#endif
}

/*
 * @pos			- iterator's current position
 * @level		- current btree depth
 * @locks_want		- btree level below which we start taking intent locks
 * @nodes_locked	- bitmask indicating which nodes in @nodes are locked
 * @nodes_intent_locked	- bitmask indicating which locks are intent locks
 */
struct btree_iter {
	struct btree_trans	*trans;
	struct btree_path	*path;
	struct btree_path	*update_path;
	struct btree_path	*key_cache_path;

	enum btree_id		btree_id:8;
	unsigned		min_depth:3;
	unsigned		advanced:1;

	/* btree_iter_copy starts here: */
	u16			flags;

	/* When we're filtering by snapshot, the snapshot ID we're looking for: */
	unsigned		snapshot;

	struct bpos		pos;
	/*
	 * Current unpacked key - so that bch2_btree_iter_next()/
	 * bch2_btree_iter_next_slot() can correctly advance pos.
	 */
	struct bkey		k;

	/* BTREE_ITER_WITH_JOURNAL: */
	size_t			journal_idx;
	struct bpos		journal_pos;
#ifdef TRACK_PATH_ALLOCATED
	unsigned long		ip_allocated;
#endif
};

struct btree_key_cache_freelist {
	struct bkey_cached	*objs[16];
	unsigned		nr;
};

struct btree_key_cache {
	struct mutex		lock;
	struct rhashtable	table;
	bool			table_init_done;
	struct list_head	freed_pcpu;
	struct list_head	freed_nonpcpu;
	struct shrinker		shrink;
	unsigned		shrink_iter;
	struct btree_key_cache_freelist __percpu *pcpu_freed;

	atomic_long_t		nr_freed;
	atomic_long_t		nr_keys;
	atomic_long_t		nr_dirty;
};

struct bkey_cached_key {
	u32			btree_id;
	struct bpos		pos;
} __packed __aligned(4);

#define BKEY_CACHED_ACCESSED		0
#define BKEY_CACHED_DIRTY		1

struct bkey_cached {
	struct btree_bkey_cached_common c;

	unsigned long		flags;
	u16			u64s;
	bool			valid;
	u32			btree_trans_barrier_seq;
	struct bkey_cached_key	key;

	struct rhash_head	hash;
	struct list_head	list;

	struct journal_preres	res;
	struct journal_entry_pin journal;
	u64			seq;

	struct bkey_i		*k;
};

static inline struct bpos btree_node_pos(struct btree_bkey_cached_common *b)
{
	return !b->cached
		? container_of(b, struct btree, c)->key.k.p
		: container_of(b, struct bkey_cached, c)->key.pos;
}

struct btree_insert_entry {
	unsigned		flags;
	u8			bkey_type;
	enum btree_id		btree_id:8;
	u8			level:4;
	bool			cached:1;
	bool			insert_trigger_run:1;
	bool			overwrite_trigger_run:1;
	bool			key_cache_already_flushed:1;
	/*
	 * @old_k may be a key from the journal; @old_btree_u64s always refers
	 * to the size of the key being overwritten in the btree:
	 */
	u8			old_btree_u64s;
	struct bkey_i		*k;
	struct btree_path	*path;
	u64			seq;
	/* key being overwritten: */
	struct bkey		old_k;
	const struct bch_val	*old_v;
	unsigned long		ip_allocated;
};

#ifndef CONFIG_LOCKDEP
#define BTREE_ITER_MAX		64
#else
#define BTREE_ITER_MAX		32
#endif

struct btree_trans_commit_hook;
typedef int (btree_trans_commit_hook_fn)(struct btree_trans *, struct btree_trans_commit_hook *);

struct btree_trans_commit_hook {
	btree_trans_commit_hook_fn	*fn;
	struct btree_trans_commit_hook	*next;
};

#define BTREE_TRANS_MEM_MAX	(1U << 16)

#define BTREE_TRANS_MAX_LOCK_HOLD_TIME_NS	10000

struct btree_trans {
	struct bch_fs		*c;
	const char		*fn;
	struct closure		ref;
	struct list_head	list;
	u64			last_begin_time;

	u8			lock_may_not_fail;
	u8			lock_must_abort;
	struct btree_bkey_cached_common *locking;
	struct six_lock_waiter	locking_wait;

	int			srcu_idx;

	u8			fn_idx;
	u8			nr_sorted;
	u8			nr_updates;
	u8			nr_wb_updates;
	u8			wb_updates_size;
	bool			used_mempool:1;
	bool			in_traverse_all:1;
	bool			paths_sorted:1;
	bool			memory_allocation_failure:1;
	bool			journal_transaction_names:1;
	bool			journal_replay_not_finished:1;
	bool			notrace_relock_fail:1;
	enum bch_errcode	restarted:16;
	u32			restart_count;
	unsigned long		last_begin_ip;
	unsigned long		last_restarted_ip;
	unsigned long		srcu_lock_time;

	/*
	 * For when bch2_trans_update notices we'll be splitting a compressed
	 * extent:
	 */
	unsigned		extra_journal_res;
	unsigned		nr_max_paths;

	u64			paths_allocated;

	unsigned		mem_top;
	unsigned		mem_max;
	unsigned		mem_bytes;
	void			*mem;

	u8			sorted[BTREE_ITER_MAX + 8];
	struct btree_path	*paths;
	struct btree_insert_entry *updates;
	struct btree_write_buffered_key *wb_updates;

	/* update path: */
	struct btree_trans_commit_hook *hooks;
	darray_u64		extra_journal_entries;
	struct journal_entry_pin *journal_pin;

	struct journal_res	journal_res;
	struct journal_preres	journal_preres;
	u64			*journal_seq;
	struct disk_reservation *disk_res;
	unsigned		journal_u64s;
	unsigned		journal_preres_u64s;
	struct replicas_delta_list *fs_usage_deltas;
};

#define BCH_BTREE_WRITE_TYPES()						\
	x(initial,		0)					\
	x(init_next_bset,	1)					\
	x(cache_reclaim,	2)					\
	x(journal_reclaim,	3)					\
	x(interior,		4)

enum btree_write_type {
#define x(t, n) BTREE_WRITE_##t,
	BCH_BTREE_WRITE_TYPES()
#undef x
	BTREE_WRITE_TYPE_NR,
};

#define BTREE_WRITE_TYPE_MASK	(roundup_pow_of_two(BTREE_WRITE_TYPE_NR) - 1)
#define BTREE_WRITE_TYPE_BITS	ilog2(roundup_pow_of_two(BTREE_WRITE_TYPE_NR))

#define BTREE_FLAGS()							\
	x(read_in_flight)						\
	x(read_error)							\
	x(dirty)							\
	x(need_write)							\
	x(write_blocked)						\
	x(will_make_reachable)						\
	x(noevict)							\
	x(write_idx)							\
	x(accessed)							\
	x(write_in_flight)						\
	x(write_in_flight_inner)					\
	x(just_written)							\
	x(dying)							\
	x(fake)								\
	x(need_rewrite)							\
	x(never_write)

enum btree_flags {
	/* First bits for btree node write type */
	BTREE_NODE_FLAGS_START = BTREE_WRITE_TYPE_BITS - 1,
#define x(flag)	BTREE_NODE_##flag,
	BTREE_FLAGS()
#undef x
};

#define x(flag)								\
static inline bool btree_node_ ## flag(struct btree *b)			\
{	return test_bit(BTREE_NODE_ ## flag, &b->flags); }		\
									\
static inline void set_btree_node_ ## flag(struct btree *b)		\
{	set_bit(BTREE_NODE_ ## flag, &b->flags); }			\
									\
static inline void clear_btree_node_ ## flag(struct btree *b)		\
{	clear_bit(BTREE_NODE_ ## flag, &b->flags); }

BTREE_FLAGS()
#undef x

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

static inline unsigned bset_u64s(struct bset_tree *t)
{
	return t->end_offset - t->data_offset -
		sizeof(struct bset) / sizeof(u64);
}

static inline unsigned bset_dead_u64s(struct btree *b, struct bset_tree *t)
{
	return bset_u64s(t) - b->nr.bset_u64s[t - b->set];
}

static inline unsigned bset_byte_offset(struct btree *b, void *i)
{
	return i - (void *) b->data;
}

enum btree_node_type {
#define x(kwd, val) BKEY_TYPE_##kwd = val,
	BCH_BTREE_IDS()
#undef x
	BKEY_TYPE_btree,
};

/* Type of a key in btree @id at level @level: */
static inline enum btree_node_type __btree_node_type(unsigned level, enum btree_id id)
{
	return level ? BKEY_TYPE_btree : (enum btree_node_type) id;
}

/* Type of keys @b contains: */
static inline enum btree_node_type btree_node_type(struct btree *b)
{
	return __btree_node_type(b->c.level, b->c.btree_id);
}

#define BTREE_NODE_TYPE_HAS_TRANS_TRIGGERS		\
	((1U << BKEY_TYPE_extents)|			\
	 (1U << BKEY_TYPE_alloc)|			\
	 (1U << BKEY_TYPE_inodes)|			\
	 (1U << BKEY_TYPE_stripes)|			\
	 (1U << BKEY_TYPE_reflink)|			\
	 (1U << BKEY_TYPE_btree))

#define BTREE_NODE_TYPE_HAS_MEM_TRIGGERS		\
	((1U << BKEY_TYPE_alloc)|			\
	 (1U << BKEY_TYPE_inodes)|			\
	 (1U << BKEY_TYPE_stripes)|			\
	 (1U << BKEY_TYPE_snapshots))

#define BTREE_NODE_TYPE_HAS_TRIGGERS			\
	(BTREE_NODE_TYPE_HAS_TRANS_TRIGGERS|		\
	 BTREE_NODE_TYPE_HAS_MEM_TRIGGERS)

#define BTREE_ID_IS_EXTENTS				\
	((1U << BTREE_ID_extents)|			\
	 (1U << BTREE_ID_reflink)|			\
	 (1U << BTREE_ID_freespace))

static inline bool btree_node_type_is_extents(enum btree_node_type type)
{
	return (1U << type) & BTREE_ID_IS_EXTENTS;
}

static inline bool btree_id_is_extents(enum btree_id btree)
{
	return btree_node_type_is_extents((enum btree_node_type) btree);
}

#define BTREE_ID_HAS_SNAPSHOTS				\
	((1U << BTREE_ID_extents)|			\
	 (1U << BTREE_ID_inodes)|			\
	 (1U << BTREE_ID_dirents)|			\
	 (1U << BTREE_ID_xattrs))

#define BTREE_ID_HAS_PTRS				\
	((1U << BTREE_ID_extents)|			\
	 (1U << BTREE_ID_reflink))

static inline bool btree_type_has_snapshots(enum btree_id id)
{
	return (1 << id) & BTREE_ID_HAS_SNAPSHOTS;
}

static inline bool btree_type_has_ptrs(enum btree_id id)
{
	return (1 << id) & BTREE_ID_HAS_PTRS;
}

static inline bool btree_node_type_needs_gc(enum btree_node_type type)
{
	return BTREE_NODE_TYPE_HAS_TRIGGERS & (1U << type);
}

struct btree_root {
	struct btree		*b;

	/* On disk root - see async splits: */
	__BKEY_PADDED(key, BKEY_BTREE_PTR_VAL_U64s_MAX);
	u8			level;
	u8			alive;
	s8			error;
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

#endif /* _BCACHEFS_BTREE_TYPES_H */
