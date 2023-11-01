// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bset.h"
#include "btree_journal_iter.h"
#include "journal_io.h"

#include <linux/sort.h>

/*
 * For managing keys we read from the journal: until journal replay works normal
 * btree lookups need to be able to find and return keys from the journal where
 * they overwrite what's in the btree, so we have a special iterator and
 * operations for the regular btree iter code to use:
 */

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

int bch2_journal_keys_sort(struct bch_fs *c)
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
