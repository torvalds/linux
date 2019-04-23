// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_cache.h"
#include "btree_io.h"
#include "btree_iter.h"
#include "btree_locking.h"
#include "debug.h"
#include "trace.h"

#include <linux/prefetch.h>

const char * const bch2_btree_ids[] = {
#define x(kwd, val, name) name,
	BCH_BTREE_IDS()
#undef x
	NULL
};

void bch2_recalc_btree_reserve(struct bch_fs *c)
{
	unsigned i, reserve = 16;

	if (!c->btree_roots[0].b)
		reserve += 8;

	for (i = 0; i < BTREE_ID_NR; i++)
		if (c->btree_roots[i].b)
			reserve += min_t(unsigned, 1,
					 c->btree_roots[i].b->level) * 8;

	c->btree_cache.reserve = reserve;
}

static inline unsigned btree_cache_can_free(struct btree_cache *bc)
{
	return max_t(int, 0, bc->used - bc->reserve);
}

static void __btree_node_data_free(struct bch_fs *c, struct btree *b)
{
	EBUG_ON(btree_node_write_in_flight(b));

	kvpfree(b->data, btree_bytes(c));
	b->data = NULL;
	bch2_btree_keys_free(b);
}

static void btree_node_data_free(struct bch_fs *c, struct btree *b)
{
	struct btree_cache *bc = &c->btree_cache;

	__btree_node_data_free(c, b);
	bc->used--;
	list_move(&b->list, &bc->freed);
}

static int bch2_btree_cache_cmp_fn(struct rhashtable_compare_arg *arg,
				   const void *obj)
{
	const struct btree *b = obj;
	const u64 *v = arg->key;

	return PTR_HASH(&b->key) == *v ? 0 : 1;
}

static const struct rhashtable_params bch_btree_cache_params = {
	.head_offset	= offsetof(struct btree, hash),
	.key_offset	= offsetof(struct btree, key.v),
	.key_len	= sizeof(struct bch_extent_ptr),
	.obj_cmpfn	= bch2_btree_cache_cmp_fn,
};

static void btree_node_data_alloc(struct bch_fs *c, struct btree *b, gfp_t gfp)
{
	struct btree_cache *bc = &c->btree_cache;

	b->data = kvpmalloc(btree_bytes(c), gfp);
	if (!b->data)
		goto err;

	if (bch2_btree_keys_alloc(b, btree_page_order(c), gfp))
		goto err;

	bc->used++;
	list_move(&b->list, &bc->freeable);
	return;
err:
	kvpfree(b->data, btree_bytes(c));
	b->data = NULL;
	list_move(&b->list, &bc->freed);
}

static struct btree *btree_node_mem_alloc(struct bch_fs *c, gfp_t gfp)
{
	struct btree *b = kzalloc(sizeof(struct btree), gfp);
	if (!b)
		return NULL;

	bkey_btree_ptr_init(&b->key);
	six_lock_init(&b->lock);
	lockdep_set_novalidate_class(&b->lock);
	INIT_LIST_HEAD(&b->list);
	INIT_LIST_HEAD(&b->write_blocked);

	btree_node_data_alloc(c, b, gfp);
	return b->data ? b : NULL;
}

/* Btree in memory cache - hash table */

void bch2_btree_node_hash_remove(struct btree_cache *bc, struct btree *b)
{
	rhashtable_remove_fast(&bc->table, &b->hash, bch_btree_cache_params);

	/* Cause future lookups for this node to fail: */
	PTR_HASH(&b->key) = 0;
}

int __bch2_btree_node_hash_insert(struct btree_cache *bc, struct btree *b)
{
	return rhashtable_lookup_insert_fast(&bc->table, &b->hash,
					     bch_btree_cache_params);
}

int bch2_btree_node_hash_insert(struct btree_cache *bc, struct btree *b,
				unsigned level, enum btree_id id)
{
	int ret;

	b->level	= level;
	b->btree_id	= id;

	mutex_lock(&bc->lock);
	ret = __bch2_btree_node_hash_insert(bc, b);
	if (!ret)
		list_add(&b->list, &bc->live);
	mutex_unlock(&bc->lock);

	return ret;
}

__flatten
static inline struct btree *btree_cache_find(struct btree_cache *bc,
				     const struct bkey_i *k)
{
	return rhashtable_lookup_fast(&bc->table, &PTR_HASH(k),
				      bch_btree_cache_params);
}

/*
 * this version is for btree nodes that have already been freed (we're not
 * reaping a real btree node)
 */
static int __btree_node_reclaim(struct bch_fs *c, struct btree *b, bool flush)
{
	struct btree_cache *bc = &c->btree_cache;
	int ret = 0;

	lockdep_assert_held(&bc->lock);

	if (!six_trylock_intent(&b->lock))
		return -ENOMEM;

	if (!six_trylock_write(&b->lock))
		goto out_unlock_intent;

	if (btree_node_noevict(b))
		goto out_unlock;

	if (!btree_node_may_write(b))
		goto out_unlock;

	if (btree_node_dirty(b) &&
	    test_bit(BCH_FS_HOLD_BTREE_WRITES, &c->flags))
		goto out_unlock;

	if (btree_node_dirty(b) ||
	    btree_node_write_in_flight(b) ||
	    btree_node_read_in_flight(b)) {
		if (!flush)
			goto out_unlock;

		wait_on_bit_io(&b->flags, BTREE_NODE_read_in_flight,
			       TASK_UNINTERRUPTIBLE);

		/*
		 * Using the underscore version because we don't want to compact
		 * bsets after the write, since this node is about to be evicted
		 * - unless btree verify mode is enabled, since it runs out of
		 * the post write cleanup:
		 */
		if (verify_btree_ondisk(c))
			bch2_btree_node_write(c, b, SIX_LOCK_intent);
		else
			__bch2_btree_node_write(c, b, SIX_LOCK_read);

		/* wait for any in flight btree write */
		btree_node_wait_on_io(b);
	}
out:
	if (PTR_HASH(&b->key) && !ret)
		trace_btree_node_reap(c, b);
	return ret;
out_unlock:
	six_unlock_write(&b->lock);
out_unlock_intent:
	six_unlock_intent(&b->lock);
	ret = -ENOMEM;
	goto out;
}

static int btree_node_reclaim(struct bch_fs *c, struct btree *b)
{
	return __btree_node_reclaim(c, b, false);
}

static int btree_node_write_and_reclaim(struct bch_fs *c, struct btree *b)
{
	return __btree_node_reclaim(c, b, true);
}

static unsigned long bch2_btree_cache_scan(struct shrinker *shrink,
					   struct shrink_control *sc)
{
	struct bch_fs *c = container_of(shrink, struct bch_fs,
					btree_cache.shrink);
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b, *t;
	unsigned long nr = sc->nr_to_scan;
	unsigned long can_free;
	unsigned long touched = 0;
	unsigned long freed = 0;
	unsigned i;

	if (btree_shrinker_disabled(c))
		return SHRINK_STOP;

	/* Return -1 if we can't do anything right now */
	if (sc->gfp_mask & __GFP_IO)
		mutex_lock(&bc->lock);
	else if (!mutex_trylock(&bc->lock))
		return -1;

	/*
	 * It's _really_ critical that we don't free too many btree nodes - we
	 * have to always leave ourselves a reserve. The reserve is how we
	 * guarantee that allocating memory for a new btree node can always
	 * succeed, so that inserting keys into the btree can always succeed and
	 * IO can always make forward progress:
	 */
	nr /= btree_pages(c);
	can_free = btree_cache_can_free(bc);
	nr = min_t(unsigned long, nr, can_free);

	i = 0;
	list_for_each_entry_safe(b, t, &bc->freeable, list) {
		touched++;

		if (freed >= nr)
			break;

		if (++i > 3 &&
		    !btree_node_reclaim(c, b)) {
			btree_node_data_free(c, b);
			six_unlock_write(&b->lock);
			six_unlock_intent(&b->lock);
			freed++;
		}
	}
restart:
	list_for_each_entry_safe(b, t, &bc->live, list) {
		touched++;

		if (freed >= nr) {
			/* Save position */
			if (&t->list != &bc->live)
				list_move_tail(&bc->live, &t->list);
			break;
		}

		if (!btree_node_accessed(b) &&
		    !btree_node_reclaim(c, b)) {
			/* can't call bch2_btree_node_hash_remove under lock  */
			freed++;
			if (&t->list != &bc->live)
				list_move_tail(&bc->live, &t->list);

			btree_node_data_free(c, b);
			mutex_unlock(&bc->lock);

			bch2_btree_node_hash_remove(bc, b);
			six_unlock_write(&b->lock);
			six_unlock_intent(&b->lock);

			if (freed >= nr)
				goto out;

			if (sc->gfp_mask & __GFP_IO)
				mutex_lock(&bc->lock);
			else if (!mutex_trylock(&bc->lock))
				goto out;
			goto restart;
		} else
			clear_btree_node_accessed(b);
	}

	mutex_unlock(&bc->lock);
out:
	return (unsigned long) freed * btree_pages(c);
}

static unsigned long bch2_btree_cache_count(struct shrinker *shrink,
					    struct shrink_control *sc)
{
	struct bch_fs *c = container_of(shrink, struct bch_fs,
					btree_cache.shrink);
	struct btree_cache *bc = &c->btree_cache;

	if (btree_shrinker_disabled(c))
		return 0;

	return btree_cache_can_free(bc) * btree_pages(c);
}

void bch2_fs_btree_cache_exit(struct bch_fs *c)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;
	unsigned i;

	if (bc->shrink.list.next)
		unregister_shrinker(&bc->shrink);

	mutex_lock(&bc->lock);

#ifdef CONFIG_BCACHEFS_DEBUG
	if (c->verify_data)
		list_move(&c->verify_data->list, &bc->live);

	kvpfree(c->verify_ondisk, btree_bytes(c));
#endif

	for (i = 0; i < BTREE_ID_NR; i++)
		if (c->btree_roots[i].b)
			list_add(&c->btree_roots[i].b->list, &bc->live);

	list_splice(&bc->freeable, &bc->live);

	while (!list_empty(&bc->live)) {
		b = list_first_entry(&bc->live, struct btree, list);

		BUG_ON(btree_node_read_in_flight(b) ||
		       btree_node_write_in_flight(b));

		if (btree_node_dirty(b))
			bch2_btree_complete_write(c, b, btree_current_write(b));
		clear_btree_node_dirty(b);

		btree_node_data_free(c, b);
	}

	while (!list_empty(&bc->freed)) {
		b = list_first_entry(&bc->freed, struct btree, list);
		list_del(&b->list);
		kfree(b);
	}

	mutex_unlock(&bc->lock);

	if (bc->table_init_done)
		rhashtable_destroy(&bc->table);
}

int bch2_fs_btree_cache_init(struct bch_fs *c)
{
	struct btree_cache *bc = &c->btree_cache;
	unsigned i;
	int ret = 0;

	pr_verbose_init(c->opts, "");

	ret = rhashtable_init(&bc->table, &bch_btree_cache_params);
	if (ret)
		goto out;

	bc->table_init_done = true;

	bch2_recalc_btree_reserve(c);

	for (i = 0; i < bc->reserve; i++)
		if (!btree_node_mem_alloc(c, GFP_KERNEL)) {
			ret = -ENOMEM;
			goto out;
		}

	list_splice_init(&bc->live, &bc->freeable);

#ifdef CONFIG_BCACHEFS_DEBUG
	mutex_init(&c->verify_lock);

	c->verify_ondisk = kvpmalloc(btree_bytes(c), GFP_KERNEL);
	if (!c->verify_ondisk) {
		ret = -ENOMEM;
		goto out;
	}

	c->verify_data = btree_node_mem_alloc(c, GFP_KERNEL);
	if (!c->verify_data) {
		ret = -ENOMEM;
		goto out;
	}

	list_del_init(&c->verify_data->list);
#endif

	bc->shrink.count_objects	= bch2_btree_cache_count;
	bc->shrink.scan_objects		= bch2_btree_cache_scan;
	bc->shrink.seeks		= 4;
	bc->shrink.batch		= btree_pages(c) * 2;
	register_shrinker(&bc->shrink, "%s/btree_cache", c->name);
out:
	pr_verbose_init(c->opts, "ret %i", ret);
	return ret;
}

void bch2_fs_btree_cache_init_early(struct btree_cache *bc)
{
	mutex_init(&bc->lock);
	INIT_LIST_HEAD(&bc->live);
	INIT_LIST_HEAD(&bc->freeable);
	INIT_LIST_HEAD(&bc->freed);
}

/*
 * We can only have one thread cannibalizing other cached btree nodes at a time,
 * or we'll deadlock. We use an open coded mutex to ensure that, which a
 * cannibalize_bucket() will take. This means every time we unlock the root of
 * the btree, we need to release this lock if we have it held.
 */
void bch2_btree_cache_cannibalize_unlock(struct bch_fs *c)
{
	struct btree_cache *bc = &c->btree_cache;

	if (bc->alloc_lock == current) {
		trace_btree_node_cannibalize_unlock(c);
		bc->alloc_lock = NULL;
		closure_wake_up(&bc->alloc_wait);
	}
}

int bch2_btree_cache_cannibalize_lock(struct bch_fs *c, struct closure *cl)
{
	struct btree_cache *bc = &c->btree_cache;
	struct task_struct *old;

	old = cmpxchg(&bc->alloc_lock, NULL, current);
	if (old == NULL || old == current)
		goto success;

	if (!cl) {
		trace_btree_node_cannibalize_lock_fail(c);
		return -ENOMEM;
	}

	closure_wait(&bc->alloc_wait, cl);

	/* Try again, after adding ourselves to waitlist */
	old = cmpxchg(&bc->alloc_lock, NULL, current);
	if (old == NULL || old == current) {
		/* We raced */
		closure_wake_up(&bc->alloc_wait);
		goto success;
	}

	trace_btree_node_cannibalize_lock_fail(c);
	return -EAGAIN;

success:
	trace_btree_node_cannibalize_lock(c);
	return 0;
}

static struct btree *btree_node_cannibalize(struct bch_fs *c)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;

	list_for_each_entry_reverse(b, &bc->live, list)
		if (!btree_node_reclaim(c, b))
			return b;

	while (1) {
		list_for_each_entry_reverse(b, &bc->live, list)
			if (!btree_node_write_and_reclaim(c, b))
				return b;

		/*
		 * Rare case: all nodes were intent-locked.
		 * Just busy-wait.
		 */
		WARN_ONCE(1, "btree cache cannibalize failed\n");
		cond_resched();
	}
}

struct btree *bch2_btree_node_mem_alloc(struct bch_fs *c)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;
	u64 start_time = local_clock();

	mutex_lock(&bc->lock);

	/*
	 * btree_free() doesn't free memory; it sticks the node on the end of
	 * the list. Check if there's any freed nodes there:
	 */
	list_for_each_entry(b, &bc->freeable, list)
		if (!btree_node_reclaim(c, b))
			goto out_unlock;

	/*
	 * We never free struct btree itself, just the memory that holds the on
	 * disk node. Check the freed list before allocating a new one:
	 */
	list_for_each_entry(b, &bc->freed, list)
		if (!btree_node_reclaim(c, b)) {
			btree_node_data_alloc(c, b, __GFP_NOWARN|GFP_NOIO);
			if (b->data)
				goto out_unlock;

			six_unlock_write(&b->lock);
			six_unlock_intent(&b->lock);
			goto err;
		}

	b = btree_node_mem_alloc(c, __GFP_NOWARN|GFP_NOIO);
	if (!b)
		goto err;

	BUG_ON(!six_trylock_intent(&b->lock));
	BUG_ON(!six_trylock_write(&b->lock));
out_unlock:
	BUG_ON(btree_node_hashed(b));
	BUG_ON(btree_node_write_in_flight(b));

	list_del_init(&b->list);
	mutex_unlock(&bc->lock);
out:
	b->flags		= 0;
	b->written		= 0;
	b->nsets		= 0;
	b->sib_u64s[0]		= 0;
	b->sib_u64s[1]		= 0;
	b->whiteout_u64s	= 0;
	b->uncompacted_whiteout_u64s = 0;
	bch2_btree_keys_init(b, &c->expensive_debug_checks);

	bch2_time_stats_update(&c->times[BCH_TIME_btree_node_mem_alloc],
			       start_time);

	return b;
err:
	/* Try to cannibalize another cached btree node: */
	if (bc->alloc_lock == current) {
		b = btree_node_cannibalize(c);
		list_del_init(&b->list);
		mutex_unlock(&bc->lock);

		bch2_btree_node_hash_remove(bc, b);

		trace_btree_node_cannibalize(c);
		goto out;
	}

	mutex_unlock(&bc->lock);
	return ERR_PTR(-ENOMEM);
}

/* Slowpath, don't want it inlined into btree_iter_traverse() */
static noinline struct btree *bch2_btree_node_fill(struct bch_fs *c,
				struct btree_iter *iter,
				const struct bkey_i *k,
				unsigned level,
				enum six_lock_type lock_type,
				bool sync)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;

	/*
	 * Parent node must be locked, else we could read in a btree node that's
	 * been freed:
	 */
	BUG_ON(!btree_node_locked(iter, level + 1));
	BUG_ON(level >= BTREE_MAX_DEPTH);

	b = bch2_btree_node_mem_alloc(c);
	if (IS_ERR(b))
		return b;

	bkey_copy(&b->key, k);
	if (bch2_btree_node_hash_insert(bc, b, level, iter->btree_id)) {
		/* raced with another fill: */

		/* mark as unhashed... */
		PTR_HASH(&b->key) = 0;

		mutex_lock(&bc->lock);
		list_add(&b->list, &bc->freeable);
		mutex_unlock(&bc->lock);

		six_unlock_write(&b->lock);
		six_unlock_intent(&b->lock);
		return NULL;
	}

	/*
	 * If the btree node wasn't cached, we can't drop our lock on
	 * the parent until after it's added to the cache - because
	 * otherwise we could race with a btree_split() freeing the node
	 * we're trying to lock.
	 *
	 * But the deadlock described below doesn't exist in this case,
	 * so it's safe to not drop the parent lock until here:
	 */
	if (btree_node_read_locked(iter, level + 1))
		btree_node_unlock(iter, level + 1);

	bch2_btree_node_read(c, b, sync);

	six_unlock_write(&b->lock);

	if (!sync) {
		six_unlock_intent(&b->lock);
		return NULL;
	}

	if (lock_type == SIX_LOCK_read)
		six_lock_downgrade(&b->lock);

	return b;
}

/**
 * bch_btree_node_get - find a btree node in the cache and lock it, reading it
 * in from disk if necessary.
 *
 * If IO is necessary and running under generic_make_request, returns -EAGAIN.
 *
 * The btree node will have either a read or a write lock held, depending on
 * the @write parameter.
 */
struct btree *bch2_btree_node_get(struct bch_fs *c, struct btree_iter *iter,
				  const struct bkey_i *k, unsigned level,
				  enum six_lock_type lock_type,
				  bool may_drop_locks)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;
	struct bset_tree *t;

	/*
	 * XXX: locking optimization
	 *
	 * we can make the locking looser here - caller can drop lock on parent
	 * node before locking child node (and potentially blocking): we just
	 * have to have bch2_btree_node_fill() call relock on the parent and
	 * return -EINTR if that fails
	 */
	EBUG_ON(!btree_node_locked(iter, level + 1));
	EBUG_ON(level >= BTREE_MAX_DEPTH);
retry:
	rcu_read_lock();
	b = btree_cache_find(bc, k);
	rcu_read_unlock();

	if (unlikely(!b)) {
		/*
		 * We must have the parent locked to call bch2_btree_node_fill(),
		 * else we could read in a btree node from disk that's been
		 * freed:
		 */
		b = bch2_btree_node_fill(c, iter, k, level, lock_type, true);

		/* We raced and found the btree node in the cache */
		if (!b)
			goto retry;

		if (IS_ERR(b))
			return b;
	} else {
		/*
		 * There's a potential deadlock with splits and insertions into
		 * interior nodes we have to avoid:
		 *
		 * The other thread might be holding an intent lock on the node
		 * we want, and they want to update its parent node so they're
		 * going to upgrade their intent lock on the parent node to a
		 * write lock.
		 *
		 * But if we're holding a read lock on the parent, and we're
		 * trying to get the intent lock they're holding, we deadlock.
		 *
		 * So to avoid this we drop the read locks on parent nodes when
		 * we're starting to take intent locks - and handle the race.
		 *
		 * The race is that they might be about to free the node we
		 * want, and dropping our read lock on the parent node lets them
		 * update the parent marking the node we want as freed, and then
		 * free it:
		 *
		 * To guard against this, btree nodes are evicted from the cache
		 * when they're freed - and PTR_HASH() is zeroed out, which we
		 * check for after we lock the node.
		 *
		 * Then, bch2_btree_node_relock() on the parent will fail - because
		 * the parent was modified, when the pointer to the node we want
		 * was removed - and we'll bail out:
		 */
		if (btree_node_read_locked(iter, level + 1))
			btree_node_unlock(iter, level + 1);

		if (!btree_node_lock(b, k->k.p, level, iter,
				     lock_type, may_drop_locks))
			return ERR_PTR(-EINTR);

		if (unlikely(PTR_HASH(&b->key) != PTR_HASH(k) ||
			     b->level != level ||
			     race_fault())) {
			six_unlock_type(&b->lock, lock_type);
			if (bch2_btree_node_relock(iter, level + 1))
				goto retry;

			trans_restart();
			trace_trans_restart_btree_node_reused(c,
						iter->trans->ip);
			return ERR_PTR(-EINTR);
		}
	}

	wait_on_bit_io(&b->flags, BTREE_NODE_read_in_flight,
		       TASK_UNINTERRUPTIBLE);

	prefetch(b->aux_data);

	for_each_bset(b, t) {
		void *p = (u64 *) b->aux_data + t->aux_data_offset;

		prefetch(p + L1_CACHE_BYTES * 0);
		prefetch(p + L1_CACHE_BYTES * 1);
		prefetch(p + L1_CACHE_BYTES * 2);
	}

	/* avoid atomic set bit if it's not needed: */
	if (btree_node_accessed(b))
		set_btree_node_accessed(b);

	if (unlikely(btree_node_read_error(b))) {
		six_unlock_type(&b->lock, lock_type);
		return ERR_PTR(-EIO);
	}

	EBUG_ON(b->btree_id != iter->btree_id ||
		BTREE_NODE_LEVEL(b->data) != level ||
		bkey_cmp(b->data->max_key, k->k.p));

	return b;
}

struct btree *bch2_btree_node_get_sibling(struct bch_fs *c,
					  struct btree_iter *iter,
					  struct btree *b,
					  bool may_drop_locks,
					  enum btree_node_sibling sib)
{
	struct btree *parent;
	struct btree_node_iter node_iter;
	struct bkey_packed *k;
	BKEY_PADDED(k) tmp;
	struct btree *ret = NULL;
	unsigned level = b->level;

	parent = btree_iter_node(iter, level + 1);
	if (!parent)
		return NULL;

	if (!bch2_btree_node_relock(iter, level + 1))
		goto out_upgrade;

	node_iter = iter->l[parent->level].iter;

	k = bch2_btree_node_iter_peek_all(&node_iter, parent);
	BUG_ON(bkey_cmp_left_packed(parent, k, &b->key.k.p));

	k = sib == btree_prev_sib
		? bch2_btree_node_iter_prev(&node_iter, parent)
		: (bch2_btree_node_iter_advance(&node_iter, parent),
		   bch2_btree_node_iter_peek(&node_iter, parent));
	if (!k)
		goto out;

	bch2_bkey_unpack(parent, &tmp.k, k);

	ret = bch2_btree_node_get(c, iter, &tmp.k, level,
				  SIX_LOCK_intent, may_drop_locks);

	if (PTR_ERR_OR_ZERO(ret) == -EINTR && may_drop_locks) {
		struct btree_iter *linked;

		if (!bch2_btree_node_relock(iter, level + 1))
			goto out_upgrade;

		/*
		 * We might have got -EINTR because trylock failed, and we're
		 * holding other locks that would cause us to deadlock:
		 */
		trans_for_each_iter(iter->trans, linked)
			if (btree_iter_cmp(iter, linked) < 0)
				__bch2_btree_iter_unlock(linked);

		if (sib == btree_prev_sib)
			btree_node_unlock(iter, level);

		ret = bch2_btree_node_get(c, iter, &tmp.k, level,
					  SIX_LOCK_intent, may_drop_locks);

		/*
		 * before btree_iter_relock() calls btree_iter_verify_locks():
		 */
		if (btree_lock_want(iter, level + 1) == BTREE_NODE_UNLOCKED)
			btree_node_unlock(iter, level + 1);

		if (!bch2_btree_node_relock(iter, level)) {
			btree_iter_set_dirty(iter, BTREE_ITER_NEED_RELOCK);

			if (!IS_ERR(ret)) {
				six_unlock_intent(&ret->lock);
				ret = ERR_PTR(-EINTR);
			}
		}

		bch2_btree_trans_relock(iter->trans);
	}
out:
	if (btree_lock_want(iter, level + 1) == BTREE_NODE_UNLOCKED)
		btree_node_unlock(iter, level + 1);

	bch2_btree_trans_verify_locks(iter->trans);

	BUG_ON((!may_drop_locks || !IS_ERR(ret)) &&
	       (iter->uptodate >= BTREE_ITER_NEED_RELOCK ||
		!btree_node_locked(iter, level)));

	if (!IS_ERR_OR_NULL(ret)) {
		struct btree *n1 = ret, *n2 = b;

		if (sib != btree_prev_sib)
			swap(n1, n2);

		BUG_ON(bkey_cmp(btree_type_successor(n1->btree_id,
						     n1->key.k.p),
				n2->data->min_key));
	}

	return ret;
out_upgrade:
	if (may_drop_locks)
		bch2_btree_iter_upgrade(iter, level + 2, true);
	ret = ERR_PTR(-EINTR);
	goto out;
}

void bch2_btree_node_prefetch(struct bch_fs *c, struct btree_iter *iter,
			      const struct bkey_i *k, unsigned level)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;

	BUG_ON(!btree_node_locked(iter, level + 1));
	BUG_ON(level >= BTREE_MAX_DEPTH);

	rcu_read_lock();
	b = btree_cache_find(bc, k);
	rcu_read_unlock();

	if (b)
		return;

	bch2_btree_node_fill(c, iter, k, level, SIX_LOCK_read, false);
}

void bch2_btree_node_to_text(struct printbuf *out, struct bch_fs *c,
			     struct btree *b)
{
	const struct bkey_format *f = &b->format;
	struct bset_stats stats;

	memset(&stats, 0, sizeof(stats));

	bch2_btree_keys_stats(b, &stats);

	pr_buf(out,
	       "l %u %llu:%llu - %llu:%llu:\n"
	       "    ptrs: ",
	       b->level,
	       b->data->min_key.inode,
	       b->data->min_key.offset,
	       b->data->max_key.inode,
	       b->data->max_key.offset);
	bch2_val_to_text(out, c, bkey_i_to_s_c(&b->key));
	pr_buf(out, "\n"
	       "    format: u64s %u fields %u %u %u %u %u\n"
	       "    unpack fn len: %u\n"
	       "    bytes used %zu/%zu (%zu%% full)\n"
	       "    sib u64s: %u, %u (merge threshold %zu)\n"
	       "    nr packed keys %u\n"
	       "    nr unpacked keys %u\n"
	       "    floats %zu\n"
	       "    failed unpacked %zu\n"
	       "    failed prev %zu\n"
	       "    failed overflow %zu\n",
	       f->key_u64s,
	       f->bits_per_field[0],
	       f->bits_per_field[1],
	       f->bits_per_field[2],
	       f->bits_per_field[3],
	       f->bits_per_field[4],
	       b->unpack_fn_len,
	       b->nr.live_u64s * sizeof(u64),
	       btree_bytes(c) - sizeof(struct btree_node),
	       b->nr.live_u64s * 100 / btree_max_u64s(c),
	       b->sib_u64s[0],
	       b->sib_u64s[1],
	       BTREE_FOREGROUND_MERGE_THRESHOLD(c),
	       b->nr.packed_keys,
	       b->nr.unpacked_keys,
	       stats.floats,
	       stats.failed_unpacked,
	       stats.failed_prev,
	       stats.failed_overflow);
}
