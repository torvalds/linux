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

static void bkey_cached_evict(struct btree_key_cache *c,
			      struct bkey_cached *ck)
{
	BUG_ON(rhashtable_remove_fast(&c->table, &ck->hash,
				      bch2_btree_key_cache_params));
	memset(&ck->key, ~0, sizeof(ck->key));

	atomic_long_dec(&c->nr_keys);
}

static void bkey_cached_free(struct btree_key_cache *bc,
			     struct bkey_cached *ck)
{
	struct bch_fs *c = container_of(bc, struct bch_fs, btree_key_cache);

	BUG_ON(test_bit(BKEY_CACHED_DIRTY, &ck->flags));

	ck->btree_trans_barrier_seq =
		start_poll_synchronize_srcu(&c->btree_trans_barrier);

	if (ck->c.lock.readers)
		list_move_tail(&ck->list, &bc->freed_pcpu);
	else
		list_move_tail(&ck->list, &bc->freed_nonpcpu);
	atomic_long_inc(&bc->nr_freed);

	kfree(ck->k);
	ck->k		= NULL;
	ck->u64s	= 0;

	six_unlock_write(&ck->c.lock);
	six_unlock_intent(&ck->c.lock);
}

#ifdef __KERNEL__
static void __bkey_cached_move_to_freelist_ordered(struct btree_key_cache *bc,
						   struct bkey_cached *ck)
{
	struct bkey_cached *pos;

	list_for_each_entry_reverse(pos, &bc->freed_nonpcpu, list) {
		if (ULONG_CMP_GE(ck->btree_trans_barrier_seq,
				 pos->btree_trans_barrier_seq)) {
			list_move(&ck->list, &pos->list);
			return;
		}
	}

	list_move(&ck->list, &bc->freed_nonpcpu);
}
#endif

static void bkey_cached_move_to_freelist(struct btree_key_cache *bc,
					 struct bkey_cached *ck)
{
	BUG_ON(test_bit(BKEY_CACHED_DIRTY, &ck->flags));

	if (!ck->c.lock.readers) {
#ifdef __KERNEL__
		struct btree_key_cache_freelist *f;
		bool freed = false;

		preempt_disable();
		f = this_cpu_ptr(bc->pcpu_freed);

		if (f->nr < ARRAY_SIZE(f->objs)) {
			f->objs[f->nr++] = ck;
			freed = true;
		}
		preempt_enable();

		if (!freed) {
			mutex_lock(&bc->lock);
			preempt_disable();
			f = this_cpu_ptr(bc->pcpu_freed);

			while (f->nr > ARRAY_SIZE(f->objs) / 2) {
				struct bkey_cached *ck2 = f->objs[--f->nr];

				__bkey_cached_move_to_freelist_ordered(bc, ck2);
			}
			preempt_enable();

			__bkey_cached_move_to_freelist_ordered(bc, ck);
			mutex_unlock(&bc->lock);
		}
#else
		mutex_lock(&bc->lock);
		list_move_tail(&ck->list, &bc->freed_nonpcpu);
		mutex_unlock(&bc->lock);
#endif
	} else {
		mutex_lock(&bc->lock);
		list_move_tail(&ck->list, &bc->freed_pcpu);
		mutex_unlock(&bc->lock);
	}
}

static void bkey_cached_free_fast(struct btree_key_cache *bc,
				  struct bkey_cached *ck)
{
	struct bch_fs *c = container_of(bc, struct bch_fs, btree_key_cache);

	ck->btree_trans_barrier_seq =
		start_poll_synchronize_srcu(&c->btree_trans_barrier);

	list_del_init(&ck->list);
	atomic_long_inc(&bc->nr_freed);

	kfree(ck->k);
	ck->k		= NULL;
	ck->u64s	= 0;

	bkey_cached_move_to_freelist(bc, ck);

	six_unlock_write(&ck->c.lock);
	six_unlock_intent(&ck->c.lock);
}

static struct bkey_cached *
bkey_cached_alloc(struct btree_trans *trans, struct btree_path *path,
		  bool *was_new)
{
	struct bch_fs *c = trans->c;
	struct btree_key_cache *bc = &c->btree_key_cache;
	struct bkey_cached *ck = NULL;
	bool pcpu_readers = btree_uses_pcpu_readers(path->btree_id);
	int ret;

	if (!pcpu_readers) {
#ifdef __KERNEL__
		struct btree_key_cache_freelist *f;

		preempt_disable();
		f = this_cpu_ptr(bc->pcpu_freed);
		if (f->nr)
			ck = f->objs[--f->nr];
		preempt_enable();

		if (!ck) {
			mutex_lock(&bc->lock);
			preempt_disable();
			f = this_cpu_ptr(bc->pcpu_freed);

			while (!list_empty(&bc->freed_nonpcpu) &&
			       f->nr < ARRAY_SIZE(f->objs) / 2) {
				ck = list_last_entry(&bc->freed_nonpcpu, struct bkey_cached, list);
				list_del_init(&ck->list);
				f->objs[f->nr++] = ck;
			}

			ck = f->nr ? f->objs[--f->nr] : NULL;
			preempt_enable();
			mutex_unlock(&bc->lock);
		}
#else
		mutex_lock(&bc->lock);
		if (!list_empty(&bc->freed_nonpcpu)) {
			ck = list_last_entry(&bc->freed_nonpcpu, struct bkey_cached, list);
			list_del_init(&ck->list);
		}
		mutex_unlock(&bc->lock);
#endif
	} else {
		mutex_lock(&bc->lock);
		if (!list_empty(&bc->freed_pcpu)) {
			ck = list_last_entry(&bc->freed_pcpu, struct bkey_cached, list);
			list_del_init(&ck->list);
		}
		mutex_unlock(&bc->lock);
	}

	if (ck) {
		int ret;

		ret = btree_node_lock_nopath(trans, &ck->c, SIX_LOCK_intent, _THIS_IP_);
		if (unlikely(ret)) {
			bkey_cached_move_to_freelist(bc, ck);
			return ERR_PTR(ret);
		}

		path->l[0].b = (void *) ck;
		path->l[0].lock_seq = ck->c.lock.state.seq;
		mark_btree_node_locked(trans, path, 0, SIX_LOCK_intent);

		ret = bch2_btree_node_lock_write(trans, path, &ck->c);
		if (unlikely(ret)) {
			btree_node_unlock(trans, path, 0);
			bkey_cached_move_to_freelist(bc, ck);
			return ERR_PTR(ret);
		}

		return ck;
	}

	ck = kmem_cache_zalloc(bch2_key_cache, GFP_NOWAIT|__GFP_NOWARN);
	if (likely(ck))
		goto init;

	bch2_trans_unlock(trans);

	ck = kmem_cache_zalloc(bch2_key_cache, GFP_KERNEL);

	ret = bch2_trans_relock(trans);
	if (ret) {
		kmem_cache_free(bch2_key_cache, ck);
		return ERR_PTR(ret);
	}

	if (!ck)
		return NULL;
init:
	INIT_LIST_HEAD(&ck->list);
	bch2_btree_lock_init(&ck->c);
	if (pcpu_readers)
		six_lock_pcpu_alloc(&ck->c.lock);

	ck->c.cached = true;
	BUG_ON(!six_trylock_intent(&ck->c.lock));
	BUG_ON(!six_trylock_write(&ck->c.lock));
	*was_new = true;
	return ck;
}

static struct bkey_cached *
bkey_cached_reuse(struct btree_key_cache *c)
{
	struct bucket_table *tbl;
	struct rhash_head *pos;
	struct bkey_cached *ck;
	unsigned i;

	mutex_lock(&c->lock);
	rcu_read_lock();
	tbl = rht_dereference_rcu(c->table.tbl, &c->table);
	for (i = 0; i < tbl->size; i++)
		rht_for_each_entry_rcu(ck, pos, tbl, i, hash) {
			if (!test_bit(BKEY_CACHED_DIRTY, &ck->flags) &&
			    bkey_cached_lock_for_evict(ck)) {
				bkey_cached_evict(c, ck);
				goto out;
			}
		}
	ck = NULL;
out:
	rcu_read_unlock();
	mutex_unlock(&c->lock);
	return ck;
}

static struct bkey_cached *
btree_key_cache_create(struct btree_trans *trans, struct btree_path *path)
{
	struct bch_fs *c = trans->c;
	struct btree_key_cache *bc = &c->btree_key_cache;
	struct bkey_cached *ck;
	bool was_new = false;

	ck = bkey_cached_alloc(trans, path, &was_new);
	if (IS_ERR(ck))
		return ck;

	if (unlikely(!ck)) {
		ck = bkey_cached_reuse(bc);
		if (unlikely(!ck)) {
			bch_err(c, "error allocating memory for key cache item, btree %s",
				bch2_btree_ids[path->btree_id]);
			return ERR_PTR(-ENOMEM);
		}

		mark_btree_node_locked(trans, path, 0, SIX_LOCK_intent);
	} else {
		if (path->btree_id == BTREE_ID_subvolumes)
			six_lock_pcpu_alloc(&ck->c.lock);
	}

	ck->c.level		= 0;
	ck->c.btree_id		= path->btree_id;
	ck->key.btree_id	= path->btree_id;
	ck->key.pos		= path->pos;
	ck->valid		= false;
	ck->flags		= 1U << BKEY_CACHED_ACCESSED;

	if (unlikely(rhashtable_lookup_insert_fast(&bc->table,
					  &ck->hash,
					  bch2_btree_key_cache_params))) {
		/* We raced with another fill: */

		if (likely(was_new)) {
			six_unlock_write(&ck->c.lock);
			six_unlock_intent(&ck->c.lock);
			kfree(ck);
		} else {
			bkey_cached_free_fast(bc, ck);
		}

		mark_btree_node_locked(trans, path, 0, BTREE_NODE_UNLOCKED);
		return NULL;
	}

	atomic_long_inc(&bc->nr_keys);

	six_unlock_write(&ck->c.lock);

	return ck;
}

static int btree_key_cache_fill(struct btree_trans *trans,
				struct btree_path *ck_path,
				struct bkey_cached *ck)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	unsigned new_u64s = 0;
	struct bkey_i *new_k = NULL;
	int ret;

	bch2_trans_iter_init(trans, &iter, ck->key.btree_id, ck->key.pos,
			     BTREE_ITER_KEY_CACHE_FILL|
			     BTREE_ITER_CACHED_NOFILL);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (!bch2_btree_node_relock(trans, ck_path, 0)) {
		trace_and_count(trans->c, trans_restart_relock_key_cache_fill, trans, _THIS_IP_, ck_path);
		ret = btree_trans_restart(trans, BCH_ERR_transaction_restart_key_cache_fill);
		goto err;
	}

	/*
	 * bch2_varint_decode can read past the end of the buffer by at
	 * most 7 bytes (it won't be used):
	 */
	new_u64s = k.k->u64s + 1;

	/*
	 * Allocate some extra space so that the transaction commit path is less
	 * likely to have to reallocate, since that requires a transaction
	 * restart:
	 */
	new_u64s = min(256U, (new_u64s * 3) / 2);

	if (new_u64s > ck->u64s) {
		new_u64s = roundup_pow_of_two(new_u64s);
		new_k = kmalloc(new_u64s * sizeof(u64), GFP_NOWAIT|__GFP_NOWARN);
		if (!new_k) {
			bch2_trans_unlock(trans);

			new_k = kmalloc(new_u64s * sizeof(u64), GFP_KERNEL);
			if (!new_k) {
				bch_err(trans->c, "error allocating memory for key cache key, btree %s u64s %u",
					bch2_btree_ids[ck->key.btree_id], new_u64s);
				ret = -ENOMEM;
				goto err;
			}

			if (!bch2_btree_node_relock(trans, ck_path, 0)) {
				kfree(new_k);
				trace_and_count(trans->c, trans_restart_relock_key_cache_fill, trans, _THIS_IP_, ck_path);
				ret = btree_trans_restart(trans, BCH_ERR_transaction_restart_key_cache_fill);
				goto err;
			}

			ret = bch2_trans_relock(trans);
			if (ret) {
				kfree(new_k);
				goto err;
			}
		}
	}

	ret = bch2_btree_node_lock_write(trans, ck_path, &ck_path->l[0].b->c);
	if (ret) {
		kfree(new_k);
		goto err;
	}

	if (new_k) {
		kfree(ck->k);
		ck->u64s = new_u64s;
		ck->k = new_k;
	}

	bkey_reassemble(ck->k, k);
	ck->valid = true;
	bch2_btree_node_unlock_write(trans, ck_path, ck_path->l[0].b);

	/* We're not likely to need this iterator again: */
	set_btree_iter_dontneed(&iter);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static noinline int
bch2_btree_path_traverse_cached_slowpath(struct btree_trans *trans, struct btree_path *path,
					 unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_cached *ck;
	int ret = 0;

	BUG_ON(path->level);

	path->l[1].b = NULL;

	if (bch2_btree_node_relock_notrace(trans, path, 0)) {
		ck = (void *) path->l[0].b;
		goto fill;
	}
retry:
	ck = bch2_btree_key_cache_find(c, path->btree_id, path->pos);
	if (!ck) {
		ck = btree_key_cache_create(trans, path);
		ret = PTR_ERR_OR_ZERO(ck);
		if (ret)
			goto err;
		if (!ck)
			goto retry;

		mark_btree_node_locked(trans, path, 0, SIX_LOCK_intent);
		path->locks_want = 1;
	} else {
		enum six_lock_type lock_want = __btree_lock_want(path, 0);

		ret = btree_node_lock(trans, path, (void *) ck, 0,
				      lock_want, _THIS_IP_);
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			goto err;

		BUG_ON(ret);

		if (ck->key.btree_id != path->btree_id ||
		    !bpos_eq(ck->key.pos, path->pos)) {
			six_unlock_type(&ck->c.lock, lock_want);
			goto retry;
		}

		mark_btree_node_locked(trans, path, 0, lock_want);
	}

	path->l[0].lock_seq	= ck->c.lock.state.seq;
	path->l[0].b		= (void *) ck;
fill:
	path->uptodate = BTREE_ITER_UPTODATE;

	if (!ck->valid && !(flags & BTREE_ITER_CACHED_NOFILL)) {
		/*
		 * Using the underscore version because we haven't set
		 * path->uptodate yet:
		 */
		if (!path->locks_want &&
		    !__bch2_btree_path_upgrade(trans, path, 1)) {
			trace_and_count(trans->c, trans_restart_key_cache_upgrade, trans, _THIS_IP_);
			ret = btree_trans_restart(trans, BCH_ERR_transaction_restart_key_cache_upgrade);
			goto err;
		}

		ret = btree_key_cache_fill(trans, path, ck);
		if (ret)
			goto err;

		ret = bch2_btree_path_relock(trans, path, _THIS_IP_);
		if (ret)
			goto err;

		path->uptodate = BTREE_ITER_UPTODATE;
	}

	if (!test_bit(BKEY_CACHED_ACCESSED, &ck->flags))
		set_bit(BKEY_CACHED_ACCESSED, &ck->flags);

	BUG_ON(btree_node_locked_type(path, 0) != btree_lock_want(path, 0));
	BUG_ON(path->uptodate);

	return ret;
err:
	path->uptodate = BTREE_ITER_NEED_TRAVERSE;
	if (!bch2_err_matches(ret, BCH_ERR_transaction_restart)) {
		btree_node_unlock(trans, path, 0);
		path->l[0].b = ERR_PTR(ret);
	}
	return ret;
}

int bch2_btree_path_traverse_cached(struct btree_trans *trans, struct btree_path *path,
				    unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_cached *ck;
	int ret = 0;

	EBUG_ON(path->level);

	path->l[1].b = NULL;

	if (bch2_btree_node_relock_notrace(trans, path, 0)) {
		ck = (void *) path->l[0].b;
		goto fill;
	}
retry:
	ck = bch2_btree_key_cache_find(c, path->btree_id, path->pos);
	if (!ck) {
		return bch2_btree_path_traverse_cached_slowpath(trans, path, flags);
	} else {
		enum six_lock_type lock_want = __btree_lock_want(path, 0);

		ret = btree_node_lock(trans, path, (void *) ck, 0,
				      lock_want, _THIS_IP_);
		EBUG_ON(ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart));

		if (ret)
			return ret;

		if (ck->key.btree_id != path->btree_id ||
		    !bpos_eq(ck->key.pos, path->pos)) {
			six_unlock_type(&ck->c.lock, lock_want);
			goto retry;
		}

		mark_btree_node_locked(trans, path, 0, lock_want);
	}

	path->l[0].lock_seq	= ck->c.lock.state.seq;
	path->l[0].b		= (void *) ck;
fill:
	if (!ck->valid)
		return bch2_btree_path_traverse_cached_slowpath(trans, path, flags);

	if (!test_bit(BKEY_CACHED_ACCESSED, &ck->flags))
		set_bit(BKEY_CACHED_ACCESSED, &ck->flags);

	path->uptodate = BTREE_ITER_UPTODATE;
	EBUG_ON(!ck->valid);
	EBUG_ON(btree_node_locked_type(path, 0) != btree_lock_want(path, 0));

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
			     BTREE_ITER_SLOTS|
			     BTREE_ITER_INTENT|
			     BTREE_ITER_ALL_SNAPSHOTS);
	bch2_trans_iter_init(trans, &c_iter, key.btree_id, key.pos,
			     BTREE_ITER_CACHED|
			     BTREE_ITER_INTENT);
	b_iter.flags &= ~BTREE_ITER_WITH_KEY_CACHE;

	ret = bch2_btree_iter_traverse(&c_iter);
	if (ret)
		goto out;

	ck = (void *) c_iter.path->l[0].b;
	if (!ck)
		goto out;

	if (!test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
		if (evict)
			goto evict;
		goto out;
	}

	BUG_ON(!ck->valid);

	if (journal_seq && ck->journal.seq != journal_seq)
		goto out;

	/*
	 * Since journal reclaim depends on us making progress here, and the
	 * allocator/copygc depend on journal reclaim making progress, we need
	 * to be using alloc reserves:
	 */
	ret   = bch2_btree_iter_traverse(&b_iter) ?:
		bch2_trans_update(trans, &b_iter, ck->k,
				  BTREE_UPDATE_KEY_CACHE_RECLAIM|
				  BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE|
				  BTREE_TRIGGER_NORUN) ?:
		bch2_trans_commit(trans, NULL, NULL,
				  BTREE_INSERT_NOCHECK_RW|
				  BTREE_INSERT_NOFAIL|
				  BTREE_INSERT_USE_RESERVE|
				  (ck->journal.seq == journal_last_seq(j)
				   ? JOURNAL_WATERMARK_reserved
				   : 0)|
				  commit_flags);

	bch2_fs_fatal_err_on(ret &&
			     !bch2_err_matches(ret, BCH_ERR_transaction_restart) &&
			     !bch2_err_matches(ret, BCH_ERR_journal_reclaim_would_deadlock) &&
			     !bch2_journal_error(j), c,
			     "error flushing key cache: %s", bch2_err_str(ret));
	if (ret)
		goto out;

	bch2_journal_pin_drop(j, &ck->journal);
	bch2_journal_preres_put(j, &ck->res);

	BUG_ON(!btree_node_locked(c_iter.path, 0));

	if (!evict) {
		if (test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
			clear_bit(BKEY_CACHED_DIRTY, &ck->flags);
			atomic_long_dec(&c->btree_key_cache.nr_dirty);
		}
	} else {
		struct btree_path *path2;
evict:
		trans_for_each_path(trans, path2)
			if (path2 != c_iter.path)
				__bch2_btree_path_unlock(trans, path2);

		bch2_btree_node_lock_write_nofail(trans, c_iter.path, &ck->c);

		if (test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
			clear_bit(BKEY_CACHED_DIRTY, &ck->flags);
			atomic_long_dec(&c->btree_key_cache.nr_dirty);
		}

		mark_btree_node_locked_noreset(c_iter.path, 0, BTREE_NODE_UNLOCKED);
		bkey_cached_evict(&c->btree_key_cache, ck);
		bkey_cached_free_fast(&c->btree_key_cache, ck);
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
	struct btree_trans trans;
	int srcu_idx = srcu_read_lock(&c->btree_trans_barrier);
	int ret = 0;

	bch2_trans_init(&trans, c, 0, 0);

	btree_node_lock_nopath_nofail(&trans, &ck->c, SIX_LOCK_read);
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

	ret = commit_do(&trans, NULL, NULL, 0,
		btree_key_cache_flush_pos(&trans, key, seq,
				BTREE_INSERT_JOURNAL_RECLAIM, false));
unlock:
	srcu_read_unlock(&c->btree_trans_barrier, srcu_idx);

	bch2_trans_exit(&trans);
	return ret;
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

	return btree_key_cache_flush_pos(trans, key, 0, 0, true);
}

bool bch2_btree_insert_key_cached(struct btree_trans *trans,
				  unsigned flags,
				  struct btree_path *path,
				  struct bkey_i *insert)
{
	struct bch_fs *c = trans->c;
	struct bkey_cached *ck = (void *) path->l[0].b;
	bool kick_reclaim = false;

	BUG_ON(insert->k.u64s > ck->u64s);

	if (likely(!(flags & BTREE_INSERT_JOURNAL_REPLAY))) {
		int difference;

		BUG_ON(jset_u64s(insert->k.u64s) > trans->journal_preres.u64s);

		difference = jset_u64s(insert->k.u64s) - ck->res.u64s;
		if (difference > 0) {
			trans->journal_preres.u64s	-= difference;
			ck->res.u64s			+= difference;
		}
	}

	bkey_copy(ck->k, insert);
	ck->valid = true;

	if (!test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
		set_bit(BKEY_CACHED_DIRTY, &ck->flags);
		atomic_long_inc(&c->btree_key_cache.nr_dirty);

		if (bch2_nr_btree_keys_need_flush(c))
			kick_reclaim = true;
	}

	bch2_journal_pin_add(&c->journal, trans->journal_res.seq,
			     &ck->journal, bch2_btree_key_cache_journal_flush);
	ck->seq = trans->journal_res.seq;

	if (kick_reclaim)
		journal_reclaim_kick(&c->journal);
	return true;
}

void bch2_btree_key_cache_drop(struct btree_trans *trans,
			       struct btree_path *path)
{
	struct bch_fs *c = trans->c;
	struct bkey_cached *ck = (void *) path->l[0].b;

	BUG_ON(!ck->valid);

	/*
	 * We just did an update to the btree, bypassing the key cache: the key
	 * cache key is now stale and must be dropped, even if dirty:
	 */
	if (test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
		clear_bit(BKEY_CACHED_DIRTY, &ck->flags);
		atomic_long_dec(&c->btree_key_cache.nr_dirty);
		bch2_journal_pin_drop(&c->journal, &ck->journal);
	}

	ck->valid = false;
}

static unsigned long bch2_btree_key_cache_scan(struct shrinker *shrink,
					   struct shrink_control *sc)
{
	struct bch_fs *c = container_of(shrink, struct bch_fs,
					btree_key_cache.shrink);
	struct btree_key_cache *bc = &c->btree_key_cache;
	struct bucket_table *tbl;
	struct bkey_cached *ck, *t;
	size_t scanned = 0, freed = 0, nr = sc->nr_to_scan;
	unsigned start, flags;
	int srcu_idx;

	mutex_lock(&bc->lock);
	srcu_idx = srcu_read_lock(&c->btree_trans_barrier);
	flags = memalloc_nofs_save();

	/*
	 * Newest freed entries are at the end of the list - once we hit one
	 * that's too new to be freed, we can bail out:
	 */
	list_for_each_entry_safe(ck, t, &bc->freed_nonpcpu, list) {
		if (!poll_state_synchronize_srcu(&c->btree_trans_barrier,
						 ck->btree_trans_barrier_seq))
			break;

		list_del(&ck->list);
		six_lock_pcpu_free(&ck->c.lock);
		kmem_cache_free(bch2_key_cache, ck);
		atomic_long_dec(&bc->nr_freed);
		scanned++;
		freed++;
	}

	if (scanned >= nr)
		goto out;

	list_for_each_entry_safe(ck, t, &bc->freed_pcpu, list) {
		if (!poll_state_synchronize_srcu(&c->btree_trans_barrier,
						 ck->btree_trans_barrier_seq))
			break;

		list_del(&ck->list);
		six_lock_pcpu_free(&ck->c.lock);
		kmem_cache_free(bch2_key_cache, ck);
		atomic_long_dec(&bc->nr_freed);
		scanned++;
		freed++;
	}

	if (scanned >= nr)
		goto out;

	rcu_read_lock();
	tbl = rht_dereference_rcu(bc->table.tbl, &bc->table);
	if (bc->shrink_iter >= tbl->size)
		bc->shrink_iter = 0;
	start = bc->shrink_iter;

	do {
		struct rhash_head *pos, *next;

		pos = rht_ptr_rcu(rht_bucket(tbl, bc->shrink_iter));

		while (!rht_is_a_nulls(pos)) {
			next = rht_dereference_bucket_rcu(pos->next, tbl, bc->shrink_iter);
			ck = container_of(pos, struct bkey_cached, hash);

			if (test_bit(BKEY_CACHED_DIRTY, &ck->flags))
				goto next;

			if (test_bit(BKEY_CACHED_ACCESSED, &ck->flags))
				clear_bit(BKEY_CACHED_ACCESSED, &ck->flags);
			else if (bkey_cached_lock_for_evict(ck)) {
				bkey_cached_evict(bc, ck);
				bkey_cached_free(bc, ck);
			}

			scanned++;
			if (scanned >= nr)
				break;
next:
			pos = next;
		}

		bc->shrink_iter++;
		if (bc->shrink_iter >= tbl->size)
			bc->shrink_iter = 0;
	} while (scanned < nr && bc->shrink_iter != start);

	rcu_read_unlock();
out:
	memalloc_nofs_restore(flags);
	srcu_read_unlock(&c->btree_trans_barrier, srcu_idx);
	mutex_unlock(&bc->lock);

	return freed;
}

static unsigned long bch2_btree_key_cache_count(struct shrinker *shrink,
					    struct shrink_control *sc)
{
	struct bch_fs *c = container_of(shrink, struct bch_fs,
					btree_key_cache.shrink);
	struct btree_key_cache *bc = &c->btree_key_cache;
	long nr = atomic_long_read(&bc->nr_keys) -
		atomic_long_read(&bc->nr_dirty);

	return max(0L, nr);
}

void bch2_fs_btree_key_cache_exit(struct btree_key_cache *bc)
{
	struct bch_fs *c = container_of(bc, struct bch_fs, btree_key_cache);
	struct bucket_table *tbl;
	struct bkey_cached *ck, *n;
	struct rhash_head *pos;
	LIST_HEAD(items);
	unsigned i;
#ifdef __KERNEL__
	int cpu;
#endif

	if (bc->shrink.list.next)
		unregister_shrinker(&bc->shrink);

	mutex_lock(&bc->lock);

	/*
	 * The loop is needed to guard against racing with rehash:
	 */
	while (atomic_long_read(&bc->nr_keys)) {
		rcu_read_lock();
		tbl = rht_dereference_rcu(bc->table.tbl, &bc->table);
		if (tbl)
			for (i = 0; i < tbl->size; i++)
				rht_for_each_entry_rcu(ck, pos, tbl, i, hash) {
					bkey_cached_evict(bc, ck);
					list_add(&ck->list, &items);
				}
		rcu_read_unlock();
	}

#ifdef __KERNEL__
	for_each_possible_cpu(cpu) {
		struct btree_key_cache_freelist *f =
			per_cpu_ptr(bc->pcpu_freed, cpu);

		for (i = 0; i < f->nr; i++) {
			ck = f->objs[i];
			list_add(&ck->list, &items);
		}
	}
#endif

	list_splice(&bc->freed_pcpu,	&items);
	list_splice(&bc->freed_nonpcpu,	&items);

	mutex_unlock(&bc->lock);

	list_for_each_entry_safe(ck, n, &items, list) {
		cond_resched();

		bch2_journal_pin_drop(&c->journal, &ck->journal);
		bch2_journal_preres_put(&c->journal, &ck->res);

		list_del(&ck->list);
		kfree(ck->k);
		six_lock_pcpu_free(&ck->c.lock);
		kmem_cache_free(bch2_key_cache, ck);
	}

	if (atomic_long_read(&bc->nr_dirty) &&
	    !bch2_journal_error(&c->journal) &&
	    test_bit(BCH_FS_WAS_RW, &c->flags))
		panic("btree key cache shutdown error: nr_dirty nonzero (%li)\n",
		      atomic_long_read(&bc->nr_dirty));

	if (atomic_long_read(&bc->nr_keys))
		panic("btree key cache shutdown error: nr_keys nonzero (%li)\n",
		      atomic_long_read(&bc->nr_keys));

	if (bc->table_init_done)
		rhashtable_destroy(&bc->table);

	free_percpu(bc->pcpu_freed);
}

void bch2_fs_btree_key_cache_init_early(struct btree_key_cache *c)
{
	mutex_init(&c->lock);
	INIT_LIST_HEAD(&c->freed_pcpu);
	INIT_LIST_HEAD(&c->freed_nonpcpu);
}

int bch2_fs_btree_key_cache_init(struct btree_key_cache *bc)
{
	struct bch_fs *c = container_of(bc, struct bch_fs, btree_key_cache);
	int ret;

#ifdef __KERNEL__
	bc->pcpu_freed = alloc_percpu(struct btree_key_cache_freelist);
	if (!bc->pcpu_freed)
		return -ENOMEM;
#endif

	ret = rhashtable_init(&bc->table, &bch2_btree_key_cache_params);
	if (ret)
		return ret;

	bc->table_init_done = true;

	bc->shrink.seeks		= 0;
	bc->shrink.count_objects	= bch2_btree_key_cache_count;
	bc->shrink.scan_objects		= bch2_btree_key_cache_scan;
	return register_shrinker(&bc->shrink, "%s/btree_key_cache", c->name);
}

void bch2_btree_key_cache_to_text(struct printbuf *out, struct btree_key_cache *c)
{
	prt_printf(out, "nr_freed:\t%zu",	atomic_long_read(&c->nr_freed));
	prt_newline(out);
	prt_printf(out, "nr_keys:\t%lu",	atomic_long_read(&c->nr_keys));
	prt_newline(out);
	prt_printf(out, "nr_dirty:\t%lu",	atomic_long_read(&c->nr_dirty));
	prt_newline(out);
}

void bch2_btree_key_cache_exit(void)
{
	kmem_cache_destroy(bch2_key_cache);
}

int __init bch2_btree_key_cache_init(void)
{
	bch2_key_cache = KMEM_CACHE(bkey_cached, 0);
	if (!bch2_key_cache)
		return -ENOMEM;

	return 0;
}
