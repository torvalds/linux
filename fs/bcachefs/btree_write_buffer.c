// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_locking.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_write_buffer.h"
#include "error.h"
#include "journal.h"
#include "journal_io.h"
#include "journal_reclaim.h"

#include <linux/prefetch.h>

static int bch2_btree_write_buffer_journal_flush(struct journal *,
				struct journal_entry_pin *, u64);

static int bch2_journal_keys_to_write_buffer(struct bch_fs *, struct journal_buf *);

static inline bool __wb_key_ref_cmp(const struct wb_key_ref *l, const struct wb_key_ref *r)
{
	return (cmp_int(l->hi, r->hi) ?:
		cmp_int(l->mi, r->mi) ?:
		cmp_int(l->lo, r->lo)) >= 0;
}

static inline bool wb_key_ref_cmp(const struct wb_key_ref *l, const struct wb_key_ref *r)
{
#ifdef CONFIG_X86_64
	int cmp;

	asm("mov   (%[l]), %%rax;"
	    "sub   (%[r]), %%rax;"
	    "mov  8(%[l]), %%rax;"
	    "sbb  8(%[r]), %%rax;"
	    "mov 16(%[l]), %%rax;"
	    "sbb 16(%[r]), %%rax;"
	    : "=@ccae" (cmp)
	    : [l] "r" (l), [r] "r" (r)
	    : "rax", "cc");

	EBUG_ON(cmp != __wb_key_ref_cmp(l, r));
	return cmp;
#else
	return __wb_key_ref_cmp(l, r);
#endif
}

/* Compare excluding idx, the low 24 bits: */
static inline bool wb_key_eq(const void *_l, const void *_r)
{
	const struct wb_key_ref *l = _l;
	const struct wb_key_ref *r = _r;

	return !((l->hi ^ r->hi)|
		 (l->mi ^ r->mi)|
		 ((l->lo >> 24) ^ (r->lo >> 24)));
}

static noinline void wb_sort(struct wb_key_ref *base, size_t num)
{
	size_t n = num, a = num / 2;

	if (!a)		/* num < 2 || size == 0 */
		return;

	for (;;) {
		size_t b, c, d;

		if (a)			/* Building heap: sift down --a */
			--a;
		else if (--n)		/* Sorting: Extract root to --n */
			swap(base[0], base[n]);
		else			/* Sort complete */
			break;

		/*
		 * Sift element at "a" down into heap.  This is the
		 * "bottom-up" variant, which significantly reduces
		 * calls to cmp_func(): we find the sift-down path all
		 * the way to the leaves (one compare per level), then
		 * backtrack to find where to insert the target element.
		 *
		 * Because elements tend to sift down close to the leaves,
		 * this uses fewer compares than doing two per level
		 * on the way down.  (A bit more than half as many on
		 * average, 3/4 worst-case.)
		 */
		for (b = a; c = 2*b + 1, (d = c + 1) < n;)
			b = wb_key_ref_cmp(base + c, base + d) ? c : d;
		if (d == n)		/* Special case last leaf with no sibling */
			b = c;

		/* Now backtrack from "b" to the correct location for "a" */
		while (b != a && wb_key_ref_cmp(base + a, base + b))
			b = (b - 1) / 2;
		c = b;			/* Where "a" belongs */
		while (b != a) {	/* Shift it into place */
			b = (b - 1) / 2;
			swap(base[b], base[c]);
		}
	}
}

static noinline int wb_flush_one_slowpath(struct btree_trans *trans,
					  struct btree_iter *iter,
					  struct btree_write_buffered_key *wb)
{
	struct btree_path *path = btree_iter_path(trans, iter);

	bch2_btree_node_unlock_write(trans, path, path->l[0].b);

	trans->journal_res.seq = wb->journal_seq;

	return bch2_trans_update(trans, iter, &wb->k,
				 BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE) ?:
		bch2_trans_commit(trans, NULL, NULL,
				  BCH_TRANS_COMMIT_no_enospc|
				  BCH_TRANS_COMMIT_no_check_rw|
				  BCH_TRANS_COMMIT_no_journal_res|
				  BCH_TRANS_COMMIT_journal_reclaim);
}

static inline int wb_flush_one(struct btree_trans *trans, struct btree_iter *iter,
			       struct btree_write_buffered_key *wb,
			       bool *write_locked, size_t *fast)
{
	struct bch_fs *c = trans->c;
	struct btree_path *path;
	int ret;

	EBUG_ON(!wb->journal_seq);
	EBUG_ON(!c->btree_write_buffer.flushing.pin.seq);
	EBUG_ON(c->btree_write_buffer.flushing.pin.seq > wb->journal_seq);

	ret = bch2_btree_iter_traverse(iter);
	if (ret)
		return ret;

	/*
	 * We can't clone a path that has write locks: unshare it now, before
	 * set_pos and traverse():
	 */
	if (btree_iter_path(trans, iter)->ref > 1)
		iter->path = __bch2_btree_path_make_mut(trans, iter->path, true, _THIS_IP_);

	path = btree_iter_path(trans, iter);

	if (!*write_locked) {
		ret = bch2_btree_node_lock_write(trans, path, &path->l[0].b->c);
		if (ret)
			return ret;

		bch2_btree_node_prep_for_write(trans, path, path->l[0].b);
		*write_locked = true;
	}

	if (unlikely(!bch2_btree_node_insert_fits(c, path->l[0].b, wb->k.k.u64s))) {
		*write_locked = false;
		return wb_flush_one_slowpath(trans, iter, wb);
	}

	bch2_btree_insert_key_leaf(trans, path, &wb->k, wb->journal_seq);
	(*fast)++;
	return 0;
}

/*
 * Update a btree with a write buffered key using the journal seq of the
 * original write buffer insert.
 *
 * It is not safe to rejournal the key once it has been inserted into the write
 * buffer because that may break recovery ordering. For example, the key may
 * have already been modified in the active write buffer in a seq that comes
 * before the current transaction. If we were to journal this key again and
 * crash, recovery would process updates in the wrong order.
 */
static int
btree_write_buffered_insert(struct btree_trans *trans,
			  struct btree_write_buffered_key *wb)
{
	struct btree_iter iter;
	int ret;

	bch2_trans_iter_init(trans, &iter, wb->btree, bkey_start_pos(&wb->k.k),
			     BTREE_ITER_CACHED|BTREE_ITER_INTENT);

	trans->journal_res.seq = wb->journal_seq;

	ret   = bch2_btree_iter_traverse(&iter) ?:
		bch2_trans_update(trans, &iter, &wb->k,
				  BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static void move_keys_from_inc_to_flushing(struct btree_write_buffer *wb)
{
	struct bch_fs *c = container_of(wb, struct bch_fs, btree_write_buffer);
	struct journal *j = &c->journal;

	if (!wb->inc.keys.nr)
		return;

	bch2_journal_pin_add(j, wb->inc.keys.data[0].journal_seq, &wb->flushing.pin,
			     bch2_btree_write_buffer_journal_flush);

	darray_resize(&wb->flushing.keys, min_t(size_t, 1U << 20, wb->flushing.keys.nr + wb->inc.keys.nr));
	darray_resize(&wb->sorted, wb->flushing.keys.size);

	if (!wb->flushing.keys.nr && wb->sorted.size >= wb->inc.keys.nr) {
		swap(wb->flushing.keys, wb->inc.keys);
		goto out;
	}

	size_t nr = min(darray_room(wb->flushing.keys),
			wb->sorted.size - wb->flushing.keys.nr);
	nr = min(nr, wb->inc.keys.nr);

	memcpy(&darray_top(wb->flushing.keys),
	       wb->inc.keys.data,
	       sizeof(wb->inc.keys.data[0]) * nr);

	memmove(wb->inc.keys.data,
		wb->inc.keys.data + nr,
	       sizeof(wb->inc.keys.data[0]) * (wb->inc.keys.nr - nr));

	wb->flushing.keys.nr	+= nr;
	wb->inc.keys.nr		-= nr;
out:
	if (!wb->inc.keys.nr)
		bch2_journal_pin_drop(j, &wb->inc.pin);
	else
		bch2_journal_pin_update(j, wb->inc.keys.data[0].journal_seq, &wb->inc.pin,
					bch2_btree_write_buffer_journal_flush);

	if (j->watermark) {
		spin_lock(&j->lock);
		bch2_journal_set_watermark(j);
		spin_unlock(&j->lock);
	}

	BUG_ON(wb->sorted.size < wb->flushing.keys.nr);
}

static int bch2_btree_write_buffer_flush_locked(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct journal *j = &c->journal;
	struct btree_write_buffer *wb = &c->btree_write_buffer;
	struct btree_iter iter = { NULL };
	size_t skipped = 0, fast = 0, slowpath = 0;
	bool write_locked = false;
	int ret = 0;

	bch2_trans_unlock(trans);
	bch2_trans_begin(trans);

	mutex_lock(&wb->inc.lock);
	move_keys_from_inc_to_flushing(wb);
	mutex_unlock(&wb->inc.lock);

	for (size_t i = 0; i < wb->flushing.keys.nr; i++) {
		wb->sorted.data[i].idx = i;
		wb->sorted.data[i].btree = wb->flushing.keys.data[i].btree;
		memcpy(&wb->sorted.data[i].pos, &wb->flushing.keys.data[i].k.k.p, sizeof(struct bpos));
	}
	wb->sorted.nr = wb->flushing.keys.nr;

	/*
	 * We first sort so that we can detect and skip redundant updates, and
	 * then we attempt to flush in sorted btree order, as this is most
	 * efficient.
	 *
	 * However, since we're not flushing in the order they appear in the
	 * journal we won't be able to drop our journal pin until everything is
	 * flushed - which means this could deadlock the journal if we weren't
	 * passing BCH_TRANS_COMMIT_journal_reclaim. This causes the update to fail
	 * if it would block taking a journal reservation.
	 *
	 * If that happens, simply skip the key so we can optimistically insert
	 * as many keys as possible in the fast path.
	 */
	wb_sort(wb->sorted.data, wb->sorted.nr);

	darray_for_each(wb->sorted, i) {
		struct btree_write_buffered_key *k = &wb->flushing.keys.data[i->idx];

		for (struct wb_key_ref *n = i + 1; n < min(i + 4, &darray_top(wb->sorted)); n++)
			prefetch(&wb->flushing.keys.data[n->idx]);

		BUG_ON(!k->journal_seq);

		if (i + 1 < &darray_top(wb->sorted) &&
		    wb_key_eq(i, i + 1)) {
			struct btree_write_buffered_key *n = &wb->flushing.keys.data[i[1].idx];

			skipped++;
			n->journal_seq = min_t(u64, n->journal_seq, k->journal_seq);
			k->journal_seq = 0;
			continue;
		}

		if (write_locked) {
			struct btree_path *path = btree_iter_path(trans, &iter);

			if (path->btree_id != i->btree ||
			    bpos_gt(k->k.k.p, path->l[0].b->key.k.p)) {
				bch2_btree_node_unlock_write(trans, path, path->l[0].b);
				write_locked = false;
			}
		}

		if (!iter.path || iter.btree_id != k->btree) {
			bch2_trans_iter_exit(trans, &iter);
			bch2_trans_iter_init(trans, &iter, k->btree, k->k.k.p,
					     BTREE_ITER_INTENT|BTREE_ITER_ALL_SNAPSHOTS);
		}

		bch2_btree_iter_set_pos(&iter, k->k.k.p);
		btree_iter_path(trans, &iter)->preserve = false;

		do {
			if (race_fault()) {
				ret = -BCH_ERR_journal_reclaim_would_deadlock;
				break;
			}

			ret = wb_flush_one(trans, &iter, k, &write_locked, &fast);
			if (!write_locked)
				bch2_trans_begin(trans);
		} while (bch2_err_matches(ret, BCH_ERR_transaction_restart));

		if (!ret) {
			k->journal_seq = 0;
		} else if (ret == -BCH_ERR_journal_reclaim_would_deadlock) {
			slowpath++;
			ret = 0;
		} else
			break;
	}

	if (write_locked) {
		struct btree_path *path = btree_iter_path(trans, &iter);
		bch2_btree_node_unlock_write(trans, path, path->l[0].b);
	}
	bch2_trans_iter_exit(trans, &iter);

	if (ret)
		goto err;

	if (slowpath) {
		/*
		 * Flush in the order they were present in the journal, so that
		 * we can release journal pins:
		 * The fastpath zapped the seq of keys that were successfully flushed so
		 * we can skip those here.
		 */
		trace_and_count(c, write_buffer_flush_slowpath, trans, slowpath, wb->flushing.keys.nr);

		darray_for_each(wb->flushing.keys, i) {
			if (!i->journal_seq)
				continue;

			bch2_journal_pin_update(j, i->journal_seq, &wb->flushing.pin,
						bch2_btree_write_buffer_journal_flush);

			bch2_trans_begin(trans);

			ret = commit_do(trans, NULL, NULL,
					BCH_WATERMARK_reclaim|
					BCH_TRANS_COMMIT_no_check_rw|
					BCH_TRANS_COMMIT_no_enospc|
					BCH_TRANS_COMMIT_no_journal_res|
					BCH_TRANS_COMMIT_journal_reclaim,
					btree_write_buffered_insert(trans, i));
			if (ret)
				goto err;
		}
	}
err:
	bch2_fs_fatal_err_on(ret, c, "%s: insert error %s", __func__, bch2_err_str(ret));
	trace_write_buffer_flush(trans, wb->flushing.keys.nr, skipped, fast, 0);
	bch2_journal_pin_drop(j, &wb->flushing.pin);
	wb->flushing.keys.nr = 0;
	return ret;
}

static int fetch_wb_keys_from_journal(struct bch_fs *c, u64 seq)
{
	struct journal *j = &c->journal;
	struct journal_buf *buf;
	int ret = 0;

	while (!ret && (buf = bch2_next_write_buffer_flush_journal_buf(j, seq))) {
		ret = bch2_journal_keys_to_write_buffer(c, buf);
		mutex_unlock(&j->buf_lock);
	}

	return ret;
}

static int btree_write_buffer_flush_seq(struct btree_trans *trans, u64 seq)
{
	struct bch_fs *c = trans->c;
	struct btree_write_buffer *wb = &c->btree_write_buffer;
	int ret = 0, fetch_from_journal_err;

	do {
		bch2_trans_unlock(trans);

		fetch_from_journal_err = fetch_wb_keys_from_journal(c, seq);

		/*
		 * On memory allocation failure, bch2_btree_write_buffer_flush_locked()
		 * is not guaranteed to empty wb->inc:
		 */
		mutex_lock(&wb->flushing.lock);
		ret = bch2_btree_write_buffer_flush_locked(trans);
		mutex_unlock(&wb->flushing.lock);
	} while (!ret &&
		 (fetch_from_journal_err ||
		  (wb->inc.pin.seq && wb->inc.pin.seq <= seq) ||
		  (wb->flushing.pin.seq && wb->flushing.pin.seq <= seq)));

	return ret;
}

static int bch2_btree_write_buffer_journal_flush(struct journal *j,
				struct journal_entry_pin *_pin, u64 seq)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);

	return bch2_trans_run(c, btree_write_buffer_flush_seq(trans, seq));
}

int bch2_btree_write_buffer_flush_sync(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;

	trace_and_count(c, write_buffer_flush_sync, trans, _RET_IP_);

	return btree_write_buffer_flush_seq(trans, journal_cur_seq(&c->journal));
}

int bch2_btree_write_buffer_flush_nocheck_rw(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct btree_write_buffer *wb = &c->btree_write_buffer;
	int ret = 0;

	if (mutex_trylock(&wb->flushing.lock)) {
		ret = bch2_btree_write_buffer_flush_locked(trans);
		mutex_unlock(&wb->flushing.lock);
	}

	return ret;
}

int bch2_btree_write_buffer_tryflush(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_btree_write_buffer))
		return -BCH_ERR_erofs_no_writes;

	int ret = bch2_btree_write_buffer_flush_nocheck_rw(trans);
	bch2_write_ref_put(c, BCH_WRITE_REF_btree_write_buffer);
	return ret;
}

static void bch2_btree_write_buffer_flush_work(struct work_struct *work)
{
	struct bch_fs *c = container_of(work, struct bch_fs, btree_write_buffer.flush_work);
	struct btree_write_buffer *wb = &c->btree_write_buffer;
	int ret;

	mutex_lock(&wb->flushing.lock);
	do {
		ret = bch2_trans_run(c, bch2_btree_write_buffer_flush_locked(trans));
	} while (!ret && bch2_btree_write_buffer_should_flush(c));
	mutex_unlock(&wb->flushing.lock);

	bch2_write_ref_put(c, BCH_WRITE_REF_btree_write_buffer);
}

int bch2_journal_key_to_wb_slowpath(struct bch_fs *c,
			     struct journal_keys_to_wb *dst,
			     enum btree_id btree, struct bkey_i *k)
{
	struct btree_write_buffer *wb = &c->btree_write_buffer;
	int ret;
retry:
	ret = darray_make_room_gfp(&dst->wb->keys, 1, GFP_KERNEL);
	if (!ret && dst->wb == &wb->flushing)
		ret = darray_resize(&wb->sorted, wb->flushing.keys.size);

	if (unlikely(ret)) {
		if (dst->wb == &c->btree_write_buffer.flushing) {
			mutex_unlock(&dst->wb->lock);
			dst->wb = &c->btree_write_buffer.inc;
			bch2_journal_pin_add(&c->journal, dst->seq, &dst->wb->pin,
					     bch2_btree_write_buffer_journal_flush);
			goto retry;
		}

		return ret;
	}

	dst->room = darray_room(dst->wb->keys);
	if (dst->wb == &wb->flushing)
		dst->room = min(dst->room, wb->sorted.size - wb->flushing.keys.nr);
	BUG_ON(!dst->room);
	BUG_ON(!dst->seq);

	struct btree_write_buffered_key *wb_k = &darray_top(dst->wb->keys);
	wb_k->journal_seq	= dst->seq;
	wb_k->btree		= btree;
	bkey_copy(&wb_k->k, k);
	dst->wb->keys.nr++;
	dst->room--;
	return 0;
}

void bch2_journal_keys_to_write_buffer_start(struct bch_fs *c, struct journal_keys_to_wb *dst, u64 seq)
{
	struct btree_write_buffer *wb = &c->btree_write_buffer;

	if (mutex_trylock(&wb->flushing.lock)) {
		mutex_lock(&wb->inc.lock);
		move_keys_from_inc_to_flushing(wb);

		/*
		 * Attempt to skip wb->inc, and add keys directly to
		 * wb->flushing, saving us a copy later:
		 */

		if (!wb->inc.keys.nr) {
			dst->wb = &wb->flushing;
		} else {
			mutex_unlock(&wb->flushing.lock);
			dst->wb = &wb->inc;
		}
	} else {
		mutex_lock(&wb->inc.lock);
		dst->wb = &wb->inc;
	}

	dst->room = darray_room(dst->wb->keys);
	if (dst->wb == &wb->flushing)
		dst->room = min(dst->room, wb->sorted.size - wb->flushing.keys.nr);
	dst->seq = seq;

	bch2_journal_pin_add(&c->journal, seq, &dst->wb->pin,
			     bch2_btree_write_buffer_journal_flush);
}

void bch2_journal_keys_to_write_buffer_end(struct bch_fs *c, struct journal_keys_to_wb *dst)
{
	struct btree_write_buffer *wb = &c->btree_write_buffer;

	if (!dst->wb->keys.nr)
		bch2_journal_pin_drop(&c->journal, &dst->wb->pin);

	if (bch2_btree_write_buffer_should_flush(c) &&
	    __bch2_write_ref_tryget(c, BCH_WRITE_REF_btree_write_buffer) &&
	    !queue_work(system_unbound_wq, &c->btree_write_buffer.flush_work))
		bch2_write_ref_put(c, BCH_WRITE_REF_btree_write_buffer);

	if (dst->wb == &wb->flushing)
		mutex_unlock(&wb->flushing.lock);
	mutex_unlock(&wb->inc.lock);
}

static int bch2_journal_keys_to_write_buffer(struct bch_fs *c, struct journal_buf *buf)
{
	struct journal_keys_to_wb dst;
	struct jset_entry *entry;
	struct bkey_i *k;
	int ret = 0;

	bch2_journal_keys_to_write_buffer_start(c, &dst, le64_to_cpu(buf->data->seq));

	for_each_jset_entry_type(entry, buf->data, BCH_JSET_ENTRY_write_buffer_keys) {
		jset_entry_for_each_key(entry, k) {
			ret = bch2_journal_key_to_wb(c, &dst, entry->btree_id, k);
			if (ret)
				goto out;
		}

		entry->type = BCH_JSET_ENTRY_btree_keys;
	}

	buf->need_flush_to_write_buffer = false;
out:
	bch2_journal_keys_to_write_buffer_end(c, &dst);
	return ret;
}

static int wb_keys_resize(struct btree_write_buffer_keys *wb, size_t new_size)
{
	if (wb->keys.size >= new_size)
		return 0;

	if (!mutex_trylock(&wb->lock))
		return -EINTR;

	int ret = darray_resize(&wb->keys, new_size);
	mutex_unlock(&wb->lock);
	return ret;
}

int bch2_btree_write_buffer_resize(struct bch_fs *c, size_t new_size)
{
	struct btree_write_buffer *wb = &c->btree_write_buffer;

	return wb_keys_resize(&wb->flushing, new_size) ?:
		wb_keys_resize(&wb->inc, new_size);
}

void bch2_fs_btree_write_buffer_exit(struct bch_fs *c)
{
	struct btree_write_buffer *wb = &c->btree_write_buffer;

	BUG_ON((wb->inc.keys.nr || wb->flushing.keys.nr) &&
	       !bch2_journal_error(&c->journal));

	darray_exit(&wb->sorted);
	darray_exit(&wb->flushing.keys);
	darray_exit(&wb->inc.keys);
}

int bch2_fs_btree_write_buffer_init(struct bch_fs *c)
{
	struct btree_write_buffer *wb = &c->btree_write_buffer;

	mutex_init(&wb->inc.lock);
	mutex_init(&wb->flushing.lock);
	INIT_WORK(&wb->flush_work, bch2_btree_write_buffer_flush_work);

	/* Will be resized by journal as needed: */
	unsigned initial_size = 1 << 16;

	return  darray_make_room(&wb->inc.keys, initial_size) ?:
		darray_make_room(&wb->flushing.keys, initial_size) ?:
		darray_make_room(&wb->sorted, initial_size);
}
