// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_buf.h"
#include "alloc_background.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_io.h"
#include "buckets.h"
#include "dirent.h"
#include "ec.h"
#include "error.h"
#include "fs-common.h"
#include "fsck.h"
#include "journal_io.h"
#include "journal_reclaim.h"
#include "journal_seq_blacklist.h"
#include "move.h"
#include "quota.h"
#include "recovery.h"
#include "replicas.h"
#include "super-io.h"

#include <linux/sort.h>
#include <linux/stat.h>

#define QSTR(n) { { { .len = strlen(n) } }, .name = n }

/* for -o reconstruct_alloc: */
static void drop_alloc_keys(struct journal_keys *keys)
{
	size_t src, dst;

	for (src = 0, dst = 0; src < keys->nr; src++)
		if (keys->d[src].btree_id != BTREE_ID_ALLOC)
			keys->d[dst++] = keys->d[src];

	keys->nr = dst;
}

/* iterate over keys read from the journal: */

static int __journal_key_cmp(enum btree_id	l_btree_id,
			     unsigned		l_level,
			     struct bpos	l_pos,
			     struct journal_key *r)
{
	return (cmp_int(l_btree_id,	r->btree_id) ?:
		cmp_int(l_level,	r->level) ?:
		bkey_cmp(l_pos,	r->k->k.p));
}

static int journal_key_cmp(struct journal_key *l, struct journal_key *r)
{
	return (cmp_int(l->btree_id,	r->btree_id) ?:
		cmp_int(l->level,	r->level) ?:
		bkey_cmp(l->k->k.p,	r->k->k.p));
}

static size_t journal_key_search(struct journal_keys *journal_keys,
				 enum btree_id id, unsigned level,
				 struct bpos pos)
{
	size_t l = 0, r = journal_keys->nr, m;

	while (l < r) {
		m = l + ((r - l) >> 1);
		if (__journal_key_cmp(id, level, pos, &journal_keys->d[m]) > 0)
			l = m + 1;
		else
			r = m;
	}

	BUG_ON(l < journal_keys->nr &&
	       __journal_key_cmp(id, level, pos, &journal_keys->d[l]) > 0);

	BUG_ON(l &&
	       __journal_key_cmp(id, level, pos, &journal_keys->d[l - 1]) <= 0);

	return l;
}

static void journal_iter_fix(struct bch_fs *c, struct journal_iter *iter, unsigned idx)
{
	struct bkey_i *n = iter->keys->d[idx].k;
	struct btree_and_journal_iter *biter =
		container_of(iter, struct btree_and_journal_iter, journal);

	if (iter->idx > idx ||
	    (iter->idx == idx &&
	     biter->last &&
	     bkey_cmp(n->k.p, biter->unpacked.p) <= 0))
		iter->idx++;
}

int bch2_journal_key_insert(struct bch_fs *c, enum btree_id id,
			    unsigned level, struct bkey_i *k)
{
	struct journal_key n = {
		.btree_id	= id,
		.level		= level,
		.k		= k,
		.allocated	= true
	};
	struct journal_keys *keys = &c->journal_keys;
	struct journal_iter *iter;
	unsigned idx = journal_key_search(keys, id, level, k->k.p);

	if (idx < keys->nr &&
	    journal_key_cmp(&n, &keys->d[idx]) == 0) {
		if (keys->d[idx].allocated)
			kfree(keys->d[idx].k);
		keys->d[idx] = n;
		return 0;
	}

	if (keys->nr == keys->size) {
		struct journal_keys new_keys = {
			.nr			= keys->nr,
			.size			= keys->size * 2,
			.journal_seq_base	= keys->journal_seq_base,
		};

		new_keys.d = kvmalloc(sizeof(new_keys.d[0]) * new_keys.size, GFP_KERNEL);
		if (!new_keys.d) {
			bch_err(c, "%s: error allocating new key array (size %zu)",
				__func__, new_keys.size);
			return -ENOMEM;
		}

		memcpy(new_keys.d, keys->d, sizeof(keys->d[0]) * keys->nr);
		kvfree(keys->d);
		*keys = new_keys;
	}

	array_insert_item(keys->d, keys->nr, idx, n);

	list_for_each_entry(iter, &c->journal_iters, list)
		journal_iter_fix(c, iter, idx);

	return 0;
}

int bch2_journal_key_delete(struct bch_fs *c, enum btree_id id,
			    unsigned level, struct bpos pos)
{
	struct bkey_i *whiteout =
		kmalloc(sizeof(struct bkey), GFP_KERNEL);
	int ret;

	if (!whiteout) {
		bch_err(c, "%s: error allocating new key", __func__);
		return -ENOMEM;
	}

	bkey_init(&whiteout->k);
	whiteout->k.p = pos;

	ret = bch2_journal_key_insert(c, id, level, whiteout);
	if (ret)
		kfree(whiteout);
	return ret;
}

static struct bkey_i *bch2_journal_iter_peek(struct journal_iter *iter)
{
	struct journal_key *k = iter->idx - iter->keys->nr
		? iter->keys->d + iter->idx : NULL;

	if (k &&
	    k->btree_id	== iter->btree_id &&
	    k->level	== iter->level)
		return k->k;

	iter->idx = iter->keys->nr;
	return NULL;
}

static void bch2_journal_iter_advance(struct journal_iter *iter)
{
	if (iter->idx < iter->keys->nr)
		iter->idx++;
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
	iter->idx	= journal_key_search(&c->journal_keys, id, level, pos);
	list_add(&iter->list, &c->journal_iters);
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
	switch (iter->last) {
	case none:
		break;
	case btree:
		bch2_journal_iter_advance_btree(iter);
		break;
	case journal:
		bch2_journal_iter_advance(&iter->journal);
		break;
	}

	iter->last = none;
}

struct bkey_s_c bch2_btree_and_journal_iter_peek(struct btree_and_journal_iter *iter)
{
	struct bkey_s_c ret;

	while (1) {
		struct bkey_s_c btree_k		=
			bch2_journal_iter_peek_btree(iter);
		struct bkey_s_c journal_k	=
			bkey_i_to_s_c(bch2_journal_iter_peek(&iter->journal));

		if (btree_k.k && journal_k.k) {
			int cmp = bkey_cmp(btree_k.k->p, journal_k.k->p);

			if (!cmp)
				bch2_journal_iter_advance_btree(iter);

			iter->last = cmp < 0 ? btree : journal;
		} else if (btree_k.k) {
			iter->last = btree;
		} else if (journal_k.k) {
			iter->last = journal;
		} else {
			iter->last = none;
			return bkey_s_c_null;
		}

		ret = iter->last == journal ? journal_k : btree_k;

		if (iter->b &&
		    bkey_cmp(ret.k->p, iter->b->data->max_key) > 0) {
			iter->journal.idx = iter->journal.keys->nr;
			iter->last = none;
			return bkey_s_c_null;
		}

		if (!bkey_deleted(ret.k))
			break;

		bch2_btree_and_journal_iter_advance(iter);
	}

	return ret;
}

struct bkey_s_c bch2_btree_and_journal_iter_next(struct btree_and_journal_iter *iter)
{
	bch2_btree_and_journal_iter_advance(iter);

	return bch2_btree_and_journal_iter_peek(iter);
}

void bch2_btree_and_journal_iter_exit(struct btree_and_journal_iter *iter)
{
	bch2_journal_iter_exit(&iter->journal);
}

void bch2_btree_and_journal_iter_init_node_iter(struct btree_and_journal_iter *iter,
						struct bch_fs *c,
						struct btree *b)
{
	memset(iter, 0, sizeof(*iter));

	iter->b = b;
	bch2_btree_node_iter_init_from_start(&iter->node_iter, iter->b);
	bch2_journal_iter_init(c, &iter->journal,
			       b->c.btree_id, b->c.level, b->data->min_key);
}

/* Walk btree, overlaying keys from the journal: */

static void btree_and_journal_iter_prefetch(struct bch_fs *c, struct btree *b,
					   struct btree_and_journal_iter iter)
{
	unsigned i = 0, nr = b->c.level > 1 ? 2 : 16;
	struct bkey_s_c k;
	struct bkey_buf tmp;

	BUG_ON(!b->c.level);

	bch2_bkey_buf_init(&tmp);

	while (i < nr &&
	       (k = bch2_btree_and_journal_iter_peek(&iter)).k) {
		bch2_bkey_buf_reassemble(&tmp, c, k);

		bch2_btree_node_prefetch(c, NULL, tmp.k,
					b->c.btree_id, b->c.level - 1);

		bch2_btree_and_journal_iter_advance(&iter);
		i++;
	}

	bch2_bkey_buf_exit(&tmp, c);
}

static int bch2_btree_and_journal_walk_recurse(struct bch_fs *c, struct btree *b,
				struct journal_keys *journal_keys,
				enum btree_id btree_id,
				btree_walk_node_fn node_fn,
				btree_walk_key_fn key_fn)
{
	struct btree_and_journal_iter iter;
	struct bkey_s_c k;
	struct bkey_buf tmp;
	struct btree *child;
	int ret = 0;

	bch2_bkey_buf_init(&tmp);
	bch2_btree_and_journal_iter_init_node_iter(&iter, c, b);

	while ((k = bch2_btree_and_journal_iter_peek(&iter)).k) {
		ret = key_fn(c, btree_id, b->c.level, k);
		if (ret)
			break;

		if (b->c.level) {
			bch2_bkey_buf_reassemble(&tmp, c, k);

			bch2_btree_and_journal_iter_advance(&iter);

			child = bch2_btree_node_get_noiter(c, tmp.k,
						b->c.btree_id, b->c.level - 1,
						false);

			ret = PTR_ERR_OR_ZERO(child);
			if (ret)
				break;

			btree_and_journal_iter_prefetch(c, b, iter);

			ret   = (node_fn ? node_fn(c, b) : 0) ?:
				bch2_btree_and_journal_walk_recurse(c, child,
					journal_keys, btree_id, node_fn, key_fn);
			six_unlock_read(&child->c.lock);

			if (ret)
				break;
		} else {
			bch2_btree_and_journal_iter_advance(&iter);
		}
	}

	bch2_btree_and_journal_iter_exit(&iter);
	bch2_bkey_buf_exit(&tmp, c);
	return ret;
}

int bch2_btree_and_journal_walk(struct bch_fs *c, struct journal_keys *journal_keys,
				enum btree_id btree_id,
				btree_walk_node_fn node_fn,
				btree_walk_key_fn key_fn)
{
	struct btree *b = c->btree_roots[btree_id].b;
	int ret = 0;

	if (btree_node_fake(b))
		return 0;

	six_lock_read(&b->c.lock, NULL, NULL);
	ret   = (node_fn ? node_fn(c, b) : 0) ?:
		bch2_btree_and_journal_walk_recurse(c, b, journal_keys, btree_id,
						    node_fn, key_fn) ?:
		key_fn(c, btree_id, b->c.level + 1, bkey_i_to_s_c(&b->key));
	six_unlock_read(&b->c.lock);

	return ret;
}

/* sort and dedup all keys in the journal: */

void bch2_journal_entries_free(struct list_head *list)
{

	while (!list_empty(list)) {
		struct journal_replay *i =
			list_first_entry(list, struct journal_replay, list);
		list_del(&i->list);
		kvpfree(i, offsetof(struct journal_replay, j) +
			vstruct_bytes(&i->j));
	}
}

/*
 * When keys compare equal, oldest compares first:
 */
static int journal_sort_key_cmp(const void *_l, const void *_r)
{
	const struct journal_key *l = _l;
	const struct journal_key *r = _r;

	return  cmp_int(l->btree_id,	r->btree_id) ?:
		cmp_int(l->level,	r->level) ?:
		bkey_cmp(l->k->k.p, r->k->k.p) ?:
		cmp_int(l->journal_seq, r->journal_seq) ?:
		cmp_int(l->journal_offset, r->journal_offset);
}

void bch2_journal_keys_free(struct journal_keys *keys)
{
	struct journal_key *i;

	for (i = keys->d; i < keys->d + keys->nr; i++)
		if (i->allocated)
			kfree(i->k);

	kvfree(keys->d);
	keys->d = NULL;
	keys->nr = 0;
}

static struct journal_keys journal_keys_sort(struct list_head *journal_entries)
{
	struct journal_replay *i;
	struct jset_entry *entry;
	struct bkey_i *k, *_n;
	struct journal_keys keys = { NULL };
	struct journal_key *src, *dst;
	size_t nr_keys = 0;

	if (list_empty(journal_entries))
		return keys;

	list_for_each_entry(i, journal_entries, list) {
		if (i->ignore)
			continue;

		if (!keys.journal_seq_base)
			keys.journal_seq_base = le64_to_cpu(i->j.seq);

		for_each_jset_key(k, _n, entry, &i->j)
			nr_keys++;
	}

	keys.size = roundup_pow_of_two(nr_keys);

	keys.d = kvmalloc(sizeof(keys.d[0]) * keys.size, GFP_KERNEL);
	if (!keys.d)
		goto err;

	list_for_each_entry(i, journal_entries, list) {
		if (i->ignore)
			continue;

		BUG_ON(le64_to_cpu(i->j.seq) - keys.journal_seq_base > U32_MAX);

		for_each_jset_key(k, _n, entry, &i->j)
			keys.d[keys.nr++] = (struct journal_key) {
				.btree_id	= entry->btree_id,
				.level		= entry->level,
				.k		= k,
				.journal_seq	= le64_to_cpu(i->j.seq) -
					keys.journal_seq_base,
				.journal_offset	= k->_data - i->j._data,
			};
	}

	sort(keys.d, keys.nr, sizeof(keys.d[0]), journal_sort_key_cmp, NULL);

	src = dst = keys.d;
	while (src < keys.d + keys.nr) {
		while (src + 1 < keys.d + keys.nr &&
		       src[0].btree_id	== src[1].btree_id &&
		       src[0].level	== src[1].level &&
		       !bkey_cmp(src[0].k->k.p, src[1].k->k.p))
			src++;

		*dst++ = *src++;
	}

	keys.nr = dst - keys.d;
err:
	return keys;
}

/* journal replay: */

static void replay_now_at(struct journal *j, u64 seq)
{
	BUG_ON(seq < j->replay_journal_seq);
	BUG_ON(seq > j->replay_journal_seq_end);

	while (j->replay_journal_seq < seq)
		bch2_journal_pin_put(j, j->replay_journal_seq++);
}

static int __bch2_journal_replay_key(struct btree_trans *trans,
				     enum btree_id id, unsigned level,
				     struct bkey_i *k)
{
	struct btree_iter *iter;
	int ret;

	iter = bch2_trans_get_node_iter(trans, id, k->k.p,
					BTREE_MAX_DEPTH, level,
					BTREE_ITER_INTENT);

	/*
	 * iter->flags & BTREE_ITER_IS_EXTENTS triggers the update path to run
	 * extent_handle_overwrites() and extent_update_to_keys() - but we don't
	 * want that here, journal replay is supposed to treat extents like
	 * regular keys:
	 */
	__bch2_btree_iter_set_pos(iter, k->k.p, false);

	ret   = bch2_btree_iter_traverse(iter) ?:
		bch2_trans_update(trans, iter, k, BTREE_TRIGGER_NORUN);
	bch2_trans_iter_put(trans, iter);
	return ret;
}

static int bch2_journal_replay_key(struct bch_fs *c, struct journal_key *k)
{
	unsigned commit_flags = BTREE_INSERT_NOFAIL|
		BTREE_INSERT_LAZY_RW;

	if (!k->allocated)
		commit_flags |= BTREE_INSERT_JOURNAL_REPLAY;

	return bch2_trans_do(c, NULL, NULL, commit_flags,
			     __bch2_journal_replay_key(&trans, k->btree_id, k->level, k->k));
}

static int __bch2_alloc_replay_key(struct btree_trans *trans, struct bkey_i *k)
{
	struct btree_iter *iter;
	int ret;

	iter = bch2_trans_get_iter(trans, BTREE_ID_ALLOC, k->k.p,
				   BTREE_ITER_CACHED|
				   BTREE_ITER_CACHED_NOFILL|
				   BTREE_ITER_INTENT);
	ret = bch2_trans_update(trans, iter, k, BTREE_TRIGGER_NORUN);
	bch2_trans_iter_put(trans, iter);
	return ret;
}

static int bch2_alloc_replay_key(struct bch_fs *c, struct bkey_i *k)
{
	return bch2_trans_do(c, NULL, NULL,
			     BTREE_INSERT_NOFAIL|
			     BTREE_INSERT_USE_RESERVE|
			     BTREE_INSERT_LAZY_RW|
			     BTREE_INSERT_JOURNAL_REPLAY,
			__bch2_alloc_replay_key(&trans, k));
}

static int journal_sort_seq_cmp(const void *_l, const void *_r)
{
	const struct journal_key *l = _l;
	const struct journal_key *r = _r;

	return  cmp_int(r->level,	l->level) ?:
		cmp_int(l->journal_seq, r->journal_seq) ?:
		cmp_int(l->btree_id,	r->btree_id) ?:
		bkey_cmp(l->k->k.p,	r->k->k.p);
}

static int bch2_journal_replay(struct bch_fs *c,
			       struct journal_keys keys)
{
	struct journal *j = &c->journal;
	struct journal_key *i;
	u64 seq;
	int ret;

	sort(keys.d, keys.nr, sizeof(keys.d[0]), journal_sort_seq_cmp, NULL);

	if (keys.nr)
		replay_now_at(j, keys.journal_seq_base);

	seq = j->replay_journal_seq;

	/*
	 * First replay updates to the alloc btree - these will only update the
	 * btree key cache:
	 */
	for_each_journal_key(keys, i) {
		cond_resched();

		if (!i->level && i->btree_id == BTREE_ID_ALLOC) {
			j->replay_journal_seq = keys.journal_seq_base + i->journal_seq;
			ret = bch2_alloc_replay_key(c, i->k);
			if (ret)
				goto err;
		}
	}

	/*
	 * Next replay updates to interior btree nodes:
	 */
	for_each_journal_key(keys, i) {
		cond_resched();

		if (i->level) {
			j->replay_journal_seq = keys.journal_seq_base + i->journal_seq;
			ret = bch2_journal_replay_key(c, i);
			if (ret)
				goto err;
		}
	}

	/*
	 * Now that the btree is in a consistent state, we can start journal
	 * reclaim (which will be flushing entries from the btree key cache back
	 * to the btree:
	 */
	set_bit(BCH_FS_BTREE_INTERIOR_REPLAY_DONE, &c->flags);
	set_bit(JOURNAL_RECLAIM_STARTED, &j->flags);
	journal_reclaim_kick(j);

	j->replay_journal_seq = seq;

	/*
	 * Now replay leaf node updates:
	 */
	for_each_journal_key(keys, i) {
		cond_resched();

		if (i->level || i->btree_id == BTREE_ID_ALLOC)
			continue;

		replay_now_at(j, keys.journal_seq_base + i->journal_seq);

		ret = bch2_journal_replay_key(c, i);
		if (ret)
			goto err;
	}

	replay_now_at(j, j->replay_journal_seq_end);
	j->replay_journal_seq = 0;

	bch2_journal_set_replay_done(j);
	bch2_journal_flush_all_pins(j);
	return bch2_journal_error(j);
err:
	bch_err(c, "journal replay: error %d while replaying key at btree %s level %u",
		ret, bch2_btree_ids[i->btree_id], i->level);
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

		if (entry->btree_id >= BTREE_ID_NR) {
			bch_err(c, "filesystem has unknown btree type %u",
				entry->btree_id);
			return -EINVAL;
		}

		r = &c->btree_roots[entry->btree_id];

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
		case FS_USAGE_RESERVED:
			if (entry->level < BCH_REPLICAS_MAX)
				c->usage_base->persistent_reserved[entry->level] =
					le64_to_cpu(u->v);
			break;
		case FS_USAGE_INODES:
			c->usage_base->nr_inodes = le64_to_cpu(u->v);
			break;
		case FS_USAGE_KEY_VERSION:
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
		struct bch_dev *ca = bch_dev_bkey_exists(c, u->dev);
		unsigned bytes = jset_u64s(le16_to_cpu(entry->u64s)) * sizeof(u64);
		unsigned nr_types = (bytes - sizeof(struct jset_entry_dev_usage)) /
			sizeof(struct jset_entry_dev_usage_type);
		unsigned i;

		ca->usage_base->buckets_ec		= le64_to_cpu(u->buckets_ec);
		ca->usage_base->buckets_unavailable	= le64_to_cpu(u->buckets_unavailable);

		for (i = 0; i < nr_types; i++) {
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

		atomic64_set(&c->io_clock[clock->rw].now, clock->time);
	}
	}

	return ret;
}

static int journal_replay_early(struct bch_fs *c,
				struct bch_sb_field_clean *clean,
				struct list_head *journal)
{
	struct journal_replay *i;
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
		list_for_each_entry(i, journal, list) {
			if (i->ignore)
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
		char buf1[200], buf2[200];
		struct bkey_i *k1, *k2;
		unsigned l1 = 0, l2 = 0;

		k1 = btree_root_find(c, clean, NULL, i, &l1);
		k2 = btree_root_find(c, NULL, j, i, &l2);

		if (!k1 && !k2)
			continue;

		mustfix_fsck_err_on(!k1 || !k2 ||
				    IS_ERR(k1) ||
				    IS_ERR(k2) ||
				    k1->k.u64s != k2->k.u64s ||
				    memcmp(k1, k2, bkey_bytes(k1)) ||
				    l1 != l2, c,
			"superblock btree root %u doesn't match journal after clean shutdown\n"
			"sb:      l=%u %s\n"
			"journal: l=%u %s\n", i,
			l1, (bch2_bkey_val_to_text(&PBUF(buf1), c, bkey_i_to_s_c(k1)), buf1),
			l2, (bch2_bkey_val_to_text(&PBUF(buf2), c, bkey_i_to_s_c(k2)), buf2));
	}
fsck_err:
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
		return ERR_PTR(-ENOMEM);
	}

	if (le16_to_cpu(c->disk_sb.sb->version) <
	    bcachefs_metadata_version_bkey_renumber)
		bch2_sb_clean_renumber(clean, READ);

	mutex_unlock(&c->sb_lock);

	return clean;
fsck_err:
	mutex_unlock(&c->sb_lock);
	return ERR_PTR(ret);
}

static int read_btree_roots(struct bch_fs *c)
{
	unsigned i;
	int ret = 0;

	for (i = 0; i < BTREE_ID_NR; i++) {
		struct btree_root *r = &c->btree_roots[i];

		if (!r->alive)
			continue;

		if (i == BTREE_ID_ALLOC &&
		    c->opts.reconstruct_alloc) {
			c->sb.compat &= ~(1ULL << BCH_COMPAT_alloc_info);
			continue;
		}

		if (r->error) {
			__fsck_err(c, i == BTREE_ID_ALLOC
				   ? FSCK_CAN_IGNORE : 0,
				   "invalid btree root %s",
				   bch2_btree_ids[i]);
			if (i == BTREE_ID_ALLOC)
				c->sb.compat &= ~(1ULL << BCH_COMPAT_alloc_info);
		}

		ret = bch2_btree_root_read(c, i, &r->key, r->level);
		if (ret) {
			__fsck_err(c, i == BTREE_ID_ALLOC
				   ? FSCK_CAN_IGNORE : 0,
				   "error reading btree root %s",
				   bch2_btree_ids[i]);
			if (i == BTREE_ID_ALLOC)
				c->sb.compat &= ~(1ULL << BCH_COMPAT_alloc_info);
		}
	}

	for (i = 0; i < BTREE_ID_NR; i++)
		if (!c->btree_roots[i].b)
			bch2_btree_root_alloc(c, i);
fsck_err:
	return ret;
}

int bch2_fs_recovery(struct bch_fs *c)
{
	const char *err = "cannot allocate memory";
	struct bch_sb_field_clean *clean = NULL;
	struct jset *last_journal_entry = NULL;
	u64 blacklist_seq, journal_seq;
	bool write_sb = false;
	int ret;

	if (c->sb.clean)
		clean = read_superblock_clean(c);
	ret = PTR_ERR_OR_ZERO(clean);
	if (ret)
		goto err;

	if (c->sb.clean)
		bch_info(c, "recovering from clean shutdown, journal seq %llu",
			 le64_to_cpu(clean->journal_seq));

	if (!(c->sb.features & (1ULL << BCH_FEATURE_alloc_v2))) {
		bch_info(c, "alloc_v2 feature bit not set, fsck required");
		c->opts.fsck = true;
		c->opts.fix_errors = FSCK_OPT_YES;
		c->disk_sb.sb->features[0] |= 1ULL << BCH_FEATURE_alloc_v2;
	}

	if (!c->replicas.entries ||
	    c->opts.rebuild_replicas) {
		bch_info(c, "building replicas info");
		set_bit(BCH_FS_REBUILD_REPLICAS, &c->flags);
	}

	ret = bch2_blacklist_table_initialize(c);
	if (ret) {
		bch_err(c, "error initializing blacklist table");
		goto err;
	}

	if (!c->sb.clean || c->opts.fsck || c->opts.keep_journal) {
		struct journal_replay *i;

		ret = bch2_journal_read(c, &c->journal_entries,
					&blacklist_seq, &journal_seq);
		if (ret)
			goto err;

		list_for_each_entry_reverse(i, &c->journal_entries, list)
			if (!i->ignore) {
				last_journal_entry = &i->j;
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
			goto use_clean;
		}

		c->journal_keys = journal_keys_sort(&c->journal_entries);
		if (!c->journal_keys.d) {
			ret = -ENOMEM;
			goto err;
		}

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
			ret = BCH_FSCK_REPAIR_IMPOSSIBLE;
			goto err;

		}
		blacklist_seq = journal_seq = le64_to_cpu(clean->journal_seq) + 1;
	}

	if (!c->sb.clean &&
	    !(c->sb.features & (1ULL << BCH_FEATURE_extents_above_btree_updates))) {
		bch_err(c, "filesystem needs recovery from older version; run fsck from older bcachefs-tools to fix");
		ret = -EINVAL;
		goto err;
	}

	if (c->opts.reconstruct_alloc) {
		c->sb.compat &= ~(1ULL << BCH_COMPAT_alloc_info);
		drop_alloc_keys(&c->journal_keys);
	}

	ret = journal_replay_early(c, clean, &c->journal_entries);
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
		ret = bch2_journal_seq_blacklist_add(c,
					blacklist_seq, journal_seq);
		if (ret) {
			bch_err(c, "error creating new journal seq blacklist entry");
			goto err;
		}
	}

	ret = bch2_fs_journal_start(&c->journal, journal_seq,
				    &c->journal_entries);
	if (ret)
		goto err;

	ret = read_btree_roots(c);
	if (ret)
		goto err;

	bch_verbose(c, "starting alloc read");
	err = "error reading allocation information";
	ret = bch2_alloc_read(c, &c->journal_keys);
	if (ret)
		goto err;
	bch_verbose(c, "alloc read done");

	bch_verbose(c, "starting stripes_read");
	err = "error reading stripes";
	ret = bch2_stripes_read(c, &c->journal_keys);
	if (ret)
		goto err;
	bch_verbose(c, "stripes_read done");

	set_bit(BCH_FS_ALLOC_READ_DONE, &c->flags);

	if (c->opts.fsck ||
	    !(c->sb.compat & (1ULL << BCH_COMPAT_alloc_info)) ||
	    !(c->sb.compat & (1ULL << BCH_COMPAT_alloc_metadata)) ||
	    test_bit(BCH_FS_REBUILD_REPLICAS, &c->flags)) {
		bool metadata_only = c->opts.norecovery;

		bch_info(c, "starting mark and sweep");
		err = "error in mark and sweep";
		ret = bch2_gc(c, true, metadata_only);
		if (ret)
			goto err;
		bch_verbose(c, "mark and sweep done");
	}

	bch2_stripes_heap_start(c);

	clear_bit(BCH_FS_REBUILD_REPLICAS, &c->flags);
	set_bit(BCH_FS_INITIAL_GC_DONE, &c->flags);

	/*
	 * Skip past versions that might have possibly been used (as nonces),
	 * but hadn't had their pointers written:
	 */
	if (c->sb.encryption_type && !c->sb.clean)
		atomic64_add(1 << 16, &c->key_version);

	if (c->opts.norecovery)
		goto out;

	bch_verbose(c, "starting journal replay");
	err = "journal replay failed";
	ret = bch2_journal_replay(c, c->journal_keys);
	if (ret)
		goto err;
	bch_verbose(c, "journal replay done");

	if (test_bit(BCH_FS_NEED_ALLOC_WRITE, &c->flags) &&
	    !c->opts.nochanges) {
		/*
		 * note that even when filesystem was clean there might be work
		 * to do here, if we ran gc (because of fsck) which recalculated
		 * oldest_gen:
		 */
		bch_verbose(c, "writing allocation info");
		err = "error writing out alloc info";
		ret = bch2_stripes_write(c, BTREE_INSERT_LAZY_RW) ?:
			bch2_alloc_write(c, BTREE_INSERT_LAZY_RW);
		if (ret) {
			bch_err(c, "error writing alloc info");
			goto err;
		}
		bch_verbose(c, "alloc write done");
	}

	if (!c->sb.clean) {
		if (!(c->sb.features & (1 << BCH_FEATURE_atomic_nlink))) {
			bch_info(c, "checking inode link counts");
			err = "error in recovery";
			ret = bch2_fsck_inode_nlink(c);
			if (ret)
				goto err;
			bch_verbose(c, "check inodes done");

		} else {
			bch_verbose(c, "checking for deleted inodes");
			err = "error in recovery";
			ret = bch2_fsck_walk_inodes_only(c);
			if (ret)
				goto err;
			bch_verbose(c, "check inodes done");
		}
	}

	if (c->opts.fsck) {
		bch_info(c, "starting fsck");
		err = "error in fsck";
		ret = bch2_fsck_full(c);
		if (ret)
			goto err;
		bch_verbose(c, "fsck done");
	}

	if (enabled_qtypes(c)) {
		bch_verbose(c, "reading quotas");
		ret = bch2_fs_quota_read(c);
		if (ret)
			goto err;
		bch_verbose(c, "quotas done");
	}

	if (!(c->sb.compat & (1ULL << BCH_COMPAT_extents_above_btree_updates_done)) ||
	    !(c->sb.compat & (1ULL << BCH_COMPAT_bformat_overflow_done))) {
		struct bch_move_stats stats = { 0 };

		bch_info(c, "scanning for old btree nodes");
		ret = bch2_fs_read_write(c);
		if (ret)
			goto err;

		ret = bch2_scan_old_btree_nodes(c, &stats);
		if (ret)
			goto err;
		bch_info(c, "scanning for old btree nodes done");
	}

	mutex_lock(&c->sb_lock);
	if (c->opts.version_upgrade) {
		if (c->sb.version < bcachefs_metadata_version_new_versioning)
			c->disk_sb.sb->version_min =
				le16_to_cpu(bcachefs_metadata_version_min);
		c->disk_sb.sb->version = le16_to_cpu(bcachefs_metadata_version_current);
		c->disk_sb.sb->features[0] |= BCH_SB_FEATURES_ALL;
		write_sb = true;
	}

	if (!test_bit(BCH_FS_ERROR, &c->flags)) {
		c->disk_sb.sb->compat[0] |= 1ULL << BCH_COMPAT_alloc_info;
		write_sb = true;
	}

	if (c->opts.fsck &&
	    !test_bit(BCH_FS_ERROR, &c->flags)) {
		c->disk_sb.sb->features[0] |= 1ULL << BCH_FEATURE_atomic_nlink;
		SET_BCH_SB_HAS_ERRORS(c->disk_sb.sb, 0);
		write_sb = true;
	}

	if (write_sb)
		bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	if (c->journal_seq_blacklist_table &&
	    c->journal_seq_blacklist_table->nr > 128)
		queue_work(system_long_wq, &c->journal_seq_blacklist_gc_work);
out:
	ret = 0;
err:
fsck_err:
	set_bit(BCH_FS_FSCK_DONE, &c->flags);
	bch2_flush_fsck_errs(c);

	if (!c->opts.keep_journal) {
		bch2_journal_keys_free(&c->journal_keys);
		bch2_journal_entries_free(&c->journal_entries);
	}
	kfree(clean);
	if (ret)
		bch_err(c, "Error in recovery: %s (%i)", err, ret);
	else
		bch_verbose(c, "ret %i", ret);
	return ret;
}

int bch2_fs_initialize(struct bch_fs *c)
{
	struct bch_inode_unpacked root_inode, lostfound_inode;
	struct bkey_inode_buf packed_inode;
	struct qstr lostfound = QSTR("lost+found");
	const char *err = "cannot allocate memory";
	struct bch_dev *ca;
	LIST_HEAD(journal);
	unsigned i;
	int ret;

	bch_notice(c, "initializing new filesystem");

	mutex_lock(&c->sb_lock);
	for_each_online_member(ca, c, i)
		bch2_mark_dev_superblock(c, ca, 0);
	mutex_unlock(&c->sb_lock);

	mutex_lock(&c->sb_lock);
	c->disk_sb.sb->version = c->disk_sb.sb->version_min =
		le16_to_cpu(bcachefs_metadata_version_current);
	c->disk_sb.sb->features[0] |= 1ULL << BCH_FEATURE_atomic_nlink;
	c->disk_sb.sb->features[0] |= BCH_SB_FEATURES_ALL;
	c->disk_sb.sb->compat[0] |= 1ULL << BCH_COMPAT_extents_above_btree_updates_done;
	c->disk_sb.sb->compat[0] |= 1ULL << BCH_COMPAT_bformat_overflow_done;

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	set_bit(BCH_FS_ALLOC_READ_DONE, &c->flags);
	set_bit(BCH_FS_INITIAL_GC_DONE, &c->flags);

	for (i = 0; i < BTREE_ID_NR; i++)
		bch2_btree_root_alloc(c, i);

	set_bit(BCH_FS_BTREE_INTERIOR_REPLAY_DONE, &c->flags);
	set_bit(JOURNAL_RECLAIM_STARTED, &c->journal.flags);

	err = "unable to allocate journal buckets";
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
	bch2_fs_journal_start(&c->journal, 1, &journal);
	bch2_journal_set_replay_done(&c->journal);

	err = "error going read-write";
	ret = bch2_fs_read_write_early(c);
	if (ret)
		goto err;

	/*
	 * Write out the superblock and journal buckets, now that we can do
	 * btree updates
	 */
	err = "error writing alloc info";
	ret = bch2_alloc_write(c, 0);
	if (ret)
		goto err;

	bch2_inode_init(c, &root_inode, 0, 0,
			S_IFDIR|S_IRWXU|S_IRUGO|S_IXUGO, 0, NULL);
	root_inode.bi_inum = BCACHEFS_ROOT_INO;
	bch2_inode_pack(c, &packed_inode, &root_inode);

	err = "error creating root directory";
	ret = bch2_btree_insert(c, BTREE_ID_INODES,
				&packed_inode.inode.k_i,
				NULL, NULL, 0);
	if (ret)
		goto err;

	bch2_inode_init_early(c, &lostfound_inode);

	err = "error creating lost+found";
	ret = bch2_trans_do(c, NULL, NULL, 0,
		bch2_create_trans(&trans, BCACHEFS_ROOT_INO,
				  &root_inode, &lostfound_inode,
				  &lostfound,
				  0, 0, S_IFDIR|0700, 0,
				  NULL, NULL));
	if (ret) {
		bch_err(c, "error creating lost+found");
		goto err;
	}

	if (enabled_qtypes(c)) {
		ret = bch2_fs_quota_read(c);
		if (ret)
			goto err;
	}

	err = "error writing first journal entry";
	ret = bch2_journal_meta(&c->journal);
	if (ret)
		goto err;

	mutex_lock(&c->sb_lock);
	SET_BCH_SB_INITIALIZED(c->disk_sb.sb, true);
	SET_BCH_SB_CLEAN(c->disk_sb.sb, false);

	bch2_write_super(c);
	mutex_unlock(&c->sb_lock);

	return 0;
err:
	pr_err("Error initializing new filesystem: %s (%i)", err, ret);
	return ret;
}
