
#include "bcachefs.h"
#include "btree_cache.h"
#include "btree_iter.h"
#include "btree_key_cache.h"
#include "btree_locking.h"
#include "btree_update.h"
#include "error.h"
#include "journal.h"
#include "journal_reclaim.h"
#include "trace.h"

static int bch2_btree_key_cache_cmp_fn(struct rhashtable_compare_arg *arg,
				       const void *obj)
{
	const struct bkey_cached *ck = obj;
	const struct bkey_cached_key *key = arg->key;

	return cmp_int(ck->key.btree_id, key->btree_id) ?:
		bkey_cmp(ck->key.pos, key->pos);
}

static const struct rhashtable_params bch2_btree_key_cache_params = {
	.head_offset	= offsetof(struct bkey_cached, hash),
	.key_offset	= offsetof(struct bkey_cached, key),
	.key_len	= sizeof(struct bkey_cached_key),
	.obj_cmpfn	= bch2_btree_key_cache_cmp_fn,
};

__flatten
inline struct bkey_cached *
bch2_btree_key_cache_find(struct bch_fs *c, enum btree_id btree_id, struct bpos pos)
{
	struct bkey_cached_key key = {
		.btree_id	= btree_id,
		.pos		= pos,
	};

	return rhashtable_lookup_fast(&c->btree_key_cache.table, &key,
				      bch2_btree_key_cache_params);
}

static bool bkey_cached_lock_for_evict(struct bkey_cached *ck)
{
	if (!six_trylock_intent(&ck->c.lock))
		return false;

	if (!six_trylock_write(&ck->c.lock)) {
		six_unlock_intent(&ck->c.lock);
		return false;
	}

	if (test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
		six_unlock_write(&ck->c.lock);
		six_unlock_intent(&ck->c.lock);
		return false;
	}

	return true;
}

static void bkey_cached_evict(struct btree_key_cache *c,
			      struct bkey_cached *ck)
{
	BUG_ON(rhashtable_remove_fast(&c->table, &ck->hash,
				      bch2_btree_key_cache_params));
	memset(&ck->key, ~0, sizeof(ck->key));

	c->nr_keys--;
}

static void bkey_cached_free(struct btree_key_cache *c,
			     struct bkey_cached *ck)
{
	list_move(&ck->list, &c->freed);

	kfree(ck->k);
	ck->k		= NULL;
	ck->u64s	= 0;

	six_unlock_write(&ck->c.lock);
	six_unlock_intent(&ck->c.lock);
}

static struct bkey_cached *
bkey_cached_alloc(struct btree_key_cache *c)
{
	struct bkey_cached *ck;

	list_for_each_entry(ck, &c->freed, list)
		if (bkey_cached_lock_for_evict(ck))
			return ck;

	list_for_each_entry(ck, &c->clean, list)
		if (bkey_cached_lock_for_evict(ck)) {
			bkey_cached_evict(c, ck);
			return ck;
		}

	ck = kzalloc(sizeof(*ck), GFP_NOFS);
	if (!ck)
		return NULL;

	INIT_LIST_HEAD(&ck->list);
	six_lock_init(&ck->c.lock);
	lockdep_set_novalidate_class(&ck->c.lock);
	BUG_ON(!six_trylock_intent(&ck->c.lock));
	BUG_ON(!six_trylock_write(&ck->c.lock));

	return ck;
}

static struct bkey_cached *
btree_key_cache_create(struct btree_key_cache *c,
		       enum btree_id btree_id,
		       struct bpos pos)
{
	struct bkey_cached *ck;

	ck = bkey_cached_alloc(c);
	if (!ck)
		return ERR_PTR(-ENOMEM);

	ck->c.level		= 0;
	ck->c.btree_id		= btree_id;
	ck->key.btree_id	= btree_id;
	ck->key.pos		= pos;
	ck->valid		= false;

	BUG_ON(ck->flags);

	if (rhashtable_lookup_insert_fast(&c->table,
					  &ck->hash,
					  bch2_btree_key_cache_params)) {
		/* We raced with another fill: */
		bkey_cached_free(c, ck);
		return NULL;
	}

	c->nr_keys++;

	list_move(&ck->list, &c->clean);
	six_unlock_write(&ck->c.lock);

	return ck;
}

static int btree_key_cache_fill(struct btree_trans *trans,
				struct btree_iter *ck_iter,
				struct bkey_cached *ck)
{
	struct btree_iter *iter;
	struct bkey_s_c k;
	unsigned new_u64s = 0;
	struct bkey_i *new_k = NULL;
	int ret;

	iter = bch2_trans_get_iter(trans, ck->key.btree_id,
				   ck->key.pos, BTREE_ITER_SLOTS);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret) {
		bch2_trans_iter_put(trans, iter);
		return ret;
	}

	if (!bch2_btree_node_relock(ck_iter, 0)) {
		bch2_trans_iter_put(trans, iter);
		trace_transaction_restart_ip(trans->ip, _THIS_IP_);
		return -EINTR;
	}

	if (k.k->u64s > ck->u64s) {
		new_u64s = roundup_pow_of_two(k.k->u64s);
		new_k = kmalloc(new_u64s * sizeof(u64), GFP_NOFS);
		if (!new_k) {
			bch2_trans_iter_put(trans, iter);
			return -ENOMEM;
		}
	}

	bch2_btree_node_lock_write(ck_iter->l[0].b, ck_iter);
	if (new_k) {
		kfree(ck->k);
		ck->u64s = new_u64s;
		ck->k = new_k;
	}

	bkey_reassemble(ck->k, k);
	ck->valid = true;
	bch2_btree_node_unlock_write(ck_iter->l[0].b, ck_iter);

	/* We're not likely to need this iterator again: */
	bch2_trans_iter_free(trans, iter);

	return 0;
}

static int bkey_cached_check_fn(struct six_lock *lock, void *p)
{
	struct bkey_cached *ck = container_of(lock, struct bkey_cached, c.lock);
	const struct btree_iter *iter = p;

	return ck->key.btree_id == iter->btree_id &&
		!bkey_cmp(ck->key.pos, iter->pos) ? 0 : -1;
}

__flatten
int bch2_btree_iter_traverse_cached(struct btree_iter *iter)
{
	struct btree_trans *trans = iter->trans;
	struct bch_fs *c = trans->c;
	struct bkey_cached *ck;
	int ret = 0;

	BUG_ON(iter->level);

	if (btree_node_locked(iter, 0)) {
		ck = (void *) iter->l[0].b;
		goto fill;
	}
retry:
	ck = bch2_btree_key_cache_find(c, iter->btree_id, iter->pos);
	if (!ck) {
		if (iter->flags & BTREE_ITER_CACHED_NOCREATE) {
			iter->l[0].b = NULL;
			return 0;
		}

		mutex_lock(&c->btree_key_cache.lock);
		ck = btree_key_cache_create(&c->btree_key_cache,
					    iter->btree_id, iter->pos);
		mutex_unlock(&c->btree_key_cache.lock);

		ret = PTR_ERR_OR_ZERO(ck);
		if (ret)
			goto err;
		if (!ck)
			goto retry;

		mark_btree_node_locked(iter, 0, SIX_LOCK_intent);
		iter->locks_want = 1;
	} else {
		enum six_lock_type lock_want = __btree_lock_want(iter, 0);

		if (!btree_node_lock((void *) ck, iter->pos, 0, iter, lock_want,
				     bkey_cached_check_fn, iter, _THIS_IP_)) {
			if (ck->key.btree_id != iter->btree_id ||
			    bkey_cmp(ck->key.pos, iter->pos)) {
				goto retry;
			}

			trace_transaction_restart_ip(trans->ip, _THIS_IP_);
			ret = -EINTR;
			goto err;
		}

		if (ck->key.btree_id != iter->btree_id ||
		    bkey_cmp(ck->key.pos, iter->pos)) {
			six_unlock_type(&ck->c.lock, lock_want);
			goto retry;
		}

		mark_btree_node_locked(iter, 0, lock_want);
	}

	iter->l[0].lock_seq	= ck->c.lock.state.seq;
	iter->l[0].b		= (void *) ck;
fill:
	if (!ck->valid && !(iter->flags & BTREE_ITER_CACHED_NOFILL)) {
		if (!btree_node_intent_locked(iter, 0))
			bch2_btree_iter_upgrade(iter, 1);
		if (!btree_node_intent_locked(iter, 0)) {
			trace_transaction_restart_ip(trans->ip, _THIS_IP_);
			ret = -EINTR;
			goto err;
		}

		ret = btree_key_cache_fill(trans, iter, ck);
		if (ret)
			goto err;
	}

	iter->uptodate = BTREE_ITER_NEED_PEEK;
	bch2_btree_iter_downgrade(iter);
	return ret;
err:
	if (ret != -EINTR) {
		btree_node_unlock(iter, 0);
		iter->flags |= BTREE_ITER_ERROR;
		iter->l[0].b = BTREE_ITER_NO_NODE_ERROR;
	}
	return ret;
}

static int btree_key_cache_flush_pos(struct btree_trans *trans,
				     struct bkey_cached_key key,
				     u64 journal_seq,
				     bool evict)
{
	struct bch_fs *c = trans->c;
	struct journal *j = &c->journal;
	struct btree_iter *c_iter = NULL, *b_iter = NULL;
	struct bkey_cached *ck;
	int ret;

	b_iter = bch2_trans_get_iter(trans, key.btree_id, key.pos,
				     BTREE_ITER_SLOTS|
				     BTREE_ITER_INTENT);
	ret = PTR_ERR_OR_ZERO(b_iter);
	if (ret)
		goto out;

	c_iter = bch2_trans_get_iter(trans, key.btree_id, key.pos,
				     BTREE_ITER_CACHED|
				     BTREE_ITER_CACHED_NOFILL|
				     BTREE_ITER_CACHED_NOCREATE|
				     BTREE_ITER_INTENT);
	ret = PTR_ERR_OR_ZERO(c_iter);
	if (ret)
		goto out;
retry:
	ret = bch2_btree_iter_traverse(c_iter);
	if (ret)
		goto err;

	ck = (void *) c_iter->l[0].b;
	if (!ck ||
	    (journal_seq && ck->journal.seq != journal_seq))
		goto out;

	if (!test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
		if (!evict)
			goto out;
		goto evict;
	}

	ret   = bch2_btree_iter_traverse(b_iter) ?:
		bch2_trans_update(trans, b_iter, ck->k, BTREE_TRIGGER_NORUN) ?:
		bch2_trans_commit(trans, NULL, NULL,
				  BTREE_INSERT_NOUNLOCK|
				  BTREE_INSERT_NOCHECK_RW|
				  BTREE_INSERT_NOFAIL|
				  BTREE_INSERT_USE_RESERVE|
				  BTREE_INSERT_USE_ALLOC_RESERVE|
				  BTREE_INSERT_JOURNAL_RESERVED|
				  BTREE_INSERT_JOURNAL_RECLAIM);
err:
	if (ret == -EINTR)
		goto retry;

	BUG_ON(ret && !bch2_journal_error(j));

	if (ret)
		goto out;

	bch2_journal_pin_drop(j, &ck->journal);
	bch2_journal_preres_put(j, &ck->res);

	if (!evict) {
		mutex_lock(&c->btree_key_cache.lock);
		if (test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
			clear_bit(BKEY_CACHED_DIRTY, &ck->flags);
			c->btree_key_cache.nr_dirty--;
		}

		list_move_tail(&ck->list, &c->btree_key_cache.clean);
		mutex_unlock(&c->btree_key_cache.lock);
	} else {
evict:
		BUG_ON(!btree_node_intent_locked(c_iter, 0));

		mark_btree_node_unlocked(c_iter, 0);
		c_iter->l[0].b = NULL;

		six_lock_write(&ck->c.lock, NULL, NULL);

		mutex_lock(&c->btree_key_cache.lock);
		if (test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
			clear_bit(BKEY_CACHED_DIRTY, &ck->flags);
			c->btree_key_cache.nr_dirty--;
		}

		bkey_cached_evict(&c->btree_key_cache, ck);
		bkey_cached_free(&c->btree_key_cache, ck);
		mutex_unlock(&c->btree_key_cache.lock);
	}
out:
	bch2_trans_iter_put(trans, b_iter);
	bch2_trans_iter_put(trans, c_iter);
	return ret;
}

static void btree_key_cache_journal_flush(struct journal *j,
					  struct journal_entry_pin *pin,
					  u64 seq)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct bkey_cached *ck =
		container_of(pin, struct bkey_cached, journal);
	struct bkey_cached_key key;
	struct btree_trans trans;

	six_lock_read(&ck->c.lock, NULL, NULL);
	key = ck->key;

	if (ck->journal.seq != seq ||
	    !test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
		six_unlock_read(&ck->c.lock);
		return;
	}
	six_unlock_read(&ck->c.lock);

	bch2_trans_init(&trans, c, 0, 0);
	btree_key_cache_flush_pos(&trans, key, seq, false);
	bch2_trans_exit(&trans);
}

/*
 * Flush and evict a key from the key cache:
 */
int bch2_btree_key_cache_flush(struct btree_trans *trans,
			       enum btree_id id, struct bpos pos)
{
	struct bch_fs *c = trans->c;
	struct bkey_cached_key key = { id, pos };

	/* Fastpath - assume it won't be found: */
	if (!bch2_btree_key_cache_find(c, id, pos))
		return 0;

	return btree_key_cache_flush_pos(trans, key, 0, true);
}

bool bch2_btree_insert_key_cached(struct btree_trans *trans,
				  struct btree_iter *iter,
				  struct bkey_i *insert)
{
	struct bch_fs *c = trans->c;
	struct bkey_cached *ck = (void *) iter->l[0].b;

	BUG_ON(insert->u64s > ck->u64s);

	if (likely(!(trans->flags & BTREE_INSERT_JOURNAL_REPLAY))) {
		int difference;

		BUG_ON(jset_u64s(insert->u64s) > trans->journal_preres.u64s);

		difference = jset_u64s(insert->u64s) - ck->res.u64s;
		if (difference > 0) {
			trans->journal_preres.u64s	-= difference;
			ck->res.u64s			+= difference;
		}
	}

	bkey_copy(ck->k, insert);
	ck->valid = true;

	if (!test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
		mutex_lock(&c->btree_key_cache.lock);
		list_move(&ck->list, &c->btree_key_cache.dirty);

		set_bit(BKEY_CACHED_DIRTY, &ck->flags);
		c->btree_key_cache.nr_dirty++;
		mutex_unlock(&c->btree_key_cache.lock);
	}

	bch2_journal_pin_update(&c->journal, trans->journal_res.seq,
				&ck->journal, btree_key_cache_journal_flush);
	return true;
}

#ifdef CONFIG_BCACHEFS_DEBUG
void bch2_btree_key_cache_verify_clean(struct btree_trans *trans,
			       enum btree_id id, struct bpos pos)
{
	BUG_ON(bch2_btree_key_cache_find(trans->c, id, pos));
}
#endif

void bch2_fs_btree_key_cache_exit(struct btree_key_cache *bc)
{
	struct bch_fs *c = container_of(bc, struct bch_fs, btree_key_cache);
	struct bkey_cached *ck, *n;

	mutex_lock(&bc->lock);
	list_splice(&bc->dirty, &bc->clean);

	list_for_each_entry_safe(ck, n, &bc->clean, list) {
		bch2_journal_pin_drop(&c->journal, &ck->journal);
		bch2_journal_preres_put(&c->journal, &ck->res);

		kfree(ck->k);
		kfree(ck);
		bc->nr_keys--;
	}

	BUG_ON(bc->nr_dirty && !bch2_journal_error(&c->journal));
	BUG_ON(bc->nr_keys);

	list_for_each_entry_safe(ck, n, &bc->freed, list)
		kfree(ck);
	mutex_unlock(&bc->lock);

	rhashtable_destroy(&bc->table);
}

void bch2_fs_btree_key_cache_init_early(struct btree_key_cache *c)
{
	mutex_init(&c->lock);
	INIT_LIST_HEAD(&c->freed);
	INIT_LIST_HEAD(&c->clean);
	INIT_LIST_HEAD(&c->dirty);
}

int bch2_fs_btree_key_cache_init(struct btree_key_cache *c)
{
	return rhashtable_init(&c->table, &bch2_btree_key_cache_params);
}

void bch2_btree_key_cache_to_text(struct printbuf *out, struct btree_key_cache *c)
{
	struct bucket_table *tbl;
	struct bkey_cached *ck;
	struct rhash_head *pos;
	size_t i;

	mutex_lock(&c->lock);
	tbl = rht_dereference_rcu(c->table.tbl, &c->table);

	for (i = 0; i < tbl->size; i++) {
		rht_for_each_entry_rcu(ck, pos, tbl, i, hash) {
			pr_buf(out, "%s:",
			       bch2_btree_ids[ck->key.btree_id]);
			bch2_bpos_to_text(out, ck->key.pos);

			if (test_bit(BKEY_CACHED_DIRTY, &ck->flags))
				pr_buf(out, " journal seq %llu", ck->journal.seq);
			pr_buf(out, "\n");
		}
	}
	mutex_unlock(&c->lock);
}
