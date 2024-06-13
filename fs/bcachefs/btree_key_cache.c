// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_cache.h"
#include "btree_iter.h"
#include "btree_key_cache.h"
#include "btree_locking.h"
#include "btree_update.h"
#include "errcode.h"
#include "error.h"
#include "journal.h"
#include "journal_reclaim.h"
#include "trace.h"

#include <linux/sched/mm.h>

static inline bool btree_uses_pcpu_readers(enum btree_id id)
{
	return id == BTREE_ID_subvolumes;
}

static struct kmem_cache *bch2_key_cache;

static int bch2_btree_key_cache_cmp_fn(struct rhashtable_compare_arg *arg,
				       const void *obj)
{
	const struct bkey_cached *ck = obj;
	const struct bkey_cached_key *key = arg->key;

	return ck->key.btree_id != key->btree_id ||
		!bpos_eq(ck->key.pos, key->pos);
}

static const struct rhashtable_params bch2_btree_key_cache_params = {
	.head_offset		= offsetof(struct bkey_cached, hash),
	.key_offset		= offsetof(struct bkey_cached, key),
	.key_len		= sizeof(struct bkey_cached_key),
	.obj_cmpfn		= bch2_btree_key_cache_cmp_fn,
	.automatic_shrinking	= true,
};

static inline void btree_path_cached_set(struct btree_trans *trans, struct btree_path *path,
					 struct bkey_cached *ck,
					 enum btree_node_locked_type lock_held)
{
	path->l[0].lock_seq	= six_lock_seq(&ck->c.lock);
	path->l[0].b		= (void *) ck;
	mark_btree_node_locked(trans, path, 0, lock_held);
}

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

	if (test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
		six_unlock_intent(&ck->c.lock);
		return false;
	}

	if (!six_trylock_write(&ck->c.lock)) {
		six_unlock_intent(&ck->c.lock);
		return false;
	}

	return true;
}

static bool bkey_cached_evict(struct btree_key_cache *c,
			      struct bkey_cached *ck)
{
	bool ret = !rhashtable_remove_fast(&c->table, &ck->hash,
				      bch2_btree_key_cache_params);
	if (ret) {
		memset(&ck->key, ~0, sizeof(ck->key));
		atomic_long_dec(&c->nr_keys);
	}

	return ret;
}

static void __bkey_cached_free(struct rcu_pending *pending, struct rcu_head *rcu)
{
	struct bch_fs *c = container_of(pending->srcu, struct bch_fs, btree_trans_barrier);
	struct bkey_cached *ck = container_of(rcu, struct bkey_cached, rcu);

	this_cpu_dec(*c->btree_key_cache.nr_pending);
	kmem_cache_free(bch2_key_cache, ck);
}

static void bkey_cached_free(struct btree_key_cache *bc,
			     struct bkey_cached *ck)
{
	kfree(ck->k);
	ck->k		= NULL;
	ck->u64s	= 0;

	six_unlock_write(&ck->c.lock);
	six_unlock_intent(&ck->c.lock);

	bool pcpu_readers = ck->c.lock.readers != NULL;
	rcu_pending_enqueue(&bc->pending[pcpu_readers], &ck->rcu);
	this_cpu_inc(*bc->nr_pending);
}

static struct bkey_cached *__bkey_cached_alloc(unsigned key_u64s, gfp_t gfp)
{
	struct bkey_cached *ck = kmem_cache_zalloc(bch2_key_cache, gfp);
	if (unlikely(!ck))
		return NULL;
	ck->k = kmalloc(key_u64s * sizeof(u64), gfp);
	if (unlikely(!ck->k)) {
		kmem_cache_free(bch2_key_cache, ck);
		return NULL;
	}
	ck->u64s = key_u64s;
	return ck;
}

static struct bkey_cached *
bkey_cached_alloc(struct btree_trans *trans, struct btree_path *path, unsigned key_u64s)
{
	struct bch_fs *c = trans->c;
	struct btree_key_cache *bc = &c->btree_key_cache;
	bool pcpu_readers = btree_uses_pcpu_readers(path->btree_id);
	int ret;

	struct bkey_cached *ck = container_of_or_null(
				rcu_pending_dequeue(&bc->pending[pcpu_readers]),
				struct bkey_cached, rcu);
	if (ck)
		goto lock;

	ck = allocate_dropping_locks(trans, ret,
				     __bkey_cached_alloc(key_u64s, _gfp));
	if (ret) {
		if (ck)
			kfree(ck->k);
		kmem_cache_free(bch2_key_cache, ck);
		return ERR_PTR(ret);
	}

	if (ck) {
		bch2_btree_lock_init(&ck->c, pcpu_readers ? SIX_LOCK_INIT_PCPU : 0);
		ck->c.cached = true;
		goto lock;
	}

	ck = container_of_or_null(rcu_pending_dequeue_from_all(&bc->pending[pcpu_readers]),
				  struct bkey_cached, rcu);
	if (ck)
		goto lock;
lock:
	six_lock_intent(&ck->c.lock, NULL, NULL);
	six_lock_write(&ck->c.lock, NULL, NULL);
	return ck;
}

static struct bkey_cached *
bkey_cached_reuse(struct btree_key_cache *c)
{
	struct bucket_table *tbl;
	struct rhash_head *pos;
	struct bkey_cached *ck;
	unsigned i;

	rcu_read_lock();
	tbl = rht_dereference_rcu(c->table.tbl, &c->table);
	for (i = 0; i < tbl->size; i++)
		rht_for_each_entry_rcu(ck, pos, tbl, i, hash) {
			if (!test_bit(BKEY_CACHED_DIRTY, &ck->flags) &&
			    bkey_cached_lock_for_evict(ck)) {
				if (bkey_cached_evict(c, ck))
					goto out;
				six_unlock_write(&ck->c.lock);
				six_unlock_intent(&ck->c.lock);
			}
		}
	ck = NULL;
out:
	rcu_read_unlock();
	return ck;
}

static int btree_key_cache_create(struct btree_trans *trans, struct btree_path *path,
				  struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct btree_key_cache *bc = &c->btree_key_cache;

	/*
	 * bch2_varint_decode can read past the end of the buffer by at
	 * most 7 bytes (it won't be used):
	 */
	unsigned key_u64s = k.k->u64s + 1;

	/*
	 * Allocate some extra space so that the transaction commit path is less
	 * likely to have to reallocate, since that requires a transaction
	 * restart:
	 */
	key_u64s = min(256U, (key_u64s * 3) / 2);
	key_u64s = roundup_pow_of_two(key_u64s);

	struct bkey_cached *ck = bkey_cached_alloc(trans, path, key_u64s);
	int ret = PTR_ERR_OR_ZERO(ck);
	if (ret)
		return ret;

	if (unlikely(!ck)) {
		ck = bkey_cached_reuse(bc);
		if (unlikely(!ck)) {
			bch_err(c, "error allocating memory for key cache item, btree %s",
				bch2_btree_id_str(path->btree_id));
			return -BCH_ERR_ENOMEM_btree_key_cache_create;
		}
	}

	ck->c.level		= 0;
	ck->c.btree_id		= path->btree_id;
	ck->key.btree_id	= path->btree_id;
	ck->key.pos		= path->pos;
	ck->flags		= 1U << BKEY_CACHED_ACCESSED;

	if (unlikely(key_u64s > ck->u64s)) {
		mark_btree_node_locked_noreset(path, 0, BTREE_NODE_UNLOCKED);

		struct bkey_i *new_k = allocate_dropping_locks(trans, ret,
				kmalloc(key_u64s * sizeof(u64), _gfp));
		if (unlikely(!new_k)) {
			bch_err(trans->c, "error allocating memory for key cache key, btree %s u64s %u",
				bch2_btree_id_str(ck->key.btree_id), key_u64s);
			ret = -BCH_ERR_ENOMEM_btree_key_cache_fill;
		} else if (ret) {
			kfree(new_k);
			goto err;
		}

		kfree(ck->k);
		ck->k = new_k;
		ck->u64s = key_u64s;
	}

	bkey_reassemble(ck->k, k);

	ret = rhashtable_lookup_insert_fast(&bc->table, &ck->hash, bch2_btree_key_cache_params);
	if (unlikely(ret)) /* raced with another fill? */
		goto err;

	atomic_long_inc(&bc->nr_keys);
	six_unlock_write(&ck->c.lock);

	enum six_lock_type lock_want = __btree_lock_want(path, 0);
	if (lock_want == SIX_LOCK_read)
		six_lock_downgrade(&ck->c.lock);
	btree_path_cached_set(trans, path, ck, (enum btree_node_locked_type) lock_want);
	path->uptodate = BTREE_ITER_UPTODATE;
	return 0;
err:
	bkey_cached_free(bc, ck);
	mark_btree_node_locked_noreset(path, 0, BTREE_NODE_UNLOCKED);

	return ret;
}

static noinline int btree_key_cache_fill(struct btree_trans *trans,
					 struct btree_path *ck_path,
					 unsigned flags)
{
	if (flags & BTREE_ITER_cached_nofill) {
		ck_path->uptodate = BTREE_ITER_UPTODATE;
		return 0;
	}

	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_iter_init(trans, &iter, ck_path->btree_id, ck_path->pos,
			     BTREE_ITER_key_cache_fill|
			     BTREE_ITER_cached_nofill);
	iter.flags &= ~BTREE_ITER_with_journal;
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	/* Recheck after btree lookup, before allocating: */
	ret = bch2_btree_key_cache_find(c, ck_path->btree_id, ck_path->pos) ? -EEXIST : 0;
	if (unlikely(ret))
		goto out;

	ret = btree_key_cache_create(trans, ck_path, k);
	if (ret)
		goto err;
out:
	/* We're not likely to need this iterator again: */
	bch2_set_btree_iter_dontneed(&iter);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static inline int btree_path_traverse_cached_fast(struct btree_trans *trans,
						  struct btree_path *path)
{
	struct bch_fs *c = trans->c;
	struct bkey_cached *ck;
retry:
	ck = bch2_btree_key_cache_find(c, path->btree_id, path->pos);
	if (!ck)
		return -ENOENT;

	enum six_lock_type lock_want = __btree_lock_want(path, 0);

	int ret = btree_node_lock(trans, path, (void *) ck, 0, lock_want, _THIS_IP_);
	if (ret)
		return ret;

	if (ck->key.btree_id != path->btree_id ||
	    !bpos_eq(ck->key.pos, path->pos)) {
		six_unlock_type(&ck->c.lock, lock_want);
		goto retry;
	}

	if (!test_bit(BKEY_CACHED_ACCESSED, &ck->flags))
		set_bit(BKEY_CACHED_ACCESSED, &ck->flags);

	btree_path_cached_set(trans, path, ck, (enum btree_node_locked_type) lock_want);
	path->uptodate = BTREE_ITER_UPTODATE;
	return 0;
}

int bch2_btree_path_traverse_cached(struct btree_trans *trans, struct btree_path *path,
				    unsigned flags)
{
	EBUG_ON(path->level);

	path->l[1].b = NULL;

	int ret;
	do {
		ret = btree_path_traverse_cached_fast(trans, path);
		if (unlikely(ret == -ENOENT))
			ret = btree_key_cache_fill(trans, path, flags);
	} while (ret == -EEXIST);

	if (unlikely(ret)) {
		path->uptodate = BTREE_ITER_NEED_TRAVERSE;
		if (!bch2_err_matches(ret, BCH_ERR_transaction_restart)) {
			btree_node_unlock(trans, path, 0);
			path->l[0].b = ERR_PTR(ret);
		}
	}
	return ret;
}

static int btree_key_cache_flush_pos(struct btree_trans *trans,
				     struct bkey_cached_key key,
				     u64 journal_seq,
				     unsigned commit_flags,
				     bool evict)
{
	struct bch_fs *c = trans->c;
	struct journal *j = &c->journal;
	struct btree_iter c_iter, b_iter;
	struct bkey_cached *ck = NULL;
	int ret;

	bch2_trans_iter_init(trans, &b_iter, key.btree_id, key.pos,
			     BTREE_ITER_slots|
			     BTREE_ITER_intent|
			     BTREE_ITER_all_snapshots);
	bch2_trans_iter_init(trans, &c_iter, key.btree_id, key.pos,
			     BTREE_ITER_cached|
			     BTREE_ITER_intent);
	b_iter.flags &= ~BTREE_ITER_with_key_cache;

	ret = bch2_btree_iter_traverse(&c_iter);
	if (ret)
		goto out;

	ck = (void *) btree_iter_path(trans, &c_iter)->l[0].b;
	if (!ck)
		goto out;

	if (!test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
		if (evict)
			goto evict;
		goto out;
	}

	if (journal_seq && ck->journal.seq != journal_seq)
		goto out;

	trans->journal_res.seq = ck->journal.seq;

	/*
	 * If we're at the end of the journal, we really want to free up space
	 * in the journal right away - we don't want to pin that old journal
	 * sequence number with a new btree node write, we want to re-journal
	 * the update
	 */
	if (ck->journal.seq == journal_last_seq(j))
		commit_flags |= BCH_WATERMARK_reclaim;

	if (ck->journal.seq != journal_last_seq(j) ||
	    !test_bit(JOURNAL_space_low, &c->journal.flags))
		commit_flags |= BCH_TRANS_COMMIT_no_journal_res;

	ret   = bch2_btree_iter_traverse(&b_iter) ?:
		bch2_trans_update(trans, &b_iter, ck->k,
				  BTREE_UPDATE_key_cache_reclaim|
				  BTREE_UPDATE_internal_snapshot_node|
				  BTREE_TRIGGER_norun) ?:
		bch2_trans_commit(trans, NULL, NULL,
				  BCH_TRANS_COMMIT_no_check_rw|
				  BCH_TRANS_COMMIT_no_enospc|
				  commit_flags);

	bch2_fs_fatal_err_on(ret &&
			     !bch2_err_matches(ret, BCH_ERR_transaction_restart) &&
			     !bch2_err_matches(ret, BCH_ERR_journal_reclaim_would_deadlock) &&
			     !bch2_journal_error(j), c,
			     "flushing key cache: %s", bch2_err_str(ret));
	if (ret)
		goto out;

	bch2_journal_pin_drop(j, &ck->journal);

	struct btree_path *path = btree_iter_path(trans, &c_iter);
	BUG_ON(!btree_node_locked(path, 0));

	if (!evict) {
		if (test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
			clear_bit(BKEY_CACHED_DIRTY, &ck->flags);
			atomic_long_dec(&c->btree_key_cache.nr_dirty);
		}
	} else {
		struct btree_path *path2;
		unsigned i;
evict:
		trans_for_each_path(trans, path2, i)
			if (path2 != path)
				__bch2_btree_path_unlock(trans, path2);

		bch2_btree_node_lock_write_nofail(trans, path, &ck->c);

		if (test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
			clear_bit(BKEY_CACHED_DIRTY, &ck->flags);
			atomic_long_dec(&c->btree_key_cache.nr_dirty);
		}

		mark_btree_node_locked_noreset(path, 0, BTREE_NODE_UNLOCKED);
		if (bkey_cached_evict(&c->btree_key_cache, ck)) {
			bkey_cached_free(&c->btree_key_cache, ck);
		} else {
			six_unlock_write(&ck->c.lock);
			six_unlock_intent(&ck->c.lock);
		}
	}
out:
	bch2_trans_iter_exit(trans, &b_iter);
	bch2_trans_iter_exit(trans, &c_iter);
	return ret;
}

int bch2_btree_key_cache_journal_flush(struct journal *j,
				struct journal_entry_pin *pin, u64 seq)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct bkey_cached *ck =
		container_of(pin, struct bkey_cached, journal);
	struct bkey_cached_key key;
	struct btree_trans *trans = bch2_trans_get(c);
	int srcu_idx = srcu_read_lock(&c->btree_trans_barrier);
	int ret = 0;

	btree_node_lock_nopath_nofail(trans, &ck->c, SIX_LOCK_read);
	key = ck->key;

	if (ck->journal.seq != seq ||
	    !test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
		six_unlock_read(&ck->c.lock);
		goto unlock;
	}

	if (ck->seq != seq) {
		bch2_journal_pin_update(&c->journal, ck->seq, &ck->journal,
					bch2_btree_key_cache_journal_flush);
		six_unlock_read(&ck->c.lock);
		goto unlock;
	}
	six_unlock_read(&ck->c.lock);

	ret = lockrestart_do(trans,
		btree_key_cache_flush_pos(trans, key, seq,
				BCH_TRANS_COMMIT_journal_reclaim, false));
unlock:
	srcu_read_unlock(&c->btree_trans_barrier, srcu_idx);

	bch2_trans_put(trans);
	return ret;
}

bool bch2_btree_insert_key_cached(struct btree_trans *trans,
				  unsigned flags,
				  struct btree_insert_entry *insert_entry)
{
	struct bch_fs *c = trans->c;
	struct bkey_cached *ck = (void *) (trans->paths + insert_entry->path)->l[0].b;
	struct bkey_i *insert = insert_entry->k;
	bool kick_reclaim = false;

	BUG_ON(insert->k.u64s > ck->u64s);

	bkey_copy(ck->k, insert);

	if (!test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
		EBUG_ON(test_bit(BCH_FS_clean_shutdown, &c->flags));
		set_bit(BKEY_CACHED_DIRTY, &ck->flags);
		atomic_long_inc(&c->btree_key_cache.nr_dirty);

		if (bch2_nr_btree_keys_need_flush(c))
			kick_reclaim = true;
	}

	/*
	 * To minimize lock contention, we only add the journal pin here and
	 * defer pin updates to the flush callback via ->seq. Be careful not to
	 * update ->seq on nojournal commits because we don't want to update the
	 * pin to a seq that doesn't include journal updates on disk. Otherwise
	 * we risk losing the update after a crash.
	 *
	 * The only exception is if the pin is not active in the first place. We
	 * have to add the pin because journal reclaim drives key cache
	 * flushing. The flush callback will not proceed unless ->seq matches
	 * the latest pin, so make sure it starts with a consistent value.
	 */
	if (!(insert_entry->flags & BTREE_UPDATE_nojournal) ||
	    !journal_pin_active(&ck->journal)) {
		ck->seq = trans->journal_res.seq;
	}
	bch2_journal_pin_add(&c->journal, trans->journal_res.seq,
			     &ck->journal, bch2_btree_key_cache_journal_flush);

	if (kick_reclaim)
		journal_reclaim_kick(&c->journal);
	return true;
}

void bch2_btree_key_cache_drop(struct btree_trans *trans,
			       struct btree_path *path)
{
	struct bch_fs *c = trans->c;
	struct btree_key_cache *bc = &c->btree_key_cache;
	struct bkey_cached *ck = (void *) path->l[0].b;

	/*
	 * We just did an update to the btree, bypassing the key cache: the key
	 * cache key is now stale and must be dropped, even if dirty:
	 */
	if (test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
		clear_bit(BKEY_CACHED_DIRTY, &ck->flags);
		atomic_long_dec(&c->btree_key_cache.nr_dirty);
		bch2_journal_pin_drop(&c->journal, &ck->journal);
	}

	bkey_cached_evict(bc, ck);
	bkey_cached_free(bc, ck);

	mark_btree_node_locked(trans, path, 0, BTREE_NODE_UNLOCKED);
	btree_path_set_dirty(path, BTREE_ITER_NEED_TRAVERSE);
	path->should_be_locked = false;
}

static unsigned long bch2_btree_key_cache_scan(struct shrinker *shrink,
					   struct shrink_control *sc)
{
	struct bch_fs *c = shrink->private_data;
	struct btree_key_cache *bc = &c->btree_key_cache;
	struct bucket_table *tbl;
	struct bkey_cached *ck;
	size_t scanned = 0, freed = 0, nr = sc->nr_to_scan;
	unsigned iter, start;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&c->btree_trans_barrier);
	rcu_read_lock();

	tbl = rht_dereference_rcu(bc->table.tbl, &bc->table);

	/*
	 * Scanning is expensive while a rehash is in progress - most elements
	 * will be on the new hashtable, if it's in progress
	 *
	 * A rehash could still start while we're scanning - that's ok, we'll
	 * still see most elements.
	 */
	if (unlikely(tbl->nest)) {
		rcu_read_unlock();
		srcu_read_unlock(&c->btree_trans_barrier, srcu_idx);
		return SHRINK_STOP;
	}

	iter = bc->shrink_iter;
	if (iter >= tbl->size)
		iter = 0;
	start = iter;

	do {
		struct rhash_head *pos, *next;

		pos = rht_ptr_rcu(&tbl->buckets[iter]);

		while (!rht_is_a_nulls(pos)) {
			next = rht_dereference_bucket_rcu(pos->next, tbl, iter);
			ck = container_of(pos, struct bkey_cached, hash);

			if (test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
				bc->skipped_dirty++;
			} else if (test_bit(BKEY_CACHED_ACCESSED, &ck->flags)) {
				clear_bit(BKEY_CACHED_ACCESSED, &ck->flags);
				bc->skipped_accessed++;
			} else if (!bkey_cached_lock_for_evict(ck)) {
				bc->skipped_lock_fail++;
			} else if (bkey_cached_evict(bc, ck)) {
				bkey_cached_free(bc, ck);
				bc->freed++;
				freed++;
			} else {
				six_unlock_write(&ck->c.lock);
				six_unlock_intent(&ck->c.lock);
			}

			scanned++;
			if (scanned >= nr)
				goto out;

			pos = next;
		}

		iter++;
		if (iter >= tbl->size)
			iter = 0;
	} while (scanned < nr && iter != start);
out:
	bc->shrink_iter = iter;

	rcu_read_unlock();
	srcu_read_unlock(&c->btree_trans_barrier, srcu_idx);

	return freed;
}

static unsigned long bch2_btree_key_cache_count(struct shrinker *shrink,
					    struct shrink_control *sc)
{
	struct bch_fs *c = shrink->private_data;
	struct btree_key_cache *bc = &c->btree_key_cache;
	long nr = atomic_long_read(&bc->nr_keys) -
		atomic_long_read(&bc->nr_dirty);

	/*
	 * Avoid hammering our shrinker too much if it's nearly empty - the
	 * shrinker code doesn't take into account how big our cache is, if it's
	 * mostly empty but the system is under memory pressure it causes nasty
	 * lock contention:
	 */
	nr -= 128;

	return max(0L, nr);
}

void bch2_fs_btree_key_cache_exit(struct btree_key_cache *bc)
{
	struct bch_fs *c = container_of(bc, struct bch_fs, btree_key_cache);
	struct bucket_table *tbl;
	struct bkey_cached *ck;
	struct rhash_head *pos;
	LIST_HEAD(items);
	unsigned i;

	shrinker_free(bc->shrink);

	/*
	 * The loop is needed to guard against racing with rehash:
	 */
	while (atomic_long_read(&bc->nr_keys)) {
		rcu_read_lock();
		tbl = rht_dereference_rcu(bc->table.tbl, &bc->table);
		if (tbl) {
			if (tbl->nest) {
				/* wait for in progress rehash */
				rcu_read_unlock();
				mutex_lock(&bc->table.mutex);
				mutex_unlock(&bc->table.mutex);
				rcu_read_lock();
				continue;
			}
			for (i = 0; i < tbl->size; i++)
				while (pos = rht_ptr_rcu(&tbl->buckets[i]), !rht_is_a_nulls(pos)) {
					ck = container_of(pos, struct bkey_cached, hash);
					BUG_ON(!bkey_cached_evict(bc, ck));
					kfree(ck->k);
					kmem_cache_free(bch2_key_cache, ck);
				}
		}
		rcu_read_unlock();
	}

	if (atomic_long_read(&bc->nr_dirty) &&
	    !bch2_journal_error(&c->journal) &&
	    test_bit(BCH_FS_was_rw, &c->flags))
		panic("btree key cache shutdown error: nr_dirty nonzero (%li)\n",
		      atomic_long_read(&bc->nr_dirty));

	if (atomic_long_read(&bc->nr_keys))
		panic("btree key cache shutdown error: nr_keys nonzero (%li)\n",
		      atomic_long_read(&bc->nr_keys));

	if (bc->table_init_done)
		rhashtable_destroy(&bc->table);

	rcu_pending_exit(&bc->pending[0]);
	rcu_pending_exit(&bc->pending[1]);

	free_percpu(bc->nr_pending);
}

void bch2_fs_btree_key_cache_init_early(struct btree_key_cache *c)
{
}

int bch2_fs_btree_key_cache_init(struct btree_key_cache *bc)
{
	struct bch_fs *c = container_of(bc, struct bch_fs, btree_key_cache);
	struct shrinker *shrink;

	bc->nr_pending = alloc_percpu(size_t);
	if (!bc->nr_pending)
		return -BCH_ERR_ENOMEM_fs_btree_cache_init;

	if (rcu_pending_init(&bc->pending[0], &c->btree_trans_barrier, __bkey_cached_free) ||
	    rcu_pending_init(&bc->pending[1], &c->btree_trans_barrier, __bkey_cached_free))
		return -BCH_ERR_ENOMEM_fs_btree_cache_init;

	if (rhashtable_init(&bc->table, &bch2_btree_key_cache_params))
		return -BCH_ERR_ENOMEM_fs_btree_cache_init;

	bc->table_init_done = true;

	shrink = shrinker_alloc(0, "%s-btree_key_cache", c->name);
	if (!shrink)
		return -BCH_ERR_ENOMEM_fs_btree_cache_init;
	bc->shrink = shrink;
	shrink->count_objects	= bch2_btree_key_cache_count;
	shrink->scan_objects	= bch2_btree_key_cache_scan;
	shrink->batch		= 1 << 14;
	shrink->seeks		= 0;
	shrink->private_data	= c;
	shrinker_register(shrink);
	return 0;
}

void bch2_btree_key_cache_to_text(struct printbuf *out, struct btree_key_cache *bc)
{
	printbuf_tabstop_push(out, 24);
	printbuf_tabstop_push(out, 12);

	prt_printf(out, "keys:\t%lu\r\n",		atomic_long_read(&bc->nr_keys));
	prt_printf(out, "dirty:\t%lu\r\n",		atomic_long_read(&bc->nr_dirty));
	prt_printf(out, "table size:\t%u\r\n",		bc->table.tbl->size);
	prt_newline(out);
	prt_printf(out, "shrinker:\n");
	prt_printf(out, "requested_to_free:\t%lu\r\n",	bc->requested_to_free);
	prt_printf(out, "freed:\t%lu\r\n",		bc->freed);
	prt_printf(out, "skipped_dirty:\t%lu\r\n",	bc->skipped_dirty);
	prt_printf(out, "skipped_accessed:\t%lu\r\n",	bc->skipped_accessed);
	prt_printf(out, "skipped_lock_fail:\t%lu\r\n",	bc->skipped_lock_fail);
	prt_newline(out);
	prt_printf(out, "pending:\t%lu\r\n",		per_cpu_sum(bc->nr_pending));
}

void bch2_btree_key_cache_exit(void)
{
	kmem_cache_destroy(bch2_key_cache);
}

int __init bch2_btree_key_cache_init(void)
{
	bch2_key_cache = KMEM_CACHE(bkey_cached, SLAB_RECLAIM_ACCOUNT);
	if (!bch2_key_cache)
		return -ENOMEM;

	return 0;
}
