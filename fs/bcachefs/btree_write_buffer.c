// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_locking.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_write_buffer.h"
#include "error.h"
#include "journal.h"
#include "journal_reclaim.h"

#include <linux/sort.h>

static int bch2_btree_write_buffer_journal_flush(struct journal *,
				struct journal_entry_pin *, u64);

static int btree_write_buffered_key_cmp(const void *_l, const void *_r)
{
	const struct btree_write_buffered_key *l = _l;
	const struct btree_write_buffered_key *r = _r;

	return  cmp_int(l->btree, r->btree) ?:
		bpos_cmp(l->k.k.p, r->k.k.p) ?:
		cmp_int(l->journal_seq, r->journal_seq) ?:
		cmp_int(l->journal_offset, r->journal_offset);
}

static int btree_write_buffered_journal_cmp(const void *_l, const void *_r)
{
	const struct btree_write_buffered_key *l = _l;
	const struct btree_write_buffered_key *r = _r;

	return  cmp_int(l->journal_seq, r->journal_seq);
}

static int bch2_btree_write_buffer_flush_one(struct btree_trans *trans,
					     struct btree_iter *iter,
					     struct btree_write_buffered_key *wb,
					     unsigned commit_flags,
					     bool *write_locked,
					     size_t *fast)
{
	struct bch_fs *c = trans->c;
	struct btree_path *path;
	int ret;

	ret = bch2_btree_iter_traverse(iter);
	if (ret)
		return ret;

	/*
	 * We can't clone a path that has write locks: unshare it now, before
	 * set_pos and traverse():
	 */
	if (iter->path->ref > 1)
		iter->path = __bch2_btree_path_make_mut(trans, iter->path, true, _THIS_IP_);

	path = iter->path;

	if (!*write_locked) {
		ret = bch2_btree_node_lock_write(trans, path, &path->l[0].b->c);
		if (ret)
			return ret;

		bch2_btree_node_prep_for_write(trans, path, path->l[0].b);
		*write_locked = true;
	}

	if (!bch2_btree_node_insert_fits(c, path->l[0].b, wb->k.k.u64s)) {
		bch2_btree_node_unlock_write(trans, path, path->l[0].b);
		*write_locked = false;
		goto trans_commit;
	}

	bch2_btree_insert_key_leaf(trans, path, &wb->k, wb->journal_seq);
	(*fast)++;
	return 0;
trans_commit:
	trans->journal_res.seq = wb->journal_seq;

	return  bch2_trans_update(trans, iter, &wb->k,
				  BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE) ?:
		bch2_trans_commit(trans, NULL, NULL,
				  commit_flags|
				  BCH_TRANS_COMMIT_no_check_rw|
				  BCH_TRANS_COMMIT_no_enospc|
				  BCH_TRANS_COMMIT_no_journal_res|
				  BCH_TRANS_COMMIT_journal_reclaim);
}

static union btree_write_buffer_state btree_write_buffer_switch(struct btree_write_buffer *wb)
{
	union btree_write_buffer_state old, new;
	u64 v = READ_ONCE(wb->state.v);

	do {
		old.v = new.v = v;

		new.nr = 0;
		new.idx++;
	} while ((v = atomic64_cmpxchg_acquire(&wb->state.counter, old.v, new.v)) != old.v);

	while (old.idx == 0 ? wb->state.ref0 : wb->state.ref1)
		cpu_relax();

	smp_mb();

	return old;
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

int bch2_btree_write_buffer_flush_locked(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct journal *j = &c->journal;
	struct btree_write_buffer *wb = &c->btree_write_buffer;
	struct journal_entry_pin pin;
	struct btree_write_buffered_key *i, *keys;
	struct btree_iter iter = { NULL };
	size_t nr = 0, skipped = 0, fast = 0, slowpath = 0;
	bool write_locked = false;
	union btree_write_buffer_state s;
	int ret = 0;

	memset(&pin, 0, sizeof(pin));

	bch2_journal_pin_copy(j, &pin, &wb->journal_pin,
			      bch2_btree_write_buffer_journal_flush);
	bch2_journal_pin_drop(j, &wb->journal_pin);

	s = btree_write_buffer_switch(wb);
	keys = wb->keys[s.idx];
	nr = s.nr;

	if (race_fault())
		goto slowpath;

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
	sort(keys, nr, sizeof(keys[0]),
	     btree_write_buffered_key_cmp, NULL);

	for (i = keys; i < keys + nr; i++) {
		if (i + 1 < keys + nr &&
		    i[0].btree == i[1].btree &&
		    bpos_eq(i[0].k.k.p, i[1].k.k.p)) {
			skipped++;
			i->journal_seq = 0;
			continue;
		}

		if (write_locked &&
		    (iter.path->btree_id != i->btree ||
		     bpos_gt(i->k.k.p, iter.path->l[0].b->key.k.p))) {
			bch2_btree_node_unlock_write(trans, iter.path, iter.path->l[0].b);
			write_locked = false;
		}

		if (!iter.path || iter.path->btree_id != i->btree) {
			bch2_trans_iter_exit(trans, &iter);
			bch2_trans_iter_init(trans, &iter, i->btree, i->k.k.p,
					     BTREE_ITER_INTENT|BTREE_ITER_ALL_SNAPSHOTS);
		}

		bch2_btree_iter_set_pos(&iter, i->k.k.p);
		iter.path->preserve = false;

		do {
			ret = bch2_btree_write_buffer_flush_one(trans, &iter, i, 0,
								&write_locked, &fast);
			if (!write_locked)
				bch2_trans_begin(trans);
		} while (bch2_err_matches(ret, BCH_ERR_transaction_restart));

		if (ret == -BCH_ERR_journal_reclaim_would_deadlock) {
			slowpath++;
			continue;
		}
		if (ret)
			break;

		i->journal_seq = 0;
	}

	if (write_locked)
		bch2_btree_node_unlock_write(trans, iter.path, iter.path->l[0].b);
	bch2_trans_iter_exit(trans, &iter);

	trace_write_buffer_flush(trans, nr, skipped, fast, wb->size);

	if (slowpath)
		goto slowpath;

	bch2_fs_fatal_err_on(ret, c, "%s: insert error %s", __func__, bch2_err_str(ret));
out:
	bch2_journal_pin_drop(j, &pin);
	return ret;
slowpath:
	trace_and_count(c, write_buffer_flush_slowpath, trans, slowpath, nr);

	/*
	 * Now sort the rest by journal seq and bump the journal pin as we go.
	 * The slowpath zapped the seq of keys that were successfully flushed so
	 * we can skip those here.
	 */
	sort(keys, nr, sizeof(keys[0]),
	     btree_write_buffered_journal_cmp,
	     NULL);

	for (i = keys; i < keys + nr; i++) {
		if (!i->journal_seq)
			continue;

		bch2_journal_pin_update(j, i->journal_seq, &pin,
			      bch2_btree_write_buffer_journal_flush);

		ret = commit_do(trans, NULL, NULL,
				BCH_WATERMARK_reclaim|
				BCH_TRANS_COMMIT_no_check_rw|
				BCH_TRANS_COMMIT_no_enospc|
				BCH_TRANS_COMMIT_no_journal_res|
				BCH_TRANS_COMMIT_journal_reclaim,
				btree_write_buffered_insert(trans, i));
		if (bch2_fs_fatal_err_on(ret, c, "%s: insert error %s", __func__, bch2_err_str(ret)))
			break;
	}

	goto out;
}

int bch2_btree_write_buffer_flush_sync(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_btree_write_buffer))
		return -BCH_ERR_erofs_no_writes;

	trace_and_count(c, write_buffer_flush_sync, trans, _RET_IP_);

	bch2_trans_unlock(trans);
	mutex_lock(&c->btree_write_buffer.flush_lock);
	int ret = bch2_btree_write_buffer_flush_locked(trans);
	mutex_unlock(&c->btree_write_buffer.flush_lock);
	bch2_write_ref_put(c, BCH_WRITE_REF_btree_write_buffer);
	return ret;
}

int bch2_btree_write_buffer_flush_nocheck_rw(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct btree_write_buffer *wb = &c->btree_write_buffer;
	int ret = 0;

	if (mutex_trylock(&wb->flush_lock)) {
		ret = bch2_btree_write_buffer_flush_locked(trans);
		mutex_unlock(&wb->flush_lock);
	}

	return ret;
}

int bch2_btree_write_buffer_flush(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_btree_write_buffer))
		return -BCH_ERR_erofs_no_writes;

	int ret = bch2_btree_write_buffer_flush_nocheck_rw(trans);
	bch2_write_ref_put(c, BCH_WRITE_REF_btree_write_buffer);
	return ret;
}

static int bch2_btree_write_buffer_journal_flush(struct journal *j,
				struct journal_entry_pin *_pin, u64 seq)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct btree_write_buffer *wb = &c->btree_write_buffer;

	mutex_lock(&wb->flush_lock);
	int ret = bch2_trans_run(c, bch2_btree_write_buffer_flush_locked(trans));
	mutex_unlock(&wb->flush_lock);

	return ret;
}

static inline u64 btree_write_buffer_ref(int idx)
{
	return ((union btree_write_buffer_state) {
		.ref0 = idx == 0,
		.ref1 = idx == 1,
	}).v;
}

int bch2_btree_insert_keys_write_buffer(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct btree_write_buffer *wb = &c->btree_write_buffer;
	struct btree_write_buffered_key *i;
	union btree_write_buffer_state old, new;
	int ret = 0;
	u64 v;

	trans_for_each_wb_update(trans, i) {
		EBUG_ON(i->k.k.u64s > BTREE_WRITE_BUFERED_U64s_MAX);

		i->journal_seq		= trans->journal_res.seq;
		i->journal_offset	= trans->journal_res.offset;
	}

	preempt_disable();
	v = READ_ONCE(wb->state.v);
	do {
		old.v = new.v = v;

		new.v += btree_write_buffer_ref(new.idx);
		new.nr += trans->nr_wb_updates;
		if (new.nr > wb->size) {
			ret = -BCH_ERR_btree_insert_need_flush_buffer;
			goto out;
		}
	} while ((v = atomic64_cmpxchg_acquire(&wb->state.counter, old.v, new.v)) != old.v);

	memcpy(wb->keys[new.idx] + old.nr,
	       trans->wb_updates,
	       sizeof(trans->wb_updates[0]) * trans->nr_wb_updates);

	bch2_journal_pin_add(&c->journal, trans->journal_res.seq, &wb->journal_pin,
			     bch2_btree_write_buffer_journal_flush);

	atomic64_sub_return_release(btree_write_buffer_ref(new.idx), &wb->state.counter);
out:
	preempt_enable();
	return ret;
}

void bch2_fs_btree_write_buffer_exit(struct bch_fs *c)
{
	struct btree_write_buffer *wb = &c->btree_write_buffer;

	BUG_ON(wb->state.nr && !bch2_journal_error(&c->journal));

	kvfree(wb->keys[1]);
	kvfree(wb->keys[0]);
}

int bch2_fs_btree_write_buffer_init(struct bch_fs *c)
{
	struct btree_write_buffer *wb = &c->btree_write_buffer;

	mutex_init(&wb->flush_lock);
	wb->size = c->opts.btree_write_buffer_size;

	wb->keys[0] = kvmalloc_array(wb->size, sizeof(*wb->keys[0]), GFP_KERNEL);
	wb->keys[1] = kvmalloc_array(wb->size, sizeof(*wb->keys[1]), GFP_KERNEL);
	if (!wb->keys[0] || !wb->keys[1])
		return -BCH_ERR_ENOMEM_fs_btree_write_buffer_init;

	return 0;
}
