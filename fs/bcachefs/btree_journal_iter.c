// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_buf.h"
#include "bset.h"
#include "btree_cache.h"
#include "btree_journal_iter.h"
#include "journal_io.h"

#include <linux/sort.h>

/*
 * For managing keys we read from the journal: until journal replay works normal
 * btree lookups need to be able to find and return keys from the journal where
 * they overwrite what's in the btree, so we have a special iterator and
 * operations for the regular btree iter code to use:
 */

static inline size_t pos_to_idx(struct journal_keys *keys, size_t pos)
{
	size_t gap_size = keys->size - keys->nr;

	BUG_ON(pos >= keys->gap && pos < keys->gap + gap_size);

	if (pos >= keys->gap)
		pos -= gap_size;
	return pos;
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
	return keys->data + idx_to_pos(keys, idx);
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

/* Returns first non-overwritten key >= search key: */
struct bkey_i *bch2_journal_keys_peek_max(struct bch_fs *c, enum btree_id btree_id,
					   unsigned level, struct bpos pos,
					   struct bpos end_pos, size_t *idx)
{
	struct journal_keys *keys = &c->journal_keys;
	unsigned iters = 0;
	struct journal_key *k;

	BUG_ON(*idx > keys->nr);
search:
	if (!*idx)
		*idx = __bch2_journal_key_search(keys, btree_id, level, pos);

	while (*idx &&
	       __journal_key_cmp(btree_id, level, end_pos, idx_to_key(keys, *idx - 1)) <= 0) {
		--(*idx);
		iters++;
		if (iters == 10) {
			*idx = 0;
			goto search;
		}
	}

	struct bkey_i *ret = NULL;
	rcu_read_lock(); /* for overwritten_ranges */

	while ((k = *idx < keys->nr ? idx_to_key(keys, *idx) : NULL)) {
		if (__journal_key_cmp(btree_id, level, end_pos, k) < 0)
			break;

		if (k->overwritten) {
			if (k->overwritten_range)
				*idx = rcu_dereference(k->overwritten_range)->end;
			else
				*idx += 1;
			continue;
		}

		if (__journal_key_cmp(btree_id, level, pos, k) <= 0) {
			ret = k->k;
			break;
		}

		(*idx)++;
		iters++;
		if (iters == 10) {
			*idx = 0;
			rcu_read_unlock();
			goto search;
		}
	}

	rcu_read_unlock();
	return ret;
}

struct bkey_i *bch2_journal_keys_peek_prev_min(struct bch_fs *c, enum btree_id btree_id,
					   unsigned level, struct bpos pos,
					   struct bpos end_pos, size_t *idx)
{
	struct journal_keys *keys = &c->journal_keys;
	unsigned iters = 0;
	struct journal_key *k;

	BUG_ON(*idx > keys->nr);
search:
	if (!*idx)
		*idx = __bch2_journal_key_search(keys, btree_id, level, pos);

	while (*idx &&
	       __journal_key_cmp(btree_id, level, end_pos, idx_to_key(keys, *idx - 1)) <= 0) {
		(*idx)++;
		iters++;
		if (iters == 10) {
			*idx = 0;
			goto search;
		}
	}

	struct bkey_i *ret = NULL;
	rcu_read_lock(); /* for overwritten_ranges */

	while ((k = *idx < keys->nr ? idx_to_key(keys, *idx) : NULL)) {
		if (__journal_key_cmp(btree_id, level, end_pos, k) > 0)
			break;

		if (k->overwritten) {
			if (k->overwritten_range)
				*idx = rcu_dereference(k->overwritten_range)->start - 1;
			else
				*idx -= 1;
			continue;
		}

		if (__journal_key_cmp(btree_id, level, pos, k) >= 0) {
			ret = k->k;
			break;
		}

		--(*idx);
		iters++;
		if (iters == 10) {
			*idx = 0;
			goto search;
		}
	}

	rcu_read_unlock();
	return ret;
}

struct bkey_i *bch2_journal_keys_peek_slot(struct bch_fs *c, enum btree_id btree_id,
					   unsigned level, struct bpos pos)
{
	size_t idx = 0;

	return bch2_journal_keys_peek_max(c, btree_id, level, pos, pos, &idx);
}

static void journal_iter_verify(struct journal_iter *iter)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	struct journal_keys *keys = iter->keys;
	size_t gap_size = keys->size - keys->nr;

	BUG_ON(iter->idx >= keys->gap &&
	       iter->idx <  keys->gap + gap_size);

	if (iter->idx < keys->size) {
		struct journal_key *k = keys->data + iter->idx;

		int cmp = __journal_key_btree_cmp(iter->btree_id, iter->level, k);
		BUG_ON(cmp > 0);
	}
#endif
}

static void journal_iters_fix(struct bch_fs *c)
{
	struct journal_keys *keys = &c->journal_keys;
	/* The key we just inserted is immediately before the gap: */
	size_t gap_end = keys->gap + (keys->size - keys->nr);
	struct journal_key *new_key = &keys->data[keys->gap - 1];
	struct journal_iter *iter;

	/*
	 * If an iterator points one after the key we just inserted, decrement
	 * the iterator so it points at the key we just inserted - if the
	 * decrement was unnecessary, bch2_btree_and_journal_iter_peek() will
	 * handle that:
	 */
	list_for_each_entry(iter, &c->journal_iters, list) {
		journal_iter_verify(iter);
		if (iter->idx		== gap_end &&
		    new_key->btree_id	== iter->btree_id &&
		    new_key->level	== iter->level)
			iter->idx = keys->gap - 1;
		journal_iter_verify(iter);
	}
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
		.journal_seq	= U64_MAX,
	};
	struct journal_keys *keys = &c->journal_keys;
	size_t idx = bch2_journal_key_search(keys, id, level, k->k.p);

	BUG_ON(test_bit(BCH_FS_rw, &c->flags));

	if (idx < keys->size &&
	    journal_key_cmp(&n, &keys->data[idx]) == 0) {
		if (keys->data[idx].allocated)
			kfree(keys->data[idx].k);
		keys->data[idx] = n;
		return 0;
	}

	if (idx > keys->gap)
		idx -= keys->size - keys->nr;

	size_t old_gap = keys->gap;

	if (keys->nr == keys->size) {
		journal_iters_move_gap(c, old_gap, keys->size);
		old_gap = keys->size;

		struct journal_keys new_keys = {
			.nr			= keys->nr,
			.size			= max_t(size_t, keys->size, 8) * 2,
		};

		new_keys.data = kvmalloc_array(new_keys.size, sizeof(new_keys.data[0]), GFP_KERNEL);
		if (!new_keys.data) {
			bch_err(c, "%s: error allocating new key array (size %zu)",
				__func__, new_keys.size);
			return -BCH_ERR_ENOMEM_journal_key_insert;
		}

		/* Since @keys was full, there was no gap: */
		memcpy(new_keys.data, keys->data, sizeof(keys->data[0]) * keys->nr);
		kvfree(keys->data);
		keys->data	= new_keys.data;
		keys->nr	= new_keys.nr;
		keys->size	= new_keys.size;

		/* And now the gap is at the end: */
		keys->gap	= keys->nr;
	}

	journal_iters_move_gap(c, old_gap, idx);

	move_gap(keys, idx);

	keys->nr++;
	keys->data[keys->gap++] = n;

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

bool bch2_key_deleted_in_journal(struct btree_trans *trans, enum btree_id btree,
				 unsigned level, struct bpos pos)
{
	struct journal_keys *keys = &trans->c->journal_keys;
	size_t idx = bch2_journal_key_search(keys, btree, level, pos);

	if (!trans->journal_replay_not_finished)
		return false;

	return (idx < keys->size &&
		keys->data[idx].btree_id	== btree &&
		keys->data[idx].level		== level &&
		bpos_eq(keys->data[idx].k->k.p, pos) &&
		bkey_deleted(&keys->data[idx].k->k));
}

static void __bch2_journal_key_overwritten(struct journal_keys *keys, size_t pos)
{
	struct journal_key *k = keys->data + pos;
	size_t idx = pos_to_idx(keys, pos);

	k->overwritten = true;

	struct journal_key *prev = idx > 0 ? keys->data + idx_to_pos(keys, idx - 1) : NULL;
	struct journal_key *next = idx + 1 < keys->nr ? keys->data + idx_to_pos(keys, idx + 1) : NULL;

	bool prev_overwritten = prev && prev->overwritten;
	bool next_overwritten = next && next->overwritten;

	struct journal_key_range_overwritten *prev_range =
		prev_overwritten ? prev->overwritten_range : NULL;
	struct journal_key_range_overwritten *next_range =
		next_overwritten ? next->overwritten_range : NULL;

	BUG_ON(prev_range && prev_range->end != idx);
	BUG_ON(next_range && next_range->start != idx + 1);

	if (prev_range && next_range) {
		prev_range->end = next_range->end;

		keys->data[pos].overwritten_range = prev_range;
		for (size_t i = next_range->start; i < next_range->end; i++) {
			struct journal_key *ip = keys->data + idx_to_pos(keys, i);
			BUG_ON(ip->overwritten_range != next_range);
			ip->overwritten_range = prev_range;
		}

		kfree_rcu_mightsleep(next_range);
	} else if (prev_range) {
		prev_range->end++;
		k->overwritten_range = prev_range;
		if (next_overwritten) {
			prev_range->end++;
			next->overwritten_range = prev_range;
		}
	} else if (next_range) {
		next_range->start--;
		k->overwritten_range = next_range;
		if (prev_overwritten) {
			next_range->start--;
			prev->overwritten_range = next_range;
		}
	} else if (prev_overwritten || next_overwritten) {
		struct journal_key_range_overwritten *r = kmalloc(sizeof(*r), GFP_KERNEL);
		if (!r)
			return;

		r->start = idx - (size_t) prev_overwritten;
		r->end = idx + 1 + (size_t) next_overwritten;

		rcu_assign_pointer(k->overwritten_range, r);
		if (prev_overwritten)
			prev->overwritten_range = r;
		if (next_overwritten)
			next->overwritten_range = r;
	}
}

void bch2_journal_key_overwritten(struct bch_fs *c, enum btree_id btree,
				  unsigned level, struct bpos pos)
{
	struct journal_keys *keys = &c->journal_keys;
	size_t idx = bch2_journal_key_search(keys, btree, level, pos);

	if (idx < keys->size &&
	    keys->data[idx].btree_id	== btree &&
	    keys->data[idx].level	== level &&
	    bpos_eq(keys->data[idx].k->k.p, pos) &&
	    !keys->data[idx].overwritten) {
		mutex_lock(&keys->overwrite_lock);
		__bch2_journal_key_overwritten(keys, idx);
		mutex_unlock(&keys->overwrite_lock);
	}
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
	struct bkey_s_c ret = bkey_s_c_null;

	journal_iter_verify(iter);

	rcu_read_lock();
	while (iter->idx < iter->keys->size) {
		struct journal_key *k = iter->keys->data + iter->idx;

		int cmp = __journal_key_btree_cmp(iter->btree_id, iter->level, k);
		if (cmp < 0)
			break;
		BUG_ON(cmp);

		if (!k->overwritten) {
			ret = bkey_i_to_s_c(k->k);
			break;
		}

		if (k->overwritten_range)
			iter->idx = idx_to_pos(iter->keys, rcu_dereference(k->overwritten_range)->end);
		else
			bch2_journal_iter_advance(iter);
	}
	rcu_read_unlock();

	return ret;
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

	journal_iter_verify(iter);
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

static void btree_and_journal_iter_prefetch(struct btree_and_journal_iter *_iter)
{
	struct btree_and_journal_iter iter = *_iter;
	struct bch_fs *c = iter.trans->c;
	unsigned level = iter.journal.level;
	struct bkey_buf tmp;
	unsigned nr = test_bit(BCH_FS_started, &c->flags)
		? (level > 1 ? 0 :  2)
		: (level > 1 ? 1 : 16);

	iter.prefetch = false;
	iter.fail_if_too_many_whiteouts = true;
	bch2_bkey_buf_init(&tmp);

	while (nr--) {
		bch2_btree_and_journal_iter_advance(&iter);
		struct bkey_s_c k = bch2_btree_and_journal_iter_peek(&iter);
		if (!k.k)
			break;

		bch2_bkey_buf_reassemble(&tmp, c, k);
		bch2_btree_node_prefetch(iter.trans, NULL, tmp.k, iter.journal.btree_id, level - 1);
	}

	bch2_bkey_buf_exit(&tmp, c);
}

struct bkey_s_c bch2_btree_and_journal_iter_peek(struct btree_and_journal_iter *iter)
{
	struct bkey_s_c btree_k, journal_k = bkey_s_c_null, ret;
	size_t iters = 0;

	if (iter->prefetch && iter->journal.level)
		btree_and_journal_iter_prefetch(iter);
again:
	if (iter->at_end)
		return bkey_s_c_null;

	iters++;

	if (iters > 20 && iter->fail_if_too_many_whiteouts)
		return bkey_s_c_null;

	while ((btree_k = bch2_journal_iter_peek_btree(iter)).k &&
	       bpos_lt(btree_k.k->p, iter->pos))
		bch2_journal_iter_advance_btree(iter);

	if (iter->trans->journal_replay_not_finished)
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

void __bch2_btree_and_journal_iter_init_node_iter(struct btree_trans *trans,
						  struct btree_and_journal_iter *iter,
						  struct btree *b,
						  struct btree_node_iter node_iter,
						  struct bpos pos)
{
	memset(iter, 0, sizeof(*iter));

	iter->trans = trans;
	iter->b = b;
	iter->node_iter = node_iter;
	iter->pos = b->data->min_key;
	iter->at_end = false;
	INIT_LIST_HEAD(&iter->journal.list);

	if (trans->journal_replay_not_finished) {
		bch2_journal_iter_init(trans->c, &iter->journal, b->c.btree_id, b->c.level, pos);
		if (!test_bit(BCH_FS_may_go_rw, &trans->c->flags))
			list_add(&iter->journal.list, &trans->c->journal_iters);
	}
}

/*
 * this version is used by btree_gc before filesystem has gone RW and
 * multithreaded, so uses the journal_iters list:
 */
void bch2_btree_and_journal_iter_init_node_iter(struct btree_trans *trans,
						struct btree_and_journal_iter *iter,
						struct btree *b)
{
	struct btree_node_iter node_iter;

	bch2_btree_node_iter_init_from_start(&node_iter, b);
	__bch2_btree_and_journal_iter_init_node_iter(trans, iter, b, node_iter, b->data->min_key);
}

/* sort and dedup all keys in the journal: */

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

void bch2_journal_keys_put(struct bch_fs *c)
{
	struct journal_keys *keys = &c->journal_keys;

	BUG_ON(atomic_read(&keys->ref) <= 0);

	if (!atomic_dec_and_test(&keys->ref))
		return;

	move_gap(keys, keys->nr);

	darray_for_each(*keys, i) {
		if (i->overwritten_range &&
		    (i == &darray_last(*keys) ||
		     i->overwritten_range != i[1].overwritten_range))
			kfree(i->overwritten_range);

		if (i->allocated)
			kfree(i->k);
	}

	kvfree(keys->data);
	keys->data = NULL;
	keys->nr = keys->gap = keys->size = 0;

	struct journal_replay **i;
	struct genradix_iter iter;

	genradix_for_each(&c->journal_entries, iter, i)
		kvfree(*i);
	genradix_free(&c->journal_entries);
}

static void __journal_keys_sort(struct journal_keys *keys)
{
	sort_nonatomic(keys->data, keys->nr, sizeof(keys->data[0]),
		       journal_sort_key_cmp, NULL);

	cond_resched();

	struct journal_key *dst = keys->data;

	darray_for_each(*keys, src) {
		/*
		 * We don't accumulate accounting keys here because we have to
		 * compare each individual accounting key against the version in
		 * the btree during replay:
		 */
		if (src->k->k.type != KEY_TYPE_accounting &&
		    src + 1 < &darray_top(*keys) &&
		    !journal_key_cmp(src, src + 1))
			continue;

		*dst++ = *src;
	}

	keys->nr = dst - keys->data;
}

int bch2_journal_keys_sort(struct bch_fs *c)
{
	struct genradix_iter iter;
	struct journal_replay *i, **_i;
	struct journal_keys *keys = &c->journal_keys;
	size_t nr_read = 0;

	genradix_for_each(&c->journal_entries, iter, _i) {
		i = *_i;

		if (journal_replay_ignore(i))
			continue;

		cond_resched();

		for_each_jset_key(k, entry, &i->j) {
			struct journal_key n = (struct journal_key) {
				.btree_id	= entry->btree_id,
				.level		= entry->level,
				.k		= k,
				.journal_seq	= le64_to_cpu(i->j.seq),
				.journal_offset	= k->_data - i->j._data,
			};

			if (darray_push(keys, n)) {
				__journal_keys_sort(keys);

				if (keys->nr * 8 > keys->size * 7) {
					bch_err(c, "Too many journal keys for slowpath; have %zu compacted, buf size %zu, processed %zu keys at seq %llu",
						keys->nr, keys->size, nr_read, le64_to_cpu(i->j.seq));
					return -BCH_ERR_ENOMEM_journal_keys_sort;
				}

				BUG_ON(darray_push(keys, n));
			}

			nr_read++;
		}
	}

	__journal_keys_sort(keys);
	keys->gap = keys->nr;

	bch_verbose(c, "Journal keys: %zu read, %zu after sorting and compacting", nr_read, keys->nr);
	return 0;
}

void bch2_shoot_down_journal_keys(struct bch_fs *c, enum btree_id btree,
				  unsigned level_min, unsigned level_max,
				  struct bpos start, struct bpos end)
{
	struct journal_keys *keys = &c->journal_keys;
	size_t dst = 0;

	move_gap(keys, keys->nr);

	darray_for_each(*keys, i)
		if (!(i->btree_id == btree &&
		      i->level >= level_min &&
		      i->level <= level_max &&
		      bpos_ge(i->k->k.p, start) &&
		      bpos_le(i->k->k.p, end)))
			keys->data[dst++] = *i;
	keys->nr = keys->gap = dst;
}

void bch2_journal_keys_dump(struct bch_fs *c)
{
	struct journal_keys *keys = &c->journal_keys;
	struct printbuf buf = PRINTBUF;

	pr_info("%zu keys:", keys->nr);

	move_gap(keys, keys->nr);

	darray_for_each(*keys, i) {
		printbuf_reset(&buf);
		prt_printf(&buf, "btree=");
		bch2_btree_id_to_text(&buf, i->btree_id);
		prt_printf(&buf, " l=%u ", i->level);
		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(i->k));
		pr_err("%s", buf.buf);
	}
	printbuf_exit(&buf);
}

void bch2_fs_journal_keys_init(struct bch_fs *c)
{
	struct journal_keys *keys = &c->journal_keys;

	atomic_set(&keys->ref, 1);
	keys->initial_ref_held = true;
	mutex_init(&keys->overwrite_lock);
}
