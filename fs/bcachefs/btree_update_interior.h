/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_UPDATE_INTERIOR_H
#define _BCACHEFS_BTREE_UPDATE_INTERIOR_H

#include "btree_cache.h"
#include "btree_locking.h"
#include "btree_update.h"

#define BTREE_UPDATE_NODES_MAX		((BTREE_MAX_DEPTH - 2) * 2 + GC_MERGE_NODES)

#define BTREE_UPDATE_JOURNAL_RES	(BTREE_UPDATE_NODES_MAX * (BKEY_BTREE_PTR_U64s_MAX + 1))

int bch2_btree_node_check_topology(struct btree_trans *, struct btree *);

#define BTREE_UPDATE_MODES()	\
	x(none)			\
	x(node)			\
	x(root)			\
	x(update)

enum btree_update_mode {
#define x(n)	BTREE_UPDATE_##n,
	BTREE_UPDATE_MODES()
#undef x
};

/*
 * Tracks an in progress split/rewrite of a btree node and the update to the
 * parent node:
 *
 * When we split/rewrite a node, we do all the updates in memory without
 * waiting for any writes to complete - we allocate the new node(s) and update
 * the parent node, possibly recursively up to the root.
 *
 * The end result is that we have one or more new nodes being written -
 * possibly several, if there were multiple splits - and then a write (updating
 * an interior node) which will make all these new nodes visible.
 *
 * Additionally, as we split/rewrite nodes we free the old nodes - but the old
 * nodes can't be freed (their space on disk can't be reclaimed) until the
 * update to the interior node that makes the new node visible completes -
 * until then, the old nodes are still reachable on disk.
 *
 */
struct btree_update {
	struct closure			cl;
	struct bch_fs			*c;
	u64				start_time;
	unsigned long			ip_started;

	struct list_head		list;
	struct list_head		unwritten_list;

	enum btree_update_mode		mode;
	enum bch_trans_commit_flags	flags;
	unsigned			nodes_written:1;
	unsigned			took_gc_lock:1;

	enum btree_id			btree_id;
	unsigned			update_level_start;
	unsigned			update_level_end;

	struct disk_reservation		disk_res;

	/*
	 * BTREE_UPDATE_node:
	 * The update that made the new nodes visible was a regular update to an
	 * existing interior node - @b. We can't write out the update to @b
	 * until the new nodes we created are finished writing, so we block @b
	 * from writing by putting this btree_interior update on the
	 * @b->write_blocked list with @write_blocked_list:
	 */
	struct btree			*b;
	struct list_head		write_blocked_list;

	/*
	 * We may be freeing nodes that were dirty, and thus had journal entries
	 * pinned: we need to transfer the oldest of those pins to the
	 * btree_update operation, and release it when the new node(s)
	 * are all persistent and reachable:
	 */
	struct journal_entry_pin	journal;

	/* Preallocated nodes we reserve when we start the update: */
	struct prealloc_nodes {
		struct btree		*b[BTREE_UPDATE_NODES_MAX];
		unsigned		nr;
	}				prealloc_nodes[2];

	/* Nodes being freed: */
	struct keylist			old_keys;
	u64				_old_keys[BTREE_UPDATE_NODES_MAX *
						  BKEY_BTREE_PTR_U64s_MAX];

	/* Nodes being added: */
	struct keylist			new_keys;
	u64				_new_keys[BTREE_UPDATE_NODES_MAX *
						  BKEY_BTREE_PTR_U64s_MAX];

	/* New nodes, that will be made reachable by this update: */
	struct btree			*new_nodes[BTREE_UPDATE_NODES_MAX];
	unsigned			nr_new_nodes;

	struct btree			*old_nodes[BTREE_UPDATE_NODES_MAX];
	__le64				old_nodes_seq[BTREE_UPDATE_NODES_MAX];
	unsigned			nr_old_nodes;

	open_bucket_idx_t		open_buckets[BTREE_UPDATE_NODES_MAX *
						     BCH_REPLICAS_MAX];
	open_bucket_idx_t		nr_open_buckets;

	unsigned			journal_u64s;
	u64				journal_entries[BTREE_UPDATE_JOURNAL_RES];

	/* Only here to reduce stack usage on recursive splits: */
	struct keylist			parent_keys;
	/*
	 * Enough room for btree_split's keys without realloc - btree node
	 * pointers never have crc/compression info, so we only need to acount
	 * for the pointers for three keys
	 */
	u64				inline_keys[BKEY_BTREE_PTR_U64s_MAX * 3];
};

struct btree *__bch2_btree_node_alloc_replacement(struct btree_update *,
						  struct btree_trans *,
						  struct btree *,
						  struct bkey_format);

int bch2_btree_split_leaf(struct btree_trans *, btree_path_idx_t, unsigned);

int bch2_btree_increase_depth(struct btree_trans *, btree_path_idx_t, unsigned);

int __bch2_foreground_maybe_merge(struct btree_trans *, btree_path_idx_t,
				  unsigned, unsigned, enum btree_node_sibling);

static inline int bch2_foreground_maybe_merge_sibling(struct btree_trans *trans,
					btree_path_idx_t path_idx,
					unsigned level, unsigned flags,
					enum btree_node_sibling sib)
{
	struct btree_path *path = trans->paths + path_idx;
	struct btree *b;

	EBUG_ON(!btree_node_locked(path, level));

	if (bch2_btree_node_merging_disabled)
		return 0;

	b = path->l[level].b;
	if (b->sib_u64s[sib] > trans->c->btree_foreground_merge_threshold)
		return 0;

	return __bch2_foreground_maybe_merge(trans, path_idx, level, flags, sib);
}

static inline int bch2_foreground_maybe_merge(struct btree_trans *trans,
					      btree_path_idx_t path,
					      unsigned level,
					      unsigned flags)
{
	bch2_trans_verify_not_unlocked_or_in_restart(trans);

	return  bch2_foreground_maybe_merge_sibling(trans, path, level, flags,
						    btree_prev_sib) ?:
		bch2_foreground_maybe_merge_sibling(trans, path, level, flags,
						    btree_next_sib);
}

int bch2_btree_node_rewrite(struct btree_trans *, struct btree_iter *,
			    struct btree *, unsigned);
void bch2_btree_node_rewrite_async(struct bch_fs *, struct btree *);
int bch2_btree_node_update_key(struct btree_trans *, struct btree_iter *,
			       struct btree *, struct bkey_i *,
			       unsigned, bool);
int bch2_btree_node_update_key_get_iter(struct btree_trans *, struct btree *,
					struct bkey_i *, unsigned, bool);

void bch2_btree_set_root_for_read(struct bch_fs *, struct btree *);

int bch2_btree_root_alloc_fake_trans(struct btree_trans *, enum btree_id, unsigned);
void bch2_btree_root_alloc_fake(struct bch_fs *, enum btree_id, unsigned);

static inline unsigned btree_update_reserve_required(struct bch_fs *c,
						     struct btree *b)
{
	unsigned depth = btree_node_root(c, b)->c.level + 1;

	/*
	 * Number of nodes we might have to allocate in a worst case btree
	 * split operation - we split all the way up to the root, then allocate
	 * a new root, unless we're already at max depth:
	 */
	if (depth < BTREE_MAX_DEPTH)
		return (depth - b->c.level) * 2 + 1;
	else
		return (depth - b->c.level) * 2 - 1;
}

static inline void btree_node_reset_sib_u64s(struct btree *b)
{
	b->sib_u64s[0] = b->nr.live_u64s;
	b->sib_u64s[1] = b->nr.live_u64s;
}

static inline void *btree_data_end(struct btree *b)
{
	return (void *) b->data + btree_buf_bytes(b);
}

static inline struct bkey_packed *unwritten_whiteouts_start(struct btree *b)
{
	return (void *) ((u64 *) btree_data_end(b) - b->whiteout_u64s);
}

static inline struct bkey_packed *unwritten_whiteouts_end(struct btree *b)
{
	return btree_data_end(b);
}

static inline void *write_block(struct btree *b)
{
	return (void *) b->data + (b->written << 9);
}

static inline bool __btree_addr_written(struct btree *b, void *p)
{
	return p < write_block(b);
}

static inline bool bset_written(struct btree *b, struct bset *i)
{
	return __btree_addr_written(b, i);
}

static inline bool bkey_written(struct btree *b, struct bkey_packed *k)
{
	return __btree_addr_written(b, k);
}

static inline ssize_t __bch2_btree_u64s_remaining(struct btree *b, void *end)
{
	ssize_t used = bset_byte_offset(b, end) / sizeof(u64) +
		b->whiteout_u64s;
	ssize_t total = btree_buf_bytes(b) >> 3;

	/* Always leave one extra u64 for bch2_varint_decode: */
	used++;

	return total - used;
}

static inline size_t bch2_btree_keys_u64s_remaining(struct btree *b)
{
	ssize_t remaining = __bch2_btree_u64s_remaining(b,
				btree_bkey_last(b, bset_tree_last(b)));

	BUG_ON(remaining < 0);

	if (bset_written(b, btree_bset_last(b)))
		return 0;

	return remaining;
}

#define BTREE_WRITE_SET_U64s_BITS	9

static inline unsigned btree_write_set_buffer(struct btree *b)
{
	/*
	 * Could buffer up larger amounts of keys for btrees with larger keys,
	 * pending benchmarking:
	 */
	return 8 << BTREE_WRITE_SET_U64s_BITS;
}

static inline struct btree_node_entry *want_new_bset(struct bch_fs *c, struct btree *b)
{
	struct bset_tree *t = bset_tree_last(b);
	struct btree_node_entry *bne = max(write_block(b),
			(void *) btree_bkey_last(b, t));
	ssize_t remaining_space =
		__bch2_btree_u64s_remaining(b, bne->keys.start);

	if (unlikely(bset_written(b, bset(b, t)))) {
		if (b->written + block_sectors(c) <= btree_sectors(c))
			return bne;
	} else {
		if (unlikely(bset_u64s(t) * sizeof(u64) > btree_write_set_buffer(b)) &&
		    remaining_space > (ssize_t) (btree_write_set_buffer(b) >> 3))
			return bne;
	}

	return NULL;
}

static inline void push_whiteout(struct btree *b, struct bpos pos)
{
	struct bkey_packed k;

	BUG_ON(bch2_btree_keys_u64s_remaining(b) < BKEY_U64s);
	EBUG_ON(btree_node_just_written(b));

	if (!bkey_pack_pos(&k, pos, b)) {
		struct bkey *u = (void *) &k;

		bkey_init(u);
		u->p = pos;
	}

	k.needs_whiteout = true;

	b->whiteout_u64s += k.u64s;
	bkey_p_copy(unwritten_whiteouts_start(b), &k);
}

/*
 * write lock must be held on @b (else the dirty bset that we were going to
 * insert into could be written out from under us)
 */
static inline bool bch2_btree_node_insert_fits(struct btree *b, unsigned u64s)
{
	if (unlikely(btree_node_need_rewrite(b)))
		return false;

	return u64s <= bch2_btree_keys_u64s_remaining(b);
}

void bch2_btree_updates_to_text(struct printbuf *, struct bch_fs *);

bool bch2_btree_interior_updates_flush(struct bch_fs *);

void bch2_journal_entry_to_btree_root(struct bch_fs *, struct jset_entry *);
struct jset_entry *bch2_btree_roots_to_journal_entries(struct bch_fs *,
					struct jset_entry *, unsigned long);

void bch2_async_btree_node_rewrites_flush(struct bch_fs *);
void bch2_do_pending_node_rewrites(struct bch_fs *);
void bch2_free_pending_node_rewrites(struct bch_fs *);

void bch2_btree_reserve_cache_to_text(struct printbuf *, struct bch_fs *);

void bch2_fs_btree_interior_update_exit(struct bch_fs *);
void bch2_fs_btree_interior_update_init_early(struct bch_fs *);
int bch2_fs_btree_interior_update_init(struct bch_fs *);

#endif /* _BCACHEFS_BTREE_UPDATE_INTERIOR_H */
