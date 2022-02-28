// SPDX-License-Identifier: GPL-2.0
/*
 * bcachefs journalling code, for btree insertions
 *
 * Copyright 2012 Google, Inc.
 */

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "bkey_methods.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "buckets.h"
#include "error.h"
#include "journal.h"
#include "journal_io.h"
#include "journal_reclaim.h"
#include "journal_seq_blacklist.h"
#include "super-io.h"
#include "trace.h"

static inline bool journal_seq_unwritten(struct journal *j, u64 seq)
{
	return seq > j->seq_ondisk;
}

static bool __journal_entry_is_open(union journal_res_state state)
{
	return state.cur_entry_offset < JOURNAL_ENTRY_CLOSED_VAL;
}

static inline unsigned nr_unwritten_journal_entries(struct journal *j)
{
	return atomic64_read(&j->seq) - j->seq_ondisk;
}

static bool journal_entry_is_open(struct journal *j)
{
	return __journal_entry_is_open(j->reservations);
}

static inline struct journal_buf *
journal_seq_to_buf(struct journal *j, u64 seq)
{
	struct journal_buf *buf = NULL;

	EBUG_ON(seq > journal_cur_seq(j));

	if (journal_seq_unwritten(j, seq)) {
		buf = j->buf + (seq & JOURNAL_BUF_MASK);
		EBUG_ON(le64_to_cpu(buf->data->seq) != seq);
	}
	return buf;
}

static void journal_pin_list_init(struct journal_entry_pin_list *p, int count)
{
	INIT_LIST_HEAD(&p->list);
	INIT_LIST_HEAD(&p->key_cache_list);
	INIT_LIST_HEAD(&p->flushed);
	atomic_set(&p->count, count);
	p->devs.nr = 0;
}

/* journal entry close/open: */

void __bch2_journal_buf_put(struct journal *j)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);

	closure_call(&j->io, bch2_journal_write, c->io_complete_wq, NULL);
}

/*
 * Returns true if journal entry is now closed:
 *
 * We don't close a journal_buf until the next journal_buf is finished writing,
 * and can be opened again - this also initializes the next journal_buf:
 */
static void __journal_entry_close(struct journal *j, unsigned closed_val)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_buf *buf = journal_cur_buf(j);
	union journal_res_state old, new;
	u64 v = atomic64_read(&j->reservations.counter);
	unsigned sectors;

	BUG_ON(closed_val != JOURNAL_ENTRY_CLOSED_VAL &&
	       closed_val != JOURNAL_ENTRY_ERROR_VAL);

	lockdep_assert_held(&j->lock);

	do {
		old.v = new.v = v;
		new.cur_entry_offset = closed_val;

		if (old.cur_entry_offset == JOURNAL_ENTRY_ERROR_VAL ||
		    old.cur_entry_offset == new.cur_entry_offset)
			return;
	} while ((v = atomic64_cmpxchg(&j->reservations.counter,
				       old.v, new.v)) != old.v);

	if (!__journal_entry_is_open(old))
		return;

	/* Close out old buffer: */
	buf->data->u64s		= cpu_to_le32(old.cur_entry_offset);

	sectors = vstruct_blocks_plus(buf->data, c->block_bits,
				      buf->u64s_reserved) << c->block_bits;
	BUG_ON(sectors > buf->sectors);
	buf->sectors = sectors;

	/*
	 * We have to set last_seq here, _before_ opening a new journal entry:
	 *
	 * A threads may replace an old pin with a new pin on their current
	 * journal reservation - the expectation being that the journal will
	 * contain either what the old pin protected or what the new pin
	 * protects.
	 *
	 * After the old pin is dropped journal_last_seq() won't include the old
	 * pin, so we can only write the updated last_seq on the entry that
	 * contains whatever the new pin protects.
	 *
	 * Restated, we can _not_ update last_seq for a given entry if there
	 * could be a newer entry open with reservations/pins that have been
	 * taken against it.
	 *
	 * Hence, we want update/set last_seq on the current journal entry right
	 * before we open a new one:
	 */
	buf->last_seq		= journal_last_seq(j);
	buf->data->last_seq	= cpu_to_le64(buf->last_seq);
	BUG_ON(buf->last_seq > le64_to_cpu(buf->data->seq));

	__bch2_journal_pin_put(j, le64_to_cpu(buf->data->seq));

	cancel_delayed_work(&j->write_work);

	bch2_journal_space_available(j);

	bch2_journal_buf_put(j, old.idx);
}

void bch2_journal_halt(struct journal *j)
{
	spin_lock(&j->lock);
	__journal_entry_close(j, JOURNAL_ENTRY_ERROR_VAL);
	if (!j->err_seq)
		j->err_seq = journal_cur_seq(j);
	spin_unlock(&j->lock);
}

static bool journal_entry_want_write(struct journal *j)
{
	bool ret = !journal_entry_is_open(j) ||
		journal_cur_seq(j) == journal_last_unwritten_seq(j);

	/* Don't close it yet if we already have a write in flight: */
	if (ret)
		__journal_entry_close(j, JOURNAL_ENTRY_CLOSED_VAL);
	else if (nr_unwritten_journal_entries(j)) {
		struct journal_buf *buf = journal_cur_buf(j);

		if (!buf->flush_time) {
			buf->flush_time	= local_clock() ?: 1;
			buf->expires = jiffies;
		}
	}

	return ret;
}

static bool journal_entry_close(struct journal *j)
{
	bool ret;

	spin_lock(&j->lock);
	ret = journal_entry_want_write(j);
	spin_unlock(&j->lock);

	return ret;
}

/*
 * should _only_ called from journal_res_get() - when we actually want a
 * journal reservation - journal entry is open means journal is dirty:
 *
 * returns:
 * 0:		success
 * -ENOSPC:	journal currently full, must invoke reclaim
 * -EAGAIN:	journal blocked, must wait
 * -EROFS:	insufficient rw devices or journal error
 */
static int journal_entry_open(struct journal *j)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_buf *buf = j->buf +
		((journal_cur_seq(j) + 1) & JOURNAL_BUF_MASK);
	union journal_res_state old, new;
	int u64s;
	u64 v;

	lockdep_assert_held(&j->lock);
	BUG_ON(journal_entry_is_open(j));
	BUG_ON(BCH_SB_CLEAN(c->disk_sb.sb));

	if (j->blocked)
		return cur_entry_blocked;

	if (j->cur_entry_error)
		return j->cur_entry_error;

	if (bch2_journal_error(j))
		return cur_entry_insufficient_devices; /* -EROFS */

	if (!fifo_free(&j->pin))
		return cur_entry_journal_pin_full;

	if (nr_unwritten_journal_entries(j) == ARRAY_SIZE(j->buf))
		return cur_entry_max_in_flight;

	BUG_ON(!j->cur_entry_sectors);

	buf->expires		=
		(journal_cur_seq(j) == j->flushed_seq_ondisk
		 ? jiffies
		 : j->last_flush_write) +
		msecs_to_jiffies(c->opts.journal_flush_delay);

	buf->u64s_reserved	= j->entry_u64s_reserved;
	buf->disk_sectors	= j->cur_entry_sectors;
	buf->sectors		= min(buf->disk_sectors, buf->buf_size >> 9);

	u64s = (int) (buf->sectors << 9) / sizeof(u64) -
		journal_entry_overhead(j);
	u64s = clamp_t(int, u64s, 0, JOURNAL_ENTRY_CLOSED_VAL - 1);

	if (u64s <= 0)
		return cur_entry_journal_full;

	/*
	 * The fifo_push() needs to happen at the same time as j->seq is
	 * incremented for journal_last_seq() to be calculated correctly
	 */
	atomic64_inc(&j->seq);
	journal_pin_list_init(fifo_push_ref(&j->pin), 1);

	BUG_ON(j->buf + (journal_cur_seq(j) & JOURNAL_BUF_MASK) != buf);

	bkey_extent_init(&buf->key);
	buf->noflush	= false;
	buf->must_flush	= false;
	buf->separate_flush = false;
	buf->flush_time	= 0;

	memset(buf->data, 0, sizeof(*buf->data));
	buf->data->seq	= cpu_to_le64(journal_cur_seq(j));
	buf->data->u64s	= 0;

	/*
	 * Must be set before marking the journal entry as open:
	 */
	j->cur_entry_u64s = u64s;

	v = atomic64_read(&j->reservations.counter);
	do {
		old.v = new.v = v;

		BUG_ON(old.cur_entry_offset == JOURNAL_ENTRY_ERROR_VAL);

		new.idx++;
		BUG_ON(journal_state_count(new, new.idx));
		BUG_ON(new.idx != (journal_cur_seq(j) & JOURNAL_BUF_MASK));

		journal_state_inc(&new);
		new.cur_entry_offset = 0;
	} while ((v = atomic64_cmpxchg(&j->reservations.counter,
				       old.v, new.v)) != old.v);

	if (j->res_get_blocked_start)
		bch2_time_stats_update(j->blocked_time,
				       j->res_get_blocked_start);
	j->res_get_blocked_start = 0;

	mod_delayed_work(c->io_complete_wq,
			 &j->write_work,
			 msecs_to_jiffies(c->opts.journal_flush_delay));
	journal_wake(j);
	return 0;
}

static bool journal_quiesced(struct journal *j)
{
	bool ret = atomic64_read(&j->seq) == j->seq_ondisk;

	if (!ret)
		journal_entry_close(j);
	return ret;
}

static void journal_quiesce(struct journal *j)
{
	wait_event(j->wait, journal_quiesced(j));
}

static void journal_write_work(struct work_struct *work)
{
	struct journal *j = container_of(work, struct journal, write_work.work);
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_buf *buf;
	long delta;

	spin_lock(&j->lock);
	buf = journal_cur_buf(j);
	delta = buf->expires - jiffies;

	if (delta > 0)
		mod_delayed_work(c->io_complete_wq, &j->write_work, delta);
	else
		__journal_entry_close(j, JOURNAL_ENTRY_CLOSED_VAL);
	spin_unlock(&j->lock);
}

static int __journal_res_get(struct journal *j, struct journal_res *res,
			     unsigned flags)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_buf *buf;
	bool can_discard;
	int ret;
retry:
	if (journal_res_get_fast(j, res, flags))
		return 0;

	if (bch2_journal_error(j))
		return -EROFS;

	spin_lock(&j->lock);

	/*
	 * Recheck after taking the lock, so we don't race with another thread
	 * that just did journal_entry_open() and call journal_entry_close()
	 * unnecessarily
	 */
	if (journal_res_get_fast(j, res, flags)) {
		spin_unlock(&j->lock);
		return 0;
	}

	if (!(flags & JOURNAL_RES_GET_RESERVED) &&
	    !test_bit(JOURNAL_MAY_GET_UNRESERVED, &j->flags)) {
		/*
		 * Don't want to close current journal entry, just need to
		 * invoke reclaim:
		 */
		ret = cur_entry_journal_full;
		goto unlock;
	}

	/*
	 * If we couldn't get a reservation because the current buf filled up,
	 * and we had room for a bigger entry on disk, signal that we want to
	 * realloc the journal bufs:
	 */
	buf = journal_cur_buf(j);
	if (journal_entry_is_open(j) &&
	    buf->buf_size >> 9 < buf->disk_sectors &&
	    buf->buf_size < JOURNAL_ENTRY_SIZE_MAX)
		j->buf_size_want = max(j->buf_size_want, buf->buf_size << 1);

	__journal_entry_close(j, JOURNAL_ENTRY_CLOSED_VAL);
	ret = journal_entry_open(j);

	if (ret == cur_entry_max_in_flight)
		trace_journal_entry_full(c);
unlock:
	if ((ret && ret != cur_entry_insufficient_devices) &&
	    !j->res_get_blocked_start) {
		j->res_get_blocked_start = local_clock() ?: 1;
		trace_journal_full(c);
	}

	can_discard = j->can_discard;
	spin_unlock(&j->lock);

	if (!ret)
		goto retry;

	if ((ret == cur_entry_journal_full ||
	     ret == cur_entry_journal_pin_full) &&
	    !can_discard &&
	    !nr_unwritten_journal_entries(j) &&
	    (flags & JOURNAL_RES_GET_RESERVED)) {
		struct printbuf buf = PRINTBUF;

		bch_err(c, "Journal stuck! Hava a pre-reservation but journal full");

		bch2_journal_debug_to_text(&buf, j);
		bch_err(c, "%s", buf.buf);

		printbuf_reset(&buf);
		bch2_journal_pins_to_text(&buf, j);
		bch_err(c, "Journal pins:\n%s", buf.buf);

		printbuf_exit(&buf);
		bch2_fatal_error(c);
		dump_stack();
	}

	/*
	 * Journal is full - can't rely on reclaim from work item due to
	 * freezing:
	 */
	if ((ret == cur_entry_journal_full ||
	     ret == cur_entry_journal_pin_full) &&
	    !(flags & JOURNAL_RES_GET_NONBLOCK)) {
		if (can_discard) {
			bch2_journal_do_discards(j);
			goto retry;
		}

		if (mutex_trylock(&j->reclaim_lock)) {
			bch2_journal_reclaim(j);
			mutex_unlock(&j->reclaim_lock);
		}
	}

	return ret == cur_entry_insufficient_devices ? -EROFS : -EAGAIN;
}

/*
 * Essentially the entry function to the journaling code. When bcachefs is doing
 * a btree insert, it calls this function to get the current journal write.
 * Journal write is the structure used set up journal writes. The calling
 * function will then add its keys to the structure, queuing them for the next
 * write.
 *
 * To ensure forward progress, the current task must not be holding any
 * btree node write locks.
 */
int bch2_journal_res_get_slowpath(struct journal *j, struct journal_res *res,
				  unsigned flags)
{
	int ret;

	closure_wait_event(&j->async_wait,
		   (ret = __journal_res_get(j, res, flags)) != -EAGAIN ||
		   (flags & JOURNAL_RES_GET_NONBLOCK));
	return ret;
}

/* journal_preres: */

static bool journal_preres_available(struct journal *j,
				     struct journal_preres *res,
				     unsigned new_u64s,
				     unsigned flags)
{
	bool ret = bch2_journal_preres_get_fast(j, res, new_u64s, flags, true);

	if (!ret && mutex_trylock(&j->reclaim_lock)) {
		bch2_journal_reclaim(j);
		mutex_unlock(&j->reclaim_lock);
	}

	return ret;
}

int __bch2_journal_preres_get(struct journal *j,
			      struct journal_preres *res,
			      unsigned new_u64s,
			      unsigned flags)
{
	int ret;

	closure_wait_event(&j->preres_wait,
		   (ret = bch2_journal_error(j)) ||
		   journal_preres_available(j, res, new_u64s, flags));
	return ret;
}

/* journal_entry_res: */

void bch2_journal_entry_res_resize(struct journal *j,
				   struct journal_entry_res *res,
				   unsigned new_u64s)
{
	union journal_res_state state;
	int d = new_u64s - res->u64s;

	spin_lock(&j->lock);

	j->entry_u64s_reserved += d;
	if (d <= 0)
		goto out;

	j->cur_entry_u64s = max_t(int, 0, j->cur_entry_u64s - d);
	smp_mb();
	state = READ_ONCE(j->reservations);

	if (state.cur_entry_offset < JOURNAL_ENTRY_CLOSED_VAL &&
	    state.cur_entry_offset > j->cur_entry_u64s) {
		j->cur_entry_u64s += d;
		/*
		 * Not enough room in current journal entry, have to flush it:
		 */
		__journal_entry_close(j, JOURNAL_ENTRY_CLOSED_VAL);
	} else {
		journal_cur_buf(j)->u64s_reserved += d;
	}
out:
	spin_unlock(&j->lock);
	res->u64s += d;
}

/* journal flushing: */

/**
 * bch2_journal_flush_seq_async - wait for a journal entry to be written
 *
 * like bch2_journal_wait_on_seq, except that it triggers a write immediately if
 * necessary
 */
int bch2_journal_flush_seq_async(struct journal *j, u64 seq,
				 struct closure *parent)
{
	struct journal_buf *buf;
	int ret = 0;

	if (seq <= j->flushed_seq_ondisk)
		return 1;

	spin_lock(&j->lock);

	if (WARN_ONCE(seq > journal_cur_seq(j),
		      "requested to flush journal seq %llu, but currently at %llu",
		      seq, journal_cur_seq(j)))
		goto out;

	/* Recheck under lock: */
	if (j->err_seq && seq >= j->err_seq) {
		ret = -EIO;
		goto out;
	}

	if (seq <= j->flushed_seq_ondisk) {
		ret = 1;
		goto out;
	}

	/* if seq was written, but not flushed - flush a newer one instead */
	seq = max(seq, journal_last_unwritten_seq(j));

recheck_need_open:
	if (seq > journal_cur_seq(j)) {
		struct journal_res res = { 0 };

		if (journal_entry_is_open(j))
			__journal_entry_close(j, JOURNAL_ENTRY_CLOSED_VAL);

		spin_unlock(&j->lock);

		ret = bch2_journal_res_get(j, &res, jset_u64s(0), 0);
		if (ret)
			return ret;

		seq = res.seq;
		buf = j->buf + (seq & JOURNAL_BUF_MASK);
		buf->must_flush = true;

		if (!buf->flush_time) {
			buf->flush_time	= local_clock() ?: 1;
			buf->expires = jiffies;
		}

		if (parent && !closure_wait(&buf->wait, parent))
			BUG();

		bch2_journal_res_put(j, &res);

		spin_lock(&j->lock);
		goto want_write;
	}

	/*
	 * if write was kicked off without a flush, flush the next sequence
	 * number instead
	 */
	buf = journal_seq_to_buf(j, seq);
	if (buf->noflush) {
		seq++;
		goto recheck_need_open;
	}

	buf->must_flush = true;

	if (parent && !closure_wait(&buf->wait, parent))
		BUG();
want_write:
	if (seq == journal_cur_seq(j))
		journal_entry_want_write(j);
out:
	spin_unlock(&j->lock);
	return ret;
}

int bch2_journal_flush_seq(struct journal *j, u64 seq)
{
	u64 start_time = local_clock();
	int ret, ret2;

	/*
	 * Don't update time_stats when @seq is already flushed:
	 */
	if (seq <= j->flushed_seq_ondisk)
		return 0;

	ret = wait_event_interruptible(j->wait, (ret2 = bch2_journal_flush_seq_async(j, seq, NULL)));

	if (!ret)
		bch2_time_stats_update(j->flush_seq_time, start_time);

	return ret ?: ret2 < 0 ? ret2 : 0;
}

int bch2_journal_meta(struct journal *j)
{
	struct journal_buf *buf;
	struct journal_res res;
	int ret;

	memset(&res, 0, sizeof(res));

	ret = bch2_journal_res_get(j, &res, jset_u64s(0), 0);
	if (ret)
		return ret;

	buf = j->buf + (res.seq & JOURNAL_BUF_MASK);
	buf->must_flush = true;

	if (!buf->flush_time) {
		buf->flush_time	= local_clock() ?: 1;
		buf->expires = jiffies;
	}

	bch2_journal_res_put(j, &res);

	return bch2_journal_flush_seq(j, res.seq);
}

/*
 * bch2_journal_flush_async - if there is an open journal entry, or a journal
 * still being written, write it and wait for the write to complete
 */
void bch2_journal_flush_async(struct journal *j, struct closure *parent)
{
	bch2_journal_flush_seq_async(j, atomic64_read(&j->seq), parent);
}

int bch2_journal_flush(struct journal *j)
{
	return bch2_journal_flush_seq(j, atomic64_read(&j->seq));
}

/*
 * bch2_journal_noflush_seq - tell the journal not to issue any flushes before
 * @seq
 */
bool bch2_journal_noflush_seq(struct journal *j, u64 seq)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	u64 unwritten_seq;
	bool ret = false;

	if (!(c->sb.features & (1ULL << BCH_FEATURE_journal_no_flush)))
		return false;

	if (seq <= c->journal.flushed_seq_ondisk)
		return false;

	spin_lock(&j->lock);
	if (seq <= c->journal.flushed_seq_ondisk)
		goto out;

	for (unwritten_seq = journal_last_unwritten_seq(j);
	     unwritten_seq < seq;
	     unwritten_seq++) {
		struct journal_buf *buf = journal_seq_to_buf(j, unwritten_seq);

		/* journal write is already in flight, and was a flush write: */
		if (unwritten_seq == journal_last_unwritten_seq(j) && !buf->noflush)
			goto out;

		buf->noflush = true;
	}

	ret = true;
out:
	spin_unlock(&j->lock);
	return ret;
}

/* block/unlock the journal: */

void bch2_journal_unblock(struct journal *j)
{
	spin_lock(&j->lock);
	j->blocked--;
	spin_unlock(&j->lock);

	journal_wake(j);
}

void bch2_journal_block(struct journal *j)
{
	spin_lock(&j->lock);
	j->blocked++;
	spin_unlock(&j->lock);

	journal_quiesce(j);
}

/* allocate journal on a device: */

static int __bch2_set_nr_journal_buckets(struct bch_dev *ca, unsigned nr,
					 bool new_fs, struct closure *cl)
{
	struct bch_fs *c = ca->fs;
	struct journal_device *ja = &ca->journal;
	struct bch_sb_field_journal *journal_buckets;
	u64 *new_bucket_seq = NULL, *new_buckets = NULL;
	int ret = 0;

	/* don't handle reducing nr of buckets yet: */
	if (nr <= ja->nr)
		return 0;

	new_buckets	= kzalloc(nr * sizeof(u64), GFP_KERNEL);
	new_bucket_seq	= kzalloc(nr * sizeof(u64), GFP_KERNEL);
	if (!new_buckets || !new_bucket_seq) {
		ret = -ENOMEM;
		goto err;
	}

	journal_buckets = bch2_sb_resize_journal(&ca->disk_sb,
					nr + sizeof(*journal_buckets) / sizeof(u64));
	if (!journal_buckets) {
		ret = -ENOSPC;
		goto err;
	}

	/*
	 * We may be called from the device add path, before the new device has
	 * actually been added to the running filesystem:
	 */
	if (!new_fs)
		spin_lock(&c->journal.lock);

	memcpy(new_buckets,	ja->buckets,	ja->nr * sizeof(u64));
	memcpy(new_bucket_seq,	ja->bucket_seq,	ja->nr * sizeof(u64));
	swap(new_buckets,	ja->buckets);
	swap(new_bucket_seq,	ja->bucket_seq);

	if (!new_fs)
		spin_unlock(&c->journal.lock);

	while (ja->nr < nr) {
		struct open_bucket *ob = NULL;
		unsigned pos;
		long b;

		if (new_fs) {
			b = bch2_bucket_alloc_new_fs(ca);
			if (b < 0) {
				ret = -ENOSPC;
				goto err;
			}
		} else {
			rcu_read_lock();
			ob = bch2_bucket_alloc(c, ca, RESERVE_NONE,
					       false, cl);
			rcu_read_unlock();
			if (IS_ERR(ob)) {
				ret = cl ? -EAGAIN : -ENOSPC;
				goto err;
			}

			b = ob->bucket;
		}

		if (c)
			spin_lock(&c->journal.lock);

		/*
		 * XXX
		 * For resize at runtime, we should be writing the new
		 * superblock before inserting into the journal array
		 */

		pos = ja->nr ? (ja->cur_idx + 1) % ja->nr : 0;
		__array_insert_item(ja->buckets,		ja->nr, pos);
		__array_insert_item(ja->bucket_seq,		ja->nr, pos);
		__array_insert_item(journal_buckets->buckets,	ja->nr, pos);
		ja->nr++;

		ja->buckets[pos] = b;
		ja->bucket_seq[pos] = 0;
		journal_buckets->buckets[pos] = cpu_to_le64(b);

		if (pos <= ja->discard_idx)
			ja->discard_idx = (ja->discard_idx + 1) % ja->nr;
		if (pos <= ja->dirty_idx_ondisk)
			ja->dirty_idx_ondisk = (ja->dirty_idx_ondisk + 1) % ja->nr;
		if (pos <= ja->dirty_idx)
			ja->dirty_idx = (ja->dirty_idx + 1) % ja->nr;
		if (pos <= ja->cur_idx)
			ja->cur_idx = (ja->cur_idx + 1) % ja->nr;

		if (c)
			spin_unlock(&c->journal.lock);

		if (!new_fs) {
			ret = bch2_trans_do(c, NULL, NULL, BTREE_INSERT_NOFAIL,
				bch2_trans_mark_metadata_bucket(&trans, ca,
						b, BCH_DATA_journal,
						ca->mi.bucket_size));

			bch2_open_bucket_put(c, ob);

			if (ret)
				goto err;
		}
	}
err:
	bch2_sb_resize_journal(&ca->disk_sb,
		ja->nr + sizeof(*journal_buckets) / sizeof(u64));
	kfree(new_bucket_seq);
	kfree(new_buckets);

	return ret;
}

/*
 * Allocate more journal space at runtime - not currently making use if it, but
 * the code works:
 */
int bch2_set_nr_journal_buckets(struct bch_fs *c, struct bch_dev *ca,
				unsigned nr)
{
	struct journal_device *ja = &ca->journal;
	struct closure cl;
	unsigned current_nr;
	int ret;

	closure_init_stack(&cl);

	do {
		struct disk_reservation disk_res = { 0, 0 };

		closure_sync(&cl);

		mutex_lock(&c->sb_lock);
		current_nr = ja->nr;

		/*
		 * note: journal buckets aren't really counted as _sectors_ used yet, so
		 * we don't need the disk reservation to avoid the BUG_ON() in buckets.c
		 * when space used goes up without a reservation - but we do need the
		 * reservation to ensure we'll actually be able to allocate:
		 */

		if (bch2_disk_reservation_get(c, &disk_res,
					      bucket_to_sector(ca, nr - ja->nr), 1, 0)) {
			mutex_unlock(&c->sb_lock);
			return -ENOSPC;
		}

		ret = __bch2_set_nr_journal_buckets(ca, nr, false, &cl);

		bch2_disk_reservation_put(c, &disk_res);

		if (ja->nr != current_nr)
			bch2_write_super(c);
		mutex_unlock(&c->sb_lock);
	} while (ret == -EAGAIN);

	return ret;
}

int bch2_dev_journal_alloc(struct bch_dev *ca)
{
	unsigned nr;

	if (dynamic_fault("bcachefs:add:journal_alloc"))
		return -ENOMEM;

	/* 1/128th of the device by default: */
	nr = ca->mi.nbuckets >> 7;

	/*
	 * clamp journal size to 8192 buckets or 8GB (in sectors), whichever
	 * is smaller:
	 */
	nr = clamp_t(unsigned, nr,
		     BCH_JOURNAL_BUCKETS_MIN,
		     min(1 << 13,
			 (1 << 24) / ca->mi.bucket_size));

	return __bch2_set_nr_journal_buckets(ca, nr, true, NULL);
}

/* startup/shutdown: */

static bool bch2_journal_writing_to_device(struct journal *j, unsigned dev_idx)
{
	bool ret = false;
	u64 seq;

	spin_lock(&j->lock);
	for (seq = journal_last_unwritten_seq(j);
	     seq <= journal_cur_seq(j) && !ret;
	     seq++) {
		struct journal_buf *buf = journal_seq_to_buf(j, seq);

		if (bch2_bkey_has_device(bkey_i_to_s_c(&buf->key), dev_idx))
			ret = true;
	}
	spin_unlock(&j->lock);

	return ret;
}

void bch2_dev_journal_stop(struct journal *j, struct bch_dev *ca)
{
	wait_event(j->wait, !bch2_journal_writing_to_device(j, ca->dev_idx));
}

void bch2_fs_journal_stop(struct journal *j)
{
	bch2_journal_flush_all_pins(j);

	wait_event(j->wait, journal_entry_close(j));

	/*
	 * Always write a new journal entry, to make sure the clock hands are up
	 * to date (and match the superblock)
	 */
	bch2_journal_meta(j);

	journal_quiesce(j);

	BUG_ON(!bch2_journal_error(j) &&
	       test_bit(JOURNAL_REPLAY_DONE, &j->flags) &&
	       j->last_empty_seq != journal_cur_seq(j));

	cancel_delayed_work_sync(&j->write_work);
	bch2_journal_reclaim_stop(j);
}

int bch2_fs_journal_start(struct journal *j, u64 cur_seq,
			  struct list_head *journal_entries)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_entry_pin_list *p;
	struct journal_replay *i;
	u64 last_seq = cur_seq, nr, seq;

	if (!list_empty(journal_entries))
		last_seq = le64_to_cpu(list_last_entry(journal_entries,
				struct journal_replay, list)->j.last_seq);

	nr = cur_seq - last_seq;

	if (nr + 1 > j->pin.size) {
		free_fifo(&j->pin);
		init_fifo(&j->pin, roundup_pow_of_two(nr + 1), GFP_KERNEL);
		if (!j->pin.data) {
			bch_err(c, "error reallocating journal fifo (%llu open entries)", nr);
			return -ENOMEM;
		}
	}

	j->replay_journal_seq	= last_seq;
	j->replay_journal_seq_end = cur_seq;
	j->last_seq_ondisk	= last_seq;
	j->flushed_seq_ondisk	= cur_seq - 1;
	j->seq_ondisk		= cur_seq - 1;
	j->pin.front		= last_seq;
	j->pin.back		= cur_seq;
	atomic64_set(&j->seq, cur_seq - 1);

	if (list_empty(journal_entries))
		j->last_empty_seq = cur_seq - 1;

	fifo_for_each_entry_ptr(p, &j->pin, seq)
		journal_pin_list_init(p, 1);

	list_for_each_entry(i, journal_entries, list) {
		unsigned ptr;

		seq = le64_to_cpu(i->j.seq);
		BUG_ON(seq >= cur_seq);

		if (seq < last_seq)
			continue;

		if (journal_entry_empty(&i->j))
			j->last_empty_seq = le64_to_cpu(i->j.seq);

		p = journal_seq_pin(j, seq);

		p->devs.nr = 0;
		for (ptr = 0; ptr < i->nr_ptrs; ptr++)
			bch2_dev_list_add_dev(&p->devs, i->ptrs[ptr].dev);
	}

	if (list_empty(journal_entries))
		j->last_empty_seq = cur_seq;

	spin_lock(&j->lock);

	set_bit(JOURNAL_STARTED, &j->flags);
	j->last_flush_write = jiffies;

	j->reservations.idx = j->reservations.unwritten_idx = journal_cur_seq(j);
	j->reservations.unwritten_idx++;

	c->last_bucket_seq_cleanup = journal_cur_seq(j);

	bch2_journal_space_available(j);
	spin_unlock(&j->lock);

	return bch2_journal_reclaim_start(j);
}

/* init/exit: */

void bch2_dev_journal_exit(struct bch_dev *ca)
{
	kfree(ca->journal.bio);
	kfree(ca->journal.buckets);
	kfree(ca->journal.bucket_seq);

	ca->journal.bio		= NULL;
	ca->journal.buckets	= NULL;
	ca->journal.bucket_seq	= NULL;
}

int bch2_dev_journal_init(struct bch_dev *ca, struct bch_sb *sb)
{
	struct journal_device *ja = &ca->journal;
	struct bch_sb_field_journal *journal_buckets =
		bch2_sb_get_journal(sb);
	unsigned i, nr_bvecs;

	ja->nr = bch2_nr_journal_buckets(journal_buckets);

	ja->bucket_seq = kcalloc(ja->nr, sizeof(u64), GFP_KERNEL);
	if (!ja->bucket_seq)
		return -ENOMEM;

	nr_bvecs = DIV_ROUND_UP(JOURNAL_ENTRY_SIZE_MAX, PAGE_SIZE);

	ca->journal.bio = bio_kmalloc(nr_bvecs, GFP_KERNEL);
	if (!ca->journal.bio)
		return -ENOMEM;

	bio_init(ca->journal.bio, NULL, ca->journal.bio->bi_inline_vecs, nr_bvecs, 0);

	ja->buckets = kcalloc(ja->nr, sizeof(u64), GFP_KERNEL);
	if (!ja->buckets)
		return -ENOMEM;

	for (i = 0; i < ja->nr; i++)
		ja->buckets[i] = le64_to_cpu(journal_buckets->buckets[i]);

	return 0;
}

void bch2_fs_journal_exit(struct journal *j)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(j->buf); i++)
		kvpfree(j->buf[i].data, j->buf[i].buf_size);
	free_fifo(&j->pin);
}

int bch2_fs_journal_init(struct journal *j)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	static struct lock_class_key res_key;
	unsigned i;
	int ret = 0;

	pr_verbose_init(c->opts, "");

	spin_lock_init(&j->lock);
	spin_lock_init(&j->err_lock);
	init_waitqueue_head(&j->wait);
	INIT_DELAYED_WORK(&j->write_work, journal_write_work);
	init_waitqueue_head(&j->reclaim_wait);
	init_waitqueue_head(&j->pin_flush_wait);
	mutex_init(&j->reclaim_lock);
	mutex_init(&j->discard_lock);

	lockdep_init_map(&j->res_map, "journal res", &res_key, 0);

	atomic64_set(&j->reservations.counter,
		((union journal_res_state)
		 { .cur_entry_offset = JOURNAL_ENTRY_CLOSED_VAL }).v);

	if (!(init_fifo(&j->pin, JOURNAL_PIN, GFP_KERNEL))) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(j->buf); i++) {
		j->buf[i].buf_size = JOURNAL_ENTRY_SIZE_MIN;
		j->buf[i].data = kvpmalloc(j->buf[i].buf_size, GFP_KERNEL);
		if (!j->buf[i].data) {
			ret = -ENOMEM;
			goto out;
		}
	}

	j->pin.front = j->pin.back = 1;
out:
	pr_verbose_init(c->opts, "ret %i", ret);
	return ret;
}

/* debug: */

void __bch2_journal_debug_to_text(struct printbuf *out, struct journal *j)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	union journal_res_state s;
	struct bch_dev *ca;
	unsigned long now = jiffies;
	u64 seq;
	unsigned i;

	out->atomic++;
	out->tabstops[0] = 24;

	rcu_read_lock();
	s = READ_ONCE(j->reservations);

	pr_buf(out, "dirty journal entries:\t%llu\n",	fifo_used(&j->pin));
	pr_buf(out, "seq:\t\t\t%llu\n",			journal_cur_seq(j));
	pr_buf(out, "seq_ondisk:\t\t%llu\n",		j->seq_ondisk);
	pr_buf(out, "last_seq:\t\t%llu\n",		journal_last_seq(j));
	pr_buf(out, "last_seq_ondisk:\t%llu\n",		j->last_seq_ondisk);
	pr_buf(out, "flushed_seq_ondisk:\t%llu\n",	j->flushed_seq_ondisk);
	pr_buf(out, "prereserved:\t\t%u/%u\n",		j->prereserved.reserved, j->prereserved.remaining);
	pr_buf(out, "each entry reserved:\t%u\n",	j->entry_u64s_reserved);
	pr_buf(out, "nr flush writes:\t%llu\n",		j->nr_flush_writes);
	pr_buf(out, "nr noflush writes:\t%llu\n",	j->nr_noflush_writes);
	pr_buf(out, "nr direct reclaim:\t%llu\n",	j->nr_direct_reclaim);
	pr_buf(out, "nr background reclaim:\t%llu\n",	j->nr_background_reclaim);
	pr_buf(out, "reclaim kicked:\t\t%u\n",		j->reclaim_kicked);
	pr_buf(out, "reclaim runs in:\t%u ms\n",	time_after(j->next_reclaim, now)
	       ? jiffies_to_msecs(j->next_reclaim - jiffies) : 0);
	pr_buf(out, "current entry sectors:\t%u\n",	j->cur_entry_sectors);
	pr_buf(out, "current entry error:\t%u\n",	j->cur_entry_error);
	pr_buf(out, "current entry:\t\t");

	switch (s.cur_entry_offset) {
	case JOURNAL_ENTRY_ERROR_VAL:
		pr_buf(out, "error");
		break;
	case JOURNAL_ENTRY_CLOSED_VAL:
		pr_buf(out, "closed");
		break;
	default:
		pr_buf(out, "%u/%u", s.cur_entry_offset, j->cur_entry_u64s);
		break;
	}

	pr_newline(out);

	for (seq = journal_cur_seq(j);
	     seq >= journal_last_unwritten_seq(j);
	     --seq) {
		i = seq & JOURNAL_BUF_MASK;

		pr_buf(out, "unwritten entry:");
		pr_tab(out);
		pr_buf(out, "%llu", seq);
		pr_newline(out);
		pr_indent_push(out, 2);

		pr_buf(out, "refcount:");
		pr_tab(out);
		pr_buf(out, "%u", journal_state_count(s, i));
		pr_newline(out);

		pr_buf(out, "sectors:");
		pr_tab(out);
		pr_buf(out, "%u", j->buf[i].sectors);
		pr_newline(out);

		pr_buf(out, "expires");
		pr_tab(out);
		pr_buf(out, "%li jiffies", j->buf[i].expires - jiffies);
		pr_newline(out);

		pr_indent_pop(out, 2);
	}

	pr_buf(out,
	       "replay done:\t\t%i\n",
	       test_bit(JOURNAL_REPLAY_DONE,	&j->flags));

	pr_buf(out, "space:\n");
	pr_buf(out, "\tdiscarded\t%u:%u\n",
	       j->space[journal_space_discarded].next_entry,
	       j->space[journal_space_discarded].total);
	pr_buf(out, "\tclean ondisk\t%u:%u\n",
	       j->space[journal_space_clean_ondisk].next_entry,
	       j->space[journal_space_clean_ondisk].total);
	pr_buf(out, "\tclean\t\t%u:%u\n",
	       j->space[journal_space_clean].next_entry,
	       j->space[journal_space_clean].total);
	pr_buf(out, "\ttotal\t\t%u:%u\n",
	       j->space[journal_space_total].next_entry,
	       j->space[journal_space_total].total);

	for_each_member_device_rcu(ca, c, i,
				   &c->rw_devs[BCH_DATA_journal]) {
		struct journal_device *ja = &ca->journal;

		if (!test_bit(ca->dev_idx, c->rw_devs[BCH_DATA_journal].d))
			continue;

		if (!ja->nr)
			continue;

		pr_buf(out, "dev %u:\n",		i);
		pr_buf(out, "\tnr\t\t%u\n",		ja->nr);
		pr_buf(out, "\tbucket size\t%u\n",	ca->mi.bucket_size);
		pr_buf(out, "\tavailable\t%u:%u\n",	bch2_journal_dev_buckets_available(j, ja, journal_space_discarded), ja->sectors_free);
		pr_buf(out, "\tdiscard_idx\t%u\n",	ja->discard_idx);
		pr_buf(out, "\tdirty_ondisk\t%u (seq %llu)\n", ja->dirty_idx_ondisk,	ja->bucket_seq[ja->dirty_idx_ondisk]);
		pr_buf(out, "\tdirty_idx\t%u (seq %llu)\n", ja->dirty_idx,		ja->bucket_seq[ja->dirty_idx]);
		pr_buf(out, "\tcur_idx\t\t%u (seq %llu)\n", ja->cur_idx,		ja->bucket_seq[ja->cur_idx]);
	}

	rcu_read_unlock();

	--out->atomic;
}

void bch2_journal_debug_to_text(struct printbuf *out, struct journal *j)
{
	spin_lock(&j->lock);
	__bch2_journal_debug_to_text(out, j);
	spin_unlock(&j->lock);
}

bool bch2_journal_seq_pins_to_text(struct printbuf *out, struct journal *j, u64 *seq)
{
	struct journal_entry_pin_list *pin_list;
	struct journal_entry_pin *pin;

	spin_lock(&j->lock);
	*seq = max(*seq, j->pin.front);

	if (*seq >= j->pin.back) {
		spin_unlock(&j->lock);
		return true;
	}

	out->atomic++;

	pin_list = journal_seq_pin(j, *seq);

	pr_buf(out, "%llu: count %u", *seq, atomic_read(&pin_list->count));
	pr_newline(out);
	pr_indent_push(out, 2);

	list_for_each_entry(pin, &pin_list->list, list) {
		pr_buf(out, "\t%px %ps", pin, pin->flush);
		pr_newline(out);
	}

	list_for_each_entry(pin, &pin_list->key_cache_list, list) {
		pr_buf(out, "\t%px %ps", pin, pin->flush);
		pr_newline(out);
	}

	if (!list_empty(&pin_list->flushed)) {
		pr_buf(out, "flushed:");
		pr_newline(out);
	}

	list_for_each_entry(pin, &pin_list->flushed, list) {
		pr_buf(out, "\t%px %ps", pin, pin->flush);
		pr_newline(out);
	}

	pr_indent_pop(out, 2);

	--out->atomic;
	spin_unlock(&j->lock);

	return false;
}

void bch2_journal_pins_to_text(struct printbuf *out, struct journal *j)
{
	u64 seq = 0;

	while (!bch2_journal_seq_pins_to_text(out, j, &seq))
		seq++;
}
