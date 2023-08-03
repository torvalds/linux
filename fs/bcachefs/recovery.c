// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "backpointers.h"
#include "bkey_buf.h"
#include "alloc_background.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_io.h"
#include "buckets.h"
#include "dirent.h"
#include "ec.h"
#include "errcode.h"
#include "error.h"
#include "fs-common.h"
#include "fsck.h"
#include "journal_io.h"
#include "journal_reclaim.h"
#include "journal_seq_blacklist.h"
#include "lru.h"
#include "move.h"
#include "quota.h"
#include "recovery.h"
#include "replicas.h"
#include "subvolume.h"
#include "super-io.h"

#include <linux/sort.h>
#include <linux/stat.h>

#define QSTR(n) { { { .len = strlen(n) } }, .name = n }

/* for -o reconstruct_alloc: */
static void drop_alloc_keys(struct journal_keys *keys)
{
	size_t src, dst;

	for (src = 0, dst = 0; src < keys->nr; src++)
		if (keys->d[src].btree_id != BTREE_ID_alloc)
			keys->d[dst++] = keys->d[src];

	keys->nr = dst;
}

/*
 * Btree node pointers have a field to stack a pointer to the in memory btree
 * node; we need to zero out this field when reading in btree nodes, or when
 * reading in keys from the journal:
 */
static void zero_out_btree_mem_ptr(struct journal_keys *keys)
{
	struct journal_key *i;

	for (i = keys->d; i < keys->d + keys->nr; i++)
		if (i->k->k.type == KEY_TYPE_btree_ptr_v2)
			bkey_i_to_btree_ptr_v2(i->k)->v.mem_ptr = 0;
}

/* iterate over keys read from the journal: */

static int __journal_key_cmp(enum btree_id	l_btree_id,
			     unsigned		l_level,
			     struct bpos	l_pos,
			     const struct journal_key *r)
{
	return (cmp_int(l_btree_id,	r->btree_id) ?:
		cmp_int(l_level,	r->level) ?:
		bpos_cmp(l_pos,	r->k->k.p));
}

static int journal_key_cmp(const struct journal_key *l, const struct journal_key *r)
{
	return __journal_key_cmp(l->btree_id, l->level, l->k->k.p, r);
}

static inline size_t idx_to_pos(struct journal_keys *keys, size_t idx)
{
	size_t gap_size = keys->size - keys->nr;

	if (idx >= keys->gap)
		idx += gap_size;
	return idx;
}

static inline struct journal_key *idx_to_key(struct journal_keys *keys, size_t idx)
{
	return keys->d + idx_to_pos(keys, idx);
}

static size_t __bch2_journal_key_search(struct journal_keys *keys,
					enum btree_id id, unsigned level,
					struct bpos pos)
{
	size_t l = 0, r = keys->nr, m;

	while (l < r) {
		m = l + ((r - l) >> 1);
		if (__journal_key_cmp(id, level, pos, idx_to_key(keys, m)) > 0)
			l = m + 1;
		else
			r = m;
	}

	BUG_ON(l < keys->nr &&
	       __journal_key_cmp(id, level, pos, idx_to_key(keys, l)) > 0);

	BUG_ON(l &&
	       __journal_key_cmp(id, level, pos, idx_to_key(keys, l - 1)) <= 0);

	return l;
}

static size_t bch2_journal_key_search(struct journal_keys *keys,
				      enum btree_id id, unsigned level,
				      struct bpos pos)
{
	return idx_to_pos(keys, __bch2_journal_key_search(keys, id, level, pos));
}

struct bkey_i *bch2_journal_keys_peek_upto(struct bch_fs *c, enum btree_id btree_id,
					   unsigned level, struct bpos pos,
					   struct bpos end_pos, size_t *idx)
{
	struct journal_keys *keys = &c->journal_keys;
	unsigned iters = 0;
	struct journal_key *k;
search:
	if (!*idx)
		*idx = __bch2_journal_key_search(keys, btree_id, level, pos);

	while ((k = *idx < keys->nr ? idx_to_key(keys, *idx) : NULL)) {
		if (__journal_key_cmp(btree_id, level, end_pos, k) < 0)
			return NULL;

		if (__journal_key_cmp(btree_id, level, pos, k) <= 0 &&
		    !k->overwritten)
			return k->k;

		(*idx)++;
		iters++;
		if (iters == 10) {
			*idx = 0;
			goto search;
		}
	}

	return NULL;
}

struct bkey_i *bch2_journal_keys_peek_slot(struct bch_fs *c, enum btree_id btree_id,
					   unsigned level, struct bpos pos)
{
	size_t idx = 0;

	return bch2_journal_keys_peek_upto(c, btree_id, level, pos, pos, &idx);
}

static void journal_iters_fix(struct bch_fs *c)
{
	struct journal_keys *keys = &c->journal_keys;
	/* The key we just inserted is immediately before the gap: */
	size_t gap_end = keys->gap + (keys->size - keys->nr);
	struct btree_and_journal_iter *iter;

	/*
	 * If an iterator points one after the key we just inserted, decrement
	 * the iterator so it points at the key we just inserted - if the
	 * decrement was unnecessary, bch2_btree_and_journal_iter_peek() will
	 * handle that:
	 */
	list_for_each_entry(iter, &c->journal_iters, journal.list)
		if (iter->journal.idx == gap_end)
			iter->journal.idx = keys->gap - 1;
}

static void journal_iters_move_gap(struct bch_fs *c, size_t old_gap, size_t new_gap)
{
	struct journal_keys *keys = &c->journal_keys;
	struct journal_iter *iter;
	size_t gap_size = keys->size - keys->nr;

	list_for_each_entry(iter, &c->journal_iters, list) {
		if (iter->idx > old_gap)
			iter->idx -= gap_size;
		if (iter->idx >= new_gap)
			iter->idx += gap_size;
	}
}

int bch2_journal_key_insert_take(struct bch_fs *c, enum btree_id id,
				 unsigned level, struct bkey_i *k)
{
	struct journal_key n = {
		.btree_id	= id,
		.level		= level,
		.k		= k,
		.allocated	= true,
		/*
		 * Ensure these keys are done last by journal replay, to unblock
		 * journal reclaim:
		 */
		.journal_seq	= U32_MAX,
	};
	struct journal_keys *keys = &c->journal_keys;
	size_t idx = bch2_journal_key_search(keys, id, level, k->k.p);

	BUG_ON(test_bit(BCH_FS_RW, &c->flags));

	if (idx < keys->size &&
	    journal_key_cmp(&n, &keys->d[idx]) == 0) {
		if (keys->d[idx].allocated)
			kfree(keys->d[idx].k);
		keys->d[idx] = n;
		return 0;
	}

	if (idx > keys->gap)
		idx -= keys->size - keys->nr;

	if (keys->nr == keys->size) {
		struct journal_keys new_keys = {
			.nr			= keys->nr,
			.size			= max_t(size_t, keys->size, 8) * 2,
		};

		new_keys.d = kvmalloc_array(new_keys.size, sizeof(new_keys.d[0]), GFP_KERNEL);
		if (!new_keys.d) {
			bch_err(c, "%s: error allocating new key array (size %zu)",
				__func__, new_keys.size);
			return -BCH_ERR_ENOMEM_journal_key_insert;
		}

		/* Since @keys was full, there was no gap: */
		memcpy(new_keys.d, keys->d, sizeof(keys->d[0]) * keys->nr);
		kvfree(keys->d);
		*keys = new_keys;

		/* And now the gap is at the end: */
		keys->gap = keys->nr;
	}

	journal_iters_move_gap(c, keys->gap, idx);

	move_gap(keys->d, keys->nr, keys->size, keys->gap, idx);
	keys->gap = idx;

	keys->nr++;
	keys->d[keys->gap++] = n;

	journal_iters_fix(c);

	return 0;
}

/*
 * Can only be used from the recovery thread while we're still RO - can't be
 * used once we've got RW, as journal_keys is at that point used by multiple
 * threads:
 */
int bch2_journal_key_insert(struct bch_fs *c, enum btree_id id,
			    unsigned level, struct bkey_i *k)
{
	struct bkey_i *n;
	int ret;

	n = kmalloc(bkey_bytes(&k->k), GFP_KERNEL);
	if (!n)
		return -BCH_ERR_ENOMEM_journal_key_insert;

	bkey_copy(n, k);
	ret = bch2_journal_key_insert_take(c, id, level, n);
	if (ret)
		kfree(n);
	return ret;
}

int bch2_journal_key_delete(struct bch_fs *c, enum btree_id id,
			    unsigned level, struct bpos pos)
{
	struct bkey_i whiteout;

	bkey_init(&whiteout.k);
	whiteout.k.p = pos;

	return bch2_journal_key_insert(c, id, level, &whiteout);
}

void bch2_journal_key_overwritten(struct bch_fs *c, enum btree_id btree,
				  unsigned level, struct bpos pos)
{
	struct journal_keys *keys = &c->journal_keys;
	size_t idx = bch2_journal_key_search(keys, btree, level, pos);

	if (idx < keys->size &&
	    keys->d[idx].btree_id	== btree &&
	    keys->d[idx].level		== level &&
	    bpos_eq(keys->d[idx].k->k.p, pos))
		keys->d[idx].overwritten = true;
}

static void bch2_journal_iter_advance(struct journal_iter *iter)
{
	if (iter->idx < iter->keys->size) {
		iter->idx++;
		if (iter->idx == iter->keys->gap)
			iter->idx += iter->keys->size - iter->keys->nr;
	}
}

static struct bkey_s_c bch2_journal_iter_peek(struct journal_iter *iter)
{
	struct journal_key *k = iter->keys->d + iter->idx;

	while (k < iter->keys->d + iter->keys->size &&
	       k->btree_id	== iter->btree_id &&
	       k->level		== iter->level) {
		if (!k->overwritten)
			return bkey_i_to_s_c(k->k);

		bch2_journal_iter_advance(iter);
		k = iter->keys->d + iter->idx;
	}

	return bkey_s_c_null;
}

static void bch2_journal_iter_exit(struct journal_iter *iter)
{
	list_del(&iter->list);
}

static void bch2_journal_iter_init(struct bch_fs *c,
				   struct journal_iter *iter,
				   enum btree_id id, unsigned level,
				   struct bpos pos)
{
	iter->btree_id	= id;
	iter->level	= level;
	iter->keys	= &c->journal_keys;
	iter->idx	= bch2_journal_key_search(&c->journal_keys, id, level, pos);
}

static struct bkey_s_c bch2_journal_iter_peek_btree(struct btree_and_journal_iter *iter)
{
	return bch2_btree_node_iter_peek_unpack(&iter->node_iter,
						iter->b, &iter->unpacked);
}

static void bch2_journal_iter_advance_btree(struct btree_and_journal_iter *iter)
{
	bch2_btree_node_iter_advance(&iter->node_iter, iter->b);
}

void bch2_btree_and_journal_iter_advance(struct btree_and_journal_iter *iter)
{
	if (bpos_eq(iter->pos, SPOS_MAX))
		iter->at_end = true;
	else
		iter->pos = bpos_successor(iter->pos);
}

struct bkey_s_c bch2_btree_and_journal_iter_peek(struct btree_and_journal_iter *iter)
{
	struct bkey_s_c btree_k, journal_k, ret;
again:
	if (iter->at_end)
		return bkey_s_c_null;

	while ((btree_k = bch2_journal_iter_peek_btree(iter)).k &&
	       bpos_lt(btree_k.k->p, iter->pos))
		bch2_journal_iter_advance_btree(iter);

	while ((journal_k = bch2_journal_iter_peek(&iter->journal)).k &&
	       bpos_lt(journal_k.k->p, iter->pos))
		bch2_journal_iter_advance(&iter->journal);

	ret = journal_k.k &&
		(!btree_k.k || bpos_le(journal_k.k->p, btree_k.k->p))
		? journal_k
		: btree_k;

	if (ret.k && iter->b && bpos_gt(ret.k->p, iter->b->data->max_key))
		ret = bkey_s_c_null;

	if (ret.k) {
		iter->pos = ret.k->p;
		if (bkey_deleted(ret.k)) {
			bch2_btree_and_journal_iter_advance(iter);
			goto again;
		}
	} else {
		iter->pos = SPOS_MAX;
		iter->at_end = true;
	}

	return ret;
}

void bch2_btree_and_journal_iter_exit(struct btree_and_journal_iter *iter)
{
	bch2_journal_iter_exit(&iter->journal);
}

void __bch2_btree_and_journal_iter_init_node_iter(struct btree_and_journal_iter *iter,
						  struct bch_fs *c,
						  struct btree *b,
						  struct btree_node_iter node_iter,
						  struct bpos pos)
{
	memset(iter, 0, sizeof(*iter));

	iter->b = b;
	iter->node_iter = node_iter;
	bch2_journal_iter_init(c, &iter->journal, b->c.btree_id, b->c.level, pos);
	INIT_LIST_HEAD(&iter->journal.list);
	iter->pos = b->data->min_key;
	iter->at_end = false;
}

/*
 * this version is used by btree_gc before filesystem has gone RW and
 * multithreaded, so uses the journal_iters list:
 */
void bch2_btree_and_journal_iter_init_node_iter(struct btree_and_journal_iter *iter,
						struct bch_fs *c,
						struct btree *b)
{
	struct btree_node_iter node_iter;

	bch2_btree_node_iter_init_from_start(&node_iter, b);
	__bch2_btree_and_journal_iter_init_node_iter(iter, c, b, node_iter, b->data->min_key);
	list_add(&iter->journal.list, &c->journal_iters);
}

/* sort and dedup all keys in the journal: */

void bch2_journal_entries_free(struct bch_fs *c)
{
	struct journal_replay **i;
	struct genradix_iter iter;

	genradix_for_each(&c->journal_entries, iter, i)
		if (*i)
			kvpfree(*i, offsetof(struct journal_replay, j) +
				vstruct_bytes(&(*i)->j));
	genradix_free(&c->journal_entries);
}

/*
 * When keys compare equal, oldest compares first:
 */
static int journal_sort_key_cmp(const void *_l, const void *_r)
{
	const struct journal_key *l = _l;
	const struct journal_key *r = _r;

	return  journal_key_cmp(l, r) ?:
		cmp_int(l->journal_seq, r->journal_seq) ?:
		cmp_int(l->journal_offset, r->journal_offset);
}

void bch2_journal_keys_free(struct journal_keys *keys)
{
	struct journal_key *i;

	move_gap(keys->d, keys->nr, keys->size, keys->gap, keys->nr);
	keys->gap = keys->nr;

	for (i = keys->d; i < keys->d + keys->nr; i++)
		if (i->allocated)
			kfree(i->k);

	kvfree(keys->d);
	keys->d = NULL;
	keys->nr = keys->gap = keys->size = 0;
}

static void __journal_keys_sort(struct journal_keys *keys)
{
	struct journal_key *src, *dst;

	sort(keys->d, keys->nr, sizeof(keys->d[0]), journal_sort_key_cmp, NULL);

	src = dst = keys->d;
	while (src < keys->d + keys->nr) {
		while (src + 1 < keys->d + keys->nr &&
		       src[0].btree_id	== src[1].btree_id &&
		       src[0].level	== src[1].level &&
		       bpos_eq(src[0].k->k.p, src[1].k->k.p))
			src++;

		*dst++ = *src++;
	}

	keys->nr = dst - keys->d;
}

static int journal_keys_sort(struct bch_fs *c)
{
	struct genradix_iter iter;
	struct journal_replay *i, **_i;
	struct jset_entry *entry;
	struct bkey_i *k;
	struct journal_keys *keys = &c->journal_keys;
	size_t nr_keys = 0, nr_read = 0;

	genradix_for_each(&c->journal_entries, iter, _i) {
		i = *_i;

		if (!i || i->ignore)
			continue;

		for_each_jset_key(k, entry, &i->j)
			nr_keys++;
	}

	if (!nr_keys)
		return 0;

	keys->size = roundup_pow_of_two(nr_keys);

	keys->d = kvmalloc_array(keys->size, sizeof(keys->d[0]), GFP_KERNEL);
	if (!keys->d) {
		bch_err(c, "Failed to allocate buffer for sorted journal keys (%zu keys); trying slowpath",
			nr_keys);

		do {
			keys->size >>= 1;
			keys->d = kvmalloc_array(keys->size, sizeof(keys->d[0]), GFP_KERNEL);
		} while (!keys->d && keys->size > nr_keys / 8);

		if (!keys->d) {
			bch_err(c, "Failed to allocate %zu size buffer for sorted journal keys; exiting",
				keys->size);
			return -BCH_ERR_ENOMEM_journal_keys_sort;
		}
	}

	genradix_for_each(&c->journal_entries, iter, _i) {
		i = *_i;

		if (!i || i->ignore)
			continue;

		cond_resched();

		for_each_jset_key(k, entry, &i->j) {
			if (keys->nr == keys->size) {
				__journal_keys_sort(keys);

				if (keys->nr > keys->size * 7 / 8) {
					bch_err(c, "Too many journal keys for slowpath; have %zu compacted, buf size %zu, processed %zu/%zu",
						keys->nr, keys->size, nr_read, nr_keys);
					return -BCH_ERR_ENOMEM_journal_keys_sort;
				}
			}

			keys->d[keys->nr++] = (struct journal_key) {
				.btree_id	= entry->btree_id,
				.level		= entry->level,
				.k		= k,
				.journal_seq	= le64_to_cpu(i->j.seq),
				.journal_offset	= k->_data - i->j._data,
			};

			nr_read++;
		}
	}

	__journal_keys_sort(keys);
	keys->gap = keys->nr;

	bch_verbose(c, "Journal keys: %zu read, %zu after sorting and compacting", nr_keys, keys->nr);
	return 0;
}

/* journal replay: */

static void replay_now_at(struct journal *j, u64 seq)
{
	BUG_ON(seq < j->replay_journal_seq);

	seq = min(seq, j->replay_journal_seq_end);

	while (j->replay_journal_seq < seq)
		bch2_journal_pin_put(j, j->replay_journal_seq++);
}

static int bch2_journal_replay_key(struct btree_trans *trans,
				   struct journal_key *k)
{
	struct btree_iter iter;
	unsigned iter_flags =
		BTREE_ITER_INTENT|
		BTREE_ITER_NOT_EXTENTS;
	unsigned update_flags = BTREE_TRIGGER_NORUN;
	int ret;

	/*
	 * BTREE_UPDATE_KEY_CACHE_RECLAIM disables key cache lookup/update to
	 * keep the key cache coherent with the underlying btree. Nothing
	 * besides the allocator is doing updates yet so we don't need key cache
	 * coherency for non-alloc btrees, and key cache fills for snapshots
	 * btrees use BTREE_ITER_FILTER_SNAPSHOTS, which isn't available until
	 * the snapshots recovery pass runs.
	 */
	if (!k->level && k->btree_id == BTREE_ID_alloc)
		iter_flags |= BTREE_ITER_CACHED;
	else
		update_flags |= BTREE_UPDATE_KEY_CACHE_RECLAIM;

	bch2_trans_node_iter_init(trans, &iter, k->btree_id, k->k->k.p,
				  BTREE_MAX_DEPTH, k->level,
				  iter_flags);
	ret = bch2_btree_iter_traverse(&iter);
	if (ret)
		goto out;

	/* Must be checked with btree locked: */
	if (k->overwritten)
		goto out;

	ret = bch2_trans_update(trans, &iter, k->k, update_flags);
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int journal_sort_seq_cmp(const void *_l, const void *_r)
{
	const struct journal_key *l = *((const struct journal_key **)_l);
	const struct journal_key *r = *((const struct journal_key **)_r);

	return cmp_int(l->journal_seq, r->journal_seq);
}

static int bch2_journal_replay(struct bch_fs *c)
{
	struct journal_keys *keys = &c->journal_keys;
	struct journal_key **keys_sorted, *k;
	struct journal *j = &c->journal;
	u64 start_seq	= c->journal_replay_seq_start;
	u64 end_seq	= c->journal_replay_seq_start;
	size_t i;
	int ret;

	move_gap(keys->d, keys->nr, keys->size, keys->gap, keys->nr);
	keys->gap = keys->nr;

	keys_sorted = kvmalloc_array(sizeof(*keys_sorted), keys->nr, GFP_KERNEL);
	if (!keys_sorted)
		return -BCH_ERR_ENOMEM_journal_replay;

	for (i = 0; i < keys->nr; i++)
		keys_sorted[i] = &keys->d[i];

	sort(keys_sorted, keys->nr,
	     sizeof(keys_sorted[0]),
	     journal_sort_seq_cmp, NULL);

	if (keys->nr) {
		ret = bch2_journal_log_msg(c, "Starting journal replay (%zu keys in entries %llu-%llu)",
					   keys->nr, start_seq, end_seq);
		if (ret)
			goto err;
	}

	for (i = 0; i < keys->nr; i++) {
		k = keys_sorted[i];

		cond_resched();

		replay_now_at(j, k->journal_seq);

		ret = bch2_trans_do(c, NULL, NULL,
				    BTREE_INSERT_LAZY_RW|
				    BTREE_INSERT_NOFAIL|
				    (!k->allocated
				     ? BTREE_INSERT_JOURNAL_REPLAY|BCH_WATERMARK_reclaim
				     : 0),
			     bch2_journal_replay_key(&trans, k));
		if (ret) {
			bch_err(c, "journal replay: error while replaying key at btree %s level %u: %s",
				bch2_btree_ids[k->btree_id], k->level, bch2_err_str(ret));
			goto err;
		}
	}

	replay_now_at(j, j->replay_journal_seq_end);
	j->replay_journal_seq = 0;

	bch2_journal_set_replay_done(j);
	bch2_journal_flush_all_pins(j);
	ret = bch2_journal_error(j);

	if (keys->nr && !ret)
		bch2_journal_log_msg(c, "journal replay finished");
err:
	kvfree(keys_sorted);

	if (ret)
		bch_err_fn(c, ret);
	return ret;
}

/* journal replay early: */

static int journal_replay_entry_early(struct bch_fs *c,
				      struct jset_entry *entry)
{
	int ret = 0;

	switch (entry->type) {
	case BCH_JSET_ENTRY_btree_root: {
		struct btree_root *r;

		while (entry->btree_id >= c->btree_roots_extra.nr + BTREE_ID_NR) {
			ret = darray_push(&c->btree_roots_extra, (struct btree_root) { NULL });
			if (ret)
				return ret;
		}

		r = bch2_btree_id_root(c, entry->btree_id);

		if (entry->u64s) {
			r->level = entry->level;
			bkey_copy(&r->key, &entry->start[0]);
			r->error = 0;
		} else {
			r->error = -EIO;
		}
		r->alive = true;
		break;
	}
	case BCH_JSET_ENTRY_usage: {
		struct jset_entry_usage *u =
			container_of(entry, struct jset_entry_usage, entry);

		switch (entry->btree_id) {
		case BCH_FS_USAGE_reserved:
			if (entry->level < BCH_REPLICAS_MAX)
				c->usage_base->persistent_reserved[entry->level] =
					le64_to_cpu(u->v);
			break;
		case BCH_FS_USAGE_inodes:
			c->usage_base->nr_inodes = le64_to_cpu(u->v);
			break;
		case BCH_FS_USAGE_key_version:
			atomic64_set(&c->key_version,
				     le64_to_cpu(u->v));
			break;
		}

		break;
	}
	case BCH_JSET_ENTRY_data_usage: {
		struct jset_entry_data_usage *u =
			container_of(entry, struct jset_entry_data_usage, entry);

		ret = bch2_replicas_set_usage(c, &u->r,
					      le64_to_cpu(u->v));
		break;
	}
	case BCH_JSET_ENTRY_dev_usage: {
		struct jset_entry_dev_usage *u =
			container_of(entry, struct jset_entry_dev_usage, entry);
		struct bch_dev *ca = bch_dev_bkey_exists(c, le32_to_cpu(u->dev));
		unsigned i, nr_types = jset_entry_dev_usage_nr_types(u);

		ca->usage_base->buckets_ec		= le64_to_cpu(u->buckets_ec);

		for (i = 0; i < min_t(unsigned, nr_types, BCH_DATA_NR); i++) {
			ca->usage_base->d[i].buckets	= le64_to_cpu(u->d[i].buckets);
			ca->usage_base->d[i].sectors	= le64_to_cpu(u->d[i].sectors);
			ca->usage_base->d[i].fragmented	= le64_to_cpu(u->d[i].fragmented);
		}

		break;
	}
	case BCH_JSET_ENTRY_blacklist: {
		struct jset_entry_blacklist *bl_entry =
			container_of(entry, struct jset_entry_blacklist, entry);

		ret = bch2_journal_seq_blacklist_add(c,
				le64_to_cpu(bl_entry->seq),
				le64_to_cpu(bl_entry->seq) + 1);
		break;
	}
	case BCH_JSET_ENTRY_blacklist_v2: {
		struct jset_entry_blacklist_v2 *bl_entry =
			container_of(entry, struct jset_entry_blacklist_v2, entry);

		ret = bch2_journal_seq_blacklist_add(c,
				le64_to_cpu(bl_entry->start),
				le64_to_cpu(bl_entry->end) + 1);
		break;
	}
	case BCH_JSET_ENTRY_clock: {
		struct jset_entry_clock *clock =
			container_of(entry, struct jset_entry_clock, entry);

		atomic64_set(&c->io_clock[clock->rw].now, le64_to_cpu(clock->time));
	}
	}

	return ret;
}

static int journal_replay_early(struct bch_fs *c,
				struct bch_sb_field_clean *clean)
{
	struct jset_entry *entry;
	int ret;

	if (clean) {
		for (entry = clean->start;
		     entry != vstruct_end(&clean->field);
		     entry = vstruct_next(entry)) {
			ret = journal_replay_entry_early(c, entry);
			if (ret)
				return ret;
		}
	} else {
		struct genradix_iter iter;
		struct journal_replay *i, **_i;

		genradix_for_each(&c->journal_entries, iter, _i) {
			i = *_i;

			if (!i || i->ignore)
				continue;

			vstruct_for_each(&i->j, entry) {
				ret = journal_replay_entry_early(c, entry);
				if (ret)
					return ret;
			}
		}
	}

	bch2_fs_usage_initialize(c);

	return 0;
}

/* sb clean section: */

static struct bkey_i *btree_root_find(struct bch_fs *c,
				      struct bch_sb_field_clean *clean,
				      struct jset *j,
				      enum btree_id id, unsigned *level)
{
	struct bkey_i *k;
	struct jset_entry *entry, *start, *end;

	if (clean) {
		start = clean->start;
		end = vstruct_end(&clean->field);
	} else {
		start = j->start;
		end = vstruct_last(j);
	}

	for (entry = start; entry < end; entry = vstruct_next(entry))
		if (entry->type == BCH_JSET_ENTRY_btree_root &&
		    entry->btree_id == id)
			goto found;

	return NULL;
found:
	if (!entry->u64s)
		return ERR_PTR(-EINVAL);

	k = entry->start;
	*level = entry->level;
	return k;
}

static int verify_superblock_clean(struct bch_fs *c,
				   struct bch_sb_field_clean **cleanp,
				   struct jset *j)
{
	unsigned i;
	struct bch_sb_field_clean *clean = *cleanp;
	struct printbuf buf1 = PRINTBUF;
	struct printbuf buf2 = PRINTBUF;
	int ret = 0;

	if (mustfix_fsck_err_on(j->seq != clean->journal_seq, c,
			"superblock journal seq (%llu) doesn't match journal (%llu) after clean shutdown",
			le64_to_cpu(clean->journal_seq),
			le64_to_cpu(j->seq))) {
		kfree(clean);
		*cleanp = NULL;
		return 0;
	}

	for (i = 0; i < BTREE_ID_NR; i++) {
		struct bkey_i *k1, *k2;
		unsigned l1 = 0, l2 = 0;

		k1 = btree_root_find(c, clean, NULL, i, &l1);
		k2 = btree_root_find(c, NULL, j, i, &l2);

		if (!k1 && !k2)
			continue;

		printbuf_reset(&buf1);
		printbuf_reset(&buf2);

		if (k1)
			bch2_bkey_val_to_text(&buf1, c, bkey_i_to_s_c(k1));
		else
			prt_printf(&buf1, "(none)");

		if (k2)
			bch2_bkey_val_to_text(&buf2, c, bkey_i_to_s_c(k2));
		else
			prt_printf(&buf2, "(none)");

		mustfix_fsck_err_on(!k1 || !k2 ||
				    IS_ERR(k1) ||
				    IS_ERR(k2) ||
				    k1->k.u64s != k2->k.u64s ||
				    memcmp(k1, k2, bkey_bytes(&k1->k)) ||
				    l1 != l2, c,
			"superblock btree root %u doesn't match journal after clean shutdown\n"
			"sb:      l=%u %s\n"
			"journal: l=%u %s\n", i,
			l1, buf1.buf,
			l2, buf2.buf);
	}
fsck_err:
	printbuf_exit(&buf2);
	printbuf_exit(&buf1);
	return ret;
}

static struct bch_sb_field_clean *read_superblock_clean(struct bch_fs *c)
{
	struct bch_sb_field_clean *clean, *sb_clean;
	int ret;

	mutex_lock(&c->sb_lock);
	sb_clean = bch2_sb_get_clean(c->disk_sb.sb);

	if (fsck_err_on(!sb_clean, c,
			"superblock marked clean but clean section not present")) {
		SET_BCH_SB_CLEAN(c->disk_sb.sb, false);
		c->sb.clean = false;
		mutex_unlock(&c->sb_lock);
		return NULL;
	}

	clean = kmemdup(sb_clean, vstruct_bytes(&sb_clean->field),
			GFP_KERNEL);
	if (!clean) {
		mutex_unlock(&c->sb_lock);
		return ERR_PTR(-BCH_ERR_ENOMEM_read_superblock_clean);
	}

	ret = bch2_sb_clean_validate_late(c, clean, READ);
	if (ret) {
		mutex_unlock(&c->sb_lock);
		return ERR_PTR(ret);
	}

	mutex_unlock(&c->sb_lock);

	return clean;
fsck_err:
	mutex_unlock(&c->sb_lock);
	return ERR_PTR(ret);
}

static bool btree_id_is_alloc(enum btree_id id)
{
	switch (id) {
	case BTREE_ID_alloc:
	case BTREE_ID_backpointers:
	case BTREE_ID_need_discard:
	case BTREE_ID_freespace:
	case BTREE_ID_bucket_gens:
		return true;
	default:
		return false;
	}
}

static int read_btree_roots(struct bch_fs *c)
{
	unsigned i;
	int ret = 0;

	for (i = 0; i < btree_id_nr_alive(c); i++) {
		struct btree_root *r = bch2_btree_id_root(c, i);

		if (!r->alive)
			continue;

		if (btree_id_is_alloc(i) &&
		    c->opts.reconstruct_alloc) {
			c->sb.compat &= ~(1ULL << BCH_COMPAT_alloc_info);
			continue;
		}

		if (r->error) {
			__fsck_err(c, btree_id_is_alloc(i)
				   ? FSCK_CAN_IGNORE : 0,
				   "invalid btree root %s",
				   bch2_btree_ids[i]);
			if (i == BTREE_ID_alloc)
				c->sb.compat &= ~(1ULL << BCH_COMPAT_alloc_info);
		}

		ret = bch2_btree_root_read(c, i, &r->key, r->level);
		if (ret) {
			__fsck_err(c,
				   btree_id_is_alloc(i)
				   ? FSCK_CAN_IGNORE : 0,
				   "error reading btree root %s",
				   bch2_btree_ids[i]);
			if (btree_id_is_alloc(i))
				c->sb.compat &= ~(1ULL << BCH_COMPAT_alloc_info);
		}
	}

	for (i = 0; i < BTREE_ID_NR; i++) {
		struct btree_root *r = bch2_btree_id_root(c, i);

		if (!r->b) {
			r->alive = false;
			r->level = 0;
			bch2_btree_root_alloc(c, i);
		}
	}
fsck_err:
	return ret;
}

static int bch2_initialize_subvolumes(struct bch_fs *c)
{
	struct bkey_i_snapshot_tree	root_tree;
	struct bkey_i_snapshot		root_snapshot;
	struct bkey_i_subvolume		root_volume;
	int ret;

	bkey_snapshot_tree_init(&root_tree.k_i);
	root_tree.k.p.offset		= 1;
	root_tree.v.master_subvol	= cpu_to_le32(1);
	root_tree.v.root_snapshot	= cpu_to_le32(U32_MAX);

	bkey_snapshot_init(&root_snapshot.k_i);
	root_snapshot.k.p.offset = U32_MAX;
	root_snapshot.v.flags	= 0;
	root_snapshot.v.parent	= 0;
	root_snapshot.v.subvol	= cpu_to_le32(BCACHEFS_ROOT_SUBVOL);
	root_snapshot.v.tree	= cpu_to_le32(1);
	SET_BCH_SNAPSHOT_SUBVOL(&root_snapshot.v, true);

	bkey_subvolume_init(&root_volume.k_i);
	root_volume.k.p.offset = BCACHEFS_ROOT_SUBVOL;
	root_volume.v.flags	= 0;
	root_volume.v.snapshot	= cpu_to_le32(U32_MAX);
	root_volume.v.inode	= cpu_to_le64(BCACHEFS_ROOT_INO);

	ret =   bch2_btree_insert(c, BTREE_ID_snapshot_trees,
				  &root_tree.k_i,
				  NULL, NULL, 0) ?:
		bch2_btree_insert(c, BTREE_ID_snapshots,
				  &root_snapshot.k_i,
				  NULL, NULL, 0) ?:
		bch2_btree_insert(c, BTREE_ID_subvolumes,
				  &root_volume.k_i,
				  NULL, NULL, 0);
	if (ret)
		bch_err_fn(c, ret);
	return ret;
}

static int __bch2_fs_upgrade_for_subvolumes(struct btree_trans *trans)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bch_inode_unpacked inode;
	int ret;

	k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_inodes,
			       SPOS(0, BCACHEFS_ROOT_INO, U32_MAX), 0);
	ret = bkey_err(k);
	if (ret)
		return ret;

	if (!bkey_is_inode(k.k)) {
		bch_err(trans->c, "root inode not found");
		ret = -BCH_ERR_ENOENT_inode;
		goto err;
	}

	ret = bch2_inode_unpack(k, &inode);
	BUG_ON(ret);

	inode.bi_subvol = BCACHEFS_ROOT_SUBVOL;

	ret = bch2_inode_write(trans, &iter, &inode);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

/* set bi_subvol on root inode */
noinline_for_stack
static int bch2_fs_upgrade_for_subvolumes(struct bch_fs *c)
{
	int ret = bch2_trans_do(c, NULL, NULL, BTREE_INSERT_LAZY_RW,
				__bch2_fs_upgrade_for_subvolumes(&trans));
	if (ret)
		bch_err_fn(c, ret);
	return ret;
}

static const char * const recovery_pass_names[] = {
#define x(_fn, _when)	#_fn,
	BCH_RECOVERY_PASSES()
#undef x
	NULL
};

static int bch2_check_allocations(struct bch_fs *c)
{
	return bch2_gc(c, true, c->opts.norecovery);
}

static int bch2_set_may_go_rw(struct bch_fs *c)
{
	set_bit(BCH_FS_MAY_GO_RW, &c->flags);
	return 0;
}

struct recovery_pass_fn {
	int		(*fn)(struct bch_fs *);
	unsigned	when;
};

static struct recovery_pass_fn recovery_passes[] = {
#define x(_fn, _when)	{ .fn = bch2_##_fn, .when = _when },
	BCH_RECOVERY_PASSES()
#undef x
};

static void check_version_upgrade(struct bch_fs *c)
{
	unsigned latest_compatible = bch2_version_compatible(c->sb.version);
	unsigned latest_version	= bcachefs_metadata_version_current;
	unsigned old_version = c->sb.version_upgrade_complete ?: c->sb.version;
	unsigned new_version = 0;
	u64 recovery_passes;

	if (old_version < bcachefs_metadata_required_upgrade_below) {
		if (c->opts.version_upgrade == BCH_VERSION_UPGRADE_incompatible ||
		    latest_compatible < bcachefs_metadata_required_upgrade_below)
			new_version = latest_version;
		else
			new_version = latest_compatible;
	} else {
		switch (c->opts.version_upgrade) {
		case BCH_VERSION_UPGRADE_compatible:
			new_version = latest_compatible;
			break;
		case BCH_VERSION_UPGRADE_incompatible:
			new_version = latest_version;
			break;
		case BCH_VERSION_UPGRADE_none:
			new_version = old_version;
			break;
		}
	}

	if (new_version > old_version) {
		struct printbuf buf = PRINTBUF;

		if (old_version < bcachefs_metadata_required_upgrade_below)
			prt_str(&buf, "Version upgrade required:\n");

		if (old_version != c->sb.version) {
			prt_str(&buf, "Version upgrade from ");
			bch2_version_to_text(&buf, c->sb.version_upgrade_complete);
			prt_str(&buf, " to ");
			bch2_version_to_text(&buf, c->sb.version);
			prt_str(&buf, " incomplete\n");
		}

		prt_printf(&buf, "Doing %s version upgrade from ",
			   BCH_VERSION_MAJOR(old_version) != BCH_VERSION_MAJOR(new_version)
			   ? "incompatible" : "compatible");
		bch2_version_to_text(&buf, old_version);
		prt_str(&buf, " to ");
		bch2_version_to_text(&buf, new_version);
		prt_newline(&buf);

		recovery_passes = bch2_upgrade_recovery_passes(c, old_version, new_version);
		if (recovery_passes) {
			if ((recovery_passes & RECOVERY_PASS_ALL_FSCK) == RECOVERY_PASS_ALL_FSCK)
				prt_str(&buf, "fsck required");
			else {
				prt_str(&buf, "running recovery passses: ");
				prt_bitflags(&buf, recovery_pass_names, recovery_passes);
			}

			c->recovery_passes_explicit |= recovery_passes;
			c->opts.fix_errors = FSCK_FIX_yes;
		}

		bch_info(c, "%s", buf.buf);

		mutex_lock(&c->sb_lock);
		bch2_sb_upgrade(c, new_version);
		mutex_unlock(&c->sb_lock);

		printbuf_exit(&buf);
	}
}

u64 bch2_fsck_recovery_passes(void)
{
	u64 ret = 0;

	for (unsigned i = 0; i < ARRAY_SIZE(recovery_passes); i++)
		if (recovery_passes[i].when & PASS_FSCK)
			ret |= BIT_ULL(i);
	return ret;
}

static bool should_run_recovery_pass(struct bch_fs *c, enum bch_recovery_pass pass)
{
	struct recovery_pass_fn *p = recovery_passes + c->curr_recovery_pass;

	if (c->opts.norecovery && pass > BCH_RECOVERY_PASS_snapshots_read)
		return false;
	if (c->recovery_passes_explicit & BIT_ULL(pass))
		return true;
	if ((p->when & PASS_FSCK) && c->opts.fsck)
		return true;
	if ((p->when & PASS_UNCLEAN) && !c->sb.clean)
		return true;
	if (p->when & PASS_ALWAYS)
		return true;
	return false;
}

static int bch2_run_recovery_pass(struct bch_fs *c, enum bch_recovery_pass pass)
{
	int ret;

	c->curr_recovery_pass = pass;

	if (should_run_recovery_pass(c, pass)) {
		struct recovery_pass_fn *p = recovery_passes + pass;

		if (!(p->when & PASS_SILENT))
			printk(KERN_INFO bch2_log_msg(c, "%s..."),
			       recovery_pass_names[pass]);
		ret = p->fn(c);
		if (ret)
			return ret;
		if (!(p->when & PASS_SILENT))
			printk(KERN_CONT " done\n");
	}

	return 0;
}

static int bch2_run_recovery_passes(struct bch_fs *c)
{
	int ret = 0;

	while (c->curr_recovery_pass < ARRAY_SIZE(recovery_passes)) {
		ret = bch2_run_recovery_pass(c, c->curr_recovery_pass);
		if (bch2_err_matches(ret, BCH_ERR_restart_recovery))
			continue;
		if (ret)
			break;
		c->curr_recovery_pass++;
	}

	return ret;
}

int bch2_fs_recovery(struct bch_fs *c)
{
	struct bch_sb_field_clean *clean = NULL;
	struct jset *last_journal_entry = NULL;
	u64 last_seq, blacklist_seq, journal_seq;
	bool write_sb = false;
	int ret = 0;

	if (c->sb.clean)
		clean = read_superblock_clean(c);
	ret = PTR_ERR_OR_ZERO(clean);
	if (ret)
		goto err;

	if (c->sb.clean)
		bch_info(c, "recovering from clean shutdown, journal seq %llu",
			 le64_to_cpu(clean->journal_seq));
	else
		bch_info(c, "recovering from unclean shutdown");

	if (!(c->sb.features & (1ULL << BCH_FEATURE_new_extent_overwrite))) {
		bch_err(c, "feature new_extent_overwrite not set, filesystem no longer supported");
		ret = -EINVAL;
		goto err;
	}

	if (!c->sb.clean &&
	    !(c->sb.features & (1ULL << BCH_FEATURE_extents_above_btree_updates))) {
		bch_err(c, "filesystem needs recovery from older version; run fsck from older bcachefs-tools to fix");
		ret = -EINVAL;
		goto err;
	}

	if (!(c->sb.compat & (1ULL << BCH_COMPAT_bformat_overflow_done))) {
		bch_err(c, "filesystem may have incompatible bkey formats; run fsck from the compat branch to fix");
		ret = -EINVAL;
		goto err;
	}

	if (c->opts.fsck || !(c->opts.nochanges && c->opts.norecovery))
		check_version_upgrade(c);

	if (c->opts.fsck && c->opts.norecovery) {
		bch_err(c, "cannot select both norecovery and fsck");
		ret = -EINVAL;
		goto err;
	}

	ret = bch2_blacklist_table_initialize(c);
	if (ret) {
		bch_err(c, "error initializing blacklist table");
		goto err;
	}

	if (!c->sb.clean || c->opts.fsck || c->opts.keep_journal) {
		struct genradix_iter iter;
		struct journal_replay **i;

		bch_verbose(c, "starting journal read");
		ret = bch2_journal_read(c, &last_seq, &blacklist_seq, &journal_seq);
		if (ret)
			goto err;

		/*
		 * note: cmd_list_journal needs the blacklist table fully up to date so
		 * it can asterisk ignored journal entries:
		 */
		if (c->opts.read_journal_only)
			goto out;

		genradix_for_each_reverse(&c->journal_entries, iter, i)
			if (*i && !(*i)->ignore) {
				last_journal_entry = &(*i)->j;
				break;
			}

		if (mustfix_fsck_err_on(c->sb.clean &&
					last_journal_entry &&
					!journal_entry_empty(last_journal_entry), c,
				"filesystem marked clean but journal not empty")) {
			c->sb.compat &= ~(1ULL << BCH_COMPAT_alloc_info);
			SET_BCH_SB_CLEAN(c->disk_sb.sb, false);
			c->sb.clean = false;
		}

		if (!last_journal_entry) {
			fsck_err_on(!c->sb.clean, c, "no journal entries found");
			if (clean)
				goto use_clean;

			genradix_for_each_reverse(&c->journal_entries, iter, i)
				if (*i) {
					last_journal_entry = &(*i)->j;
					(*i)->ignore = false;
					break;
				}
		}

		ret = journal_keys_sort(c);
		if (ret)
			goto err;

		if (c->sb.clean && last_journal_entry) {
			ret = verify_superblock_clean(c, &clean,
						      last_journal_entry);
			if (ret)
				goto err;
		}
	} else {
use_clean:
		if (!clean) {
			bch_err(c, "no superblock clean section found");
			ret = -BCH_ERR_fsck_repair_impossible;
			goto err;

		}
		blacklist_seq = journal_seq = le64_to_cpu(clean->journal_seq) + 1;
	}

	c->journal_replay_seq_start	= last_seq;
	c->journal_replay_seq_end	= blacklist_seq - 1;;

	if (c->opts.reconstruct_alloc) {
		c->sb.compat &= ~(1ULL << BCH_COMPAT_alloc_info);
		drop_alloc_keys(&c->journal_keys);
	}

	zero_out_btree_mem_ptr(&c->journal_keys);

	ret = journal_replay_early(c, clean);
	if (ret)
		goto err;

	/*
	 * After an unclean shutdown, skip then next few journal sequence
	 * numbers as they may have been referenced by btree writes that
	 * happened before their corresponding journal writes - those btree
	 * writes need to be ignored, by skipping and blacklisting the next few
	 * journal sequence numbers:
	 */
	if (!c->sb.clean)
		journal_seq += 8;

	if (blacklist_seq != journal_seq) {
		ret =   bch2_journal_log_msg(c, "blacklisting entries %llu-%llu",
					     blacklist_seq, journal_seq) ?:
			bch2_journal_seq_blacklist_add(c,
					blacklist_seq, journal_seq);
		if (ret) {
			bch_err(c, "error creating new journal seq blacklist entry");
			goto err;
		}
	}

	ret =   bch2_journal_log_msg(c, "starting journal at entry %llu, replaying %llu-%llu",
				     journal_seq, last_seq, blacklist_seq - 1) ?:
		bch2_fs_journal_start(&c->journal, journal_seq);
	if (ret)
		goto err;

	if (c->opts.reconstruct_alloc)
		bch2_journal_log_msg(c, "dropping alloc info");

	/*
	 * Skip past versions that might have possibly been used (as nonces),
	 * but hadn't had their pointers written:
	 */
	if (c->sb.encryption_type && !c->sb.clean)
		atomic64_add(1 << 16, &c->key_version);

	ret = read_btree_roots(c);
	if (ret)
		goto err;

	if (c->opts.fsck &&
	    (IS_ENABLED(CONFIG_BCACHEFS_DEBUG) ||
	     BCH_SB_HAS_TOPOLOGY_ERRORS(c->disk_sb.sb)))
		c->recovery_passes_explicit |= BIT_ULL(BCH_RECOVERY_PASS_check_topology);

	ret = bch2_run_recovery_passes(c);
	if (ret)
		goto err;

	/* If we fixed errors, verify that fs is actually clean now: */
	if (IS_ENABLED(CONFIG_BCACHEFS_DEBUG) &&
	    test_bit(BCH_FS_ERRORS_FIXED, &c->flags) &&
	    !test_bit(BCH_FS_ERRORS_NOT_FIXED, &c->flags) &&
	    !test_bit(BCH_FS_ERROR, &c->flags)) {
		bch_info(c, "Fixed errors, running fsck a second time to verify fs is clean");
		clear_bit(BCH_FS_ERRORS_FIXED, &c->flags);

		c->curr_recovery_pass = BCH_RECOVERY_PASS_check_alloc_info;

		ret = bch2_run_recovery_passes(c);
		if (ret)
			goto err;

		if (test_bit(BCH_FS_ERRORS_FIXED, &c->flags) ||
		    test_bit(BCH_FS_ERRORS_NOT_FIXED, &c->flags)) {
			bch_err(c, "Second fsck run was not clean");
			set_bit(BCH_FS_ERRORS_NOT_FIXED, &c->flags);
		}

		set_bit(BCH_FS_ERRORS_FIXED, &c->flags);
	}

	if (enabled_qtypes(c)) {
		bch_verbose(c, "reading quotas");
		ret = bch2_fs_quota_read(c);
		if (ret)
			goto err;
		bch_verbose(c, "quotas done");
	}

	mutex_lock(&c->sb_lock);
	if (BCH_SB_VERSION_UPGRADE_COMPLETE(c->disk_sb.sb) != c->sb.version) {
		SET_BCH_SB_VERSION_UPGRADE_COMPLETE(c->disk_sb.sb, c->sb.version);
		write_sb = true;
	}

	if (!test_bit(BCH_FS_ERROR, &c->flags)) {
		c->disk_sb.sb->compat[0] |= cpu_to_le64(1ULL << BCH_COMPAT_alloc_info);
		write_sb = true;
	}

	if (c->opts.fsck &&
	    !test_bit(BCH_FS_ERROR, &c->flags) &&
	    !test_bit(BCH_FS_ERRORS_NOT_FIXED, &c->flags)) {
		SET_BCH_SB_HAS_ERRORS(c->disk_sb.sb, 0);
		SET_BCH_SB_HAS_TOPOLOGY_ERRORS(c->disk_sb.sb, 0);
		write_sb = true;
	}

	if (write_sb)
		bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	if (!(c->sb.compat & (1ULL << BCH_COMPAT_extents_above_btree_updates_done)) ||
	    !(c->sb.compat & (1ULL << BCH_COMPAT_bformat_overflow_done)) ||
	    c->sb.version_min < bcachefs_metadata_version_btree_ptr_sectors_written) {
		struct bch_move_stats stats;

		bch2_move_stats_init(&stats, "recovery");

		bch_info(c, "scanning for old btree nodes");
		ret =   bch2_fs_read_write(c) ?:
			bch2_scan_old_btree_nodes(c, &stats);
		if (ret)
			goto err;
		bch_info(c, "scanning for old btree nodes done");
	}

	if (c->journal_seq_blacklist_table &&
	    c->journal_seq_blacklist_table->nr > 128)
		queue_work(system_long_wq, &c->journal_seq_blacklist_gc_work);

	ret = 0;
out:
	set_bit(BCH_FS_FSCK_DONE, &c->flags);
	bch2_flush_fsck_errs(c);

	if (!c->opts.keep_journal &&
	    test_bit(JOURNAL_REPLAY_DONE, &c->journal.flags)) {
		bch2_journal_keys_free(&c->journal_keys);
		bch2_journal_entries_free(c);
	}
	kfree(clean);

	if (!ret && test_bit(BCH_FS_HAVE_DELETED_SNAPSHOTS, &c->flags)) {
		bch2_fs_read_write_early(c);
		bch2_delete_dead_snapshots_async(c);
	}

	if (ret)
		bch_err_fn(c, ret);
	return ret;
err:
fsck_err:
	bch2_fs_emergency_read_only(c);
	goto out;
}

int bch2_fs_initialize(struct bch_fs *c)
{
	struct bch_inode_unpacked root_inode, lostfound_inode;
	struct bkey_inode_buf packed_inode;
	struct qstr lostfound = QSTR("lost+found");
	struct bch_dev *ca;
	unsigned i;
	int ret;

	bch_notice(c, "initializing new filesystem");

	mutex_lock(&c->sb_lock);
	c->disk_sb.sb->compat[0] |= cpu_to_le64(1ULL << BCH_COMPAT_extents_above_btree_updates_done);
	c->disk_sb.sb->compat[0] |= cpu_to_le64(1ULL << BCH_COMPAT_bformat_overflow_done);

	bch2_sb_maybe_downgrade(c);

	if (c->opts.version_upgrade != BCH_VERSION_UPGRADE_none) {
		bch2_sb_upgrade(c, bcachefs_metadata_version_current);
		SET_BCH_SB_VERSION_UPGRADE_COMPLETE(c->disk_sb.sb, bcachefs_metadata_version_current);
		bch2_write_super(c);
	}
	mutex_unlock(&c->sb_lock);

	c->curr_recovery_pass = ARRAY_SIZE(recovery_passes);
	set_bit(BCH_FS_MAY_GO_RW, &c->flags);
	set_bit(BCH_FS_FSCK_DONE, &c->flags);

	for (i = 0; i < BTREE_ID_NR; i++)
		bch2_btree_root_alloc(c, i);

	for_each_online_member(ca, c, i)
		bch2_dev_usage_init(ca);

	for_each_online_member(ca, c, i) {
		ret = bch2_dev_journal_alloc(ca);
		if (ret) {
			percpu_ref_put(&ca->io_ref);
			goto err;
		}
	}

	/*
	 * journal_res_get() will crash if called before this has
	 * set up the journal.pin FIFO and journal.cur pointer:
	 */
	bch2_fs_journal_start(&c->journal, 1);
	bch2_journal_set_replay_done(&c->journal);

	ret = bch2_fs_read_write_early(c);
	if (ret)
		goto err;

	/*
	 * Write out the superblock and journal buckets, now that we can do
	 * btree updates
	 */
	bch_verbose(c, "marking superblocks");
	for_each_member_device(ca, c, i) {
		ret = bch2_trans_mark_dev_sb(c, ca);
		if (ret) {
			percpu_ref_put(&ca->ref);
			goto err;
		}

		ca->new_fs_bucket_idx = 0;
	}

	ret = bch2_fs_freespace_init(c);
	if (ret)
		goto err;

	ret = bch2_initialize_subvolumes(c);
	if (ret)
		goto err;

	bch_verbose(c, "reading snapshots table");
	ret = bch2_snapshots_read(c);
	if (ret)
		goto err;
	bch_verbose(c, "reading snapshots done");

	bch2_inode_init(c, &root_inode, 0, 0, S_IFDIR|0755, 0, NULL);
	root_inode.bi_inum	= BCACHEFS_ROOT_INO;
	root_inode.bi_subvol	= BCACHEFS_ROOT_SUBVOL;
	bch2_inode_pack(&packed_inode, &root_inode);
	packed_inode.inode.k.p.snapshot = U32_MAX;

	ret = bch2_btree_insert(c, BTREE_ID_inodes,
				&packed_inode.inode.k_i,
				NULL, NULL, 0);
	if (ret) {
		bch_err_msg(c, ret, "creating root directory");
		goto err;
	}

	bch2_inode_init_early(c, &lostfound_inode);

	ret = bch2_trans_do(c, NULL, NULL, 0,
		bch2_create_trans(&trans,
				  BCACHEFS_ROOT_SUBVOL_INUM,
				  &root_inode, &lostfound_inode,
				  &lostfound,
				  0, 0, S_IFDIR|0700, 0,
				  NULL, NULL, (subvol_inum) { 0 }, 0));
	if (ret) {
		bch_err_msg(c, ret, "creating lost+found");
		goto err;
	}

	if (enabled_qtypes(c)) {
		ret = bch2_fs_quota_read(c);
		if (ret)
			goto err;
	}

	ret = bch2_journal_flush(&c->journal);
	if (ret) {
		bch_err_msg(c, ret, "writing first journal entry");
		goto err;
	}

	mutex_lock(&c->sb_lock);
	SET_BCH_SB_INITIALIZED(c->disk_sb.sb, true);
	SET_BCH_SB_CLEAN(c->disk_sb.sb, false);

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	return 0;
err:
	bch_err_fn(ca, ret);
	return ret;
}
