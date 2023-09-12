// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_buf.h"
#include "btree_cache.h"
#include "btree_io.h"
#include "btree_iter.h"
#include "btree_locking.h"
#include "debug.h"
#include "errcode.h"
#include "error.h"
#include "trace.h"

#include <linux/prefetch.h>
#include <linux/sched/mm.h>

const char * const bch2_btree_node_flags[] = {
#define x(f)	#f,
	BTREE_FLAGS()
#undef x
	NULL
};

void bch2_recalc_btree_reserve(struct bch_fs *c)
{
	unsigned i, reserve = 16;

	if (!c->btree_roots_known[0].b)
		reserve += 8;

	for (i = 0; i < btree_id_nr_alive(c); i++) {
		struct btree_root *r = bch2_btree_id_root(c, i);

		if (r->b)
			reserve += min_t(unsigned, 1, r->b->c.level) * 8;
	}

	c->btree_cache.reserve = reserve;
}

static inline unsigned btree_cache_can_free(struct btree_cache *bc)
{
	return max_t(int, 0, bc->used - bc->reserve);
}

static void btree_node_to_freedlist(struct btree_cache *bc, struct btree *b)
{
	if (b->c.lock.readers)
		list_move(&b->list, &bc->freed_pcpu);
	else
		list_move(&b->list, &bc->freed_nonpcpu);
}

static void btree_node_data_free(struct bch_fs *c, struct btree *b)
{
	struct btree_cache *bc = &c->btree_cache;

	EBUG_ON(btree_node_write_in_flight(b));

	clear_btree_node_just_written(b);

	kvpfree(b->data, btree_bytes(c));
	b->data = NULL;
#ifdef __KERNEL__
	kvfree(b->aux_data);
#else
	munmap(b->aux_data, btree_aux_data_bytes(b));
#endif
	b->aux_data = NULL;

	bc->used--;

	btree_node_to_freedlist(bc, b);
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
		return -BCH_ERR_ENOMEM_btree_node_mem_alloc;
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
		return -BCH_ERR_ENOMEM_btree_node_mem_alloc;
	}

	return 0;
}

static struct btree *__btree_node_mem_alloc(struct bch_fs *c, gfp_t gfp)
{
	struct btree *b;

	b = kzalloc(sizeof(struct btree), gfp);
	if (!b)
		return NULL;

	bkey_btree_ptr_init(&b->key);
	INIT_LIST_HEAD(&b->list);
	INIT_LIST_HEAD(&b->write_blocked);
	b->byte_order = ilog2(btree_bytes(c));
	return b;
}

struct btree *__bch2_btree_node_mem_alloc(struct bch_fs *c)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;

	b = __btree_node_mem_alloc(c, GFP_KERNEL);
	if (!b)
		return NULL;

	if (btree_node_data_alloc(c, b, GFP_KERNEL)) {
		kfree(b);
		return NULL;
	}

	bch2_btree_lock_init(&b->c, 0);

	bc->used++;
	list_add(&b->list, &bc->freeable);
	return b;
}

/* Btree in memory cache - hash table */

void bch2_btree_node_hash_remove(struct btree_cache *bc, struct btree *b)
{
	int ret = rhashtable_remove_fast(&bc->table, &b->hash, bch_btree_cache_params);

	BUG_ON(ret);

	/* Cause future lookups for this node to fail: */
	b->hash_val = 0;
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

	mutex_lock(&bc->lock);
	ret = __bch2_btree_node_hash_insert(bc, b);
	if (!ret)
		list_add_tail(&b->list, &bc->live);
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
wait_on_io:
	if (b->flags & ((1U << BTREE_NODE_dirty)|
			(1U << BTREE_NODE_read_in_flight)|
			(1U << BTREE_NODE_write_in_flight))) {
		if (!flush)
			return -BCH_ERR_ENOMEM_btree_node_reclaim;

		/* XXX: waiting on IO with btree cache lock held */
		bch2_btree_node_wait_on_read(b);
		bch2_btree_node_wait_on_write(b);
	}

	if (!six_trylock_intent(&b->c.lock))
		return -BCH_ERR_ENOMEM_btree_node_reclaim;

	if (!six_trylock_write(&b->c.lock))
		goto out_unlock_intent;

	/* recheck under lock */
	if (b->flags & ((1U << BTREE_NODE_read_in_flight)|
			(1U << BTREE_NODE_write_in_flight))) {
		if (!flush)
			goto out_unlock;
		six_unlock_write(&b->c.lock);
		six_unlock_intent(&b->c.lock);
		goto wait_on_io;
	}

	if (btree_node_noevict(b) ||
	    btree_node_write_blocked(b) ||
	    btree_node_will_make_reachable(b))
		goto out_unlock;

	if (btree_node_dirty(b)) {
		if (!flush)
			goto out_unlock;
		/*
		 * Using the underscore version because we don't want to compact
		 * bsets after the write, since this node is about to be evicted
		 * - unless btree verify mode is enabled, since it runs out of
		 * the post write cleanup:
		 */
		if (bch2_verify_btree_ondisk)
			bch2_btree_node_write(c, b, SIX_LOCK_intent,
					      BTREE_WRITE_cache_reclaim);
		else
			__bch2_btree_node_write(c, b,
						BTREE_WRITE_cache_reclaim);

		six_unlock_write(&b->c.lock);
		six_unlock_intent(&b->c.lock);
		goto wait_on_io;
	}
out:
	if (b->hash_val && !ret)
		trace_and_count(c, btree_cache_reap, c, b);
	return ret;
out_unlock:
	six_unlock_write(&b->c.lock);
out_unlock_intent:
	six_unlock_intent(&b->c.lock);
	ret = -BCH_ERR_ENOMEM_btree_node_reclaim;
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
	unsigned long can_free = 0;
	unsigned long freed = 0;
	unsigned long touched = 0;
	unsigned i, flags;
	unsigned long ret = SHRINK_STOP;
	bool trigger_writes = atomic_read(&bc->dirty) + nr >=
		bc->used * 3 / 4;

	if (bch2_btree_shrinker_disabled)
		return SHRINK_STOP;

	mutex_lock(&bc->lock);
	flags = memalloc_nofs_save();

	/*
	 * It's _really_ critical that we don't free too many btree nodes - we
	 * have to always leave ourselves a reserve. The reserve is how we
	 * guarantee that allocating memory for a new btree node can always
	 * succeed, so that inserting keys into the btree can always succeed and
	 * IO can always make forward progress:
	 */
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

		if (touched >= nr)
			goto out;

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

		if (btree_node_accessed(b)) {
			clear_btree_node_accessed(b);
		} else if (!btree_node_reclaim(c, b)) {
			freed++;
			btree_node_data_free(c, b);

			bch2_btree_node_hash_remove(bc, b);
			six_unlock_write(&b->c.lock);
			six_unlock_intent(&b->c.lock);

			if (freed == nr)
				goto out_rotate;
		} else if (trigger_writes &&
			   btree_node_dirty(b) &&
			   !btree_node_will_make_reachable(b) &&
			   !btree_node_write_blocked(b) &&
			   six_trylock_read(&b->c.lock)) {
			list_move(&bc->live, &b->list);
			mutex_unlock(&bc->lock);
			__bch2_btree_node_write(c, b, BTREE_WRITE_cache_reclaim);
			six_unlock_read(&b->c.lock);
			if (touched >= nr)
				goto out_nounlock;
			mutex_lock(&bc->lock);
			goto restart;
		}

		if (touched >= nr)
			break;
	}
out_rotate:
	if (&t->list != &bc->live)
		list_move_tail(&bc->live, &t->list);
out:
	mutex_unlock(&bc->lock);
out_nounlock:
	ret = freed;
	memalloc_nofs_restore(flags);
	trace_and_count(c, btree_cache_scan, sc->nr_to_scan, can_free, ret);
	return ret;
}

static unsigned long bch2_btree_cache_count(struct shrinker *shrink,
					    struct shrink_control *sc)
{
	struct bch_fs *c = container_of(shrink, struct bch_fs,
					btree_cache.shrink);
	struct btree_cache *bc = &c->btree_cache;

	if (bch2_btree_shrinker_disabled)
		return 0;

	return btree_cache_can_free(bc);
}

void bch2_fs_btree_cache_exit(struct bch_fs *c)
{
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;
	unsigned i, flags;

	unregister_shrinker(&bc->shrink);

	/* vfree() can allocate memory: */
	flags = memalloc_nofs_save();
	mutex_lock(&bc->lock);

	if (c->verify_data)
		list_move(&c->verify_data->list, &bc->live);

	kvpfree(c->verify_ondisk, btree_bytes(c));

	for (i = 0; i < btree_id_nr_alive(c); i++) {
		struct btree_root *r = bch2_btree_id_root(c, i);

		if (r->b)
			list_add(&r->b->list, &bc->live);
	}

	list_splice(&bc->freeable, &bc->live);

	while (!list_empty(&bc->live)) {
		b = list_first_entry(&bc->live, struct btree, list);

		BUG_ON(btree_node_read_in_flight(b) ||
		       btree_node_write_in_flight(b));

		if (btree_node_dirty(b))
			bch2_btree_complete_write(c, b, btree_current_write(b));
		clear_btree_node_dirty_acct(c, b);

		btree_node_data_free(c, b);
	}

	BUG_ON(atomic_read(&c->btree_cache.dirty));

	list_splice(&bc->freed_pcpu, &bc->freed_nonpcpu);

	while (!list_empty(&bc->freed_nonpcpu)) {
		b = list_first_entry(&bc->freed_nonpcpu, struct btree, list);
		list_del(&b->list);
		six_lock_exit(&b->c.lock);
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

	ret = rhashtable_init(&bc->table, &bch_btree_cache_params);
	if (ret)
		goto err;

	bc->table_init_done = true;

	bch2_recalc_btree_reserve(c);

	for (i = 0; i < bc->reserve; i++)
		if (!__bch2_btree_node_mem_alloc(c))
			goto err;

	list_splice_init(&bc->live, &bc->freeable);

	mutex_init(&c->verify_lock);

	bc->shrink.count_objects	= bch2_btree_cache_count;
	bc->shrink.scan_objects		= bch2_btree_cache_scan;
	bc->shrink.seeks		= 4;
	ret = register_shrinker(&bc->shrink, "%s/btree_cache", c->name);
	if (ret)
		goto err;

	return 0;
err:
	return -BCH_ERR_ENOMEM_fs_btree_cache_init;
}

void bch2_fs_btree_cache_init_early(struct btree_cache *bc)
{
	mutex_init(&bc->lock);
	INIT_LIST_HEAD(&bc->live);
	INIT_LIST_HEAD(&bc->freeable);
	INIT_LIST_HEAD(&bc->freed_pcpu);
	INIT_LIST_HEAD(&bc->freed_nonpcpu);
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
		trace_and_count(c, btree_cache_cannibalize_unlock, c);
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
		trace_and_count(c, btree_cache_cannibalize_lock_fail, c);
		return -BCH_ERR_ENOMEM_btree_cache_cannibalize_lock;
	}

	closure_wait(&bc->alloc_wait, cl);

	/* Try again, after adding ourselves to waitlist */
	old = cmpxchg(&bc->alloc_lock, NULL, current);
	if (old == NULL || old == current) {
		/* We raced */
		closure_wake_up(&bc->alloc_wait);
		goto success;
	}

	trace_and_count(c, btree_cache_cannibalize_lock_fail, c);
	return -BCH_ERR_btree_cache_cannibalize_lock_blocked;

success:
	trace_and_count(c, btree_cache_cannibalize_lock, c);
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

struct btree *bch2_btree_node_mem_alloc(struct btree_trans *trans, bool pcpu_read_locks)
{
	struct bch_fs *c = trans->c;
	struct btree_cache *bc = &c->btree_cache;
	struct list_head *freed = pcpu_read_locks
		? &bc->freed_pcpu
		: &bc->freed_nonpcpu;
	struct btree *b, *b2;
	u64 start_time = local_clock();
	unsigned flags;

	flags = memalloc_nofs_save();
	mutex_lock(&bc->lock);

	/*
	 * We never free struct btree itself, just the memory that holds the on
	 * disk node. Check the freed list before allocating a new one:
	 */
	list_for_each_entry(b, freed, list)
		if (!btree_node_reclaim(c, b)) {
			list_del_init(&b->list);
			goto got_node;
		}

	b = __btree_node_mem_alloc(c, GFP_NOWAIT|__GFP_NOWARN);
	if (!b) {
		mutex_unlock(&bc->lock);
		bch2_trans_unlock(trans);
		b = __btree_node_mem_alloc(c, GFP_KERNEL);
		if (!b)
			goto err;
		mutex_lock(&bc->lock);
	}

	bch2_btree_lock_init(&b->c, pcpu_read_locks ? SIX_LOCK_INIT_PCPU : 0);

	BUG_ON(!six_trylock_intent(&b->c.lock));
	BUG_ON(!six_trylock_write(&b->c.lock));
got_node:

	/*
	 * btree_free() doesn't free memory; it sticks the node on the end of
	 * the list. Check if there's any freed nodes there:
	 */
	list_for_each_entry(b2, &bc->freeable, list)
		if (!btree_node_reclaim(c, b2)) {
			swap(b->data, b2->data);
			swap(b->aux_data, b2->aux_data);
			btree_node_to_freedlist(bc, b2);
			six_unlock_write(&b2->c.lock);
			six_unlock_intent(&b2->c.lock);
			goto got_mem;
		}

	mutex_unlock(&bc->lock);

	if (btree_node_data_alloc(c, b, GFP_NOWAIT|__GFP_NOWARN)) {
		bch2_trans_unlock(trans);
		if (btree_node_data_alloc(c, b, GFP_KERNEL|__GFP_NOWARN))
			goto err;
	}

	mutex_lock(&bc->lock);
	bc->used++;
got_mem:
	mutex_unlock(&bc->lock);

	BUG_ON(btree_node_hashed(b));
	BUG_ON(btree_node_dirty(b));
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

	/* Try to cannibalize another cached btree node: */
	if (bc->alloc_lock == current) {
		b2 = btree_node_cannibalize(c);
		clear_btree_node_just_written(b2);
		bch2_btree_node_hash_remove(bc, b2);

		if (b) {
			swap(b->data, b2->data);
			swap(b->aux_data, b2->aux_data);
			btree_node_to_freedlist(bc, b2);
			six_unlock_write(&b2->c.lock);
			six_unlock_intent(&b2->c.lock);
		} else {
			b = b2;
			list_del_init(&b->list);
		}

		mutex_unlock(&bc->lock);

		trace_and_count(c, btree_cache_cannibalize, c);
		goto out;
	}

	mutex_unlock(&bc->lock);
	memalloc_nofs_restore(flags);
	return ERR_PTR(-BCH_ERR_ENOMEM_btree_node_mem_alloc);
}

/* Slowpath, don't want it inlined into btree_iter_traverse() */
static noinline struct btree *bch2_btree_node_fill(struct btree_trans *trans,
				struct btree_path *path,
				const struct bkey_i *k,
				enum btree_id btree_id,
				unsigned level,
				enum six_lock_type lock_type,
				bool sync)
{
	struct bch_fs *c = trans->c;
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;
	u32 seq;

	BUG_ON(level + 1 >= BTREE_MAX_DEPTH);
	/*
	 * Parent node must be locked, else we could read in a btree node that's
	 * been freed:
	 */
	if (path && !bch2_btree_node_relock(trans, path, level + 1)) {
		trace_and_count(c, trans_restart_relock_parent_for_fill, trans, _THIS_IP_, path);
		return ERR_PTR(btree_trans_restart(trans, BCH_ERR_transaction_restart_fill_relock));
	}

	b = bch2_btree_node_mem_alloc(trans, level != 0);

	if (bch2_err_matches(PTR_ERR_OR_ZERO(b), ENOMEM)) {
		trans->memory_allocation_failure = true;
		trace_and_count(c, trans_restart_memory_allocation_failure, trans, _THIS_IP_, path);
		return ERR_PTR(btree_trans_restart(trans, BCH_ERR_transaction_restart_fill_mem_alloc_fail));
	}

	if (IS_ERR(b))
		return b;

	/*
	 * Btree nodes read in from disk should not have the accessed bit set
	 * initially, so that linear scans don't thrash the cache:
	 */
	clear_btree_node_accessed(b);

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

	set_btree_node_read_in_flight(b);

	six_unlock_write(&b->c.lock);
	seq = six_lock_seq(&b->c.lock);
	six_unlock_intent(&b->c.lock);

	/* Unlock before doing IO: */
	if (trans && sync)
		bch2_trans_unlock_noassert(trans);

	bch2_btree_node_read(c, b, sync);

	if (!sync)
		return NULL;

	if (path) {
		int ret = bch2_trans_relock(trans) ?:
			bch2_btree_path_relock_intent(trans, path);
		if (ret) {
			BUG_ON(!trans->restarted);
			return ERR_PTR(ret);
		}
	}

	if (!six_relock_type(&b->c.lock, lock_type, seq)) {
		if (path)
			trace_and_count(c, trans_restart_relock_after_fill, trans, _THIS_IP_, path);
		return ERR_PTR(btree_trans_restart(trans, BCH_ERR_transaction_restart_relock_after_fill));
	}

	return b;
}

static noinline void btree_bad_header(struct bch_fs *c, struct btree *b)
{
	struct printbuf buf = PRINTBUF;

	if (c->curr_recovery_pass <= BCH_RECOVERY_PASS_check_allocations)
		return;

	prt_printf(&buf,
	       "btree node header doesn't match ptr\n"
	       "btree %s level %u\n"
	       "ptr: ",
	       bch2_btree_ids[b->c.btree_id], b->c.level);
	bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&b->key));

	prt_printf(&buf, "\nheader: btree %s level %llu\n"
	       "min ",
	       bch2_btree_ids[BTREE_NODE_ID(b->data)],
	       BTREE_NODE_LEVEL(b->data));
	bch2_bpos_to_text(&buf, b->data->min_key);

	prt_printf(&buf, "\nmax ");
	bch2_bpos_to_text(&buf, b->data->max_key);

	bch2_fs_inconsistent(c, "%s", buf.buf);
	printbuf_exit(&buf);
}

static inline void btree_check_header(struct bch_fs *c, struct btree *b)
{
	if (b->c.btree_id != BTREE_NODE_ID(b->data) ||
	    b->c.level != BTREE_NODE_LEVEL(b->data) ||
	    !bpos_eq(b->data->max_key, b->key.k.p) ||
	    (b->key.k.type == KEY_TYPE_btree_ptr_v2 &&
	     !bpos_eq(b->data->min_key,
		      bkey_i_to_btree_ptr_v2(&b->key)->v.min_key)))
		btree_bad_header(c, b);
}

static struct btree *__bch2_btree_node_get(struct btree_trans *trans, struct btree_path *path,
					   const struct bkey_i *k, unsigned level,
					   enum six_lock_type lock_type,
					   unsigned long trace_ip)
{
	struct bch_fs *c = trans->c;
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;
	struct bset_tree *t;
	bool need_relock = false;
	int ret;

	EBUG_ON(level >= BTREE_MAX_DEPTH);
retry:
	b = btree_cache_find(bc, k);
	if (unlikely(!b)) {
		/*
		 * We must have the parent locked to call bch2_btree_node_fill(),
		 * else we could read in a btree node from disk that's been
		 * freed:
		 */
		b = bch2_btree_node_fill(trans, path, k, path->btree_id,
					 level, lock_type, true);
		need_relock = true;

		/* We raced and found the btree node in the cache */
		if (!b)
			goto retry;

		if (IS_ERR(b))
			return b;
	} else {
		if (btree_node_read_locked(path, level + 1))
			btree_node_unlock(trans, path, level + 1);

		ret = btree_node_lock(trans, path, &b->c, level, lock_type, trace_ip);
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			return ERR_PTR(ret);

		BUG_ON(ret);

		if (unlikely(b->hash_val != btree_ptr_hash_val(k) ||
			     b->c.level != level ||
			     race_fault())) {
			six_unlock_type(&b->c.lock, lock_type);
			if (bch2_btree_node_relock(trans, path, level + 1))
				goto retry;

			trace_and_count(c, trans_restart_btree_node_reused, trans, trace_ip, path);
			return ERR_PTR(btree_trans_restart(trans, BCH_ERR_transaction_restart_lock_node_reused));
		}

		/* avoid atomic set bit if it's not needed: */
		if (!btree_node_accessed(b))
			set_btree_node_accessed(b);
	}

	if (unlikely(btree_node_read_in_flight(b))) {
		u32 seq = six_lock_seq(&b->c.lock);

		six_unlock_type(&b->c.lock, lock_type);
		bch2_trans_unlock(trans);
		need_relock = true;

		bch2_btree_node_wait_on_read(b);

		/*
		 * should_be_locked is not set on this path yet, so we need to
		 * relock it specifically:
		 */
		if (!six_relock_type(&b->c.lock, lock_type, seq))
			goto retry;
	}

	if (unlikely(need_relock)) {
		ret = bch2_trans_relock(trans) ?:
			bch2_btree_path_relock_intent(trans, path);
		if (ret) {
			six_unlock_type(&b->c.lock, lock_type);
			return ERR_PTR(ret);
		}
	}

	prefetch(b->aux_data);

	for_each_bset(b, t) {
		void *p = (u64 *) b->aux_data + t->aux_data_offset;

		prefetch(p + L1_CACHE_BYTES * 0);
		prefetch(p + L1_CACHE_BYTES * 1);
		prefetch(p + L1_CACHE_BYTES * 2);
	}

	if (unlikely(btree_node_read_error(b))) {
		six_unlock_type(&b->c.lock, lock_type);
		return ERR_PTR(-EIO);
	}

	EBUG_ON(b->c.btree_id != path->btree_id);
	EBUG_ON(BTREE_NODE_LEVEL(b->data) != level);
	btree_check_header(c, b);

	return b;
}

/**
 * bch2_btree_node_get - find a btree node in the cache and lock it, reading it
 * in from disk if necessary.
 *
 * @trans:	btree transaction object
 * @path:	btree_path being traversed
 * @k:		pointer to btree node (generally KEY_TYPE_btree_ptr_v2)
 * @level:	level of btree node being looked up (0 == leaf node)
 * @lock_type:	SIX_LOCK_read or SIX_LOCK_intent
 * @trace_ip:	ip of caller of btree iterator code (i.e. caller of bch2_btree_iter_peek())
 *
 * The btree node will have either a read or a write lock held, depending on
 * the @write parameter.
 *
 * Returns: btree node or ERR_PTR()
 */
struct btree *bch2_btree_node_get(struct btree_trans *trans, struct btree_path *path,
				  const struct bkey_i *k, unsigned level,
				  enum six_lock_type lock_type,
				  unsigned long trace_ip)
{
	struct bch_fs *c = trans->c;
	struct btree *b;
	struct bset_tree *t;
	int ret;

	EBUG_ON(level >= BTREE_MAX_DEPTH);

	b = btree_node_mem_ptr(k);

	/*
	 * Check b->hash_val _before_ calling btree_node_lock() - this might not
	 * be the node we want anymore, and trying to lock the wrong node could
	 * cause an unneccessary transaction restart:
	 */
	if (unlikely(!c->opts.btree_node_mem_ptr_optimization ||
		     !b ||
		     b->hash_val != btree_ptr_hash_val(k)))
		return __bch2_btree_node_get(trans, path, k, level, lock_type, trace_ip);

	if (btree_node_read_locked(path, level + 1))
		btree_node_unlock(trans, path, level + 1);

	ret = btree_node_lock(trans, path, &b->c, level, lock_type, trace_ip);
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		return ERR_PTR(ret);

	BUG_ON(ret);

	if (unlikely(b->hash_val != btree_ptr_hash_val(k) ||
		     b->c.level != level ||
		     race_fault())) {
		six_unlock_type(&b->c.lock, lock_type);
		if (bch2_btree_node_relock(trans, path, level + 1))
			return __bch2_btree_node_get(trans, path, k, level, lock_type, trace_ip);

		trace_and_count(c, trans_restart_btree_node_reused, trans, trace_ip, path);
		return ERR_PTR(btree_trans_restart(trans, BCH_ERR_transaction_restart_lock_node_reused));
	}

	if (unlikely(btree_node_read_in_flight(b))) {
		u32 seq = six_lock_seq(&b->c.lock);

		six_unlock_type(&b->c.lock, lock_type);
		bch2_trans_unlock(trans);

		bch2_btree_node_wait_on_read(b);

		/*
		 * should_be_locked is not set on this path yet, so we need to
		 * relock it specifically:
		 */
		if (trans) {
			ret = bch2_trans_relock(trans) ?:
				bch2_btree_path_relock_intent(trans, path);
			if (ret) {
				BUG_ON(!trans->restarted);
				return ERR_PTR(ret);
			}
		}

		if (!six_relock_type(&b->c.lock, lock_type, seq))
			return __bch2_btree_node_get(trans, path, k, level, lock_type, trace_ip);
	}

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

	EBUG_ON(b->c.btree_id != path->btree_id);
	EBUG_ON(BTREE_NODE_LEVEL(b->data) != level);
	btree_check_header(c, b);

	return b;
}

struct btree *bch2_btree_node_get_noiter(struct btree_trans *trans,
					 const struct bkey_i *k,
					 enum btree_id btree_id,
					 unsigned level,
					 bool nofill)
{
	struct bch_fs *c = trans->c;
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;
	struct bset_tree *t;
	int ret;

	EBUG_ON(level >= BTREE_MAX_DEPTH);

	if (c->opts.btree_node_mem_ptr_optimization) {
		b = btree_node_mem_ptr(k);
		if (b)
			goto lock_node;
	}
retry:
	b = btree_cache_find(bc, k);
	if (unlikely(!b)) {
		if (nofill)
			goto out;

		b = bch2_btree_node_fill(trans, NULL, k, btree_id,
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
		ret = btree_node_lock_nopath(trans, &b->c, SIX_LOCK_read, _THIS_IP_);
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			return ERR_PTR(ret);

		BUG_ON(ret);

		if (unlikely(b->hash_val != btree_ptr_hash_val(k) ||
			     b->c.btree_id != btree_id ||
			     b->c.level != level)) {
			six_unlock_read(&b->c.lock);
			goto retry;
		}
	}

	/* XXX: waiting on IO with btree locks held: */
	__bch2_btree_node_wait_on_read(b);

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

int bch2_btree_node_prefetch(struct btree_trans *trans,
			     struct btree_path *path,
			     const struct bkey_i *k,
			     enum btree_id btree_id, unsigned level)
{
	struct bch_fs *c = trans->c;
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;

	BUG_ON(trans && !btree_node_locked(path, level + 1));
	BUG_ON(level >= BTREE_MAX_DEPTH);

	b = btree_cache_find(bc, k);
	if (b)
		return 0;

	b = bch2_btree_node_fill(trans, path, k, btree_id,
				 level, SIX_LOCK_read, false);
	return PTR_ERR_OR_ZERO(b);
}

void bch2_btree_node_evict(struct btree_trans *trans, const struct bkey_i *k)
{
	struct bch_fs *c = trans->c;
	struct btree_cache *bc = &c->btree_cache;
	struct btree *b;

	b = btree_cache_find(bc, k);
	if (!b)
		return;
wait_on_io:
	/* not allowed to wait on io with btree locks held: */

	/* XXX we're called from btree_gc which will be holding other btree
	 * nodes locked
	 */
	__bch2_btree_node_wait_on_read(b);
	__bch2_btree_node_wait_on_write(b);

	btree_node_lock_nopath_nofail(trans, &b->c, SIX_LOCK_intent);
	btree_node_lock_nopath_nofail(trans, &b->c, SIX_LOCK_write);

	if (btree_node_dirty(b)) {
		__bch2_btree_node_write(c, b, BTREE_WRITE_cache_reclaim);
		six_unlock_write(&b->c.lock);
		six_unlock_intent(&b->c.lock);
		goto wait_on_io;
	}

	BUG_ON(btree_node_dirty(b));

	mutex_lock(&bc->lock);
	btree_node_data_free(c, b);
	bch2_btree_node_hash_remove(bc, b);
	mutex_unlock(&bc->lock);

	six_unlock_write(&b->c.lock);
	six_unlock_intent(&b->c.lock);
}

void bch2_btree_node_to_text(struct printbuf *out, struct bch_fs *c,
			     const struct btree *b)
{
	struct bset_stats stats;

	memset(&stats, 0, sizeof(stats));

	bch2_btree_keys_stats(b, &stats);

	prt_printf(out, "l %u ", b->c.level);
	bch2_bpos_to_text(out, b->data->min_key);
	prt_printf(out, " - ");
	bch2_bpos_to_text(out, b->data->max_key);
	prt_printf(out, ":\n"
	       "    ptrs: ");
	bch2_val_to_text(out, c, bkey_i_to_s_c(&b->key));
	prt_newline(out);

	prt_printf(out,
	       "    format: ");
	bch2_bkey_format_to_text(out, &b->format);

	prt_printf(out,
	       "    unpack fn len: %u\n"
	       "    bytes used %zu/%zu (%zu%% full)\n"
	       "    sib u64s: %u, %u (merge threshold %u)\n"
	       "    nr packed keys %u\n"
	       "    nr unpacked keys %u\n"
	       "    floats %zu\n"
	       "    failed unpacked %zu\n",
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

void bch2_btree_cache_to_text(struct printbuf *out, const struct bch_fs *c)
{
	prt_printf(out, "nr nodes:\t\t%u\n", c->btree_cache.used);
	prt_printf(out, "nr dirty:\t\t%u\n", atomic_read(&c->btree_cache.dirty));
	prt_printf(out, "cannibalize lock:\t%p\n", c->btree_cache.alloc_lock);
}
