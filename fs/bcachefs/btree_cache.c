// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_buf.h"
#include "btree_cache.h"
#include "btree_io.h"
#include "btree_iter.h"
#include "btree_locking.h"
#include "debug.h"
#include "error.h"
#include "trace.h"

#include <linux/prefetch.h>
#include <linux/sched/mm.h>

void bch2_recalc_btree_reserve(struct bch_fs *c)
{
	unsigned i, reserve = 16;

	if (!c->btree_roots[0].b)
		reserve += 8;

	for (i = 0; i < BTREE_ID_NR; i++)
		if (c->btree_roots[i].b)
			reserve += min_t(unsigned, 1,
					 c->btree_roots[i].b->c.level) * 8;

	c->btree_cache.reserve = reserve;
}

static inline unsigned btree_cache_can_free(struct btree_cache *bc)
{
	return max_t(int, 0, bc->used - bc->reserve);
}

static void btree_node_data_free(struct bch_fs *c, struct btree *b)
{
	struct btree_cache *bc = &c->btree_cache;

	EBUG_ON(btree_node_write_in_flight(b));

	kvpfree(b->data, btree_bytes(c));
	b->data = NULL;
#ifdef __KERNEL__
	kvfree(b->aux_data);
#else
	munmap(b->aux_data, btree_aux_data_bytes(b));
#endif
	b->aux_data = NULL;

	bc->used--;
	list_move(&b->list, &bc->freed);
}

static int bch2_btree_cache_cmp_fn(struct rhashtable_compare_arg *arg,
				   const void *obj)
{
	const struct btree *b = obj;
	const u64 *v = arg->key;

	return b->hash_val == *v ? 0 : 1;
}

static const struct rhashtable_params bch_btree_cache_params = {
	.head_offset	= offsetof(struct btree, hash),
	.key_offset	= offsetof(struct btree, hash_val),
	.key_len	= sizeof(u64),
	.obj_cmpfn	= bch2_btree_cache_cmp_fn,
};

static int btree_node_data_alloc(struct bch_fs *c, struct btree *b, gfp_t gfp)
{
	BUG_ON(b->data || b->aux_data);

	b->data = kvpmalloc(btree_bytes(c), gfp);
	if (!b->data)
		return -ENOMEM;
#ifdef __KERNEL__
	b->aux_data = kvmalloc(btree_aux_data_bytes(b), gfp);
#else
	b->aux_data = mmap(NULL, btree_aux_data_bytes(b),
			   PROT_READ|PROT_WRITE|PROT_EXEC,
			   MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
	if (b->aux_data == MAP_FAILED)
		b->aux_data = NULL;
#endif
	if (!b->aux_data) {
		kvpfree(b->data, btree_bytes(c));
		b->data = NULL;
		return -ENOMEM;
	}

	return 0;
}

static struct btree *__btree_node_mem_alloc(struct bch_fs *c)
{
	struct btree *b = kzalloc(sizeof(struct btree), GFP_KERNEL);
	if (!b)
		return NULL;

	bkey_btree_ptr_init(&b->key);
	six_lock_init(&b->c.lock);
	lockdep_set_novalidate_class(&b->c.lock);
	INIT_LIST_HEAD(&b->list);
	INIT_LIST_HEAD(&b->write_blocked);
	b->byte_order = ilog2(btree_bytes(c));
	return b;
}

struct btree *__bch2_btree_node_mem_alloc(struct bch_fs *c)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b = __btree_node_mem_alloc(c);
	if (!b)
		return NULL;

	if (btree_node_data_alloc(c, b, GFP_KERNEL)) {
		kfree(b);
		return NULL;
	}

	bc->used++;
	list_add(&b->list, &bc->freeable);
	return b;
}

/* Btree in memory cache - hash table */

void bch2_btree_node_hash_remove(struct btree_cache *bc, struct btree *b)
{
	rhashtable_remove_fast(&bc->table, &b->hash, bch_btree_cache_params);

	/* Cause future lookups for this node to fail: */
	b->hash_val = 0;

	six_lock_wakeup_all(&b->c.lock);
}

int __bch2_btree_node_hash_insert(struct btree_cache *bc, struct btree *b)
{
	BUG_ON(b->hash_val);
	b->hash_val = btree_ptr_hash_val(&b->key);

	return rhashtable_lookup_insert_fast(&bc->table, &b->hash,
					     bch_btree_cache_params);
}

int bch2_btree_node_hash_insert(struct btree_cache *bc, struct btree *b,
				unsigned level, enum btree_id id)
{
	int ret;

	b->c.level	= level;
	b->c.btree_id	= id;

	if (level)
		six_lock_pcpu_alloc(&b->c.lock);
	else
		six_lock_pcpu_free_rcu(&b->c.lock);

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
	u64 v = btree_ptr_hash_val(k);

	return rhashtable_lookup_fast(&bc->table, &v, bch_btree_cache_params);
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

	if (!six_trylock_intent(&b->c.lock))
		return -ENOMEM;

	if (!six_trylock_write(&b->c.lock))
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
		if (bch2_verify_btree_ondisk)
			bch2_btree_node_write(c, b, SIX_LOCK_intent);
		else
			__bch2_btree_node_write(c, b);

		/* wait for any in flight btree write */
		btree_node_wait_on_io(b);
	}
out:
	if (b->hash_val && !ret)
		trace_btree_node_reap(c, b);
	return ret;
out_unlock:
	six_unlock_write(&b->c.lock);
out_unlock_intent:
	six_unlock_intent(&b->c.lock);
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
	unsigned i, flags;

	if (bch2_btree_shrinker_disabled)
		return SHRINK_STOP;

	/* Return -1 if we can't do anything right now */
	if (sc->gfp_mask & __GFP_FS)
		mutex_lock(&bc->lock);
	else if (!mutex_trylock(&bc->lock))
		return -1;

	flags = memalloc_nofs_save();

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
		/*
		 * Leave a few nodes on the freeable list, so that a btree split
		 * won't have to hit the system allocator:
		 */
		if (++i <= 3)
			continue;

		touched++;

		if (freed >= nr)
			break;

		if (!btree_node_reclaim(c, b)) {
			btree_node_data_free(c, b);
			six_unlock_write(&b->c.lock);
			six_unlock_intent(&b->c.lock);
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
			six_unlock_write(&b->c.lock);
			six_unlock_intent(&b->c.lock);

			if (freed >= nr)
				goto out;

			if (sc->gfp_mask & __GFP_FS)
				mutex_lock(&bc->lock);
			else if (!mutex_trylock(&bc->lock))
				goto out;
			goto restart;
		} else
			clear_btree_node_accessed(b);
	}

	mutex_unlock(&bc->lock);
out:
	memalloc_nofs_restore(flags);
	return (unsigned long) freed * btree_pages(c);
}

static unsigned long bch2_btree_cache_count(struct shrinker *shrink,
					    struct shrink_control *sc)
{
	struct bch_fs *c = container_of(shrink, struct bch_fs,
					btree_cache.shrink);
	struct btree_cache *bc = &c->btree_cache;

	if (bch2_btree_shrinker_disabled)
		return 0;

	return btree_cache_can_free(bc) * btree_pages(c);
}

void bch2_fs_btree_cache_exit(struct bch_fs *c)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;
	unsigned i, flags;

	if (bc->shrink.list.next)
		unregister_shrinker(&bc->shrink);

	/* vfree() can allocate memory: */
	flags = memalloc_nofs_save();
	mutex_lock(&bc->lock);

	if (c->verify_data)
		list_move(&c->verify_data->list, &bc->live);

	kvpfree(c->verify_ondisk, btree_bytes(c));

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
		clear_btree_node_dirty(c, b);

		btree_node_data_free(c, b);
	}

	BUG_ON(atomic_read(&c->btree_cache.dirty));

	while (!list_empty(&bc->freed)) {
		b = list_first_entry(&bc->freed, struct btree, list);
		list_del(&b->list);
		six_lock_pcpu_free(&b->c.lock);
		kfree(b);
	}

	mutex_unlock(&bc->lock);
	memalloc_nofs_restore(flags);

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
		if (!__bch2_btree_node_mem_alloc(c)) {
			ret = -ENOMEM;
			goto out;
		}

	list_splice_init(&bc->live, &bc->freeable);

	mutex_init(&c->verify_lock);

	bc->shrink.count_objects	= bch2_btree_cache_count;
	bc->shrink.scan_objects		= bch2_btree_cache_scan;
	bc->shrink.seeks		= 4;
	bc->shrink.batch		= btree_pages(c) * 2;
	ret = register_shrinker(&bc->shrink, "%s/btree_cache", c->name);
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
	unsigned flags;

	flags = memalloc_nofs_save();
	mutex_lock(&bc->lock);

	/*
	 * btree_free() doesn't free memory; it sticks the node on the end of
	 * the list. Check if there's any freed nodes there:
	 */
	list_for_each_entry(b, &bc->freeable, list)
		if (!btree_node_reclaim(c, b))
			goto got_node;

	/*
	 * We never free struct btree itself, just the memory that holds the on
	 * disk node. Check the freed list before allocating a new one:
	 */
	list_for_each_entry(b, &bc->freed, list)
		if (!btree_node_reclaim(c, b))
			goto got_node;

	b = NULL;
got_node:
	if (b)
		list_del_init(&b->list);
	mutex_unlock(&bc->lock);

	if (!b) {
		b = __btree_node_mem_alloc(c);
		if (!b)
			goto err;

		BUG_ON(!six_trylock_intent(&b->c.lock));
		BUG_ON(!six_trylock_write(&b->c.lock));
	}

	if (!b->data) {
		if (btree_node_data_alloc(c, b, __GFP_NOWARN|GFP_KERNEL))
			goto err;

		mutex_lock(&bc->lock);
		bc->used++;
		mutex_unlock(&bc->lock);
	}

	BUG_ON(btree_node_hashed(b));
	BUG_ON(btree_node_write_in_flight(b));
out:
	b->flags		= 0;
	b->written		= 0;
	b->nsets		= 0;
	b->sib_u64s[0]		= 0;
	b->sib_u64s[1]		= 0;
	b->whiteout_u64s	= 0;
	bch2_btree_keys_init(b);
	set_btree_node_accessed(b);

	bch2_time_stats_update(&c->times[BCH_TIME_btree_node_mem_alloc],
			       start_time);

	memalloc_nofs_restore(flags);
	return b;
err:
	mutex_lock(&bc->lock);

	if (b) {
		list_add(&b->list, &bc->freed);
		six_unlock_write(&b->c.lock);
		six_unlock_intent(&b->c.lock);
	}

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
	memalloc_nofs_restore(flags);
	return ERR_PTR(-ENOMEM);
}

/* Slowpath, don't want it inlined into btree_iter_traverse() */
static noinline struct btree *bch2_btree_node_fill(struct bch_fs *c,
				struct btree_iter *iter,
				const struct bkey_i *k,
				enum btree_id btree_id,
				unsigned level,
				enum six_lock_type lock_type,
				bool sync)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;

	BUG_ON(level + 1 >= BTREE_MAX_DEPTH);
	/*
	 * Parent node must be locked, else we could read in a btree node that's
	 * been freed:
	 */
	if (iter && !bch2_btree_node_relock(iter, level + 1))
		return ERR_PTR(-EINTR);

	b = bch2_btree_node_mem_alloc(c);
	if (IS_ERR(b))
		return b;

	bkey_copy(&b->key, k);
	if (bch2_btree_node_hash_insert(bc, b, level, btree_id)) {
		/* raced with another fill: */

		/* mark as unhashed... */
		b->hash_val = 0;

		mutex_lock(&bc->lock);
		list_add(&b->list, &bc->freeable);
		mutex_unlock(&bc->lock);

		six_unlock_write(&b->c.lock);
		six_unlock_intent(&b->c.lock);
		return NULL;
	}

	/*
	 * Unlock before doing IO:
	 *
	 * XXX: ideally should be dropping all btree node locks here
	 */
	if (iter && btree_node_read_locked(iter, level + 1))
		btree_node_unlock(iter, level + 1);

	bch2_btree_node_read(c, b, sync);

	six_unlock_write(&b->c.lock);

	if (!sync) {
		six_unlock_intent(&b->c.lock);
		return NULL;
	}

	if (lock_type == SIX_LOCK_read)
		six_lock_downgrade(&b->c.lock);

	return b;
}

static int lock_node_check_fn(struct six_lock *lock, void *p)
{
	struct btree *b = container_of(lock, struct btree, c.lock);
	const struct bkey_i *k = p;

	return b->hash_val == btree_ptr_hash_val(k) ? 0 : -1;
}

static noinline void btree_bad_header(struct bch_fs *c, struct btree *b)
{
	char buf1[100], buf2[100], buf3[100], buf4[100];

	if (!test_bit(BCH_FS_INITIAL_GC_DONE, &c->flags))
		return;

	bch2_bpos_to_text(&PBUF(buf1), b->key.k.type == KEY_TYPE_btree_ptr_v2
		? bkey_i_to_btree_ptr_v2(&b->key)->v.min_key
		: POS_MIN);
	bch2_bpos_to_text(&PBUF(buf2), b->data->min_key);

	bch2_bpos_to_text(&PBUF(buf3), b->key.k.p);
	bch2_bpos_to_text(&PBUF(buf4), b->data->max_key);
	bch2_fs_inconsistent(c, "btree node header doesn't match ptr\n"
			     "btree: ptr %u header %llu\n"
			     "level: ptr %u header %llu\n"
			     "min ptr %s node header %s\n"
			     "max ptr %s node header %s",
			     b->c.btree_id,	BTREE_NODE_ID(b->data),
			     b->c.level,	BTREE_NODE_LEVEL(b->data),
			     buf1, buf2, buf3, buf4);
}

static inline void btree_check_header(struct bch_fs *c, struct btree *b)
{
	if (b->c.btree_id != BTREE_NODE_ID(b->data) ||
	    b->c.level != BTREE_NODE_LEVEL(b->data) ||
	    bpos_cmp(b->data->max_key, b->key.k.p) ||
	    (b->key.k.type == KEY_TYPE_btree_ptr_v2 &&
	     bpos_cmp(b->data->min_key,
		      bkey_i_to_btree_ptr_v2(&b->key)->v.min_key)))
		btree_bad_header(c, b);
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
				  unsigned long trace_ip)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;
	struct bset_tree *t;

	EBUG_ON(level >= BTREE_MAX_DEPTH);

	b = btree_node_mem_ptr(k);
	if (b)
		goto lock_node;
retry:
	b = btree_cache_find(bc, k);
	if (unlikely(!b)) {
		/*
		 * We must have the parent locked to call bch2_btree_node_fill(),
		 * else we could read in a btree node from disk that's been
		 * freed:
		 */
		b = bch2_btree_node_fill(c, iter, k, iter->btree_id,
					 level, lock_type, true);

		/* We raced and found the btree node in the cache */
		if (!b)
			goto retry;

		if (IS_ERR(b))
			return b;
	} else {
lock_node:
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
		 * when they're freed - and b->hash_val is zeroed out, which we
		 * check for after we lock the node.
		 *
		 * Then, bch2_btree_node_relock() on the parent will fail - because
		 * the parent was modified, when the pointer to the node we want
		 * was removed - and we'll bail out:
		 */
		if (btree_node_read_locked(iter, level + 1))
			btree_node_unlock(iter, level + 1);

		if (!btree_node_lock(b, k->k.p, level, iter, lock_type,
				     lock_node_check_fn, (void *) k, trace_ip)) {
			if (b->hash_val != btree_ptr_hash_val(k))
				goto retry;
			return ERR_PTR(-EINTR);
		}

		if (unlikely(b->hash_val != btree_ptr_hash_val(k) ||
			     b->c.level != level ||
			     race_fault())) {
			six_unlock_type(&b->c.lock, lock_type);
			if (bch2_btree_node_relock(iter, level + 1))
				goto retry;

			trace_trans_restart_btree_node_reused(iter->trans->ip);
			return ERR_PTR(-EINTR);
		}
	}

	/* XXX: waiting on IO with btree locks held: */
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
	if (!btree_node_accessed(b))
		set_btree_node_accessed(b);

	if (unlikely(btree_node_read_error(b))) {
		six_unlock_type(&b->c.lock, lock_type);
		return ERR_PTR(-EIO);
	}

	EBUG_ON(b->c.btree_id != iter->btree_id);
	EBUG_ON(BTREE_NODE_LEVEL(b->data) != level);
	btree_check_header(c, b);

	return b;
}

struct btree *bch2_btree_node_get_noiter(struct bch_fs *c,
					 const struct bkey_i *k,
					 enum btree_id btree_id,
					 unsigned level,
					 bool nofill)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;
	struct bset_tree *t;
	int ret;

	EBUG_ON(level >= BTREE_MAX_DEPTH);

	b = btree_node_mem_ptr(k);
	if (b)
		goto lock_node;
retry:
	b = btree_cache_find(bc, k);
	if (unlikely(!b)) {
		if (nofill)
			goto out;

		b = bch2_btree_node_fill(c, NULL, k, btree_id,
					 level, SIX_LOCK_read, true);

		/* We raced and found the btree node in the cache */
		if (!b)
			goto retry;

		if (IS_ERR(b) &&
		    !bch2_btree_cache_cannibalize_lock(c, NULL))
			goto retry;

		if (IS_ERR(b))
			goto out;
	} else {
lock_node:
		ret = six_lock_read(&b->c.lock, lock_node_check_fn, (void *) k);
		if (ret)
			goto retry;

		if (unlikely(b->hash_val != btree_ptr_hash_val(k) ||
			     b->c.btree_id != btree_id ||
			     b->c.level != level)) {
			six_unlock_read(&b->c.lock);
			goto retry;
		}
	}

	/* XXX: waiting on IO with btree locks held: */
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
	if (!btree_node_accessed(b))
		set_btree_node_accessed(b);

	if (unlikely(btree_node_read_error(b))) {
		six_unlock_read(&b->c.lock);
		b = ERR_PTR(-EIO);
		goto out;
	}

	EBUG_ON(b->c.btree_id != btree_id);
	EBUG_ON(BTREE_NODE_LEVEL(b->data) != level);
	btree_check_header(c, b);
out:
	bch2_btree_cache_cannibalize_unlock(c);
	return b;
}

void bch2_btree_node_prefetch(struct bch_fs *c, struct btree_iter *iter,
			      const struct bkey_i *k,
			      enum btree_id btree_id, unsigned level)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;

	BUG_ON(iter && !btree_node_locked(iter, level + 1));
	BUG_ON(level >= BTREE_MAX_DEPTH);

	b = btree_cache_find(bc, k);
	if (b)
		return;

	bch2_btree_node_fill(c, iter, k, btree_id, level, SIX_LOCK_read, false);
}

void bch2_btree_node_evict(struct bch_fs *c, const struct bkey_i *k)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;

	b = btree_cache_find(bc, k);
	if (!b)
		return;

	six_lock_intent(&b->c.lock, NULL, NULL);
	six_lock_write(&b->c.lock, NULL, NULL);

	wait_on_bit_io(&b->flags, BTREE_NODE_read_in_flight,
		       TASK_UNINTERRUPTIBLE);
	__bch2_btree_node_write(c, b);

	/* wait for any in flight btree write */
	btree_node_wait_on_io(b);

	BUG_ON(btree_node_dirty(b));

	mutex_lock(&bc->lock);
	btree_node_data_free(c, b);
	bch2_btree_node_hash_remove(bc, b);
	mutex_unlock(&bc->lock);

	six_unlock_write(&b->c.lock);
	six_unlock_intent(&b->c.lock);
}

void bch2_btree_node_to_text(struct printbuf *out, struct bch_fs *c,
			     struct btree *b)
{
	const struct bkey_format *f = &b->format;
	struct bset_stats stats;

	memset(&stats, 0, sizeof(stats));

	bch2_btree_keys_stats(b, &stats);

	pr_buf(out, "l %u ", b->c.level);
	bch2_bpos_to_text(out, b->data->min_key);
	pr_buf(out, " - ");
	bch2_bpos_to_text(out, b->data->max_key);
	pr_buf(out, ":\n"
	       "    ptrs: ");
	bch2_val_to_text(out, c, bkey_i_to_s_c(&b->key));

	pr_buf(out, "\n"
	       "    format: u64s %u fields %u %u %u %u %u\n"
	       "    unpack fn len: %u\n"
	       "    bytes used %zu/%zu (%zu%% full)\n"
	       "    sib u64s: %u, %u (merge threshold %u)\n"
	       "    nr packed keys %u\n"
	       "    nr unpacked keys %u\n"
	       "    floats %zu\n"
	       "    failed unpacked %zu\n",
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
	       c->btree_foreground_merge_threshold,
	       b->nr.packed_keys,
	       b->nr.unpacked_keys,
	       stats.floats,
	       stats.failed);
}

void bch2_btree_cache_to_text(struct printbuf *out, struct bch_fs *c)
{
	pr_buf(out, "nr nodes:\t\t%u\n", c->btree_cache.used);
	pr_buf(out, "nr dirty:\t\t%u\n", atomic_read(&c->btree_cache.dirty));
	pr_buf(out, "cannibalize lock:\t%p\n", c->btree_cache.alloc_lock);
}
