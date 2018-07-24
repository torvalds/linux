// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc.h"
#include "bkey_methods.h"
#include "btree_cache.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_io.h"
#include "btree_iter.h"
#include "btree_locking.h"
#include "buckets.h"
#include "extents.h"
#include "journal.h"
#include "journal_reclaim.h"
#include "keylist.h"
#include "replicas.h"
#include "super-io.h"
#include "trace.h"

#include <linux/random.h>

static void btree_node_will_make_reachable(struct btree_update *,
					   struct btree *);
static void btree_update_drop_new_node(struct bch_fs *, struct btree *);
static void bch2_btree_set_root_ondisk(struct bch_fs *, struct btree *, int);

/* Debug code: */

static void btree_node_interior_verify(struct btree *b)
{
	struct btree_node_iter iter;
	struct bkey_packed *k;

	BUG_ON(!b->level);

	bch2_btree_node_iter_init(&iter, b, b->key.k.p, false, false);
#if 1
	BUG_ON(!(k = bch2_btree_node_iter_peek(&iter, b)) ||
	       bkey_cmp_left_packed(b, k, &b->key.k.p));

	BUG_ON((bch2_btree_node_iter_advance(&iter, b),
		!bch2_btree_node_iter_end(&iter)));
#else
	const char *msg;

	msg = "not found";
	k = bch2_btree_node_iter_peek(&iter, b);
	if (!k)
		goto err;

	msg = "isn't what it should be";
	if (bkey_cmp_left_packed(b, k, &b->key.k.p))
		goto err;

	bch2_btree_node_iter_advance(&iter, b);

	msg = "isn't last key";
	if (!bch2_btree_node_iter_end(&iter))
		goto err;
	return;
err:
	bch2_dump_btree_node(b);
	printk(KERN_ERR "last key %llu:%llu %s\n", b->key.k.p.inode,
	       b->key.k.p.offset, msg);
	BUG();
#endif
}

/* Calculate ideal packed bkey format for new btree nodes: */

void __bch2_btree_calc_format(struct bkey_format_state *s, struct btree *b)
{
	struct bkey_packed *k;
	struct bset_tree *t;
	struct bkey uk;

	bch2_bkey_format_add_pos(s, b->data->min_key);

	for_each_bset(b, t)
		for (k = btree_bkey_first(b, t);
		     k != btree_bkey_last(b, t);
		     k = bkey_next(k))
			if (!bkey_whiteout(k)) {
				uk = bkey_unpack_key(b, k);
				bch2_bkey_format_add_key(s, &uk);
			}
}

static struct bkey_format bch2_btree_calc_format(struct btree *b)
{
	struct bkey_format_state s;

	bch2_bkey_format_init(&s);
	__bch2_btree_calc_format(&s, b);

	return bch2_bkey_format_done(&s);
}

static size_t btree_node_u64s_with_format(struct btree *b,
					  struct bkey_format *new_f)
{
	struct bkey_format *old_f = &b->format;

	/* stupid integer promotion rules */
	ssize_t delta =
	    (((int) new_f->key_u64s - old_f->key_u64s) *
	     (int) b->nr.packed_keys) +
	    (((int) new_f->key_u64s - BKEY_U64s) *
	     (int) b->nr.unpacked_keys);

	BUG_ON(delta + b->nr.live_u64s < 0);

	return b->nr.live_u64s + delta;
}

/**
 * btree_node_format_fits - check if we could rewrite node with a new format
 *
 * This assumes all keys can pack with the new format -- it just checks if
 * the re-packed keys would fit inside the node itself.
 */
bool bch2_btree_node_format_fits(struct bch_fs *c, struct btree *b,
				 struct bkey_format *new_f)
{
	size_t u64s = btree_node_u64s_with_format(b, new_f);

	return __vstruct_bytes(struct btree_node, u64s) < btree_bytes(c);
}

/* Btree node freeing/allocation: */

static bool btree_key_matches(struct bch_fs *c,
			      struct bkey_s_c_extent l,
			      struct bkey_s_c_extent r)
{
	const struct bch_extent_ptr *ptr1, *ptr2;

	extent_for_each_ptr(l, ptr1)
		extent_for_each_ptr(r, ptr2)
			if (ptr1->dev == ptr2->dev &&
			    ptr1->gen == ptr2->gen &&
			    ptr1->offset == ptr2->offset)
				return true;

	return false;
}

/*
 * We're doing the index update that makes @b unreachable, update stuff to
 * reflect that:
 *
 * Must be called _before_ btree_update_updated_root() or
 * btree_update_updated_node:
 */
static void bch2_btree_node_free_index(struct btree_update *as, struct btree *b,
				       struct bkey_s_c k,
				       struct bch_fs_usage *stats)
{
	struct bch_fs *c = as->c;
	struct pending_btree_node_free *d;
	unsigned replicas;

	/*
	 * btree_update lock is only needed here to avoid racing with
	 * gc:
	 */
	mutex_lock(&c->btree_interior_update_lock);

	for (d = as->pending; d < as->pending + as->nr_pending; d++)
		if (!bkey_cmp(k.k->p, d->key.k.p) &&
		    btree_key_matches(c, bkey_s_c_to_extent(k),
				      bkey_i_to_s_c_extent(&d->key)))
			goto found;
	BUG();
found:
	BUG_ON(d->index_update_done);
	d->index_update_done = true;

	/*
	 * Btree nodes are accounted as freed in bch_alloc_stats when they're
	 * freed from the index:
	 */
	replicas = bch2_extent_nr_dirty_ptrs(k);
	if (replicas)
		stats->s[replicas - 1].data[S_META] -= c->opts.btree_node_size;

	/*
	 * We're dropping @k from the btree, but it's still live until the
	 * index update is persistent so we need to keep a reference around for
	 * mark and sweep to find - that's primarily what the
	 * btree_node_pending_free list is for.
	 *
	 * So here (when we set index_update_done = true), we're moving an
	 * existing reference to a different part of the larger "gc keyspace" -
	 * and the new position comes after the old position, since GC marks
	 * the pending free list after it walks the btree.
	 *
	 * If we move the reference while mark and sweep is _between_ the old
	 * and the new position, mark and sweep will see the reference twice
	 * and it'll get double accounted - so check for that here and subtract
	 * to cancel out one of mark and sweep's markings if necessary:
	 */

	/*
	 * bch2_mark_key() compares the current gc pos to the pos we're
	 * moving this reference from, hence one comparison here:
	 */
	if (gc_pos_cmp(c->gc_pos, gc_phase(GC_PHASE_PENDING_DELETE)) < 0) {
		struct bch_fs_usage tmp = { 0 };

		bch2_mark_key(c, bkey_i_to_s_c(&d->key),
			     -c->opts.btree_node_size, BCH_DATA_BTREE, b
			     ? gc_pos_btree_node(b)
			     : gc_pos_btree_root(as->btree_id),
			     &tmp, 0, 0);
		/*
		 * Don't apply tmp - pending deletes aren't tracked in
		 * bch_alloc_stats:
		 */
	}

	mutex_unlock(&c->btree_interior_update_lock);
}

static void __btree_node_free(struct bch_fs *c, struct btree *b)
{
	trace_btree_node_free(c, b);

	BUG_ON(btree_node_dirty(b));
	BUG_ON(btree_node_need_write(b));
	BUG_ON(b == btree_node_root(c, b));
	BUG_ON(b->ob.nr);
	BUG_ON(!list_empty(&b->write_blocked));
	BUG_ON(b->will_make_reachable);

	clear_btree_node_noevict(b);

	bch2_btree_node_hash_remove(&c->btree_cache, b);

	mutex_lock(&c->btree_cache.lock);
	list_move(&b->list, &c->btree_cache.freeable);
	mutex_unlock(&c->btree_cache.lock);
}

void bch2_btree_node_free_never_inserted(struct bch_fs *c, struct btree *b)
{
	struct btree_ob_ref ob = b->ob;

	btree_update_drop_new_node(c, b);

	b->ob.nr = 0;

	clear_btree_node_dirty(b);

	btree_node_lock_type(c, b, SIX_LOCK_write);
	__btree_node_free(c, b);
	six_unlock_write(&b->lock);

	bch2_open_bucket_put_refs(c, &ob.nr, ob.refs);
}

void bch2_btree_node_free_inmem(struct bch_fs *c, struct btree *b,
				struct btree_iter *iter)
{
	/*
	 * Is this a node that isn't reachable on disk yet?
	 *
	 * Nodes that aren't reachable yet have writes blocked until they're
	 * reachable - now that we've cancelled any pending writes and moved
	 * things waiting on that write to wait on this update, we can drop this
	 * node from the list of nodes that the other update is making
	 * reachable, prior to freeing it:
	 */
	btree_update_drop_new_node(c, b);

	__bch2_btree_node_lock_write(b, iter);
	__btree_node_free(c, b);
	six_unlock_write(&b->lock);

	bch2_btree_iter_node_drop(iter, b);
}

static void bch2_btree_node_free_ondisk(struct bch_fs *c,
					struct pending_btree_node_free *pending)
{
	struct bch_fs_usage stats = { 0 };

	BUG_ON(!pending->index_update_done);

	bch2_mark_key(c, bkey_i_to_s_c(&pending->key),
		     -c->opts.btree_node_size, BCH_DATA_BTREE,
		     gc_phase(GC_PHASE_PENDING_DELETE),
		     &stats, 0, 0);
	/*
	 * Don't apply stats - pending deletes aren't tracked in
	 * bch_alloc_stats:
	 */
}

void bch2_btree_open_bucket_put(struct bch_fs *c, struct btree *b)
{
	bch2_open_bucket_put_refs(c, &b->ob.nr, b->ob.refs);
}

static struct btree *__bch2_btree_node_alloc(struct bch_fs *c,
					     struct disk_reservation *res,
					     struct closure *cl,
					     unsigned flags)
{
	struct write_point *wp;
	struct btree *b;
	BKEY_PADDED(k) tmp;
	struct bkey_i_extent *e;
	struct btree_ob_ref ob;
	struct bch_devs_list devs_have = (struct bch_devs_list) { 0 };
	unsigned nr_reserve;
	enum alloc_reserve alloc_reserve;

	if (flags & BTREE_INSERT_USE_ALLOC_RESERVE) {
		nr_reserve	= 0;
		alloc_reserve	= RESERVE_ALLOC;
	} else if (flags & BTREE_INSERT_USE_RESERVE) {
		nr_reserve	= BTREE_NODE_RESERVE / 2;
		alloc_reserve	= RESERVE_BTREE;
	} else {
		nr_reserve	= BTREE_NODE_RESERVE;
		alloc_reserve	= RESERVE_NONE;
	}

	mutex_lock(&c->btree_reserve_cache_lock);
	if (c->btree_reserve_cache_nr > nr_reserve) {
		struct btree_alloc *a =
			&c->btree_reserve_cache[--c->btree_reserve_cache_nr];

		ob = a->ob;
		bkey_copy(&tmp.k, &a->k);
		mutex_unlock(&c->btree_reserve_cache_lock);
		goto mem_alloc;
	}
	mutex_unlock(&c->btree_reserve_cache_lock);

retry:
	wp = bch2_alloc_sectors_start(c, c->opts.foreground_target,
				      writepoint_ptr(&c->btree_write_point),
				      &devs_have,
				      res->nr_replicas,
				      c->opts.metadata_replicas_required,
				      alloc_reserve, 0, cl);
	if (IS_ERR(wp))
		return ERR_CAST(wp);

	if (wp->sectors_free < c->opts.btree_node_size) {
		struct open_bucket *ob;
		unsigned i;

		writepoint_for_each_ptr(wp, ob, i)
			if (ob->sectors_free < c->opts.btree_node_size)
				ob->sectors_free = 0;

		bch2_alloc_sectors_done(c, wp);
		goto retry;
	}

	e = bkey_extent_init(&tmp.k);
	bch2_alloc_sectors_append_ptrs(c, wp, e, c->opts.btree_node_size);

	ob.nr = 0;
	bch2_open_bucket_get(c, wp, &ob.nr, ob.refs);
	bch2_alloc_sectors_done(c, wp);
mem_alloc:
	b = bch2_btree_node_mem_alloc(c);

	/* we hold cannibalize_lock: */
	BUG_ON(IS_ERR(b));
	BUG_ON(b->ob.nr);

	bkey_copy(&b->key, &tmp.k);
	b->ob = ob;

	return b;
}

static struct btree *bch2_btree_node_alloc(struct btree_update *as, unsigned level)
{
	struct bch_fs *c = as->c;
	struct btree *b;

	BUG_ON(level >= BTREE_MAX_DEPTH);
	BUG_ON(!as->reserve->nr);

	b = as->reserve->b[--as->reserve->nr];

	BUG_ON(bch2_btree_node_hash_insert(&c->btree_cache, b, level, as->btree_id));

	set_btree_node_accessed(b);
	set_btree_node_dirty(b);

	bch2_bset_init_first(b, &b->data->keys);
	memset(&b->nr, 0, sizeof(b->nr));
	b->data->magic = cpu_to_le64(bset_magic(c));
	b->data->flags = 0;
	SET_BTREE_NODE_ID(b->data, as->btree_id);
	SET_BTREE_NODE_LEVEL(b->data, level);
	b->data->ptr = bkey_i_to_extent(&b->key)->v.start->ptr;

	bch2_btree_build_aux_trees(b);

	btree_node_will_make_reachable(as, b);

	trace_btree_node_alloc(c, b);
	return b;
}

struct btree *__bch2_btree_node_alloc_replacement(struct btree_update *as,
						  struct btree *b,
						  struct bkey_format format)
{
	struct btree *n;

	n = bch2_btree_node_alloc(as, b->level);

	n->data->min_key	= b->data->min_key;
	n->data->max_key	= b->data->max_key;
	n->data->format		= format;
	SET_BTREE_NODE_SEQ(n->data, BTREE_NODE_SEQ(b->data) + 1);

	btree_node_set_format(n, format);

	bch2_btree_sort_into(as->c, n, b);

	btree_node_reset_sib_u64s(n);

	n->key.k.p = b->key.k.p;
	return n;
}

static struct btree *bch2_btree_node_alloc_replacement(struct btree_update *as,
						       struct btree *b)
{
	struct bkey_format new_f = bch2_btree_calc_format(b);

	/*
	 * The keys might expand with the new format - if they wouldn't fit in
	 * the btree node anymore, use the old format for now:
	 */
	if (!bch2_btree_node_format_fits(as->c, b, &new_f))
		new_f = b->format;

	return __bch2_btree_node_alloc_replacement(as, b, new_f);
}

static struct btree *__btree_root_alloc(struct btree_update *as, unsigned level)
{
	struct btree *b = bch2_btree_node_alloc(as, level);

	b->data->min_key = POS_MIN;
	b->data->max_key = POS_MAX;
	b->data->format = bch2_btree_calc_format(b);
	b->key.k.p = POS_MAX;

	btree_node_set_format(b, b->data->format);
	bch2_btree_build_aux_trees(b);

	six_unlock_write(&b->lock);

	return b;
}

static void bch2_btree_reserve_put(struct bch_fs *c, struct btree_reserve *reserve)
{
	bch2_disk_reservation_put(c, &reserve->disk_res);

	mutex_lock(&c->btree_reserve_cache_lock);

	while (reserve->nr) {
		struct btree *b = reserve->b[--reserve->nr];

		six_unlock_write(&b->lock);

		if (c->btree_reserve_cache_nr <
		    ARRAY_SIZE(c->btree_reserve_cache)) {
			struct btree_alloc *a =
				&c->btree_reserve_cache[c->btree_reserve_cache_nr++];

			a->ob = b->ob;
			b->ob.nr = 0;
			bkey_copy(&a->k, &b->key);
		} else {
			bch2_btree_open_bucket_put(c, b);
		}

		btree_node_lock_type(c, b, SIX_LOCK_write);
		__btree_node_free(c, b);
		six_unlock_write(&b->lock);

		six_unlock_intent(&b->lock);
	}

	mutex_unlock(&c->btree_reserve_cache_lock);

	mempool_free(reserve, &c->btree_reserve_pool);
}

static struct btree_reserve *bch2_btree_reserve_get(struct bch_fs *c,
						    unsigned nr_nodes,
						    unsigned flags,
						    struct closure *cl)
{
	struct btree_reserve *reserve;
	struct btree *b;
	struct disk_reservation disk_res = { 0, 0 };
	unsigned sectors = nr_nodes * c->opts.btree_node_size;
	int ret, disk_res_flags = BCH_DISK_RESERVATION_GC_LOCK_HELD;

	if (flags & BTREE_INSERT_NOFAIL)
		disk_res_flags |= BCH_DISK_RESERVATION_NOFAIL;

	/*
	 * This check isn't necessary for correctness - it's just to potentially
	 * prevent us from doing a lot of work that'll end up being wasted:
	 */
	ret = bch2_journal_error(&c->journal);
	if (ret)
		return ERR_PTR(ret);

	if (bch2_disk_reservation_get(c, &disk_res, sectors,
				      c->opts.metadata_replicas,
				      disk_res_flags))
		return ERR_PTR(-ENOSPC);

	BUG_ON(nr_nodes > BTREE_RESERVE_MAX);

	/*
	 * Protects reaping from the btree node cache and using the btree node
	 * open bucket reserve:
	 */
	ret = bch2_btree_cache_cannibalize_lock(c, cl);
	if (ret) {
		bch2_disk_reservation_put(c, &disk_res);
		return ERR_PTR(ret);
	}

	reserve = mempool_alloc(&c->btree_reserve_pool, GFP_NOIO);

	reserve->disk_res = disk_res;
	reserve->nr = 0;

	while (reserve->nr < nr_nodes) {
		b = __bch2_btree_node_alloc(c, &disk_res,
					    flags & BTREE_INSERT_NOWAIT
					    ? NULL : cl, flags);
		if (IS_ERR(b)) {
			ret = PTR_ERR(b);
			goto err_free;
		}

		ret = bch2_mark_bkey_replicas(c, BCH_DATA_BTREE,
					      bkey_i_to_s_c(&b->key));
		if (ret)
			goto err_free;

		reserve->b[reserve->nr++] = b;
	}

	bch2_btree_cache_cannibalize_unlock(c);
	return reserve;
err_free:
	bch2_btree_reserve_put(c, reserve);
	bch2_btree_cache_cannibalize_unlock(c);
	trace_btree_reserve_get_fail(c, nr_nodes, cl);
	return ERR_PTR(ret);
}

/* Asynchronous interior node update machinery */

static void bch2_btree_update_free(struct btree_update *as)
{
	struct bch_fs *c = as->c;

	bch2_journal_pin_flush(&c->journal, &as->journal);

	BUG_ON(as->nr_new_nodes);
	BUG_ON(as->nr_pending);

	if (as->reserve)
		bch2_btree_reserve_put(c, as->reserve);

	mutex_lock(&c->btree_interior_update_lock);
	list_del(&as->list);

	closure_debug_destroy(&as->cl);
	mempool_free(as, &c->btree_interior_update_pool);
	percpu_ref_put(&c->writes);

	closure_wake_up(&c->btree_interior_update_wait);
	mutex_unlock(&c->btree_interior_update_lock);
}

static void btree_update_nodes_reachable(struct closure *cl)
{
	struct btree_update *as = container_of(cl, struct btree_update, cl);
	struct bch_fs *c = as->c;

	bch2_journal_pin_drop(&c->journal, &as->journal);

	mutex_lock(&c->btree_interior_update_lock);

	while (as->nr_new_nodes) {
		struct btree *b = as->new_nodes[--as->nr_new_nodes];

		BUG_ON(b->will_make_reachable != (unsigned long) as);
		b->will_make_reachable = 0;
		mutex_unlock(&c->btree_interior_update_lock);

		/*
		 * b->will_make_reachable prevented it from being written, so
		 * write it now if it needs to be written:
		 */
		btree_node_lock_type(c, b, SIX_LOCK_read);
		bch2_btree_node_write_cond(c, b, btree_node_need_write(b));
		six_unlock_read(&b->lock);
		mutex_lock(&c->btree_interior_update_lock);
	}

	while (as->nr_pending)
		bch2_btree_node_free_ondisk(c, &as->pending[--as->nr_pending]);

	mutex_unlock(&c->btree_interior_update_lock);

	closure_wake_up(&as->wait);

	bch2_btree_update_free(as);
}

static void btree_update_wait_on_journal(struct closure *cl)
{
	struct btree_update *as = container_of(cl, struct btree_update, cl);
	struct bch_fs *c = as->c;
	int ret;

	ret = bch2_journal_open_seq_async(&c->journal, as->journal_seq, cl);
	if (ret < 0)
		goto err;
	if (!ret) {
		continue_at(cl, btree_update_wait_on_journal, system_wq);
		return;
	}

	bch2_journal_flush_seq_async(&c->journal, as->journal_seq, cl);
err:
	continue_at(cl, btree_update_nodes_reachable, system_wq);
}

static void btree_update_nodes_written(struct closure *cl)
{
	struct btree_update *as = container_of(cl, struct btree_update, cl);
	struct bch_fs *c = as->c;
	struct btree *b;

	/*
	 * We did an update to a parent node where the pointers we added pointed
	 * to child nodes that weren't written yet: now, the child nodes have
	 * been written so we can write out the update to the interior node.
	 */
retry:
	mutex_lock(&c->btree_interior_update_lock);
	as->nodes_written = true;

	switch (as->mode) {
	case BTREE_INTERIOR_NO_UPDATE:
		BUG();
	case BTREE_INTERIOR_UPDATING_NODE:
		/* The usual case: */
		b = READ_ONCE(as->b);

		if (!six_trylock_read(&b->lock)) {
			mutex_unlock(&c->btree_interior_update_lock);
			btree_node_lock_type(c, b, SIX_LOCK_read);
			six_unlock_read(&b->lock);
			goto retry;
		}

		BUG_ON(!btree_node_dirty(b));
		closure_wait(&btree_current_write(b)->wait, cl);

		list_del(&as->write_blocked_list);
		mutex_unlock(&c->btree_interior_update_lock);

		/*
		 * b->write_blocked prevented it from being written, so
		 * write it now if it needs to be written:
		 */
		bch2_btree_node_write_cond(c, b, true);
		six_unlock_read(&b->lock);
		break;

	case BTREE_INTERIOR_UPDATING_AS:
		/*
		 * The btree node we originally updated has been freed and is
		 * being rewritten - so we need to write anything here, we just
		 * need to signal to that btree_update that it's ok to make the
		 * new replacement node visible:
		 */
		closure_put(&as->parent_as->cl);

		/*
		 * and then we have to wait on that btree_update to finish:
		 */
		closure_wait(&as->parent_as->wait, cl);
		mutex_unlock(&c->btree_interior_update_lock);
		break;

	case BTREE_INTERIOR_UPDATING_ROOT:
		/* b is the new btree root: */
		b = READ_ONCE(as->b);

		if (!six_trylock_read(&b->lock)) {
			mutex_unlock(&c->btree_interior_update_lock);
			btree_node_lock_type(c, b, SIX_LOCK_read);
			six_unlock_read(&b->lock);
			goto retry;
		}

		BUG_ON(c->btree_roots[b->btree_id].as != as);
		c->btree_roots[b->btree_id].as = NULL;

		bch2_btree_set_root_ondisk(c, b, WRITE);

		/*
		 * We don't have to wait anything anything here (before
		 * btree_update_nodes_reachable frees the old nodes
		 * ondisk) - we've ensured that the very next journal write will
		 * have the pointer to the new root, and before the allocator
		 * can reuse the old nodes it'll have to do a journal commit:
		 */
		six_unlock_read(&b->lock);
		mutex_unlock(&c->btree_interior_update_lock);

		/*
		 * Bit of funny circularity going on here we have to break:
		 *
		 * We have to drop our journal pin before writing the journal
		 * entry that points to the new btree root: else, we could
		 * deadlock if the journal currently happens to be full.
		 *
		 * This mean we're dropping the journal pin _before_ the new
		 * nodes are technically reachable - but this is safe, because
		 * after the bch2_btree_set_root_ondisk() call above they will
		 * be reachable as of the very next journal write:
		 */
		bch2_journal_pin_drop(&c->journal, &as->journal);

		as->journal_seq = bch2_journal_last_unwritten_seq(&c->journal);

		btree_update_wait_on_journal(cl);
		return;
	}

	continue_at(cl, btree_update_nodes_reachable, system_wq);
}

/*
 * We're updating @b with pointers to nodes that haven't finished writing yet:
 * block @b from being written until @as completes
 */
static void btree_update_updated_node(struct btree_update *as, struct btree *b)
{
	struct bch_fs *c = as->c;

	mutex_lock(&c->btree_interior_update_lock);

	BUG_ON(as->mode != BTREE_INTERIOR_NO_UPDATE);
	BUG_ON(!btree_node_dirty(b));

	as->mode = BTREE_INTERIOR_UPDATING_NODE;
	as->b = b;
	list_add(&as->write_blocked_list, &b->write_blocked);

	mutex_unlock(&c->btree_interior_update_lock);

	/*
	 * In general, when you're staging things in a journal that will later
	 * be written elsewhere, and you also want to guarantee ordering: that
	 * is, if you have updates a, b, c, after a crash you should never see c
	 * and not a or b - there's a problem:
	 *
	 * If the final destination of the update(s) (i.e. btree node) can be
	 * written/flushed _before_ the relevant journal entry - oops, that
	 * breaks ordering, since the various leaf nodes can be written in any
	 * order.
	 *
	 * Normally we use bset->journal_seq to deal with this - if during
	 * recovery we find a btree node write that's newer than the newest
	 * journal entry, we just ignore it - we don't need it, anything we're
	 * supposed to have (that we reported as completed via fsync()) will
	 * still be in the journal, and as far as the state of the journal is
	 * concerned that btree node write never happened.
	 *
	 * That breaks when we're rewriting/splitting/merging nodes, since we're
	 * mixing btree node writes that haven't happened yet with previously
	 * written data that has been reported as completed to the journal.
	 *
	 * Thus, before making the new nodes reachable, we have to wait the
	 * newest journal sequence number we have data for to be written (if it
	 * hasn't been yet).
	 */
	bch2_journal_wait_on_seq(&c->journal, as->journal_seq, &as->cl);
}

static void interior_update_flush(struct journal *j,
			struct journal_entry_pin *pin, u64 seq)
{
	struct btree_update *as =
		container_of(pin, struct btree_update, journal);

	bch2_journal_flush_seq_async(j, as->journal_seq, NULL);
}

static void btree_update_reparent(struct btree_update *as,
				  struct btree_update *child)
{
	struct bch_fs *c = as->c;

	child->b = NULL;
	child->mode = BTREE_INTERIOR_UPDATING_AS;
	child->parent_as = as;
	closure_get(&as->cl);

	/*
	 * When we write a new btree root, we have to drop our journal pin
	 * _before_ the new nodes are technically reachable; see
	 * btree_update_nodes_written().
	 *
	 * This goes for journal pins that are recursively blocked on us - so,
	 * just transfer the journal pin to the new interior update so
	 * btree_update_nodes_written() can drop it.
	 */
	bch2_journal_pin_add_if_older(&c->journal, &child->journal,
				      &as->journal, interior_update_flush);
	bch2_journal_pin_drop(&c->journal, &child->journal);

	as->journal_seq = max(as->journal_seq, child->journal_seq);
}

static void btree_update_updated_root(struct btree_update *as)
{
	struct bch_fs *c = as->c;
	struct btree_root *r = &c->btree_roots[as->btree_id];

	mutex_lock(&c->btree_interior_update_lock);

	BUG_ON(as->mode != BTREE_INTERIOR_NO_UPDATE);

	/*
	 * Old root might not be persistent yet - if so, redirect its
	 * btree_update operation to point to us:
	 */
	if (r->as)
		btree_update_reparent(as, r->as);

	as->mode = BTREE_INTERIOR_UPDATING_ROOT;
	as->b = r->b;
	r->as = as;

	mutex_unlock(&c->btree_interior_update_lock);

	/*
	 * When we're rewriting nodes and updating interior nodes, there's an
	 * issue with updates that haven't been written in the journal getting
	 * mixed together with older data - see btree_update_updated_node()
	 * for the explanation.
	 *
	 * However, this doesn't affect us when we're writing a new btree root -
	 * because to make that new root reachable we have to write out a new
	 * journal entry, which must necessarily be newer than as->journal_seq.
	 */
}

static void btree_node_will_make_reachable(struct btree_update *as,
					   struct btree *b)
{
	struct bch_fs *c = as->c;

	mutex_lock(&c->btree_interior_update_lock);
	BUG_ON(as->nr_new_nodes >= ARRAY_SIZE(as->new_nodes));
	BUG_ON(b->will_make_reachable);

	as->new_nodes[as->nr_new_nodes++] = b;
	b->will_make_reachable = 1UL|(unsigned long) as;

	closure_get(&as->cl);
	mutex_unlock(&c->btree_interior_update_lock);
}

static void btree_update_drop_new_node(struct bch_fs *c, struct btree *b)
{
	struct btree_update *as;
	unsigned long v;
	unsigned i;

	mutex_lock(&c->btree_interior_update_lock);
	v = xchg(&b->will_make_reachable, 0);
	as = (struct btree_update *) (v & ~1UL);

	if (!as) {
		mutex_unlock(&c->btree_interior_update_lock);
		return;
	}

	for (i = 0; i < as->nr_new_nodes; i++)
		if (as->new_nodes[i] == b)
			goto found;

	BUG();
found:
	array_remove_item(as->new_nodes, as->nr_new_nodes, i);
	mutex_unlock(&c->btree_interior_update_lock);

	if (v & 1)
		closure_put(&as->cl);
}

static void btree_interior_update_add_node_reference(struct btree_update *as,
						     struct btree *b)
{
	struct bch_fs *c = as->c;
	struct pending_btree_node_free *d;

	mutex_lock(&c->btree_interior_update_lock);

	/* Add this node to the list of nodes being freed: */
	BUG_ON(as->nr_pending >= ARRAY_SIZE(as->pending));

	d = &as->pending[as->nr_pending++];
	d->index_update_done	= false;
	d->seq			= b->data->keys.seq;
	d->btree_id		= b->btree_id;
	d->level		= b->level;
	bkey_copy(&d->key, &b->key);

	mutex_unlock(&c->btree_interior_update_lock);
}

/*
 * @b is being split/rewritten: it may have pointers to not-yet-written btree
 * nodes and thus outstanding btree_updates - redirect @b's
 * btree_updates to point to this btree_update:
 */
void bch2_btree_interior_update_will_free_node(struct btree_update *as,
					       struct btree *b)
{
	struct bch_fs *c = as->c;
	struct closure *cl, *cl_n;
	struct btree_update *p, *n;
	struct btree_write *w;
	struct bset_tree *t;

	set_btree_node_dying(b);

	if (btree_node_fake(b))
		return;

	btree_interior_update_add_node_reference(as, b);

	/*
	 * Does this node have data that hasn't been written in the journal?
	 *
	 * If so, we have to wait for the corresponding journal entry to be
	 * written before making the new nodes reachable - we can't just carry
	 * over the bset->journal_seq tracking, since we'll be mixing those keys
	 * in with keys that aren't in the journal anymore:
	 */
	for_each_bset(b, t)
		as->journal_seq = max(as->journal_seq,
				      le64_to_cpu(bset(b, t)->journal_seq));

	mutex_lock(&c->btree_interior_update_lock);

	/*
	 * Does this node have any btree_update operations preventing
	 * it from being written?
	 *
	 * If so, redirect them to point to this btree_update: we can
	 * write out our new nodes, but we won't make them visible until those
	 * operations complete
	 */
	list_for_each_entry_safe(p, n, &b->write_blocked, write_blocked_list) {
		list_del(&p->write_blocked_list);
		btree_update_reparent(as, p);
	}

	clear_btree_node_dirty(b);
	clear_btree_node_need_write(b);
	w = btree_current_write(b);

	/*
	 * Does this node have any btree_update operations waiting on this node
	 * to be written?
	 *
	 * If so, wake them up when this btree_update operation is reachable:
	 */
	llist_for_each_entry_safe(cl, cl_n, llist_del_all(&w->wait.list), list)
		llist_add(&cl->list, &as->wait.list);

	/*
	 * Does this node have unwritten data that has a pin on the journal?
	 *
	 * If so, transfer that pin to the btree_update operation -
	 * note that if we're freeing multiple nodes, we only need to keep the
	 * oldest pin of any of the nodes we're freeing. We'll release the pin
	 * when the new nodes are persistent and reachable on disk:
	 */
	bch2_journal_pin_add_if_older(&c->journal, &w->journal,
				      &as->journal, interior_update_flush);
	bch2_journal_pin_drop(&c->journal, &w->journal);

	w = btree_prev_write(b);
	bch2_journal_pin_add_if_older(&c->journal, &w->journal,
				      &as->journal, interior_update_flush);
	bch2_journal_pin_drop(&c->journal, &w->journal);

	mutex_unlock(&c->btree_interior_update_lock);
}

void bch2_btree_update_done(struct btree_update *as)
{
	BUG_ON(as->mode == BTREE_INTERIOR_NO_UPDATE);

	bch2_btree_reserve_put(as->c, as->reserve);
	as->reserve = NULL;

	continue_at(&as->cl, btree_update_nodes_written, system_freezable_wq);
}

struct btree_update *
bch2_btree_update_start(struct bch_fs *c, enum btree_id id,
			unsigned nr_nodes, unsigned flags,
			struct closure *cl)
{
	struct btree_reserve *reserve;
	struct btree_update *as;

	if (unlikely(!percpu_ref_tryget(&c->writes)))
		return ERR_PTR(-EROFS);

	reserve = bch2_btree_reserve_get(c, nr_nodes, flags, cl);
	if (IS_ERR(reserve)) {
		percpu_ref_put(&c->writes);
		return ERR_CAST(reserve);
	}

	as = mempool_alloc(&c->btree_interior_update_pool, GFP_NOIO);
	memset(as, 0, sizeof(*as));
	closure_init(&as->cl, NULL);
	as->c		= c;
	as->mode	= BTREE_INTERIOR_NO_UPDATE;
	as->btree_id	= id;
	as->reserve	= reserve;
	INIT_LIST_HEAD(&as->write_blocked_list);

	bch2_keylist_init(&as->parent_keys, as->inline_keys);

	mutex_lock(&c->btree_interior_update_lock);
	list_add_tail(&as->list, &c->btree_interior_update_list);
	mutex_unlock(&c->btree_interior_update_lock);

	return as;
}

/* Btree root updates: */

static void __bch2_btree_set_root_inmem(struct bch_fs *c, struct btree *b)
{
	/* Root nodes cannot be reaped */
	mutex_lock(&c->btree_cache.lock);
	list_del_init(&b->list);
	mutex_unlock(&c->btree_cache.lock);

	mutex_lock(&c->btree_root_lock);
	BUG_ON(btree_node_root(c, b) &&
	       (b->level < btree_node_root(c, b)->level ||
		!btree_node_dying(btree_node_root(c, b))));

	btree_node_root(c, b) = b;
	mutex_unlock(&c->btree_root_lock);

	bch2_recalc_btree_reserve(c);
}

static void bch2_btree_set_root_inmem(struct btree_update *as, struct btree *b)
{
	struct bch_fs *c = as->c;
	struct btree *old = btree_node_root(c, b);
	struct bch_fs_usage stats = { 0 };

	__bch2_btree_set_root_inmem(c, b);

	bch2_mark_key(c, bkey_i_to_s_c(&b->key),
		      c->opts.btree_node_size, BCH_DATA_BTREE,
		      gc_pos_btree_root(b->btree_id),
		      &stats, 0, 0);

	if (old && !btree_node_fake(old))
		bch2_btree_node_free_index(as, NULL,
					   bkey_i_to_s_c(&old->key),
					   &stats);
	bch2_fs_usage_apply(c, &stats, &as->reserve->disk_res,
			    gc_pos_btree_root(b->btree_id));
}

static void bch2_btree_set_root_ondisk(struct bch_fs *c, struct btree *b, int rw)
{
	struct btree_root *r = &c->btree_roots[b->btree_id];

	mutex_lock(&c->btree_root_lock);

	BUG_ON(b != r->b);
	bkey_copy(&r->key, &b->key);
	r->level = b->level;
	r->alive = true;
	if (rw == WRITE)
		c->btree_roots_dirty = true;

	mutex_unlock(&c->btree_root_lock);
}

/**
 * bch_btree_set_root - update the root in memory and on disk
 *
 * To ensure forward progress, the current task must not be holding any
 * btree node write locks. However, you must hold an intent lock on the
 * old root.
 *
 * Note: This allocates a journal entry but doesn't add any keys to
 * it.  All the btree roots are part of every journal write, so there
 * is nothing new to be done.  This just guarantees that there is a
 * journal write.
 */
static void bch2_btree_set_root(struct btree_update *as, struct btree *b,
				struct btree_iter *iter)
{
	struct bch_fs *c = as->c;
	struct btree *old;

	trace_btree_set_root(c, b);
	BUG_ON(!b->written &&
	       !test_bit(BCH_FS_HOLD_BTREE_WRITES, &c->flags));

	old = btree_node_root(c, b);

	/*
	 * Ensure no one is using the old root while we switch to the
	 * new root:
	 */
	bch2_btree_node_lock_write(old, iter);

	bch2_btree_set_root_inmem(as, b);

	btree_update_updated_root(as);

	/*
	 * Unlock old root after new root is visible:
	 *
	 * The new root isn't persistent, but that's ok: we still have
	 * an intent lock on the new root, and any updates that would
	 * depend on the new root would have to update the new root.
	 */
	bch2_btree_node_unlock_write(old, iter);
}

/* Interior node updates: */

static void bch2_insert_fixup_btree_ptr(struct btree_update *as, struct btree *b,
					struct btree_iter *iter,
					struct bkey_i *insert,
					struct btree_node_iter *node_iter)
{
	struct bch_fs *c = as->c;
	struct bch_fs_usage stats = { 0 };
	struct bkey_packed *k;
	struct bkey tmp;

	BUG_ON(insert->k.u64s > bch_btree_keys_u64s_remaining(c, b));

	if (bkey_extent_is_data(&insert->k))
		bch2_mark_key(c, bkey_i_to_s_c(insert),
			     c->opts.btree_node_size, BCH_DATA_BTREE,
			     gc_pos_btree_node(b), &stats, 0, 0);

	while ((k = bch2_btree_node_iter_peek_all(node_iter, b)) &&
	       !btree_iter_pos_cmp_packed(b, &insert->k.p, k, false))
		bch2_btree_node_iter_advance(node_iter, b);

	/*
	 * If we're overwriting, look up pending delete and mark so that gc
	 * marks it on the pending delete list:
	 */
	if (k && !bkey_cmp_packed(b, k, &insert->k))
		bch2_btree_node_free_index(as, b,
					   bkey_disassemble(b, k, &tmp),
					   &stats);

	bch2_fs_usage_apply(c, &stats, &as->reserve->disk_res,
			    gc_pos_btree_node(b));

	bch2_btree_bset_insert_key(iter, b, node_iter, insert);
	set_btree_node_dirty(b);
	set_btree_node_need_write(b);
}

/*
 * Move keys from n1 (original replacement node, now lower node) to n2 (higher
 * node)
 */
static struct btree *__btree_split_node(struct btree_update *as,
					struct btree *n1,
					struct btree_iter *iter)
{
	size_t nr_packed = 0, nr_unpacked = 0;
	struct btree *n2;
	struct bset *set1, *set2;
	struct bkey_packed *k, *prev = NULL;

	n2 = bch2_btree_node_alloc(as, n1->level);

	n2->data->max_key	= n1->data->max_key;
	n2->data->format	= n1->format;
	SET_BTREE_NODE_SEQ(n2->data, BTREE_NODE_SEQ(n1->data));
	n2->key.k.p = n1->key.k.p;

	btree_node_set_format(n2, n2->data->format);

	set1 = btree_bset_first(n1);
	set2 = btree_bset_first(n2);

	/*
	 * Has to be a linear search because we don't have an auxiliary
	 * search tree yet
	 */
	k = set1->start;
	while (1) {
		if (bkey_next(k) == vstruct_last(set1))
			break;
		if (k->_data - set1->_data >= (le16_to_cpu(set1->u64s) * 3) / 5)
			break;

		if (bkey_packed(k))
			nr_packed++;
		else
			nr_unpacked++;

		prev = k;
		k = bkey_next(k);
	}

	BUG_ON(!prev);

	n1->key.k.p = bkey_unpack_pos(n1, prev);
	n1->data->max_key = n1->key.k.p;
	n2->data->min_key =
		btree_type_successor(n1->btree_id, n1->key.k.p);

	set2->u64s = cpu_to_le16((u64 *) vstruct_end(set1) - (u64 *) k);
	set1->u64s = cpu_to_le16(le16_to_cpu(set1->u64s) - le16_to_cpu(set2->u64s));

	set_btree_bset_end(n1, n1->set);
	set_btree_bset_end(n2, n2->set);

	n2->nr.live_u64s	= le16_to_cpu(set2->u64s);
	n2->nr.bset_u64s[0]	= le16_to_cpu(set2->u64s);
	n2->nr.packed_keys	= n1->nr.packed_keys - nr_packed;
	n2->nr.unpacked_keys	= n1->nr.unpacked_keys - nr_unpacked;

	n1->nr.live_u64s	= le16_to_cpu(set1->u64s);
	n1->nr.bset_u64s[0]	= le16_to_cpu(set1->u64s);
	n1->nr.packed_keys	= nr_packed;
	n1->nr.unpacked_keys	= nr_unpacked;

	BUG_ON(!set1->u64s);
	BUG_ON(!set2->u64s);

	memcpy_u64s(set2->start,
		    vstruct_end(set1),
		    le16_to_cpu(set2->u64s));

	btree_node_reset_sib_u64s(n1);
	btree_node_reset_sib_u64s(n2);

	bch2_verify_btree_nr_keys(n1);
	bch2_verify_btree_nr_keys(n2);

	if (n1->level) {
		btree_node_interior_verify(n1);
		btree_node_interior_verify(n2);
	}

	return n2;
}

/*
 * For updates to interior nodes, we've got to do the insert before we split
 * because the stuff we're inserting has to be inserted atomically. Post split,
 * the keys might have to go in different nodes and the split would no longer be
 * atomic.
 *
 * Worse, if the insert is from btree node coalescing, if we do the insert after
 * we do the split (and pick the pivot) - the pivot we pick might be between
 * nodes that were coalesced, and thus in the middle of a child node post
 * coalescing:
 */
static void btree_split_insert_keys(struct btree_update *as, struct btree *b,
				    struct btree_iter *iter,
				    struct keylist *keys)
{
	struct btree_node_iter node_iter;
	struct bkey_i *k = bch2_keylist_front(keys);
	struct bkey_packed *p;
	struct bset *i;

	BUG_ON(btree_node_type(b) != BKEY_TYPE_BTREE);

	bch2_btree_node_iter_init(&node_iter, b, k->k.p, false, false);

	while (!bch2_keylist_empty(keys)) {
		k = bch2_keylist_front(keys);

		BUG_ON(bch_keylist_u64s(keys) >
		       bch_btree_keys_u64s_remaining(as->c, b));
		BUG_ON(bkey_cmp(k->k.p, b->data->min_key) < 0);
		BUG_ON(bkey_cmp(k->k.p, b->data->max_key) > 0);

		bch2_insert_fixup_btree_ptr(as, b, iter, k, &node_iter);
		bch2_keylist_pop_front(keys);
	}

	/*
	 * We can't tolerate whiteouts here - with whiteouts there can be
	 * duplicate keys, and it would be rather bad if we picked a duplicate
	 * for the pivot:
	 */
	i = btree_bset_first(b);
	p = i->start;
	while (p != vstruct_last(i))
		if (bkey_deleted(p)) {
			le16_add_cpu(&i->u64s, -p->u64s);
			set_btree_bset_end(b, b->set);
			memmove_u64s_down(p, bkey_next(p),
					  (u64 *) vstruct_last(i) -
					  (u64 *) p);
		} else
			p = bkey_next(p);

	BUG_ON(b->nsets != 1 ||
	       b->nr.live_u64s != le16_to_cpu(btree_bset_first(b)->u64s));

	btree_node_interior_verify(b);
}

static void btree_split(struct btree_update *as, struct btree *b,
			struct btree_iter *iter, struct keylist *keys,
			unsigned flags)
{
	struct bch_fs *c = as->c;
	struct btree *parent = btree_node_parent(iter, b);
	struct btree *n1, *n2 = NULL, *n3 = NULL;
	u64 start_time = local_clock();

	BUG_ON(!parent && (b != btree_node_root(c, b)));
	BUG_ON(!btree_node_intent_locked(iter, btree_node_root(c, b)->level));

	bch2_btree_interior_update_will_free_node(as, b);

	n1 = bch2_btree_node_alloc_replacement(as, b);

	if (keys)
		btree_split_insert_keys(as, n1, iter, keys);

	if (vstruct_blocks(n1->data, c->block_bits) > BTREE_SPLIT_THRESHOLD(c)) {
		trace_btree_split(c, b);

		n2 = __btree_split_node(as, n1, iter);

		bch2_btree_build_aux_trees(n2);
		bch2_btree_build_aux_trees(n1);
		six_unlock_write(&n2->lock);
		six_unlock_write(&n1->lock);

		bch2_btree_node_write(c, n2, SIX_LOCK_intent);

		/*
		 * Note that on recursive parent_keys == keys, so we
		 * can't start adding new keys to parent_keys before emptying it
		 * out (which we did with btree_split_insert_keys() above)
		 */
		bch2_keylist_add(&as->parent_keys, &n1->key);
		bch2_keylist_add(&as->parent_keys, &n2->key);

		if (!parent) {
			/* Depth increases, make a new root */
			n3 = __btree_root_alloc(as, b->level + 1);

			n3->sib_u64s[0] = U16_MAX;
			n3->sib_u64s[1] = U16_MAX;

			btree_split_insert_keys(as, n3, iter, &as->parent_keys);

			bch2_btree_node_write(c, n3, SIX_LOCK_intent);
		}
	} else {
		trace_btree_compact(c, b);

		bch2_btree_build_aux_trees(n1);
		six_unlock_write(&n1->lock);

		bch2_keylist_add(&as->parent_keys, &n1->key);
	}

	bch2_btree_node_write(c, n1, SIX_LOCK_intent);

	/* New nodes all written, now make them visible: */

	if (parent) {
		/* Split a non root node */
		bch2_btree_insert_node(as, parent, iter, &as->parent_keys, flags);
	} else if (n3) {
		bch2_btree_set_root(as, n3, iter);
	} else {
		/* Root filled up but didn't need to be split */
		bch2_btree_set_root(as, n1, iter);
	}

	bch2_btree_open_bucket_put(c, n1);
	if (n2)
		bch2_btree_open_bucket_put(c, n2);
	if (n3)
		bch2_btree_open_bucket_put(c, n3);

	/*
	 * Note - at this point other linked iterators could still have @b read
	 * locked; we're depending on the bch2_btree_iter_node_replace() calls
	 * below removing all references to @b so we don't return with other
	 * iterators pointing to a node they have locked that's been freed.
	 *
	 * We have to free the node first because the bch2_iter_node_replace()
	 * calls will drop _our_ iterator's reference - and intent lock - to @b.
	 */
	bch2_btree_node_free_inmem(c, b, iter);

	/* Successful split, update the iterator to point to the new nodes: */

	if (n3)
		bch2_btree_iter_node_replace(iter, n3);
	if (n2)
		bch2_btree_iter_node_replace(iter, n2);
	bch2_btree_iter_node_replace(iter, n1);

	bch2_time_stats_update(&c->times[BCH_TIME_btree_split], start_time);
}

static void
bch2_btree_insert_keys_interior(struct btree_update *as, struct btree *b,
				struct btree_iter *iter, struct keylist *keys)
{
	struct btree_iter *linked;
	struct btree_node_iter node_iter;
	struct bkey_i *insert = bch2_keylist_front(keys);
	struct bkey_packed *k;

	/* Don't screw up @iter's position: */
	node_iter = iter->l[b->level].iter;

	/*
	 * btree_split(), btree_gc_coalesce() will insert keys before
	 * the iterator's current position - they know the keys go in
	 * the node the iterator points to:
	 */
	while ((k = bch2_btree_node_iter_prev_all(&node_iter, b)) &&
	       (bkey_cmp_packed(b, k, &insert->k) >= 0))
		;

	while (!bch2_keylist_empty(keys)) {
		insert = bch2_keylist_front(keys);

		bch2_insert_fixup_btree_ptr(as, b, iter, insert, &node_iter);
		bch2_keylist_pop_front(keys);
	}

	btree_update_updated_node(as, b);

	for_each_btree_iter_with_node(iter, b, linked)
		bch2_btree_node_iter_peek(&linked->l[b->level].iter, b);

	bch2_btree_iter_verify(iter, b);
}

/**
 * bch_btree_insert_node - insert bkeys into a given btree node
 *
 * @iter:		btree iterator
 * @keys:		list of keys to insert
 * @hook:		insert callback
 * @persistent:		if not null, @persistent will wait on journal write
 *
 * Inserts as many keys as it can into a given btree node, splitting it if full.
 * If a split occurred, this function will return early. This can only happen
 * for leaf nodes -- inserts into interior nodes have to be atomic.
 */
void bch2_btree_insert_node(struct btree_update *as, struct btree *b,
			    struct btree_iter *iter, struct keylist *keys,
			    unsigned flags)
{
	struct bch_fs *c = as->c;
	int old_u64s = le16_to_cpu(btree_bset_last(b)->u64s);
	int old_live_u64s = b->nr.live_u64s;
	int live_u64s_added, u64s_added;

	BUG_ON(!btree_node_intent_locked(iter, btree_node_root(c, b)->level));
	BUG_ON(!b->level);
	BUG_ON(!as || as->b);
	bch2_verify_keylist_sorted(keys);

	if (as->must_rewrite)
		goto split;

	bch2_btree_node_lock_for_insert(c, b, iter);

	if (!bch2_btree_node_insert_fits(c, b, bch_keylist_u64s(keys))) {
		bch2_btree_node_unlock_write(b, iter);
		goto split;
	}

	bch2_btree_insert_keys_interior(as, b, iter, keys);

	live_u64s_added = (int) b->nr.live_u64s - old_live_u64s;
	u64s_added = (int) le16_to_cpu(btree_bset_last(b)->u64s) - old_u64s;

	if (b->sib_u64s[0] != U16_MAX && live_u64s_added < 0)
		b->sib_u64s[0] = max(0, (int) b->sib_u64s[0] + live_u64s_added);
	if (b->sib_u64s[1] != U16_MAX && live_u64s_added < 0)
		b->sib_u64s[1] = max(0, (int) b->sib_u64s[1] + live_u64s_added);

	if (u64s_added > live_u64s_added &&
	    bch2_maybe_compact_whiteouts(c, b))
		bch2_btree_iter_reinit_node(iter, b);

	bch2_btree_node_unlock_write(b, iter);

	btree_node_interior_verify(b);

	bch2_foreground_maybe_merge(c, iter, b->level, flags);
	return;
split:
	btree_split(as, b, iter, keys, flags);
}

int bch2_btree_split_leaf(struct bch_fs *c, struct btree_iter *iter,
			  unsigned flags)
{
	struct btree *b = iter->l[0].b;
	struct btree_update *as;
	struct closure cl;
	int ret = 0;
	struct btree_iter *linked;

	/*
	 * We already have a disk reservation and open buckets pinned; this
	 * allocation must not block:
	 */
	for_each_btree_iter(iter, linked)
		if (linked->btree_id == BTREE_ID_EXTENTS)
			flags |= BTREE_INSERT_USE_RESERVE;

	closure_init_stack(&cl);

	/* Hack, because gc and splitting nodes doesn't mix yet: */
	if (!down_read_trylock(&c->gc_lock)) {
		if (flags & BTREE_INSERT_NOUNLOCK)
			return -EINTR;

		bch2_btree_iter_unlock(iter);
		down_read(&c->gc_lock);

		if (btree_iter_linked(iter))
			ret = -EINTR;
	}

	/*
	 * XXX: figure out how far we might need to split,
	 * instead of locking/reserving all the way to the root:
	 */
	if (!bch2_btree_iter_upgrade(iter, U8_MAX,
			!(flags & BTREE_INSERT_NOUNLOCK))) {
		ret = -EINTR;
		goto out;
	}

	as = bch2_btree_update_start(c, iter->btree_id,
		btree_update_reserve_required(c, b), flags,
		!(flags & BTREE_INSERT_NOUNLOCK) ? &cl : NULL);
	if (IS_ERR(as)) {
		ret = PTR_ERR(as);
		if (ret == -EAGAIN) {
			BUG_ON(flags & BTREE_INSERT_NOUNLOCK);
			bch2_btree_iter_unlock(iter);
			ret = -EINTR;
		}
		goto out;
	}

	btree_split(as, b, iter, NULL, flags);
	bch2_btree_update_done(as);

	/*
	 * We haven't successfully inserted yet, so don't downgrade all the way
	 * back to read locks;
	 */
	__bch2_btree_iter_downgrade(iter, 1);
out:
	up_read(&c->gc_lock);
	closure_sync(&cl);
	return ret;
}

void __bch2_foreground_maybe_merge(struct bch_fs *c,
				   struct btree_iter *iter,
				   unsigned level,
				   unsigned flags,
				   enum btree_node_sibling sib)
{
	struct btree_update *as;
	struct bkey_format_state new_s;
	struct bkey_format new_f;
	struct bkey_i delete;
	struct btree *b, *m, *n, *prev, *next, *parent;
	struct closure cl;
	size_t sib_u64s;
	int ret = 0;

	closure_init_stack(&cl);
retry:
	BUG_ON(!btree_node_locked(iter, level));

	b = iter->l[level].b;

	parent = btree_node_parent(iter, b);
	if (!parent)
		goto out;

	if (b->sib_u64s[sib] > BTREE_FOREGROUND_MERGE_THRESHOLD(c))
		goto out;

	/* XXX: can't be holding read locks */
	m = bch2_btree_node_get_sibling(c, iter, b,
			!(flags & BTREE_INSERT_NOUNLOCK), sib);
	if (IS_ERR(m)) {
		ret = PTR_ERR(m);
		goto err;
	}

	/* NULL means no sibling: */
	if (!m) {
		b->sib_u64s[sib] = U16_MAX;
		goto out;
	}

	if (sib == btree_prev_sib) {
		prev = m;
		next = b;
	} else {
		prev = b;
		next = m;
	}

	bch2_bkey_format_init(&new_s);
	__bch2_btree_calc_format(&new_s, b);
	__bch2_btree_calc_format(&new_s, m);
	new_f = bch2_bkey_format_done(&new_s);

	sib_u64s = btree_node_u64s_with_format(b, &new_f) +
		btree_node_u64s_with_format(m, &new_f);

	if (sib_u64s > BTREE_FOREGROUND_MERGE_HYSTERESIS(c)) {
		sib_u64s -= BTREE_FOREGROUND_MERGE_HYSTERESIS(c);
		sib_u64s /= 2;
		sib_u64s += BTREE_FOREGROUND_MERGE_HYSTERESIS(c);
	}

	sib_u64s = min(sib_u64s, btree_max_u64s(c));
	b->sib_u64s[sib] = sib_u64s;

	if (b->sib_u64s[sib] > BTREE_FOREGROUND_MERGE_THRESHOLD(c)) {
		six_unlock_intent(&m->lock);
		goto out;
	}

	/* We're changing btree topology, doesn't mix with gc: */
	if (!down_read_trylock(&c->gc_lock))
		goto err_cycle_gc_lock;

	if (!bch2_btree_iter_upgrade(iter, U8_MAX,
			!(flags & BTREE_INSERT_NOUNLOCK))) {
		ret = -EINTR;
		goto err_unlock;
	}

	as = bch2_btree_update_start(c, iter->btree_id,
			 btree_update_reserve_required(c, parent) + 1,
			 BTREE_INSERT_NOFAIL|
			 BTREE_INSERT_USE_RESERVE,
			 !(flags & BTREE_INSERT_NOUNLOCK) ? &cl : NULL);
	if (IS_ERR(as)) {
		ret = PTR_ERR(as);
		goto err_unlock;
	}

	trace_btree_merge(c, b);

	bch2_btree_interior_update_will_free_node(as, b);
	bch2_btree_interior_update_will_free_node(as, m);

	n = bch2_btree_node_alloc(as, b->level);

	n->data->min_key	= prev->data->min_key;
	n->data->max_key	= next->data->max_key;
	n->data->format		= new_f;
	n->key.k.p		= next->key.k.p;

	btree_node_set_format(n, new_f);

	bch2_btree_sort_into(c, n, prev);
	bch2_btree_sort_into(c, n, next);

	bch2_btree_build_aux_trees(n);
	six_unlock_write(&n->lock);

	bkey_init(&delete.k);
	delete.k.p = prev->key.k.p;
	bch2_keylist_add(&as->parent_keys, &delete);
	bch2_keylist_add(&as->parent_keys, &n->key);

	bch2_btree_node_write(c, n, SIX_LOCK_intent);

	bch2_btree_insert_node(as, parent, iter, &as->parent_keys, flags);

	bch2_btree_open_bucket_put(c, n);
	bch2_btree_node_free_inmem(c, b, iter);
	bch2_btree_node_free_inmem(c, m, iter);
	bch2_btree_iter_node_replace(iter, n);

	bch2_btree_iter_verify(iter, n);

	bch2_btree_update_done(as);

	six_unlock_intent(&m->lock);
	up_read(&c->gc_lock);
out:
	/*
	 * Don't downgrade locks here: we're called after successful insert,
	 * and the caller will downgrade locks after a successful insert
	 * anyways (in case e.g. a split was required first)
	 *
	 * And we're also called when inserting into interior nodes in the
	 * split path, and downgrading to read locks in there is potentially
	 * confusing:
	 */
	closure_sync(&cl);
	return;

err_cycle_gc_lock:
	six_unlock_intent(&m->lock);

	if (flags & BTREE_INSERT_NOUNLOCK)
		goto out;

	bch2_btree_iter_unlock(iter);

	down_read(&c->gc_lock);
	up_read(&c->gc_lock);
	ret = -EINTR;
	goto err;

err_unlock:
	six_unlock_intent(&m->lock);
	up_read(&c->gc_lock);
err:
	BUG_ON(ret == -EAGAIN && (flags & BTREE_INSERT_NOUNLOCK));

	if ((ret == -EAGAIN || ret == -EINTR) &&
	    !(flags & BTREE_INSERT_NOUNLOCK)) {
		bch2_btree_iter_unlock(iter);
		closure_sync(&cl);
		ret = bch2_btree_iter_traverse(iter);
		if (ret)
			goto out;

		goto retry;
	}

	goto out;
}

static int __btree_node_rewrite(struct bch_fs *c, struct btree_iter *iter,
				struct btree *b, unsigned flags,
				struct closure *cl)
{
	struct btree *n, *parent = btree_node_parent(iter, b);
	struct btree_update *as;

	as = bch2_btree_update_start(c, iter->btree_id,
		(parent
		 ? btree_update_reserve_required(c, parent)
		 : 0) + 1,
		flags, cl);
	if (IS_ERR(as)) {
		trace_btree_gc_rewrite_node_fail(c, b);
		return PTR_ERR(as);
	}

	bch2_btree_interior_update_will_free_node(as, b);

	n = bch2_btree_node_alloc_replacement(as, b);

	bch2_btree_build_aux_trees(n);
	six_unlock_write(&n->lock);

	trace_btree_gc_rewrite_node(c, b);

	bch2_btree_node_write(c, n, SIX_LOCK_intent);

	if (parent) {
		bch2_keylist_add(&as->parent_keys, &n->key);
		bch2_btree_insert_node(as, parent, iter, &as->parent_keys, flags);
	} else {
		bch2_btree_set_root(as, n, iter);
	}

	bch2_btree_open_bucket_put(c, n);

	bch2_btree_node_free_inmem(c, b, iter);

	bch2_btree_iter_node_replace(iter, n);

	bch2_btree_update_done(as);
	return 0;
}

/**
 * bch_btree_node_rewrite - Rewrite/move a btree node
 *
 * Returns 0 on success, -EINTR or -EAGAIN on failure (i.e.
 * btree_check_reserve() has to wait)
 */
int bch2_btree_node_rewrite(struct bch_fs *c, struct btree_iter *iter,
			    __le64 seq, unsigned flags)
{
	struct closure cl;
	struct btree *b;
	int ret;

	flags |= BTREE_INSERT_NOFAIL;

	closure_init_stack(&cl);

	bch2_btree_iter_upgrade(iter, U8_MAX, true);

	if (!(flags & BTREE_INSERT_GC_LOCK_HELD)) {
		if (!down_read_trylock(&c->gc_lock)) {
			bch2_btree_iter_unlock(iter);
			down_read(&c->gc_lock);
		}
	}

	while (1) {
		ret = bch2_btree_iter_traverse(iter);
		if (ret)
			break;

		b = bch2_btree_iter_peek_node(iter);
		if (!b || b->data->keys.seq != seq)
			break;

		ret = __btree_node_rewrite(c, iter, b, flags, &cl);
		if (ret != -EAGAIN &&
		    ret != -EINTR)
			break;

		bch2_btree_iter_unlock(iter);
		closure_sync(&cl);
	}

	bch2_btree_iter_downgrade(iter);

	if (!(flags & BTREE_INSERT_GC_LOCK_HELD))
		up_read(&c->gc_lock);

	closure_sync(&cl);
	return ret;
}

static void __bch2_btree_node_update_key(struct bch_fs *c,
					 struct btree_update *as,
					 struct btree_iter *iter,
					 struct btree *b, struct btree *new_hash,
					 struct bkey_i_extent *new_key)
{
	struct btree *parent;
	int ret;

	/*
	 * Two corner cases that need to be thought about here:
	 *
	 * @b may not be reachable yet - there might be another interior update
	 * operation waiting on @b to be written, and we're gonna deliver the
	 * write completion to that interior update operation _before_
	 * persisting the new_key update
	 *
	 * That ends up working without us having to do anything special here:
	 * the reason is, we do kick off (and do the in memory updates) for the
	 * update for @new_key before we return, creating a new interior_update
	 * operation here.
	 *
	 * The new interior update operation here will in effect override the
	 * previous one. The previous one was going to terminate - make @b
	 * reachable - in one of two ways:
	 * - updating the btree root pointer
	 *   In that case,
	 *   no, this doesn't work. argh.
	 */

	if (b->will_make_reachable)
		as->must_rewrite = true;

	btree_interior_update_add_node_reference(as, b);

	parent = btree_node_parent(iter, b);
	if (parent) {
		if (new_hash) {
			bkey_copy(&new_hash->key, &new_key->k_i);
			ret = bch2_btree_node_hash_insert(&c->btree_cache,
					new_hash, b->level, b->btree_id);
			BUG_ON(ret);
		}

		bch2_keylist_add(&as->parent_keys, &new_key->k_i);
		bch2_btree_insert_node(as, parent, iter, &as->parent_keys, 0);

		if (new_hash) {
			mutex_lock(&c->btree_cache.lock);
			bch2_btree_node_hash_remove(&c->btree_cache, new_hash);

			bch2_btree_node_hash_remove(&c->btree_cache, b);

			bkey_copy(&b->key, &new_key->k_i);
			ret = __bch2_btree_node_hash_insert(&c->btree_cache, b);
			BUG_ON(ret);
			mutex_unlock(&c->btree_cache.lock);
		} else {
			bkey_copy(&b->key, &new_key->k_i);
		}
	} else {
		struct bch_fs_usage stats = { 0 };

		BUG_ON(btree_node_root(c, b) != b);

		bch2_btree_node_lock_write(b, iter);

		bch2_mark_key(c, bkey_i_to_s_c(&new_key->k_i),
			      c->opts.btree_node_size, BCH_DATA_BTREE,
			      gc_pos_btree_root(b->btree_id),
			      &stats, 0, 0);
		bch2_btree_node_free_index(as, NULL,
					   bkey_i_to_s_c(&b->key),
					   &stats);
		bch2_fs_usage_apply(c, &stats, &as->reserve->disk_res,
				    gc_pos_btree_root(b->btree_id));

		if (PTR_HASH(&new_key->k_i) != PTR_HASH(&b->key)) {
			mutex_lock(&c->btree_cache.lock);
			bch2_btree_node_hash_remove(&c->btree_cache, b);

			bkey_copy(&b->key, &new_key->k_i);
			ret = __bch2_btree_node_hash_insert(&c->btree_cache, b);
			BUG_ON(ret);
			mutex_unlock(&c->btree_cache.lock);
		} else {
			bkey_copy(&b->key, &new_key->k_i);
		}

		btree_update_updated_root(as);
		bch2_btree_node_unlock_write(b, iter);
	}

	bch2_btree_update_done(as);
}

int bch2_btree_node_update_key(struct bch_fs *c, struct btree_iter *iter,
			       struct btree *b, struct bkey_i_extent *new_key)
{
	struct btree *parent = btree_node_parent(iter, b);
	struct btree_update *as = NULL;
	struct btree *new_hash = NULL;
	struct closure cl;
	int ret;

	closure_init_stack(&cl);

	if (!bch2_btree_iter_upgrade(iter, U8_MAX, true))
		return -EINTR;

	if (!down_read_trylock(&c->gc_lock)) {
		bch2_btree_iter_unlock(iter);
		down_read(&c->gc_lock);

		if (!bch2_btree_iter_relock(iter)) {
			ret = -EINTR;
			goto err;
		}
	}

	/* check PTR_HASH() after @b is locked by btree_iter_traverse(): */
	if (PTR_HASH(&new_key->k_i) != PTR_HASH(&b->key)) {
		/* bch2_btree_reserve_get will unlock */
		ret = bch2_btree_cache_cannibalize_lock(c, &cl);
		if (ret) {
			ret = -EINTR;

			bch2_btree_iter_unlock(iter);
			up_read(&c->gc_lock);
			closure_sync(&cl);
			down_read(&c->gc_lock);

			if (!bch2_btree_iter_relock(iter))
				goto err;
		}

		new_hash = bch2_btree_node_mem_alloc(c);
	}

	as = bch2_btree_update_start(c, iter->btree_id,
		parent ? btree_update_reserve_required(c, parent) : 0,
		BTREE_INSERT_NOFAIL|
		BTREE_INSERT_USE_RESERVE|
		BTREE_INSERT_USE_ALLOC_RESERVE,
		&cl);

	if (IS_ERR(as)) {
		ret = PTR_ERR(as);
		if (ret == -EAGAIN)
			ret = -EINTR;

		if (ret != -EINTR)
			goto err;

		bch2_btree_iter_unlock(iter);
		up_read(&c->gc_lock);
		closure_sync(&cl);
		down_read(&c->gc_lock);

		if (!bch2_btree_iter_relock(iter))
			goto err;
	}

	ret = bch2_mark_bkey_replicas(c, BCH_DATA_BTREE,
				      extent_i_to_s_c(new_key).s_c);
	if (ret)
		goto err_free_update;

	__bch2_btree_node_update_key(c, as, iter, b, new_hash, new_key);

	bch2_btree_iter_downgrade(iter);
err:
	if (new_hash) {
		mutex_lock(&c->btree_cache.lock);
		list_move(&new_hash->list, &c->btree_cache.freeable);
		mutex_unlock(&c->btree_cache.lock);

		six_unlock_write(&new_hash->lock);
		six_unlock_intent(&new_hash->lock);
	}
	up_read(&c->gc_lock);
	closure_sync(&cl);
	return ret;
err_free_update:
	bch2_btree_update_free(as);
	goto err;
}

/* Init code: */

/*
 * Only for filesystem bringup, when first reading the btree roots or allocating
 * btree roots when initializing a new filesystem:
 */
void bch2_btree_set_root_for_read(struct bch_fs *c, struct btree *b)
{
	BUG_ON(btree_node_root(c, b));

	__bch2_btree_set_root_inmem(c, b);
	bch2_btree_set_root_ondisk(c, b, READ);
}

void bch2_btree_root_alloc(struct bch_fs *c, enum btree_id id)
{
	struct closure cl;
	struct btree *b;
	int ret;

	closure_init_stack(&cl);

	do {
		ret = bch2_btree_cache_cannibalize_lock(c, &cl);
		closure_sync(&cl);
	} while (ret);

	b = bch2_btree_node_mem_alloc(c);
	bch2_btree_cache_cannibalize_unlock(c);

	set_btree_node_fake(b);
	b->level	= 0;
	b->btree_id	= id;

	bkey_extent_init(&b->key);
	b->key.k.p = POS_MAX;
	bkey_i_to_extent(&b->key)->v._data[0] = U64_MAX - id;

	bch2_bset_init_first(b, &b->data->keys);
	bch2_btree_build_aux_trees(b);

	b->data->min_key = POS_MIN;
	b->data->max_key = POS_MAX;
	b->data->format = bch2_btree_calc_format(b);
	btree_node_set_format(b, b->data->format);

	ret = bch2_btree_node_hash_insert(&c->btree_cache, b, b->level, b->btree_id);
	BUG_ON(ret);

	__bch2_btree_set_root_inmem(c, b);

	six_unlock_write(&b->lock);
	six_unlock_intent(&b->lock);
}

ssize_t bch2_btree_updates_print(struct bch_fs *c, char *buf)
{
	char *out = buf, *end = buf + PAGE_SIZE;
	struct btree_update *as;

	mutex_lock(&c->btree_interior_update_lock);
	list_for_each_entry(as, &c->btree_interior_update_list, list)
		out += scnprintf(out, end - out, "%p m %u w %u r %u j %llu\n",
				 as,
				 as->mode,
				 as->nodes_written,
				 atomic_read(&as->cl.remaining) & CLOSURE_REMAINING_MASK,
				 as->journal.seq);
	mutex_unlock(&c->btree_interior_update_lock);

	return out - buf;
}

size_t bch2_btree_interior_updates_nr_pending(struct bch_fs *c)
{
	size_t ret = 0;
	struct list_head *i;

	mutex_lock(&c->btree_interior_update_lock);
	list_for_each(i, &c->btree_interior_update_list)
		ret++;
	mutex_unlock(&c->btree_interior_update_lock);

	return ret;
}
