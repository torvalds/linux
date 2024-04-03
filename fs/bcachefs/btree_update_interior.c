// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "bkey_methods.h"
#include "btree_cache.h"
#include "btree_gc.h"
#include "btree_journal_iter.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_io.h"
#include "btree_iter.h"
#include "btree_locking.h"
#include "buckets.h"
#include "clock.h"
#include "error.h"
#include "extents.h"
#include "journal.h"
#include "journal_reclaim.h"
#include "keylist.h"
#include "replicas.h"
#include "super-io.h"
#include "trace.h"

#include <linux/random.h>

static int bch2_btree_insert_node(struct btree_update *, struct btree_trans *,
				  btree_path_idx_t, struct btree *, struct keylist *);
static void bch2_btree_update_add_new_node(struct btree_update *, struct btree *);

static btree_path_idx_t get_unlocked_mut_path(struct btree_trans *trans,
					      enum btree_id btree_id,
					      unsigned level,
					      struct bpos pos)
{
	btree_path_idx_t path_idx = bch2_path_get(trans, btree_id, pos, level + 1, level,
			     BTREE_ITER_NOPRESERVE|
			     BTREE_ITER_INTENT, _RET_IP_);
	path_idx = bch2_btree_path_make_mut(trans, path_idx, true, _RET_IP_);

	struct btree_path *path = trans->paths + path_idx;
	bch2_btree_path_downgrade(trans, path);
	__bch2_btree_path_unlock(trans, path);
	return path_idx;
}

/* Debug code: */

/*
 * Verify that child nodes correctly span parent node's range:
 */
static void btree_node_interior_verify(struct bch_fs *c, struct btree *b)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	struct bpos next_node = b->data->min_key;
	struct btree_node_iter iter;
	struct bkey_s_c k;
	struct bkey_s_c_btree_ptr_v2 bp;
	struct bkey unpacked;
	struct printbuf buf1 = PRINTBUF, buf2 = PRINTBUF;

	BUG_ON(!b->c.level);

	if (!test_bit(JOURNAL_REPLAY_DONE, &c->journal.flags))
		return;

	bch2_btree_node_iter_init_from_start(&iter, b);

	while (1) {
		k = bch2_btree_node_iter_peek_unpack(&iter, b, &unpacked);
		if (k.k->type != KEY_TYPE_btree_ptr_v2)
			break;
		bp = bkey_s_c_to_btree_ptr_v2(k);

		if (!bpos_eq(next_node, bp.v->min_key)) {
			bch2_dump_btree_node(c, b);
			bch2_bpos_to_text(&buf1, next_node);
			bch2_bpos_to_text(&buf2, bp.v->min_key);
			panic("expected next min_key %s got %s\n", buf1.buf, buf2.buf);
		}

		bch2_btree_node_iter_advance(&iter, b);

		if (bch2_btree_node_iter_end(&iter)) {
			if (!bpos_eq(k.k->p, b->key.k.p)) {
				bch2_dump_btree_node(c, b);
				bch2_bpos_to_text(&buf1, b->key.k.p);
				bch2_bpos_to_text(&buf2, k.k->p);
				panic("expected end %s got %s\n", buf1.buf, buf2.buf);
			}
			break;
		}

		next_node = bpos_successor(k.k->p);
	}
#endif
}

/* Calculate ideal packed bkey format for new btree nodes: */

static void __bch2_btree_calc_format(struct bkey_format_state *s, struct btree *b)
{
	struct bkey_packed *k;
	struct bset_tree *t;
	struct bkey uk;

	for_each_bset(b, t)
		bset_tree_for_each_key(b, t, k)
			if (!bkey_deleted(k)) {
				uk = bkey_unpack_key(b, k);
				bch2_bkey_format_add_key(s, &uk);
			}
}

static struct bkey_format bch2_btree_calc_format(struct btree *b)
{
	struct bkey_format_state s;

	bch2_bkey_format_init(&s);
	bch2_bkey_format_add_pos(&s, b->data->min_key);
	bch2_bkey_format_add_pos(&s, b->data->max_key);
	__bch2_btree_calc_format(&s, b);

	return bch2_bkey_format_done(&s);
}

static size_t btree_node_u64s_with_format(struct btree_nr_keys nr,
					  struct bkey_format *old_f,
					  struct bkey_format *new_f)
{
	/* stupid integer promotion rules */
	ssize_t delta =
	    (((int) new_f->key_u64s - old_f->key_u64s) *
	     (int) nr.packed_keys) +
	    (((int) new_f->key_u64s - BKEY_U64s) *
	     (int) nr.unpacked_keys);

	BUG_ON(delta + nr.live_u64s < 0);

	return nr.live_u64s + delta;
}

/**
 * bch2_btree_node_format_fits - check if we could rewrite node with a new format
 *
 * @c:		filesystem handle
 * @b:		btree node to rewrite
 * @nr:		number of keys for new node (i.e. b->nr)
 * @new_f:	bkey format to translate keys to
 *
 * Returns: true if all re-packed keys will be able to fit in a new node.
 *
 * Assumes all keys will successfully pack with the new format.
 */
static bool bch2_btree_node_format_fits(struct bch_fs *c, struct btree *b,
				 struct btree_nr_keys nr,
				 struct bkey_format *new_f)
{
	size_t u64s = btree_node_u64s_with_format(nr, &b->format, new_f);

	return __vstruct_bytes(struct btree_node, u64s) < btree_buf_bytes(b);
}

/* Btree node freeing/allocation: */

static void __btree_node_free(struct btree_trans *trans, struct btree *b)
{
	struct bch_fs *c = trans->c;

	trace_and_count(c, btree_node_free, trans, b);

	BUG_ON(btree_node_write_blocked(b));
	BUG_ON(btree_node_dirty(b));
	BUG_ON(btree_node_need_write(b));
	BUG_ON(b == btree_node_root(c, b));
	BUG_ON(b->ob.nr);
	BUG_ON(!list_empty(&b->write_blocked));
	BUG_ON(b->will_make_reachable);

	clear_btree_node_noevict(b);

	mutex_lock(&c->btree_cache.lock);
	list_move(&b->list, &c->btree_cache.freeable);
	mutex_unlock(&c->btree_cache.lock);
}

static void bch2_btree_node_free_inmem(struct btree_trans *trans,
				       struct btree_path *path,
				       struct btree *b)
{
	struct bch_fs *c = trans->c;
	unsigned i, level = b->c.level;

	bch2_btree_node_lock_write_nofail(trans, path, &b->c);
	bch2_btree_node_hash_remove(&c->btree_cache, b);
	__btree_node_free(trans, b);
	six_unlock_write(&b->c.lock);
	mark_btree_node_locked_noreset(path, level, BTREE_NODE_INTENT_LOCKED);

	trans_for_each_path(trans, path, i)
		if (path->l[level].b == b) {
			btree_node_unlock(trans, path, level);
			path->l[level].b = ERR_PTR(-BCH_ERR_no_btree_node_init);
		}
}

static void bch2_btree_node_free_never_used(struct btree_update *as,
					    struct btree_trans *trans,
					    struct btree *b)
{
	struct bch_fs *c = as->c;
	struct prealloc_nodes *p = &as->prealloc_nodes[b->c.lock.readers != NULL];
	struct btree_path *path;
	unsigned i, level = b->c.level;

	BUG_ON(!list_empty(&b->write_blocked));
	BUG_ON(b->will_make_reachable != (1UL|(unsigned long) as));

	b->will_make_reachable = 0;
	closure_put(&as->cl);

	clear_btree_node_will_make_reachable(b);
	clear_btree_node_accessed(b);
	clear_btree_node_dirty_acct(c, b);
	clear_btree_node_need_write(b);

	mutex_lock(&c->btree_cache.lock);
	list_del_init(&b->list);
	bch2_btree_node_hash_remove(&c->btree_cache, b);
	mutex_unlock(&c->btree_cache.lock);

	BUG_ON(p->nr >= ARRAY_SIZE(p->b));
	p->b[p->nr++] = b;

	six_unlock_intent(&b->c.lock);

	trans_for_each_path(trans, path, i)
		if (path->l[level].b == b) {
			btree_node_unlock(trans, path, level);
			path->l[level].b = ERR_PTR(-BCH_ERR_no_btree_node_init);
		}
}

static struct btree *__bch2_btree_node_alloc(struct btree_trans *trans,
					     struct disk_reservation *res,
					     struct closure *cl,
					     bool interior_node,
					     unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct write_point *wp;
	struct btree *b;
	BKEY_PADDED_ONSTACK(k, BKEY_BTREE_PTR_VAL_U64s_MAX) tmp;
	struct open_buckets obs = { .nr = 0 };
	struct bch_devs_list devs_have = (struct bch_devs_list) { 0 };
	enum bch_watermark watermark = flags & BCH_WATERMARK_MASK;
	unsigned nr_reserve = watermark > BCH_WATERMARK_reclaim
		? BTREE_NODE_RESERVE
		: 0;
	int ret;

	mutex_lock(&c->btree_reserve_cache_lock);
	if (c->btree_reserve_cache_nr > nr_reserve) {
		struct btree_alloc *a =
			&c->btree_reserve_cache[--c->btree_reserve_cache_nr];

		obs = a->ob;
		bkey_copy(&tmp.k, &a->k);
		mutex_unlock(&c->btree_reserve_cache_lock);
		goto mem_alloc;
	}
	mutex_unlock(&c->btree_reserve_cache_lock);

retry:
	ret = bch2_alloc_sectors_start_trans(trans,
				      c->opts.metadata_target ?:
				      c->opts.foreground_target,
				      0,
				      writepoint_ptr(&c->btree_write_point),
				      &devs_have,
				      res->nr_replicas,
				      min(res->nr_replicas,
					  c->opts.metadata_replicas_required),
				      watermark, 0, cl, &wp);
	if (unlikely(ret))
		return ERR_PTR(ret);

	if (wp->sectors_free < btree_sectors(c)) {
		struct open_bucket *ob;
		unsigned i;

		open_bucket_for_each(c, &wp->ptrs, ob, i)
			if (ob->sectors_free < btree_sectors(c))
				ob->sectors_free = 0;

		bch2_alloc_sectors_done(c, wp);
		goto retry;
	}

	bkey_btree_ptr_v2_init(&tmp.k);
	bch2_alloc_sectors_append_ptrs(c, wp, &tmp.k, btree_sectors(c), false);

	bch2_open_bucket_get(c, wp, &obs);
	bch2_alloc_sectors_done(c, wp);
mem_alloc:
	b = bch2_btree_node_mem_alloc(trans, interior_node);
	six_unlock_write(&b->c.lock);
	six_unlock_intent(&b->c.lock);

	/* we hold cannibalize_lock: */
	BUG_ON(IS_ERR(b));
	BUG_ON(b->ob.nr);

	bkey_copy(&b->key, &tmp.k);
	b->ob = obs;

	return b;
}

static struct btree *bch2_btree_node_alloc(struct btree_update *as,
					   struct btree_trans *trans,
					   unsigned level)
{
	struct bch_fs *c = as->c;
	struct btree *b;
	struct prealloc_nodes *p = &as->prealloc_nodes[!!level];
	int ret;

	BUG_ON(level >= BTREE_MAX_DEPTH);
	BUG_ON(!p->nr);

	b = p->b[--p->nr];

	btree_node_lock_nopath_nofail(trans, &b->c, SIX_LOCK_intent);
	btree_node_lock_nopath_nofail(trans, &b->c, SIX_LOCK_write);

	set_btree_node_accessed(b);
	set_btree_node_dirty_acct(c, b);
	set_btree_node_need_write(b);

	bch2_bset_init_first(b, &b->data->keys);
	b->c.level	= level;
	b->c.btree_id	= as->btree_id;
	b->version_ondisk = c->sb.version;

	memset(&b->nr, 0, sizeof(b->nr));
	b->data->magic = cpu_to_le64(bset_magic(c));
	memset(&b->data->_ptr, 0, sizeof(b->data->_ptr));
	b->data->flags = 0;
	SET_BTREE_NODE_ID(b->data, as->btree_id);
	SET_BTREE_NODE_LEVEL(b->data, level);

	if (b->key.k.type == KEY_TYPE_btree_ptr_v2) {
		struct bkey_i_btree_ptr_v2 *bp = bkey_i_to_btree_ptr_v2(&b->key);

		bp->v.mem_ptr		= 0;
		bp->v.seq		= b->data->keys.seq;
		bp->v.sectors_written	= 0;
	}

	SET_BTREE_NODE_NEW_EXTENT_OVERWRITE(b->data, true);

	bch2_btree_build_aux_trees(b);

	ret = bch2_btree_node_hash_insert(&c->btree_cache, b, level, as->btree_id);
	BUG_ON(ret);

	trace_and_count(c, btree_node_alloc, trans, b);
	bch2_increment_clock(c, btree_sectors(c), WRITE);
	return b;
}

static void btree_set_min(struct btree *b, struct bpos pos)
{
	if (b->key.k.type == KEY_TYPE_btree_ptr_v2)
		bkey_i_to_btree_ptr_v2(&b->key)->v.min_key = pos;
	b->data->min_key = pos;
}

static void btree_set_max(struct btree *b, struct bpos pos)
{
	b->key.k.p = pos;
	b->data->max_key = pos;
}

static struct btree *bch2_btree_node_alloc_replacement(struct btree_update *as,
						       struct btree_trans *trans,
						       struct btree *b)
{
	struct btree *n = bch2_btree_node_alloc(as, trans, b->c.level);
	struct bkey_format format = bch2_btree_calc_format(b);

	/*
	 * The keys might expand with the new format - if they wouldn't fit in
	 * the btree node anymore, use the old format for now:
	 */
	if (!bch2_btree_node_format_fits(as->c, b, b->nr, &format))
		format = b->format;

	SET_BTREE_NODE_SEQ(n->data, BTREE_NODE_SEQ(b->data) + 1);

	btree_set_min(n, b->data->min_key);
	btree_set_max(n, b->data->max_key);

	n->data->format		= format;
	btree_node_set_format(n, format);

	bch2_btree_sort_into(as->c, n, b);

	btree_node_reset_sib_u64s(n);
	return n;
}

static struct btree *__btree_root_alloc(struct btree_update *as,
				struct btree_trans *trans, unsigned level)
{
	struct btree *b = bch2_btree_node_alloc(as, trans, level);

	btree_set_min(b, POS_MIN);
	btree_set_max(b, SPOS_MAX);
	b->data->format = bch2_btree_calc_format(b);

	btree_node_set_format(b, b->data->format);
	bch2_btree_build_aux_trees(b);

	return b;
}

static void bch2_btree_reserve_put(struct btree_update *as, struct btree_trans *trans)
{
	struct bch_fs *c = as->c;
	struct prealloc_nodes *p;

	for (p = as->prealloc_nodes;
	     p < as->prealloc_nodes + ARRAY_SIZE(as->prealloc_nodes);
	     p++) {
		while (p->nr) {
			struct btree *b = p->b[--p->nr];

			mutex_lock(&c->btree_reserve_cache_lock);

			if (c->btree_reserve_cache_nr <
			    ARRAY_SIZE(c->btree_reserve_cache)) {
				struct btree_alloc *a =
					&c->btree_reserve_cache[c->btree_reserve_cache_nr++];

				a->ob = b->ob;
				b->ob.nr = 0;
				bkey_copy(&a->k, &b->key);
			} else {
				bch2_open_buckets_put(c, &b->ob);
			}

			mutex_unlock(&c->btree_reserve_cache_lock);

			btree_node_lock_nopath_nofail(trans, &b->c, SIX_LOCK_intent);
			btree_node_lock_nopath_nofail(trans, &b->c, SIX_LOCK_write);
			__btree_node_free(trans, b);
			six_unlock_write(&b->c.lock);
			six_unlock_intent(&b->c.lock);
		}
	}
}

static int bch2_btree_reserve_get(struct btree_trans *trans,
				  struct btree_update *as,
				  unsigned nr_nodes[2],
				  unsigned flags,
				  struct closure *cl)
{
	struct btree *b;
	unsigned interior;
	int ret = 0;

	BUG_ON(nr_nodes[0] + nr_nodes[1] > BTREE_RESERVE_MAX);

	/*
	 * Protects reaping from the btree node cache and using the btree node
	 * open bucket reserve:
	 */
	ret = bch2_btree_cache_cannibalize_lock(trans, cl);
	if (ret)
		return ret;

	for (interior = 0; interior < 2; interior++) {
		struct prealloc_nodes *p = as->prealloc_nodes + interior;

		while (p->nr < nr_nodes[interior]) {
			b = __bch2_btree_node_alloc(trans, &as->disk_res, cl,
						    interior, flags);
			if (IS_ERR(b)) {
				ret = PTR_ERR(b);
				goto err;
			}

			p->b[p->nr++] = b;
		}
	}
err:
	bch2_btree_cache_cannibalize_unlock(trans);
	return ret;
}

/* Asynchronous interior node update machinery */

static void bch2_btree_update_free(struct btree_update *as, struct btree_trans *trans)
{
	struct bch_fs *c = as->c;

	if (as->took_gc_lock)
		up_read(&c->gc_lock);
	as->took_gc_lock = false;

	bch2_journal_pin_drop(&c->journal, &as->journal);
	bch2_journal_pin_flush(&c->journal, &as->journal);
	bch2_disk_reservation_put(c, &as->disk_res);
	bch2_btree_reserve_put(as, trans);

	bch2_time_stats_update(&c->times[BCH_TIME_btree_interior_update_total],
			       as->start_time);

	mutex_lock(&c->btree_interior_update_lock);
	list_del(&as->unwritten_list);
	list_del(&as->list);

	closure_debug_destroy(&as->cl);
	mempool_free(as, &c->btree_interior_update_pool);

	/*
	 * Have to do the wakeup with btree_interior_update_lock still held,
	 * since being on btree_interior_update_list is our ref on @c:
	 */
	closure_wake_up(&c->btree_interior_update_wait);

	mutex_unlock(&c->btree_interior_update_lock);
}

static void btree_update_add_key(struct btree_update *as,
				 struct keylist *keys, struct btree *b)
{
	struct bkey_i *k = &b->key;

	BUG_ON(bch2_keylist_u64s(keys) + k->k.u64s >
	       ARRAY_SIZE(as->_old_keys));

	bkey_copy(keys->top, k);
	bkey_i_to_btree_ptr_v2(keys->top)->v.mem_ptr = b->c.level + 1;

	bch2_keylist_push(keys);
}

/*
 * The transactional part of an interior btree node update, where we journal the
 * update we did to the interior node and update alloc info:
 */
static int btree_update_nodes_written_trans(struct btree_trans *trans,
					    struct btree_update *as)
{
	struct jset_entry *e = bch2_trans_jset_entry_alloc(trans, as->journal_u64s);
	int ret = PTR_ERR_OR_ZERO(e);
	if (ret)
		return ret;

	memcpy(e, as->journal_entries, as->journal_u64s * sizeof(u64));

	trans->journal_pin = &as->journal;

	for_each_keylist_key(&as->old_keys, k) {
		unsigned level = bkey_i_to_btree_ptr_v2(k)->v.mem_ptr;

		ret = bch2_key_trigger_old(trans, as->btree_id, level, bkey_i_to_s_c(k),
					   BTREE_TRIGGER_TRANSACTIONAL);
		if (ret)
			return ret;
	}

	for_each_keylist_key(&as->new_keys, k) {
		unsigned level = bkey_i_to_btree_ptr_v2(k)->v.mem_ptr;

		ret = bch2_key_trigger_new(trans, as->btree_id, level, bkey_i_to_s(k),
					   BTREE_TRIGGER_TRANSACTIONAL);
		if (ret)
			return ret;
	}

	return 0;
}

static void btree_update_nodes_written(struct btree_update *as)
{
	struct bch_fs *c = as->c;
	struct btree *b;
	struct btree_trans *trans = bch2_trans_get(c);
	u64 journal_seq = 0;
	unsigned i;
	int ret;

	/*
	 * If we're already in an error state, it might be because a btree node
	 * was never written, and we might be trying to free that same btree
	 * node here, but it won't have been marked as allocated and we'll see
	 * spurious disk usage inconsistencies in the transactional part below
	 * if we don't skip it:
	 */
	ret = bch2_journal_error(&c->journal);
	if (ret)
		goto err;

	/*
	 * Wait for any in flight writes to finish before we free the old nodes
	 * on disk:
	 */
	for (i = 0; i < as->nr_old_nodes; i++) {
		__le64 seq;

		b = as->old_nodes[i];

		btree_node_lock_nopath_nofail(trans, &b->c, SIX_LOCK_read);
		seq = b->data ? b->data->keys.seq : 0;
		six_unlock_read(&b->c.lock);

		if (seq == as->old_nodes_seq[i])
			wait_on_bit_io(&b->flags, BTREE_NODE_write_in_flight_inner,
				       TASK_UNINTERRUPTIBLE);
	}

	/*
	 * We did an update to a parent node where the pointers we added pointed
	 * to child nodes that weren't written yet: now, the child nodes have
	 * been written so we can write out the update to the interior node.
	 */

	/*
	 * We can't call into journal reclaim here: we'd block on the journal
	 * reclaim lock, but we may need to release the open buckets we have
	 * pinned in order for other btree updates to make forward progress, and
	 * journal reclaim does btree updates when flushing bkey_cached entries,
	 * which may require allocations as well.
	 */
	ret = commit_do(trans, &as->disk_res, &journal_seq,
			BCH_WATERMARK_reclaim|
			BCH_TRANS_COMMIT_no_enospc|
			BCH_TRANS_COMMIT_no_check_rw|
			BCH_TRANS_COMMIT_journal_reclaim,
			btree_update_nodes_written_trans(trans, as));
	bch2_trans_unlock(trans);

	bch2_fs_fatal_err_on(ret && !bch2_journal_error(&c->journal), c,
			     "%s", bch2_err_str(ret));
err:
	if (as->b) {

		b = as->b;
		btree_path_idx_t path_idx = get_unlocked_mut_path(trans,
						as->btree_id, b->c.level, b->key.k.p);
		struct btree_path *path = trans->paths + path_idx;
		/*
		 * @b is the node we did the final insert into:
		 *
		 * On failure to get a journal reservation, we still have to
		 * unblock the write and allow most of the write path to happen
		 * so that shutdown works, but the i->journal_seq mechanism
		 * won't work to prevent the btree write from being visible (we
		 * didn't get a journal sequence number) - instead
		 * __bch2_btree_node_write() doesn't do the actual write if
		 * we're in journal error state:
		 */

		/*
		 * Ensure transaction is unlocked before using
		 * btree_node_lock_nopath() (the use of which is always suspect,
		 * we need to work on removing this in the future)
		 *
		 * It should be, but get_unlocked_mut_path() -> bch2_path_get()
		 * calls bch2_path_upgrade(), before we call path_make_mut(), so
		 * we may rarely end up with a locked path besides the one we
		 * have here:
		 */
		bch2_trans_unlock(trans);
		btree_node_lock_nopath_nofail(trans, &b->c, SIX_LOCK_intent);
		mark_btree_node_locked(trans, path, b->c.level, BTREE_NODE_INTENT_LOCKED);
		path->l[b->c.level].lock_seq = six_lock_seq(&b->c.lock);
		path->l[b->c.level].b = b;

		bch2_btree_node_lock_write_nofail(trans, path, &b->c);

		mutex_lock(&c->btree_interior_update_lock);

		list_del(&as->write_blocked_list);
		if (list_empty(&b->write_blocked))
			clear_btree_node_write_blocked(b);

		/*
		 * Node might have been freed, recheck under
		 * btree_interior_update_lock:
		 */
		if (as->b == b) {
			BUG_ON(!b->c.level);
			BUG_ON(!btree_node_dirty(b));

			if (!ret) {
				struct bset *last = btree_bset_last(b);

				last->journal_seq = cpu_to_le64(
							     max(journal_seq,
								 le64_to_cpu(last->journal_seq)));

				bch2_btree_add_journal_pin(c, b, journal_seq);
			} else {
				/*
				 * If we didn't get a journal sequence number we
				 * can't write this btree node, because recovery
				 * won't know to ignore this write:
				 */
				set_btree_node_never_write(b);
			}
		}

		mutex_unlock(&c->btree_interior_update_lock);

		mark_btree_node_locked_noreset(path, b->c.level, BTREE_NODE_INTENT_LOCKED);
		six_unlock_write(&b->c.lock);

		btree_node_write_if_need(c, b, SIX_LOCK_intent);
		btree_node_unlock(trans, path, b->c.level);
		bch2_path_put(trans, path_idx, true);
	}

	bch2_journal_pin_drop(&c->journal, &as->journal);

	mutex_lock(&c->btree_interior_update_lock);
	for (i = 0; i < as->nr_new_nodes; i++) {
		b = as->new_nodes[i];

		BUG_ON(b->will_make_reachable != (unsigned long) as);
		b->will_make_reachable = 0;
		clear_btree_node_will_make_reachable(b);
	}
	mutex_unlock(&c->btree_interior_update_lock);

	for (i = 0; i < as->nr_new_nodes; i++) {
		b = as->new_nodes[i];

		btree_node_lock_nopath_nofail(trans, &b->c, SIX_LOCK_read);
		btree_node_write_if_need(c, b, SIX_LOCK_read);
		six_unlock_read(&b->c.lock);
	}

	for (i = 0; i < as->nr_open_buckets; i++)
		bch2_open_bucket_put(c, c->open_buckets + as->open_buckets[i]);

	bch2_btree_update_free(as, trans);
	bch2_trans_put(trans);
}

static void btree_interior_update_work(struct work_struct *work)
{
	struct bch_fs *c =
		container_of(work, struct bch_fs, btree_interior_update_work);
	struct btree_update *as;

	while (1) {
		mutex_lock(&c->btree_interior_update_lock);
		as = list_first_entry_or_null(&c->btree_interior_updates_unwritten,
					      struct btree_update, unwritten_list);
		if (as && !as->nodes_written)
			as = NULL;
		mutex_unlock(&c->btree_interior_update_lock);

		if (!as)
			break;

		btree_update_nodes_written(as);
	}
}

static CLOSURE_CALLBACK(btree_update_set_nodes_written)
{
	closure_type(as, struct btree_update, cl);
	struct bch_fs *c = as->c;

	mutex_lock(&c->btree_interior_update_lock);
	as->nodes_written = true;
	mutex_unlock(&c->btree_interior_update_lock);

	queue_work(c->btree_interior_update_worker, &c->btree_interior_update_work);
}

/*
 * We're updating @b with pointers to nodes that haven't finished writing yet:
 * block @b from being written until @as completes
 */
static void btree_update_updated_node(struct btree_update *as, struct btree *b)
{
	struct bch_fs *c = as->c;

	mutex_lock(&c->btree_interior_update_lock);
	list_add_tail(&as->unwritten_list, &c->btree_interior_updates_unwritten);

	BUG_ON(as->mode != BTREE_INTERIOR_NO_UPDATE);
	BUG_ON(!btree_node_dirty(b));
	BUG_ON(!b->c.level);

	as->mode	= BTREE_INTERIOR_UPDATING_NODE;
	as->b		= b;

	set_btree_node_write_blocked(b);
	list_add(&as->write_blocked_list, &b->write_blocked);

	mutex_unlock(&c->btree_interior_update_lock);
}

static int bch2_update_reparent_journal_pin_flush(struct journal *j,
				struct journal_entry_pin *_pin, u64 seq)
{
	return 0;
}

static void btree_update_reparent(struct btree_update *as,
				  struct btree_update *child)
{
	struct bch_fs *c = as->c;

	lockdep_assert_held(&c->btree_interior_update_lock);

	child->b = NULL;
	child->mode = BTREE_INTERIOR_UPDATING_AS;

	bch2_journal_pin_copy(&c->journal, &as->journal, &child->journal,
			      bch2_update_reparent_journal_pin_flush);
}

static void btree_update_updated_root(struct btree_update *as, struct btree *b)
{
	struct bkey_i *insert = &b->key;
	struct bch_fs *c = as->c;

	BUG_ON(as->mode != BTREE_INTERIOR_NO_UPDATE);

	BUG_ON(as->journal_u64s + jset_u64s(insert->k.u64s) >
	       ARRAY_SIZE(as->journal_entries));

	as->journal_u64s +=
		journal_entry_set((void *) &as->journal_entries[as->journal_u64s],
				  BCH_JSET_ENTRY_btree_root,
				  b->c.btree_id, b->c.level,
				  insert, insert->k.u64s);

	mutex_lock(&c->btree_interior_update_lock);
	list_add_tail(&as->unwritten_list, &c->btree_interior_updates_unwritten);

	as->mode	= BTREE_INTERIOR_UPDATING_ROOT;
	mutex_unlock(&c->btree_interior_update_lock);
}

/*
 * bch2_btree_update_add_new_node:
 *
 * This causes @as to wait on @b to be written, before it gets to
 * bch2_btree_update_nodes_written
 *
 * Additionally, it sets b->will_make_reachable to prevent any additional writes
 * to @b from happening besides the first until @b is reachable on disk
 *
 * And it adds @b to the list of @as's new nodes, so that we can update sector
 * counts in bch2_btree_update_nodes_written:
 */
static void bch2_btree_update_add_new_node(struct btree_update *as, struct btree *b)
{
	struct bch_fs *c = as->c;

	closure_get(&as->cl);

	mutex_lock(&c->btree_interior_update_lock);
	BUG_ON(as->nr_new_nodes >= ARRAY_SIZE(as->new_nodes));
	BUG_ON(b->will_make_reachable);

	as->new_nodes[as->nr_new_nodes++] = b;
	b->will_make_reachable = 1UL|(unsigned long) as;
	set_btree_node_will_make_reachable(b);

	mutex_unlock(&c->btree_interior_update_lock);

	btree_update_add_key(as, &as->new_keys, b);

	if (b->key.k.type == KEY_TYPE_btree_ptr_v2) {
		unsigned bytes = vstruct_end(&b->data->keys) - (void *) b->data;
		unsigned sectors = round_up(bytes, block_bytes(c)) >> 9;

		bkey_i_to_btree_ptr_v2(&b->key)->v.sectors_written =
			cpu_to_le16(sectors);
	}
}

/*
 * returns true if @b was a new node
 */
static void btree_update_drop_new_node(struct bch_fs *c, struct btree *b)
{
	struct btree_update *as;
	unsigned long v;
	unsigned i;

	mutex_lock(&c->btree_interior_update_lock);
	/*
	 * When b->will_make_reachable != 0, it owns a ref on as->cl that's
	 * dropped when it gets written by bch2_btree_complete_write - the
	 * xchg() is for synchronization with bch2_btree_complete_write:
	 */
	v = xchg(&b->will_make_reachable, 0);
	clear_btree_node_will_make_reachable(b);
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

static void bch2_btree_update_get_open_buckets(struct btree_update *as, struct btree *b)
{
	while (b->ob.nr)
		as->open_buckets[as->nr_open_buckets++] =
			b->ob.v[--b->ob.nr];
}

static int bch2_btree_update_will_free_node_journal_pin_flush(struct journal *j,
				struct journal_entry_pin *_pin, u64 seq)
{
	return 0;
}

/*
 * @b is being split/rewritten: it may have pointers to not-yet-written btree
 * nodes and thus outstanding btree_updates - redirect @b's
 * btree_updates to point to this btree_update:
 */
static void bch2_btree_interior_update_will_free_node(struct btree_update *as,
						      struct btree *b)
{
	struct bch_fs *c = as->c;
	struct btree_update *p, *n;
	struct btree_write *w;

	set_btree_node_dying(b);

	if (btree_node_fake(b))
		return;

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
		list_del_init(&p->write_blocked_list);
		btree_update_reparent(as, p);

		/*
		 * for flush_held_btree_writes() waiting on updates to flush or
		 * nodes to be writeable:
		 */
		closure_wake_up(&c->btree_interior_update_wait);
	}

	clear_btree_node_dirty_acct(c, b);
	clear_btree_node_need_write(b);
	clear_btree_node_write_blocked(b);

	/*
	 * Does this node have unwritten data that has a pin on the journal?
	 *
	 * If so, transfer that pin to the btree_update operation -
	 * note that if we're freeing multiple nodes, we only need to keep the
	 * oldest pin of any of the nodes we're freeing. We'll release the pin
	 * when the new nodes are persistent and reachable on disk:
	 */
	w = btree_current_write(b);
	bch2_journal_pin_copy(&c->journal, &as->journal, &w->journal,
			      bch2_btree_update_will_free_node_journal_pin_flush);
	bch2_journal_pin_drop(&c->journal, &w->journal);

	w = btree_prev_write(b);
	bch2_journal_pin_copy(&c->journal, &as->journal, &w->journal,
			      bch2_btree_update_will_free_node_journal_pin_flush);
	bch2_journal_pin_drop(&c->journal, &w->journal);

	mutex_unlock(&c->btree_interior_update_lock);

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

	btree_update_add_key(as, &as->old_keys, b);

	as->old_nodes[as->nr_old_nodes] = b;
	as->old_nodes_seq[as->nr_old_nodes] = b->data->keys.seq;
	as->nr_old_nodes++;
}

static void bch2_btree_update_done(struct btree_update *as, struct btree_trans *trans)
{
	struct bch_fs *c = as->c;
	u64 start_time = as->start_time;

	BUG_ON(as->mode == BTREE_INTERIOR_NO_UPDATE);

	if (as->took_gc_lock)
		up_read(&as->c->gc_lock);
	as->took_gc_lock = false;

	bch2_btree_reserve_put(as, trans);

	continue_at(&as->cl, btree_update_set_nodes_written,
		    as->c->btree_interior_update_worker);

	bch2_time_stats_update(&c->times[BCH_TIME_btree_interior_update_foreground],
			       start_time);
}

static struct btree_update *
bch2_btree_update_start(struct btree_trans *trans, struct btree_path *path,
			unsigned level, bool split, unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct btree_update *as;
	u64 start_time = local_clock();
	int disk_res_flags = (flags & BCH_TRANS_COMMIT_no_enospc)
		? BCH_DISK_RESERVATION_NOFAIL : 0;
	unsigned nr_nodes[2] = { 0, 0 };
	unsigned update_level = level;
	enum bch_watermark watermark = flags & BCH_WATERMARK_MASK;
	int ret = 0;
	u32 restart_count = trans->restart_count;

	BUG_ON(!path->should_be_locked);

	if (watermark == BCH_WATERMARK_copygc)
		watermark = BCH_WATERMARK_btree_copygc;
	if (watermark < BCH_WATERMARK_btree)
		watermark = BCH_WATERMARK_btree;

	flags &= ~BCH_WATERMARK_MASK;
	flags |= watermark;

	if (watermark < c->journal.watermark) {
		struct journal_res res = { 0 };
		unsigned journal_flags = watermark|JOURNAL_RES_GET_CHECK;

		if ((flags & BCH_TRANS_COMMIT_journal_reclaim) &&
		    watermark != BCH_WATERMARK_reclaim)
			journal_flags |= JOURNAL_RES_GET_NONBLOCK;

		ret = drop_locks_do(trans,
			bch2_journal_res_get(&c->journal, &res, 1, journal_flags));
		if (bch2_err_matches(ret, BCH_ERR_operation_blocked))
			ret = -BCH_ERR_journal_reclaim_would_deadlock;
		if (ret)
			return ERR_PTR(ret);
	}

	while (1) {
		nr_nodes[!!update_level] += 1 + split;
		update_level++;

		ret = bch2_btree_path_upgrade(trans, path, update_level + 1);
		if (ret)
			return ERR_PTR(ret);

		if (!btree_path_node(path, update_level)) {
			/* Allocating new root? */
			nr_nodes[1] += split;
			update_level = BTREE_MAX_DEPTH;
			break;
		}

		/*
		 * Always check for space for two keys, even if we won't have to
		 * split at prior level - it might have been a merge instead:
		 */
		if (bch2_btree_node_insert_fits(path->l[update_level].b,
						BKEY_BTREE_PTR_U64s_MAX * 2))
			break;

		split = path->l[update_level].b->nr.live_u64s > BTREE_SPLIT_THRESHOLD(c);
	}

	if (!down_read_trylock(&c->gc_lock)) {
		ret = drop_locks_do(trans, (down_read(&c->gc_lock), 0));
		if (ret) {
			up_read(&c->gc_lock);
			return ERR_PTR(ret);
		}
	}

	as = mempool_alloc(&c->btree_interior_update_pool, GFP_NOFS);
	memset(as, 0, sizeof(*as));
	closure_init(&as->cl, NULL);
	as->c		= c;
	as->start_time	= start_time;
	as->ip_started	= _RET_IP_;
	as->mode	= BTREE_INTERIOR_NO_UPDATE;
	as->took_gc_lock = true;
	as->btree_id	= path->btree_id;
	as->update_level = update_level;
	INIT_LIST_HEAD(&as->list);
	INIT_LIST_HEAD(&as->unwritten_list);
	INIT_LIST_HEAD(&as->write_blocked_list);
	bch2_keylist_init(&as->old_keys, as->_old_keys);
	bch2_keylist_init(&as->new_keys, as->_new_keys);
	bch2_keylist_init(&as->parent_keys, as->inline_keys);

	mutex_lock(&c->btree_interior_update_lock);
	list_add_tail(&as->list, &c->btree_interior_update_list);
	mutex_unlock(&c->btree_interior_update_lock);

	/*
	 * We don't want to allocate if we're in an error state, that can cause
	 * deadlock on emergency shutdown due to open buckets getting stuck in
	 * the btree_reserve_cache after allocator shutdown has cleared it out.
	 * This check needs to come after adding us to the btree_interior_update
	 * list but before calling bch2_btree_reserve_get, to synchronize with
	 * __bch2_fs_read_only().
	 */
	ret = bch2_journal_error(&c->journal);
	if (ret)
		goto err;

	ret = bch2_disk_reservation_get(c, &as->disk_res,
			(nr_nodes[0] + nr_nodes[1]) * btree_sectors(c),
			c->opts.metadata_replicas,
			disk_res_flags);
	if (ret)
		goto err;

	ret = bch2_btree_reserve_get(trans, as, nr_nodes, flags, NULL);
	if (bch2_err_matches(ret, ENOSPC) ||
	    bch2_err_matches(ret, ENOMEM)) {
		struct closure cl;

		/*
		 * XXX: this should probably be a separate BTREE_INSERT_NONBLOCK
		 * flag
		 */
		if (bch2_err_matches(ret, ENOSPC) &&
		    (flags & BCH_TRANS_COMMIT_journal_reclaim) &&
		    watermark != BCH_WATERMARK_reclaim) {
			ret = -BCH_ERR_journal_reclaim_would_deadlock;
			goto err;
		}

		closure_init_stack(&cl);

		do {
			ret = bch2_btree_reserve_get(trans, as, nr_nodes, flags, &cl);

			bch2_trans_unlock(trans);
			closure_sync(&cl);
		} while (bch2_err_matches(ret, BCH_ERR_operation_blocked));
	}

	if (ret) {
		trace_and_count(c, btree_reserve_get_fail, trans->fn,
				_RET_IP_, nr_nodes[0] + nr_nodes[1], ret);
		goto err;
	}

	ret = bch2_trans_relock(trans);
	if (ret)
		goto err;

	bch2_trans_verify_not_restarted(trans, restart_count);
	return as;
err:
	bch2_btree_update_free(as, trans);
	if (!bch2_err_matches(ret, ENOSPC) &&
	    !bch2_err_matches(ret, EROFS) &&
	    ret != -BCH_ERR_journal_reclaim_would_deadlock)
		bch_err_fn_ratelimited(c, ret);
	return ERR_PTR(ret);
}

/* Btree root updates: */

static void bch2_btree_set_root_inmem(struct bch_fs *c, struct btree *b)
{
	/* Root nodes cannot be reaped */
	mutex_lock(&c->btree_cache.lock);
	list_del_init(&b->list);
	mutex_unlock(&c->btree_cache.lock);

	mutex_lock(&c->btree_root_lock);
	bch2_btree_id_root(c, b->c.btree_id)->b = b;
	mutex_unlock(&c->btree_root_lock);

	bch2_recalc_btree_reserve(c);
}

static void bch2_btree_set_root(struct btree_update *as,
				struct btree_trans *trans,
				struct btree_path *path,
				struct btree *b)
{
	struct bch_fs *c = as->c;
	struct btree *old;

	trace_and_count(c, btree_node_set_root, trans, b);

	old = btree_node_root(c, b);

	/*
	 * Ensure no one is using the old root while we switch to the
	 * new root:
	 */
	bch2_btree_node_lock_write_nofail(trans, path, &old->c);

	bch2_btree_set_root_inmem(c, b);

	btree_update_updated_root(as, b);

	/*
	 * Unlock old root after new root is visible:
	 *
	 * The new root isn't persistent, but that's ok: we still have
	 * an intent lock on the new root, and any updates that would
	 * depend on the new root would have to update the new root.
	 */
	bch2_btree_node_unlock_write(trans, path, old);
}

/* Interior node updates: */

static void bch2_insert_fixup_btree_ptr(struct btree_update *as,
					struct btree_trans *trans,
					struct btree_path *path,
					struct btree *b,
					struct btree_node_iter *node_iter,
					struct bkey_i *insert)
{
	struct bch_fs *c = as->c;
	struct bkey_packed *k;
	struct printbuf buf = PRINTBUF;
	unsigned long old, new, v;

	BUG_ON(insert->k.type == KEY_TYPE_btree_ptr_v2 &&
	       !btree_ptr_sectors_written(insert));

	if (unlikely(!test_bit(JOURNAL_REPLAY_DONE, &c->journal.flags)))
		bch2_journal_key_overwritten(c, b->c.btree_id, b->c.level, insert->k.p);

	if (bch2_bkey_invalid(c, bkey_i_to_s_c(insert),
			      btree_node_type(b), WRITE, &buf) ?:
	    bch2_bkey_in_btree_node(c, b, bkey_i_to_s_c(insert), &buf)) {
		printbuf_reset(&buf);
		prt_printf(&buf, "inserting invalid bkey\n  ");
		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(insert));
		prt_printf(&buf, "\n  ");
		bch2_bkey_invalid(c, bkey_i_to_s_c(insert),
				  btree_node_type(b), WRITE, &buf);
		bch2_bkey_in_btree_node(c, b, bkey_i_to_s_c(insert), &buf);

		bch2_fs_inconsistent(c, "%s", buf.buf);
		dump_stack();
	}

	BUG_ON(as->journal_u64s + jset_u64s(insert->k.u64s) >
	       ARRAY_SIZE(as->journal_entries));

	as->journal_u64s +=
		journal_entry_set((void *) &as->journal_entries[as->journal_u64s],
				  BCH_JSET_ENTRY_btree_keys,
				  b->c.btree_id, b->c.level,
				  insert, insert->k.u64s);

	while ((k = bch2_btree_node_iter_peek_all(node_iter, b)) &&
	       bkey_iter_pos_cmp(b, k, &insert->k.p) < 0)
		bch2_btree_node_iter_advance(node_iter, b);

	bch2_btree_bset_insert_key(trans, path, b, node_iter, insert);
	set_btree_node_dirty_acct(c, b);

	v = READ_ONCE(b->flags);
	do {
		old = new = v;

		new &= ~BTREE_WRITE_TYPE_MASK;
		new |= BTREE_WRITE_interior;
		new |= 1 << BTREE_NODE_need_write;
	} while ((v = cmpxchg(&b->flags, old, new)) != old);

	printbuf_exit(&buf);
}

static void
__bch2_btree_insert_keys_interior(struct btree_update *as,
				  struct btree_trans *trans,
				  struct btree_path *path,
				  struct btree *b,
				  struct btree_node_iter node_iter,
				  struct keylist *keys)
{
	struct bkey_i *insert = bch2_keylist_front(keys);
	struct bkey_packed *k;

	BUG_ON(btree_node_type(b) != BKEY_TYPE_btree);

	while ((k = bch2_btree_node_iter_prev_all(&node_iter, b)) &&
	       (bkey_cmp_left_packed(b, k, &insert->k.p) >= 0))
		;

	while (!bch2_keylist_empty(keys)) {
		insert = bch2_keylist_front(keys);

		if (bpos_gt(insert->k.p, b->key.k.p))
			break;

		bch2_insert_fixup_btree_ptr(as, trans, path, b, &node_iter, insert);
		bch2_keylist_pop_front(keys);
	}
}

/*
 * Move keys from n1 (original replacement node, now lower node) to n2 (higher
 * node)
 */
static void __btree_split_node(struct btree_update *as,
			       struct btree_trans *trans,
			       struct btree *b,
			       struct btree *n[2])
{
	struct bkey_packed *k;
	struct bpos n1_pos = POS_MIN;
	struct btree_node_iter iter;
	struct bset *bsets[2];
	struct bkey_format_state format[2];
	struct bkey_packed *out[2];
	struct bkey uk;
	unsigned u64s, n1_u64s = (b->nr.live_u64s * 3) / 5;
	struct { unsigned nr_keys, val_u64s; } nr_keys[2];
	int i;

	memset(&nr_keys, 0, sizeof(nr_keys));

	for (i = 0; i < 2; i++) {
		BUG_ON(n[i]->nsets != 1);

		bsets[i] = btree_bset_first(n[i]);
		out[i] = bsets[i]->start;

		SET_BTREE_NODE_SEQ(n[i]->data, BTREE_NODE_SEQ(b->data) + 1);
		bch2_bkey_format_init(&format[i]);
	}

	u64s = 0;
	for_each_btree_node_key(b, k, &iter) {
		if (bkey_deleted(k))
			continue;

		i = u64s >= n1_u64s;
		u64s += k->u64s;
		uk = bkey_unpack_key(b, k);
		if (!i)
			n1_pos = uk.p;
		bch2_bkey_format_add_key(&format[i], &uk);

		nr_keys[i].nr_keys++;
		nr_keys[i].val_u64s += bkeyp_val_u64s(&b->format, k);
	}

	btree_set_min(n[0], b->data->min_key);
	btree_set_max(n[0], n1_pos);
	btree_set_min(n[1], bpos_successor(n1_pos));
	btree_set_max(n[1], b->data->max_key);

	for (i = 0; i < 2; i++) {
		bch2_bkey_format_add_pos(&format[i], n[i]->data->min_key);
		bch2_bkey_format_add_pos(&format[i], n[i]->data->max_key);

		n[i]->data->format = bch2_bkey_format_done(&format[i]);

		unsigned u64s = nr_keys[i].nr_keys * n[i]->data->format.key_u64s +
			nr_keys[i].val_u64s;
		if (__vstruct_bytes(struct btree_node, u64s) > btree_buf_bytes(b))
			n[i]->data->format = b->format;

		btree_node_set_format(n[i], n[i]->data->format);
	}

	u64s = 0;
	for_each_btree_node_key(b, k, &iter) {
		if (bkey_deleted(k))
			continue;

		i = u64s >= n1_u64s;
		u64s += k->u64s;

		if (bch2_bkey_transform(&n[i]->format, out[i], bkey_packed(k)
					? &b->format: &bch2_bkey_format_current, k))
			out[i]->format = KEY_FORMAT_LOCAL_BTREE;
		else
			bch2_bkey_unpack(b, (void *) out[i], k);

		out[i]->needs_whiteout = false;

		btree_keys_account_key_add(&n[i]->nr, 0, out[i]);
		out[i] = bkey_p_next(out[i]);
	}

	for (i = 0; i < 2; i++) {
		bsets[i]->u64s = cpu_to_le16((u64 *) out[i] - bsets[i]->_data);

		BUG_ON(!bsets[i]->u64s);

		set_btree_bset_end(n[i], n[i]->set);

		btree_node_reset_sib_u64s(n[i]);

		bch2_verify_btree_nr_keys(n[i]);

		if (b->c.level)
			btree_node_interior_verify(as->c, n[i]);
	}
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
static void btree_split_insert_keys(struct btree_update *as,
				    struct btree_trans *trans,
				    btree_path_idx_t path_idx,
				    struct btree *b,
				    struct keylist *keys)
{
	struct btree_path *path = trans->paths + path_idx;

	if (!bch2_keylist_empty(keys) &&
	    bpos_le(bch2_keylist_front(keys)->k.p, b->data->max_key)) {
		struct btree_node_iter node_iter;

		bch2_btree_node_iter_init(&node_iter, b, &bch2_keylist_front(keys)->k.p);

		__bch2_btree_insert_keys_interior(as, trans, path, b, node_iter, keys);

		btree_node_interior_verify(as->c, b);
	}
}

static int btree_split(struct btree_update *as, struct btree_trans *trans,
		       btree_path_idx_t path, struct btree *b,
		       struct keylist *keys)
{
	struct bch_fs *c = as->c;
	struct btree *parent = btree_node_parent(trans->paths + path, b);
	struct btree *n1, *n2 = NULL, *n3 = NULL;
	btree_path_idx_t path1 = 0, path2 = 0;
	u64 start_time = local_clock();
	int ret = 0;

	BUG_ON(!parent && (b != btree_node_root(c, b)));
	BUG_ON(parent && !btree_node_intent_locked(trans->paths + path, b->c.level + 1));

	bch2_btree_interior_update_will_free_node(as, b);

	if (b->nr.live_u64s > BTREE_SPLIT_THRESHOLD(c)) {
		struct btree *n[2];

		trace_and_count(c, btree_node_split, trans, b);

		n[0] = n1 = bch2_btree_node_alloc(as, trans, b->c.level);
		n[1] = n2 = bch2_btree_node_alloc(as, trans, b->c.level);

		__btree_split_node(as, trans, b, n);

		if (keys) {
			btree_split_insert_keys(as, trans, path, n1, keys);
			btree_split_insert_keys(as, trans, path, n2, keys);
			BUG_ON(!bch2_keylist_empty(keys));
		}

		bch2_btree_build_aux_trees(n2);
		bch2_btree_build_aux_trees(n1);

		bch2_btree_update_add_new_node(as, n1);
		bch2_btree_update_add_new_node(as, n2);
		six_unlock_write(&n2->c.lock);
		six_unlock_write(&n1->c.lock);

		path1 = get_unlocked_mut_path(trans, as->btree_id, n1->c.level, n1->key.k.p);
		six_lock_increment(&n1->c.lock, SIX_LOCK_intent);
		mark_btree_node_locked(trans, trans->paths + path1, n1->c.level, BTREE_NODE_INTENT_LOCKED);
		bch2_btree_path_level_init(trans, trans->paths + path1, n1);

		path2 = get_unlocked_mut_path(trans, as->btree_id, n2->c.level, n2->key.k.p);
		six_lock_increment(&n2->c.lock, SIX_LOCK_intent);
		mark_btree_node_locked(trans, trans->paths + path2, n2->c.level, BTREE_NODE_INTENT_LOCKED);
		bch2_btree_path_level_init(trans, trans->paths + path2, n2);

		/*
		 * Note that on recursive parent_keys == keys, so we
		 * can't start adding new keys to parent_keys before emptying it
		 * out (which we did with btree_split_insert_keys() above)
		 */
		bch2_keylist_add(&as->parent_keys, &n1->key);
		bch2_keylist_add(&as->parent_keys, &n2->key);

		if (!parent) {
			/* Depth increases, make a new root */
			n3 = __btree_root_alloc(as, trans, b->c.level + 1);

			bch2_btree_update_add_new_node(as, n3);
			six_unlock_write(&n3->c.lock);

			trans->paths[path2].locks_want++;
			BUG_ON(btree_node_locked(trans->paths + path2, n3->c.level));
			six_lock_increment(&n3->c.lock, SIX_LOCK_intent);
			mark_btree_node_locked(trans, trans->paths + path2, n3->c.level, BTREE_NODE_INTENT_LOCKED);
			bch2_btree_path_level_init(trans, trans->paths + path2, n3);

			n3->sib_u64s[0] = U16_MAX;
			n3->sib_u64s[1] = U16_MAX;

			btree_split_insert_keys(as, trans, path, n3, &as->parent_keys);
		}
	} else {
		trace_and_count(c, btree_node_compact, trans, b);

		n1 = bch2_btree_node_alloc_replacement(as, trans, b);

		if (keys) {
			btree_split_insert_keys(as, trans, path, n1, keys);
			BUG_ON(!bch2_keylist_empty(keys));
		}

		bch2_btree_build_aux_trees(n1);
		bch2_btree_update_add_new_node(as, n1);
		six_unlock_write(&n1->c.lock);

		path1 = get_unlocked_mut_path(trans, as->btree_id, n1->c.level, n1->key.k.p);
		six_lock_increment(&n1->c.lock, SIX_LOCK_intent);
		mark_btree_node_locked(trans, trans->paths + path1, n1->c.level, BTREE_NODE_INTENT_LOCKED);
		bch2_btree_path_level_init(trans, trans->paths + path1, n1);

		if (parent)
			bch2_keylist_add(&as->parent_keys, &n1->key);
	}

	/* New nodes all written, now make them visible: */

	if (parent) {
		/* Split a non root node */
		ret = bch2_btree_insert_node(as, trans, path, parent, &as->parent_keys);
		if (ret)
			goto err;
	} else if (n3) {
		bch2_btree_set_root(as, trans, trans->paths + path, n3);
	} else {
		/* Root filled up but didn't need to be split */
		bch2_btree_set_root(as, trans, trans->paths + path, n1);
	}

	if (n3) {
		bch2_btree_update_get_open_buckets(as, n3);
		bch2_btree_node_write(c, n3, SIX_LOCK_intent, 0);
	}
	if (n2) {
		bch2_btree_update_get_open_buckets(as, n2);
		bch2_btree_node_write(c, n2, SIX_LOCK_intent, 0);
	}
	bch2_btree_update_get_open_buckets(as, n1);
	bch2_btree_node_write(c, n1, SIX_LOCK_intent, 0);

	/*
	 * The old node must be freed (in memory) _before_ unlocking the new
	 * nodes - else another thread could re-acquire a read lock on the old
	 * node after another thread has locked and updated the new node, thus
	 * seeing stale data:
	 */
	bch2_btree_node_free_inmem(trans, trans->paths + path, b);

	if (n3)
		bch2_trans_node_add(trans, trans->paths + path, n3);
	if (n2)
		bch2_trans_node_add(trans, trans->paths + path2, n2);
	bch2_trans_node_add(trans, trans->paths + path1, n1);

	if (n3)
		six_unlock_intent(&n3->c.lock);
	if (n2)
		six_unlock_intent(&n2->c.lock);
	six_unlock_intent(&n1->c.lock);
out:
	if (path2) {
		__bch2_btree_path_unlock(trans, trans->paths + path2);
		bch2_path_put(trans, path2, true);
	}
	if (path1) {
		__bch2_btree_path_unlock(trans, trans->paths + path1);
		bch2_path_put(trans, path1, true);
	}

	bch2_trans_verify_locks(trans);

	bch2_time_stats_update(&c->times[n2
			       ? BCH_TIME_btree_node_split
			       : BCH_TIME_btree_node_compact],
			       start_time);
	return ret;
err:
	if (n3)
		bch2_btree_node_free_never_used(as, trans, n3);
	if (n2)
		bch2_btree_node_free_never_used(as, trans, n2);
	bch2_btree_node_free_never_used(as, trans, n1);
	goto out;
}

static void
bch2_btree_insert_keys_interior(struct btree_update *as,
				struct btree_trans *trans,
				struct btree_path *path,
				struct btree *b,
				struct keylist *keys)
{
	struct btree_path *linked;
	unsigned i;

	__bch2_btree_insert_keys_interior(as, trans, path, b,
					  path->l[b->c.level].iter, keys);

	btree_update_updated_node(as, b);

	trans_for_each_path_with_node(trans, b, linked, i)
		bch2_btree_node_iter_peek(&linked->l[b->c.level].iter, b);

	bch2_trans_verify_paths(trans);
}

/**
 * bch2_btree_insert_node - insert bkeys into a given btree node
 *
 * @as:			btree_update object
 * @trans:		btree_trans object
 * @path_idx:		path that points to current node
 * @b:			node to insert keys into
 * @keys:		list of keys to insert
 *
 * Returns: 0 on success, typically transaction restart error on failure
 *
 * Inserts as many keys as it can into a given btree node, splitting it if full.
 * If a split occurred, this function will return early. This can only happen
 * for leaf nodes -- inserts into interior nodes have to be atomic.
 */
static int bch2_btree_insert_node(struct btree_update *as, struct btree_trans *trans,
				  btree_path_idx_t path_idx, struct btree *b,
				  struct keylist *keys)
{
	struct bch_fs *c = as->c;
	struct btree_path *path = trans->paths + path_idx;
	int old_u64s = le16_to_cpu(btree_bset_last(b)->u64s);
	int old_live_u64s = b->nr.live_u64s;
	int live_u64s_added, u64s_added;
	int ret;

	lockdep_assert_held(&c->gc_lock);
	BUG_ON(!btree_node_intent_locked(path, b->c.level));
	BUG_ON(!b->c.level);
	BUG_ON(!as || as->b);
	bch2_verify_keylist_sorted(keys);

	ret = bch2_btree_node_lock_write(trans, path, &b->c);
	if (ret)
		return ret;

	bch2_btree_node_prep_for_write(trans, path, b);

	if (!bch2_btree_node_insert_fits(b, bch2_keylist_u64s(keys))) {
		bch2_btree_node_unlock_write(trans, path, b);
		goto split;
	}

	btree_node_interior_verify(c, b);

	bch2_btree_insert_keys_interior(as, trans, path, b, keys);

	live_u64s_added = (int) b->nr.live_u64s - old_live_u64s;
	u64s_added = (int) le16_to_cpu(btree_bset_last(b)->u64s) - old_u64s;

	if (b->sib_u64s[0] != U16_MAX && live_u64s_added < 0)
		b->sib_u64s[0] = max(0, (int) b->sib_u64s[0] + live_u64s_added);
	if (b->sib_u64s[1] != U16_MAX && live_u64s_added < 0)
		b->sib_u64s[1] = max(0, (int) b->sib_u64s[1] + live_u64s_added);

	if (u64s_added > live_u64s_added &&
	    bch2_maybe_compact_whiteouts(c, b))
		bch2_trans_node_reinit_iter(trans, b);

	bch2_btree_node_unlock_write(trans, path, b);

	btree_node_interior_verify(c, b);
	return 0;
split:
	/*
	 * We could attempt to avoid the transaction restart, by calling
	 * bch2_btree_path_upgrade() and allocating more nodes:
	 */
	if (b->c.level >= as->update_level) {
		trace_and_count(c, trans_restart_split_race, trans, _THIS_IP_, b);
		return btree_trans_restart(trans, BCH_ERR_transaction_restart_split_race);
	}

	return btree_split(as, trans, path_idx, b, keys);
}

int bch2_btree_split_leaf(struct btree_trans *trans,
			  btree_path_idx_t path,
			  unsigned flags)
{
	/* btree_split & merge may both cause paths array to be reallocated */
	struct btree *b = path_l(trans->paths + path)->b;
	struct btree_update *as;
	unsigned l;
	int ret = 0;

	as = bch2_btree_update_start(trans, trans->paths + path,
				     trans->paths[path].level,
				     true, flags);
	if (IS_ERR(as))
		return PTR_ERR(as);

	ret = btree_split(as, trans, path, b, NULL);
	if (ret) {
		bch2_btree_update_free(as, trans);
		return ret;
	}

	bch2_btree_update_done(as, trans);

	for (l = trans->paths[path].level + 1;
	     btree_node_intent_locked(&trans->paths[path], l) && !ret;
	     l++)
		ret = bch2_foreground_maybe_merge(trans, path, l, flags);

	return ret;
}

static void __btree_increase_depth(struct btree_update *as, struct btree_trans *trans,
				   btree_path_idx_t path_idx)
{
	struct bch_fs *c = as->c;
	struct btree_path *path = trans->paths + path_idx;
	struct btree *n, *b = bch2_btree_id_root(c, path->btree_id)->b;

	BUG_ON(!btree_node_locked(path, b->c.level));

	n = __btree_root_alloc(as, trans, b->c.level + 1);

	bch2_btree_update_add_new_node(as, n);
	six_unlock_write(&n->c.lock);

	path->locks_want++;
	BUG_ON(btree_node_locked(path, n->c.level));
	six_lock_increment(&n->c.lock, SIX_LOCK_intent);
	mark_btree_node_locked(trans, path, n->c.level, BTREE_NODE_INTENT_LOCKED);
	bch2_btree_path_level_init(trans, path, n);

	n->sib_u64s[0] = U16_MAX;
	n->sib_u64s[1] = U16_MAX;

	bch2_keylist_add(&as->parent_keys, &b->key);
	btree_split_insert_keys(as, trans, path_idx, n, &as->parent_keys);

	bch2_btree_set_root(as, trans, path, n);
	bch2_btree_update_get_open_buckets(as, n);
	bch2_btree_node_write(c, n, SIX_LOCK_intent, 0);
	bch2_trans_node_add(trans, path, n);
	six_unlock_intent(&n->c.lock);

	mutex_lock(&c->btree_cache.lock);
	list_add_tail(&b->list, &c->btree_cache.live);
	mutex_unlock(&c->btree_cache.lock);

	bch2_trans_verify_locks(trans);
}

int bch2_btree_increase_depth(struct btree_trans *trans, btree_path_idx_t path, unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct btree *b = bch2_btree_id_root(c, trans->paths[path].btree_id)->b;
	struct btree_update *as =
		bch2_btree_update_start(trans, trans->paths + path,
					b->c.level, true, flags);
	if (IS_ERR(as))
		return PTR_ERR(as);

	__btree_increase_depth(as, trans, path);
	bch2_btree_update_done(as, trans);
	return 0;
}

int __bch2_foreground_maybe_merge(struct btree_trans *trans,
				  btree_path_idx_t path,
				  unsigned level,
				  unsigned flags,
				  enum btree_node_sibling sib)
{
	struct bch_fs *c = trans->c;
	struct btree_update *as;
	struct bkey_format_state new_s;
	struct bkey_format new_f;
	struct bkey_i delete;
	struct btree *b, *m, *n, *prev, *next, *parent;
	struct bpos sib_pos;
	size_t sib_u64s;
	enum btree_id btree = trans->paths[path].btree_id;
	btree_path_idx_t sib_path = 0, new_path = 0;
	u64 start_time = local_clock();
	int ret = 0;

	BUG_ON(!trans->paths[path].should_be_locked);
	BUG_ON(!btree_node_locked(&trans->paths[path], level));

	b = trans->paths[path].l[level].b;

	if ((sib == btree_prev_sib && bpos_eq(b->data->min_key, POS_MIN)) ||
	    (sib == btree_next_sib && bpos_eq(b->data->max_key, SPOS_MAX))) {
		b->sib_u64s[sib] = U16_MAX;
		return 0;
	}

	sib_pos = sib == btree_prev_sib
		? bpos_predecessor(b->data->min_key)
		: bpos_successor(b->data->max_key);

	sib_path = bch2_path_get(trans, btree, sib_pos,
				 U8_MAX, level, BTREE_ITER_INTENT, _THIS_IP_);
	ret = bch2_btree_path_traverse(trans, sib_path, false);
	if (ret)
		goto err;

	btree_path_set_should_be_locked(trans->paths + sib_path);

	m = trans->paths[sib_path].l[level].b;

	if (btree_node_parent(trans->paths + path, b) !=
	    btree_node_parent(trans->paths + sib_path, m)) {
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

	if (!bpos_eq(bpos_successor(prev->data->max_key), next->data->min_key)) {
		struct printbuf buf1 = PRINTBUF, buf2 = PRINTBUF;

		bch2_bpos_to_text(&buf1, prev->data->max_key);
		bch2_bpos_to_text(&buf2, next->data->min_key);
		bch_err(c,
			"%s(): btree topology error:\n"
			"  prev ends at   %s\n"
			"  next starts at %s",
			__func__, buf1.buf, buf2.buf);
		printbuf_exit(&buf1);
		printbuf_exit(&buf2);
		ret = bch2_topology_error(c);
		goto err;
	}

	bch2_bkey_format_init(&new_s);
	bch2_bkey_format_add_pos(&new_s, prev->data->min_key);
	__bch2_btree_calc_format(&new_s, prev);
	__bch2_btree_calc_format(&new_s, next);
	bch2_bkey_format_add_pos(&new_s, next->data->max_key);
	new_f = bch2_bkey_format_done(&new_s);

	sib_u64s = btree_node_u64s_with_format(b->nr, &b->format, &new_f) +
		btree_node_u64s_with_format(m->nr, &m->format, &new_f);

	if (sib_u64s > BTREE_FOREGROUND_MERGE_HYSTERESIS(c)) {
		sib_u64s -= BTREE_FOREGROUND_MERGE_HYSTERESIS(c);
		sib_u64s /= 2;
		sib_u64s += BTREE_FOREGROUND_MERGE_HYSTERESIS(c);
	}

	sib_u64s = min(sib_u64s, btree_max_u64s(c));
	sib_u64s = min(sib_u64s, (size_t) U16_MAX - 1);
	b->sib_u64s[sib] = sib_u64s;

	if (b->sib_u64s[sib] > c->btree_foreground_merge_threshold)
		goto out;

	parent = btree_node_parent(trans->paths + path, b);
	as = bch2_btree_update_start(trans, trans->paths + path, level, false,
				     BCH_TRANS_COMMIT_no_enospc|flags);
	ret = PTR_ERR_OR_ZERO(as);
	if (ret)
		goto err;

	trace_and_count(c, btree_node_merge, trans, b);

	bch2_btree_interior_update_will_free_node(as, b);
	bch2_btree_interior_update_will_free_node(as, m);

	n = bch2_btree_node_alloc(as, trans, b->c.level);

	SET_BTREE_NODE_SEQ(n->data,
			   max(BTREE_NODE_SEQ(b->data),
			       BTREE_NODE_SEQ(m->data)) + 1);

	btree_set_min(n, prev->data->min_key);
	btree_set_max(n, next->data->max_key);

	n->data->format	 = new_f;
	btree_node_set_format(n, new_f);

	bch2_btree_sort_into(c, n, prev);
	bch2_btree_sort_into(c, n, next);

	bch2_btree_build_aux_trees(n);
	bch2_btree_update_add_new_node(as, n);
	six_unlock_write(&n->c.lock);

	new_path = get_unlocked_mut_path(trans, btree, n->c.level, n->key.k.p);
	six_lock_increment(&n->c.lock, SIX_LOCK_intent);
	mark_btree_node_locked(trans, trans->paths + new_path, n->c.level, BTREE_NODE_INTENT_LOCKED);
	bch2_btree_path_level_init(trans, trans->paths + new_path, n);

	bkey_init(&delete.k);
	delete.k.p = prev->key.k.p;
	bch2_keylist_add(&as->parent_keys, &delete);
	bch2_keylist_add(&as->parent_keys, &n->key);

	bch2_trans_verify_paths(trans);

	ret = bch2_btree_insert_node(as, trans, path, parent, &as->parent_keys);
	if (ret)
		goto err_free_update;

	bch2_trans_verify_paths(trans);

	bch2_btree_update_get_open_buckets(as, n);
	bch2_btree_node_write(c, n, SIX_LOCK_intent, 0);

	bch2_btree_node_free_inmem(trans, trans->paths + path, b);
	bch2_btree_node_free_inmem(trans, trans->paths + sib_path, m);

	bch2_trans_node_add(trans, trans->paths + path, n);

	bch2_trans_verify_paths(trans);

	six_unlock_intent(&n->c.lock);

	bch2_btree_update_done(as, trans);

	bch2_time_stats_update(&c->times[BCH_TIME_btree_node_merge], start_time);
out:
err:
	if (new_path)
		bch2_path_put(trans, new_path, true);
	bch2_path_put(trans, sib_path, true);
	bch2_trans_verify_locks(trans);
	return ret;
err_free_update:
	bch2_btree_node_free_never_used(as, trans, n);
	bch2_btree_update_free(as, trans);
	goto out;
}

int bch2_btree_node_rewrite(struct btree_trans *trans,
			    struct btree_iter *iter,
			    struct btree *b,
			    unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct btree *n, *parent;
	struct btree_update *as;
	btree_path_idx_t new_path = 0;
	int ret;

	flags |= BCH_TRANS_COMMIT_no_enospc;

	struct btree_path *path = btree_iter_path(trans, iter);
	parent = btree_node_parent(path, b);
	as = bch2_btree_update_start(trans, path, b->c.level, false, flags);
	ret = PTR_ERR_OR_ZERO(as);
	if (ret)
		goto out;

	bch2_btree_interior_update_will_free_node(as, b);

	n = bch2_btree_node_alloc_replacement(as, trans, b);

	bch2_btree_build_aux_trees(n);
	bch2_btree_update_add_new_node(as, n);
	six_unlock_write(&n->c.lock);

	new_path = get_unlocked_mut_path(trans, iter->btree_id, n->c.level, n->key.k.p);
	six_lock_increment(&n->c.lock, SIX_LOCK_intent);
	mark_btree_node_locked(trans, trans->paths + new_path, n->c.level, BTREE_NODE_INTENT_LOCKED);
	bch2_btree_path_level_init(trans, trans->paths + new_path, n);

	trace_and_count(c, btree_node_rewrite, trans, b);

	if (parent) {
		bch2_keylist_add(&as->parent_keys, &n->key);
		ret = bch2_btree_insert_node(as, trans, iter->path, parent, &as->parent_keys);
		if (ret)
			goto err;
	} else {
		bch2_btree_set_root(as, trans, btree_iter_path(trans, iter), n);
	}

	bch2_btree_update_get_open_buckets(as, n);
	bch2_btree_node_write(c, n, SIX_LOCK_intent, 0);

	bch2_btree_node_free_inmem(trans, btree_iter_path(trans, iter), b);

	bch2_trans_node_add(trans, trans->paths + iter->path, n);
	six_unlock_intent(&n->c.lock);

	bch2_btree_update_done(as, trans);
out:
	if (new_path)
		bch2_path_put(trans, new_path, true);
	bch2_trans_downgrade(trans);
	return ret;
err:
	bch2_btree_node_free_never_used(as, trans, n);
	bch2_btree_update_free(as, trans);
	goto out;
}

struct async_btree_rewrite {
	struct bch_fs		*c;
	struct work_struct	work;
	struct list_head	list;
	enum btree_id		btree_id;
	unsigned		level;
	struct bpos		pos;
	__le64			seq;
};

static int async_btree_node_rewrite_trans(struct btree_trans *trans,
					  struct async_btree_rewrite *a)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct btree *b;
	int ret;

	bch2_trans_node_iter_init(trans, &iter, a->btree_id, a->pos,
				  BTREE_MAX_DEPTH, a->level, 0);
	b = bch2_btree_iter_peek_node(&iter);
	ret = PTR_ERR_OR_ZERO(b);
	if (ret)
		goto out;

	if (!b || b->data->keys.seq != a->seq) {
		struct printbuf buf = PRINTBUF;

		if (b)
			bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&b->key));
		else
			prt_str(&buf, "(null");
		bch_info(c, "%s: node to rewrite not found:, searching for seq %llu, got\n%s",
			 __func__, a->seq, buf.buf);
		printbuf_exit(&buf);
		goto out;
	}

	ret = bch2_btree_node_rewrite(trans, &iter, b, 0);
out:
	bch2_trans_iter_exit(trans, &iter);

	return ret;
}

static void async_btree_node_rewrite_work(struct work_struct *work)
{
	struct async_btree_rewrite *a =
		container_of(work, struct async_btree_rewrite, work);
	struct bch_fs *c = a->c;
	int ret;

	ret = bch2_trans_do(c, NULL, NULL, 0,
		      async_btree_node_rewrite_trans(trans, a));
	bch_err_fn_ratelimited(c, ret);
	bch2_write_ref_put(c, BCH_WRITE_REF_node_rewrite);
	kfree(a);
}

void bch2_btree_node_rewrite_async(struct bch_fs *c, struct btree *b)
{
	struct async_btree_rewrite *a;
	int ret;

	a = kmalloc(sizeof(*a), GFP_NOFS);
	if (!a) {
		bch_err(c, "%s: error allocating memory", __func__);
		return;
	}

	a->c		= c;
	a->btree_id	= b->c.btree_id;
	a->level	= b->c.level;
	a->pos		= b->key.k.p;
	a->seq		= b->data->keys.seq;
	INIT_WORK(&a->work, async_btree_node_rewrite_work);

	if (unlikely(!test_bit(BCH_FS_may_go_rw, &c->flags))) {
		mutex_lock(&c->pending_node_rewrites_lock);
		list_add(&a->list, &c->pending_node_rewrites);
		mutex_unlock(&c->pending_node_rewrites_lock);
		return;
	}

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_node_rewrite)) {
		if (test_bit(BCH_FS_started, &c->flags)) {
			bch_err(c, "%s: error getting c->writes ref", __func__);
			kfree(a);
			return;
		}

		ret = bch2_fs_read_write_early(c);
		bch_err_msg(c, ret, "going read-write");
		if (ret) {
			kfree(a);
			return;
		}

		bch2_write_ref_get(c, BCH_WRITE_REF_node_rewrite);
	}

	queue_work(c->btree_node_rewrite_worker, &a->work);
}

void bch2_do_pending_node_rewrites(struct bch_fs *c)
{
	struct async_btree_rewrite *a, *n;

	mutex_lock(&c->pending_node_rewrites_lock);
	list_for_each_entry_safe(a, n, &c->pending_node_rewrites, list) {
		list_del(&a->list);

		bch2_write_ref_get(c, BCH_WRITE_REF_node_rewrite);
		queue_work(c->btree_node_rewrite_worker, &a->work);
	}
	mutex_unlock(&c->pending_node_rewrites_lock);
}

void bch2_free_pending_node_rewrites(struct bch_fs *c)
{
	struct async_btree_rewrite *a, *n;

	mutex_lock(&c->pending_node_rewrites_lock);
	list_for_each_entry_safe(a, n, &c->pending_node_rewrites, list) {
		list_del(&a->list);

		kfree(a);
	}
	mutex_unlock(&c->pending_node_rewrites_lock);
}

static int __bch2_btree_node_update_key(struct btree_trans *trans,
					struct btree_iter *iter,
					struct btree *b, struct btree *new_hash,
					struct bkey_i *new_key,
					unsigned commit_flags,
					bool skip_triggers)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter2 = { NULL };
	struct btree *parent;
	int ret;

	if (!skip_triggers) {
		ret   = bch2_key_trigger_old(trans, b->c.btree_id, b->c.level + 1,
					     bkey_i_to_s_c(&b->key),
					     BTREE_TRIGGER_TRANSACTIONAL) ?:
			bch2_key_trigger_new(trans, b->c.btree_id, b->c.level + 1,
					     bkey_i_to_s(new_key),
					     BTREE_TRIGGER_TRANSACTIONAL);
		if (ret)
			return ret;
	}

	if (new_hash) {
		bkey_copy(&new_hash->key, new_key);
		ret = bch2_btree_node_hash_insert(&c->btree_cache,
				new_hash, b->c.level, b->c.btree_id);
		BUG_ON(ret);
	}

	parent = btree_node_parent(btree_iter_path(trans, iter), b);
	if (parent) {
		bch2_trans_copy_iter(&iter2, iter);

		iter2.path = bch2_btree_path_make_mut(trans, iter2.path,
				iter2.flags & BTREE_ITER_INTENT,
				_THIS_IP_);

		struct btree_path *path2 = btree_iter_path(trans, &iter2);
		BUG_ON(path2->level != b->c.level);
		BUG_ON(!bpos_eq(path2->pos, new_key->k.p));

		btree_path_set_level_up(trans, path2);

		trans->paths_sorted = false;

		ret   = bch2_btree_iter_traverse(&iter2) ?:
			bch2_trans_update(trans, &iter2, new_key, BTREE_TRIGGER_NORUN);
		if (ret)
			goto err;
	} else {
		BUG_ON(btree_node_root(c, b) != b);

		struct jset_entry *e = bch2_trans_jset_entry_alloc(trans,
				       jset_u64s(new_key->k.u64s));
		ret = PTR_ERR_OR_ZERO(e);
		if (ret)
			return ret;

		journal_entry_set(e,
				  BCH_JSET_ENTRY_btree_root,
				  b->c.btree_id, b->c.level,
				  new_key, new_key->k.u64s);
	}

	ret = bch2_trans_commit(trans, NULL, NULL, commit_flags);
	if (ret)
		goto err;

	bch2_btree_node_lock_write_nofail(trans, btree_iter_path(trans, iter), &b->c);

	if (new_hash) {
		mutex_lock(&c->btree_cache.lock);
		bch2_btree_node_hash_remove(&c->btree_cache, new_hash);
		bch2_btree_node_hash_remove(&c->btree_cache, b);

		bkey_copy(&b->key, new_key);
		ret = __bch2_btree_node_hash_insert(&c->btree_cache, b);
		BUG_ON(ret);
		mutex_unlock(&c->btree_cache.lock);
	} else {
		bkey_copy(&b->key, new_key);
	}

	bch2_btree_node_unlock_write(trans, btree_iter_path(trans, iter), b);
out:
	bch2_trans_iter_exit(trans, &iter2);
	return ret;
err:
	if (new_hash) {
		mutex_lock(&c->btree_cache.lock);
		bch2_btree_node_hash_remove(&c->btree_cache, b);
		mutex_unlock(&c->btree_cache.lock);
	}
	goto out;
}

int bch2_btree_node_update_key(struct btree_trans *trans, struct btree_iter *iter,
			       struct btree *b, struct bkey_i *new_key,
			       unsigned commit_flags, bool skip_triggers)
{
	struct bch_fs *c = trans->c;
	struct btree *new_hash = NULL;
	struct btree_path *path = btree_iter_path(trans, iter);
	struct closure cl;
	int ret = 0;

	ret = bch2_btree_path_upgrade(trans, path, b->c.level + 1);
	if (ret)
		return ret;

	closure_init_stack(&cl);

	/*
	 * check btree_ptr_hash_val() after @b is locked by
	 * btree_iter_traverse():
	 */
	if (btree_ptr_hash_val(new_key) != b->hash_val) {
		ret = bch2_btree_cache_cannibalize_lock(trans, &cl);
		if (ret) {
			ret = drop_locks_do(trans, (closure_sync(&cl), 0));
			if (ret)
				return ret;
		}

		new_hash = bch2_btree_node_mem_alloc(trans, false);
	}

	path->intent_ref++;
	ret = __bch2_btree_node_update_key(trans, iter, b, new_hash, new_key,
					   commit_flags, skip_triggers);
	--path->intent_ref;

	if (new_hash) {
		mutex_lock(&c->btree_cache.lock);
		list_move(&new_hash->list, &c->btree_cache.freeable);
		mutex_unlock(&c->btree_cache.lock);

		six_unlock_write(&new_hash->c.lock);
		six_unlock_intent(&new_hash->c.lock);
	}
	closure_sync(&cl);
	bch2_btree_cache_cannibalize_unlock(trans);
	return ret;
}

int bch2_btree_node_update_key_get_iter(struct btree_trans *trans,
					struct btree *b, struct bkey_i *new_key,
					unsigned commit_flags, bool skip_triggers)
{
	struct btree_iter iter;
	int ret;

	bch2_trans_node_iter_init(trans, &iter, b->c.btree_id, b->key.k.p,
				  BTREE_MAX_DEPTH, b->c.level,
				  BTREE_ITER_INTENT);
	ret = bch2_btree_iter_traverse(&iter);
	if (ret)
		goto out;

	/* has node been freed? */
	if (btree_iter_path(trans, &iter)->l[b->c.level].b != b) {
		/* node has been freed: */
		BUG_ON(!btree_node_dying(b));
		goto out;
	}

	BUG_ON(!btree_node_hashed(b));

	struct bch_extent_ptr *ptr;
	bch2_bkey_drop_ptrs(bkey_i_to_s(new_key), ptr,
			    !bch2_bkey_has_device(bkey_i_to_s(&b->key), ptr->dev));

	ret = bch2_btree_node_update_key(trans, &iter, b, new_key,
					 commit_flags, skip_triggers);
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

/* Init code: */

/*
 * Only for filesystem bringup, when first reading the btree roots or allocating
 * btree roots when initializing a new filesystem:
 */
void bch2_btree_set_root_for_read(struct bch_fs *c, struct btree *b)
{
	BUG_ON(btree_node_root(c, b));

	bch2_btree_set_root_inmem(c, b);
}

static int __bch2_btree_root_alloc(struct btree_trans *trans, enum btree_id id)
{
	struct bch_fs *c = trans->c;
	struct closure cl;
	struct btree *b;
	int ret;

	closure_init_stack(&cl);

	do {
		ret = bch2_btree_cache_cannibalize_lock(trans, &cl);
		closure_sync(&cl);
	} while (ret);

	b = bch2_btree_node_mem_alloc(trans, false);
	bch2_btree_cache_cannibalize_unlock(trans);

	set_btree_node_fake(b);
	set_btree_node_need_rewrite(b);
	b->c.level	= 0;
	b->c.btree_id	= id;

	bkey_btree_ptr_init(&b->key);
	b->key.k.p = SPOS_MAX;
	*((u64 *) bkey_i_to_btree_ptr(&b->key)->v.start) = U64_MAX - id;

	bch2_bset_init_first(b, &b->data->keys);
	bch2_btree_build_aux_trees(b);

	b->data->flags = 0;
	btree_set_min(b, POS_MIN);
	btree_set_max(b, SPOS_MAX);
	b->data->format = bch2_btree_calc_format(b);
	btree_node_set_format(b, b->data->format);

	ret = bch2_btree_node_hash_insert(&c->btree_cache, b,
					  b->c.level, b->c.btree_id);
	BUG_ON(ret);

	bch2_btree_set_root_inmem(c, b);

	six_unlock_write(&b->c.lock);
	six_unlock_intent(&b->c.lock);
	return 0;
}

void bch2_btree_root_alloc(struct bch_fs *c, enum btree_id id)
{
	bch2_trans_run(c, __bch2_btree_root_alloc(trans, id));
}

void bch2_btree_updates_to_text(struct printbuf *out, struct bch_fs *c)
{
	struct btree_update *as;

	mutex_lock(&c->btree_interior_update_lock);
	list_for_each_entry(as, &c->btree_interior_update_list, list)
		prt_printf(out, "%ps: mode=%u nodes_written=%u cl.remaining=%u journal_seq=%llu\n",
			   (void *) as->ip_started,
			   as->mode,
			   as->nodes_written,
			   closure_nr_remaining(&as->cl),
			   as->journal.seq);
	mutex_unlock(&c->btree_interior_update_lock);
}

static bool bch2_btree_interior_updates_pending(struct bch_fs *c)
{
	bool ret;

	mutex_lock(&c->btree_interior_update_lock);
	ret = !list_empty(&c->btree_interior_update_list);
	mutex_unlock(&c->btree_interior_update_lock);

	return ret;
}

bool bch2_btree_interior_updates_flush(struct bch_fs *c)
{
	bool ret = bch2_btree_interior_updates_pending(c);

	if (ret)
		closure_wait_event(&c->btree_interior_update_wait,
				   !bch2_btree_interior_updates_pending(c));
	return ret;
}

void bch2_journal_entry_to_btree_root(struct bch_fs *c, struct jset_entry *entry)
{
	struct btree_root *r = bch2_btree_id_root(c, entry->btree_id);

	mutex_lock(&c->btree_root_lock);

	r->level = entry->level;
	r->alive = true;
	bkey_copy(&r->key, (struct bkey_i *) entry->start);

	mutex_unlock(&c->btree_root_lock);
}

struct jset_entry *
bch2_btree_roots_to_journal_entries(struct bch_fs *c,
				    struct jset_entry *end,
				    unsigned long skip)
{
	unsigned i;

	mutex_lock(&c->btree_root_lock);

	for (i = 0; i < btree_id_nr_alive(c); i++) {
		struct btree_root *r = bch2_btree_id_root(c, i);

		if (r->alive && !test_bit(i, &skip)) {
			journal_entry_set(end, BCH_JSET_ENTRY_btree_root,
					  i, r->level, &r->key, r->key.k.u64s);
			end = vstruct_next(end);
		}
	}

	mutex_unlock(&c->btree_root_lock);

	return end;
}

void bch2_fs_btree_interior_update_exit(struct bch_fs *c)
{
	if (c->btree_node_rewrite_worker)
		destroy_workqueue(c->btree_node_rewrite_worker);
	if (c->btree_interior_update_worker)
		destroy_workqueue(c->btree_interior_update_worker);
	mempool_exit(&c->btree_interior_update_pool);
}

void bch2_fs_btree_interior_update_init_early(struct bch_fs *c)
{
	mutex_init(&c->btree_reserve_cache_lock);
	INIT_LIST_HEAD(&c->btree_interior_update_list);
	INIT_LIST_HEAD(&c->btree_interior_updates_unwritten);
	mutex_init(&c->btree_interior_update_lock);
	INIT_WORK(&c->btree_interior_update_work, btree_interior_update_work);

	INIT_LIST_HEAD(&c->pending_node_rewrites);
	mutex_init(&c->pending_node_rewrites_lock);
}

int bch2_fs_btree_interior_update_init(struct bch_fs *c)
{
	c->btree_interior_update_worker =
		alloc_workqueue("btree_update", WQ_UNBOUND|WQ_MEM_RECLAIM, 8);
	if (!c->btree_interior_update_worker)
		return -BCH_ERR_ENOMEM_btree_interior_update_worker_init;

	c->btree_node_rewrite_worker =
		alloc_ordered_workqueue("btree_node_rewrite", WQ_UNBOUND);
	if (!c->btree_node_rewrite_worker)
		return -BCH_ERR_ENOMEM_btree_interior_update_worker_init;

	if (mempool_init_kmalloc_pool(&c->btree_interior_update_pool, 1,
				      sizeof(struct btree_update)))
		return -BCH_ERR_ENOMEM_btree_interior_update_pool_init;

	return 0;
}
