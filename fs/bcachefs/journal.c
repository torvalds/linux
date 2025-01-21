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
#include "btree_write_buffer.h"
#include "buckets.h"
#include "error.h"
#include "journal.h"
#include "journal_io.h"
#include "journal_reclaim.h"
#include "journal_sb.h"
#include "journal_seq_blacklist.h"
#include "trace.h"

static const char * const bch2_journal_errors[] = {
#define x(n)	#n,
	JOURNAL_ERRORS()
#undef x
	NULL
};

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

static void bch2_journal_buf_to_text(struct printbuf *out, struct journal *j, u64 seq)
{
	union journal_res_state s = READ_ONCE(j->reservations);
	unsigned i = seq & JOURNAL_BUF_MASK;
	struct journal_buf *buf = j->buf + i;

	prt_printf(out, "seq:\t%llu\n", seq);
	printbuf_indent_add(out, 2);

	prt_printf(out, "refcount:\t%u\n", journal_state_count(s, i));

	prt_printf(out, "size:\t");
	prt_human_readable_u64(out, vstruct_bytes(buf->data));
	prt_newline(out);

	prt_printf(out, "expires:\t");
	prt_printf(out, "%li jiffies\n", buf->expires - jiffies);

	prt_printf(out, "flags:\t");
	if (buf->noflush)
		prt_str(out, "noflush ");
	if (buf->must_flush)
		prt_str(out, "must_flush ");
	if (buf->separate_flush)
		prt_str(out, "separate_flush ");
	if (buf->need_flush_to_write_buffer)
		prt_str(out, "need_flush_to_write_buffer ");
	if (buf->write_started)
		prt_str(out, "write_started ");
	if (buf->write_allocated)
		prt_str(out, "write_allocated ");
	if (buf->write_done)
		prt_str(out, "write_done");
	prt_newline(out);

	printbuf_indent_sub(out, 2);
}

static void bch2_journal_bufs_to_text(struct printbuf *out, struct journal *j)
{
	if (!out->nr_tabstops)
		printbuf_tabstop_push(out, 24);

	for (u64 seq = journal_last_unwritten_seq(j);
	     seq <= journal_cur_seq(j);
	     seq++)
		bch2_journal_buf_to_text(out, j, seq);
	prt_printf(out, "last buf %s\n", journal_entry_is_open(j) ? "open" : "closed");
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
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(p->list); i++)
		INIT_LIST_HEAD(&p->list[i]);
	INIT_LIST_HEAD(&p->flushed);
	atomic_set(&p->count, count);
	p->devs.nr = 0;
}

/*
 * Detect stuck journal conditions and trigger shutdown. Technically the journal
 * can end up stuck for a variety of reasons, such as a blocked I/O, journal
 * reservation lockup, etc. Since this is a fatal error with potentially
 * unpredictable characteristics, we want to be fairly conservative before we
 * decide to shut things down.
 *
 * Consider the journal stuck when it appears full with no ability to commit
 * btree transactions, to discard journal buckets, nor acquire priority
 * (reserved watermark) reservation.
 */
static inline bool
journal_error_check_stuck(struct journal *j, int error, unsigned flags)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	bool stuck = false;
	struct printbuf buf = PRINTBUF;

	if (!(error == JOURNAL_ERR_journal_full ||
	      error == JOURNAL_ERR_journal_pin_full) ||
	    nr_unwritten_journal_entries(j) ||
	    (flags & BCH_WATERMARK_MASK) != BCH_WATERMARK_reclaim)
		return stuck;

	spin_lock(&j->lock);

	if (j->can_discard) {
		spin_unlock(&j->lock);
		return stuck;
	}

	stuck = true;

	/*
	 * The journal shutdown path will set ->err_seq, but do it here first to
	 * serialize against concurrent failures and avoid duplicate error
	 * reports.
	 */
	if (j->err_seq) {
		spin_unlock(&j->lock);
		return stuck;
	}
	j->err_seq = journal_cur_seq(j);
	spin_unlock(&j->lock);

	bch_err(c, "Journal stuck! Hava a pre-reservation but journal full (error %s)",
		bch2_journal_errors[error]);
	bch2_journal_debug_to_text(&buf, j);
	bch_err(c, "%s", buf.buf);

	printbuf_reset(&buf);
	bch2_journal_pins_to_text(&buf, j);
	bch_err(c, "Journal pins:\n%s", buf.buf);
	printbuf_exit(&buf);

	bch2_fatal_error(c);
	dump_stack();

	return stuck;
}

void bch2_journal_do_writes(struct journal *j)
{
	for (u64 seq = journal_last_unwritten_seq(j);
	     seq <= journal_cur_seq(j);
	     seq++) {
		unsigned idx = seq & JOURNAL_BUF_MASK;
		struct journal_buf *w = j->buf + idx;

		if (w->write_started && !w->write_allocated)
			break;
		if (w->write_started)
			continue;

		if (!journal_state_count(j->reservations, idx)) {
			w->write_started = true;
			closure_call(&w->io, bch2_journal_write, j->wq, NULL);
		}

		break;
	}
}

/*
 * Final processing when the last reference of a journal buffer has been
 * dropped. Drop the pin list reference acquired at journal entry open and write
 * the buffer, if requested.
 */
void bch2_journal_buf_put_final(struct journal *j, u64 seq)
{
	lockdep_assert_held(&j->lock);

	if (__bch2_journal_pin_put(j, seq))
		bch2_journal_reclaim_fast(j);
	bch2_journal_do_writes(j);
}

/*
 * Returns true if journal entry is now closed:
 *
 * We don't close a journal_buf until the next journal_buf is finished writing,
 * and can be opened again - this also initializes the next journal_buf:
 */
static void __journal_entry_close(struct journal *j, unsigned closed_val, bool trace)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_buf *buf = journal_cur_buf(j);
	union journal_res_state old, new;
	unsigned sectors;

	BUG_ON(closed_val != JOURNAL_ENTRY_CLOSED_VAL &&
	       closed_val != JOURNAL_ENTRY_ERROR_VAL);

	lockdep_assert_held(&j->lock);

	old.v = atomic64_read(&j->reservations.counter);
	do {
		new.v = old.v;
		new.cur_entry_offset = closed_val;

		if (old.cur_entry_offset == JOURNAL_ENTRY_ERROR_VAL ||
		    old.cur_entry_offset == new.cur_entry_offset)
			return;
	} while (!atomic64_try_cmpxchg(&j->reservations.counter,
				       &old.v, new.v));

	if (!__journal_entry_is_open(old))
		return;

	/* Close out old buffer: */
	buf->data->u64s		= cpu_to_le32(old.cur_entry_offset);

	if (trace_journal_entry_close_enabled() && trace) {
		struct printbuf pbuf = PRINTBUF;
		pbuf.atomic++;

		prt_str(&pbuf, "entry size: ");
		prt_human_readable_u64(&pbuf, vstruct_bytes(buf->data));
		prt_newline(&pbuf);
		bch2_prt_task_backtrace(&pbuf, current, 1, GFP_NOWAIT);
		trace_journal_entry_close(c, pbuf.buf);
		printbuf_exit(&pbuf);
	}

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

	cancel_delayed_work(&j->write_work);

	bch2_journal_space_available(j);

	__bch2_journal_buf_put(j, old.idx, le64_to_cpu(buf->data->seq));
}

void bch2_journal_halt(struct journal *j)
{
	spin_lock(&j->lock);
	__journal_entry_close(j, JOURNAL_ENTRY_ERROR_VAL, true);
	if (!j->err_seq)
		j->err_seq = journal_cur_seq(j);
	journal_wake(j);
	spin_unlock(&j->lock);
}

static bool journal_entry_want_write(struct journal *j)
{
	bool ret = !journal_entry_is_open(j) ||
		journal_cur_seq(j) == journal_last_unwritten_seq(j);

	/* Don't close it yet if we already have a write in flight: */
	if (ret)
		__journal_entry_close(j, JOURNAL_ENTRY_CLOSED_VAL, true);
	else if (nr_unwritten_journal_entries(j)) {
		struct journal_buf *buf = journal_cur_buf(j);

		if (!buf->flush_time) {
			buf->flush_time	= local_clock() ?: 1;
			buf->expires = jiffies;
		}
	}

	return ret;
}

bool bch2_journal_entry_close(struct journal *j)
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
 */
static int journal_entry_open(struct journal *j)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_buf *buf = j->buf +
		((journal_cur_seq(j) + 1) & JOURNAL_BUF_MASK);
	union journal_res_state old, new;
	int u64s;

	lockdep_assert_held(&j->lock);
	BUG_ON(journal_entry_is_open(j));
	BUG_ON(BCH_SB_CLEAN(c->disk_sb.sb));

	if (j->blocked)
		return JOURNAL_ERR_blocked;

	if (j->cur_entry_error)
		return j->cur_entry_error;

	if (bch2_journal_error(j))
		return JOURNAL_ERR_insufficient_devices; /* -EROFS */

	if (!fifo_free(&j->pin))
		return JOURNAL_ERR_journal_pin_full;

	if (nr_unwritten_journal_entries(j) == ARRAY_SIZE(j->buf))
		return JOURNAL_ERR_max_in_flight;

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

	if (u64s <= (ssize_t) j->early_journal_entries.nr)
		return JOURNAL_ERR_journal_full;

	if (fifo_empty(&j->pin) && j->reclaim_thread)
		wake_up_process(j->reclaim_thread);

	/*
	 * The fifo_push() needs to happen at the same time as j->seq is
	 * incremented for journal_last_seq() to be calculated correctly
	 */
	atomic64_inc(&j->seq);
	journal_pin_list_init(fifo_push_ref(&j->pin), 1);

	BUG_ON(j->pin.back - 1 != atomic64_read(&j->seq));

	BUG_ON(j->buf + (journal_cur_seq(j) & JOURNAL_BUF_MASK) != buf);

	bkey_extent_init(&buf->key);
	buf->noflush		= false;
	buf->must_flush		= false;
	buf->separate_flush	= false;
	buf->flush_time		= 0;
	buf->need_flush_to_write_buffer = true;
	buf->write_started	= false;
	buf->write_allocated	= false;
	buf->write_done		= false;

	memset(buf->data, 0, sizeof(*buf->data));
	buf->data->seq	= cpu_to_le64(journal_cur_seq(j));
	buf->data->u64s	= 0;

	if (j->early_journal_entries.nr) {
		memcpy(buf->data->_data, j->early_journal_entries.data,
		       j->early_journal_entries.nr * sizeof(u64));
		le32_add_cpu(&buf->data->u64s, j->early_journal_entries.nr);
	}

	/*
	 * Must be set before marking the journal entry as open:
	 */
	j->cur_entry_u64s = u64s;

	old.v = atomic64_read(&j->reservations.counter);
	do {
		new.v = old.v;

		BUG_ON(old.cur_entry_offset == JOURNAL_ENTRY_ERROR_VAL);

		new.idx++;
		BUG_ON(journal_state_count(new, new.idx));
		BUG_ON(new.idx != (journal_cur_seq(j) & JOURNAL_BUF_MASK));

		journal_state_inc(&new);

		/* Handle any already added entries */
		new.cur_entry_offset = le32_to_cpu(buf->data->u64s);
	} while (!atomic64_try_cmpxchg(&j->reservations.counter,
				       &old.v, new.v));

	if (nr_unwritten_journal_entries(j) == 1)
		mod_delayed_work(j->wq,
				 &j->write_work,
				 msecs_to_jiffies(c->opts.journal_flush_delay));
	journal_wake(j);

	if (j->early_journal_entries.nr)
		darray_exit(&j->early_journal_entries);
	return 0;
}

static bool journal_quiesced(struct journal *j)
{
	bool ret = atomic64_read(&j->seq) == j->seq_ondisk;

	if (!ret)
		bch2_journal_entry_close(j);
	return ret;
}

static void journal_quiesce(struct journal *j)
{
	wait_event(j->wait, journal_quiesced(j));
}

static void journal_write_work(struct work_struct *work)
{
	struct journal *j = container_of(work, struct journal, write_work.work);

	spin_lock(&j->lock);
	if (__journal_entry_is_open(j->reservations)) {
		long delta = journal_cur_buf(j)->expires - jiffies;

		if (delta > 0)
			mod_delayed_work(j->wq, &j->write_work, delta);
		else
			__journal_entry_close(j, JOURNAL_ENTRY_CLOSED_VAL, true);
	}
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
		return -BCH_ERR_erofs_journal_err;

	if (j->blocked)
		return -BCH_ERR_journal_res_get_blocked;

	if ((flags & BCH_WATERMARK_MASK) < j->watermark) {
		ret = JOURNAL_ERR_journal_full;
		can_discard = j->can_discard;
		goto out;
	}

	if (nr_unwritten_journal_entries(j) == ARRAY_SIZE(j->buf) && !journal_entry_is_open(j)) {
		ret = JOURNAL_ERR_max_in_flight;
		goto out;
	}

	spin_lock(&j->lock);

	/*
	 * Recheck after taking the lock, so we don't race with another thread
	 * that just did journal_entry_open() and call bch2_journal_entry_close()
	 * unnecessarily
	 */
	if (journal_res_get_fast(j, res, flags)) {
		ret = 0;
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

	__journal_entry_close(j, JOURNAL_ENTRY_CLOSED_VAL, false);
	ret = journal_entry_open(j) ?: JOURNAL_ERR_retry;
unlock:
	can_discard = j->can_discard;
	spin_unlock(&j->lock);
out:
	if (ret == JOURNAL_ERR_retry)
		goto retry;
	if (!ret)
		return 0;

	if (journal_error_check_stuck(j, ret, flags))
		ret = -BCH_ERR_journal_res_get_blocked;

	if (ret == JOURNAL_ERR_max_in_flight &&
	    track_event_change(&c->times[BCH_TIME_blocked_journal_max_in_flight], true)) {

		struct printbuf buf = PRINTBUF;
		prt_printf(&buf, "seq %llu\n", journal_cur_seq(j));
		bch2_journal_bufs_to_text(&buf, j);
		trace_journal_entry_full(c, buf.buf);
		printbuf_exit(&buf);
		count_event(c, journal_entry_full);
	}

	/*
	 * Journal is full - can't rely on reclaim from work item due to
	 * freezing:
	 */
	if ((ret == JOURNAL_ERR_journal_full ||
	     ret == JOURNAL_ERR_journal_pin_full) &&
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

	return ret == JOURNAL_ERR_insufficient_devices
		? -BCH_ERR_erofs_journal_err
		: -BCH_ERR_journal_res_get_blocked;
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

	if (closure_wait_event_timeout(&j->async_wait,
		   (ret = __journal_res_get(j, res, flags)) != -BCH_ERR_journal_res_get_blocked ||
		   (flags & JOURNAL_RES_GET_NONBLOCK),
		   HZ * 10))
		return ret;

	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct printbuf buf = PRINTBUF;
	bch2_journal_debug_to_text(&buf, j);
	bch_err(c, "Journal stuck? Waited for 10 seconds...\n%s",
		buf.buf);
	printbuf_exit(&buf);

	closure_wait_event(&j->async_wait,
		   (ret = __journal_res_get(j, res, flags)) != -BCH_ERR_journal_res_get_blocked ||
		   (flags & JOURNAL_RES_GET_NONBLOCK));
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
		__journal_entry_close(j, JOURNAL_ENTRY_CLOSED_VAL, true);
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
 * @j:		journal object
 * @seq:	seq to flush
 * @parent:	closure object to wait with
 * Returns:	1 if @seq has already been flushed, 0 if @seq is being flushed,
 *		-EIO if @seq will never be flushed
 *
 * Like bch2_journal_wait_on_seq, except that it triggers a write immediately if
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
			__journal_entry_close(j, JOURNAL_ENTRY_CLOSED_VAL, true);

		spin_unlock(&j->lock);

		/*
		 * We're called from bch2_journal_flush_seq() -> wait_event();
		 * but this might block. We won't usually block, so we won't
		 * livelock:
		 */
		sched_annotate_sleep();
		ret = bch2_journal_res_get(j, &res, jset_u64s(0), 0);
		if (ret)
			return ret;

		seq = res.seq;
		buf = journal_seq_to_buf(j, seq);
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
	 * if write was kicked off without a flush, or if we promised it
	 * wouldn't be a flush, flush the next sequence number instead
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

int bch2_journal_flush_seq(struct journal *j, u64 seq, unsigned task_state)
{
	u64 start_time = local_clock();
	int ret, ret2;

	/*
	 * Don't update time_stats when @seq is already flushed:
	 */
	if (seq <= j->flushed_seq_ondisk)
		return 0;

	ret = wait_event_state(j->wait,
			       (ret2 = bch2_journal_flush_seq_async(j, seq, NULL)),
			       task_state);

	if (!ret)
		bch2_time_stats_update(j->flush_seq_time, start_time);

	return ret ?: ret2 < 0 ? ret2 : 0;
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
	return bch2_journal_flush_seq(j, atomic64_read(&j->seq), TASK_UNINTERRUPTIBLE);
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

		/* journal flush already in flight, or flush requseted */
		if (buf->must_flush)
			goto out;

		buf->noflush = true;
	}

	ret = true;
out:
	spin_unlock(&j->lock);
	return ret;
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

	return bch2_journal_flush_seq(j, res.seq, TASK_UNINTERRUPTIBLE);
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

static struct journal_buf *__bch2_next_write_buffer_flush_journal_buf(struct journal *j, u64 max_seq)
{
	struct journal_buf *ret = NULL;

	/* We're inside wait_event(), but using mutex_lock(: */
	sched_annotate_sleep();
	mutex_lock(&j->buf_lock);
	spin_lock(&j->lock);
	max_seq = min(max_seq, journal_cur_seq(j));

	for (u64 seq = journal_last_unwritten_seq(j);
	     seq <= max_seq;
	     seq++) {
		unsigned idx = seq & JOURNAL_BUF_MASK;
		struct journal_buf *buf = j->buf + idx;

		if (buf->need_flush_to_write_buffer) {
			if (seq == journal_cur_seq(j))
				__journal_entry_close(j, JOURNAL_ENTRY_CLOSED_VAL, true);

			union journal_res_state s;
			s.v = atomic64_read_acquire(&j->reservations.counter);

			ret = journal_state_count(s, idx)
				? ERR_PTR(-EAGAIN)
				: buf;
			break;
		}
	}

	spin_unlock(&j->lock);
	if (IS_ERR_OR_NULL(ret))
		mutex_unlock(&j->buf_lock);
	return ret;
}

struct journal_buf *bch2_next_write_buffer_flush_journal_buf(struct journal *j, u64 max_seq)
{
	struct journal_buf *ret;

	wait_event(j->wait, (ret = __bch2_next_write_buffer_flush_journal_buf(j, max_seq)) != ERR_PTR(-EAGAIN));
	return ret;
}

/* allocate journal on a device: */

static int __bch2_set_nr_journal_buckets(struct bch_dev *ca, unsigned nr,
					 bool new_fs, struct closure *cl)
{
	struct bch_fs *c = ca->fs;
	struct journal_device *ja = &ca->journal;
	u64 *new_bucket_seq = NULL, *new_buckets = NULL;
	struct open_bucket **ob = NULL;
	long *bu = NULL;
	unsigned i, pos, nr_got = 0, nr_want = nr - ja->nr;
	int ret = 0;

	BUG_ON(nr <= ja->nr);

	bu		= kcalloc(nr_want, sizeof(*bu), GFP_KERNEL);
	ob		= kcalloc(nr_want, sizeof(*ob), GFP_KERNEL);
	new_buckets	= kcalloc(nr, sizeof(u64), GFP_KERNEL);
	new_bucket_seq	= kcalloc(nr, sizeof(u64), GFP_KERNEL);
	if (!bu || !ob || !new_buckets || !new_bucket_seq) {
		ret = -BCH_ERR_ENOMEM_set_nr_journal_buckets;
		goto err_free;
	}

	for (nr_got = 0; nr_got < nr_want; nr_got++) {
		if (new_fs) {
			bu[nr_got] = bch2_bucket_alloc_new_fs(ca);
			if (bu[nr_got] < 0) {
				ret = -BCH_ERR_ENOSPC_bucket_alloc;
				break;
			}
		} else {
			ob[nr_got] = bch2_bucket_alloc(c, ca, BCH_WATERMARK_normal,
						       BCH_DATA_journal, cl);
			ret = PTR_ERR_OR_ZERO(ob[nr_got]);
			if (ret)
				break;

			ret = bch2_trans_run(c,
				bch2_trans_mark_metadata_bucket(trans, ca,
						ob[nr_got]->bucket, BCH_DATA_journal,
						ca->mi.bucket_size, BTREE_TRIGGER_transactional));
			if (ret) {
				bch2_open_bucket_put(c, ob[nr_got]);
				bch_err_msg(c, ret, "marking new journal buckets");
				break;
			}

			bu[nr_got] = ob[nr_got]->bucket;
		}
	}

	if (!nr_got)
		goto err_free;

	/* Don't return an error if we successfully allocated some buckets: */
	ret = 0;

	if (c) {
		bch2_journal_flush_all_pins(&c->journal);
		bch2_journal_block(&c->journal);
		mutex_lock(&c->sb_lock);
	}

	memcpy(new_buckets,	ja->buckets,	ja->nr * sizeof(u64));
	memcpy(new_bucket_seq,	ja->bucket_seq,	ja->nr * sizeof(u64));

	BUG_ON(ja->discard_idx > ja->nr);

	pos = ja->discard_idx ?: ja->nr;

	memmove(new_buckets + pos + nr_got,
		new_buckets + pos,
		sizeof(new_buckets[0]) * (ja->nr - pos));
	memmove(new_bucket_seq + pos + nr_got,
		new_bucket_seq + pos,
		sizeof(new_bucket_seq[0]) * (ja->nr - pos));

	for (i = 0; i < nr_got; i++) {
		new_buckets[pos + i] = bu[i];
		new_bucket_seq[pos + i] = 0;
	}

	nr = ja->nr + nr_got;

	ret = bch2_journal_buckets_to_sb(c, ca, new_buckets, nr);
	if (ret)
		goto err_unblock;

	if (!new_fs)
		bch2_write_super(c);

	/* Commit: */
	if (c)
		spin_lock(&c->journal.lock);

	swap(new_buckets,	ja->buckets);
	swap(new_bucket_seq,	ja->bucket_seq);
	ja->nr = nr;

	if (pos <= ja->discard_idx)
		ja->discard_idx = (ja->discard_idx + nr_got) % ja->nr;
	if (pos <= ja->dirty_idx_ondisk)
		ja->dirty_idx_ondisk = (ja->dirty_idx_ondisk + nr_got) % ja->nr;
	if (pos <= ja->dirty_idx)
		ja->dirty_idx = (ja->dirty_idx + nr_got) % ja->nr;
	if (pos <= ja->cur_idx)
		ja->cur_idx = (ja->cur_idx + nr_got) % ja->nr;

	if (c)
		spin_unlock(&c->journal.lock);
err_unblock:
	if (c) {
		bch2_journal_unblock(&c->journal);
		mutex_unlock(&c->sb_lock);
	}

	if (ret && !new_fs)
		for (i = 0; i < nr_got; i++)
			bch2_trans_run(c,
				bch2_trans_mark_metadata_bucket(trans, ca,
						bu[i], BCH_DATA_free, 0,
						BTREE_TRIGGER_transactional));
err_free:
	if (!new_fs)
		for (i = 0; i < nr_got; i++)
			bch2_open_bucket_put(c, ob[i]);

	kfree(new_bucket_seq);
	kfree(new_buckets);
	kfree(ob);
	kfree(bu);
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
	int ret = 0;

	closure_init_stack(&cl);

	down_write(&c->state_lock);

	/* don't handle reducing nr of buckets yet: */
	if (nr < ja->nr)
		goto unlock;

	while (ja->nr < nr) {
		struct disk_reservation disk_res = { 0, 0, 0 };

		/*
		 * note: journal buckets aren't really counted as _sectors_ used yet, so
		 * we don't need the disk reservation to avoid the BUG_ON() in buckets.c
		 * when space used goes up without a reservation - but we do need the
		 * reservation to ensure we'll actually be able to allocate:
		 *
		 * XXX: that's not right, disk reservations only ensure a
		 * filesystem-wide allocation will succeed, this is a device
		 * specific allocation - we can hang here:
		 */

		ret = bch2_disk_reservation_get(c, &disk_res,
						bucket_to_sector(ca, nr - ja->nr), 1, 0);
		if (ret)
			break;

		ret = __bch2_set_nr_journal_buckets(ca, nr, false, &cl);

		bch2_disk_reservation_put(c, &disk_res);

		closure_sync(&cl);

		if (ret && ret != -BCH_ERR_bucket_alloc_blocked)
			break;
	}

	bch_err_fn(c, ret);
unlock:
	up_write(&c->state_lock);
	return ret;
}

int bch2_dev_journal_alloc(struct bch_dev *ca, bool new_fs)
{
	unsigned nr;
	int ret;

	if (dynamic_fault("bcachefs:add:journal_alloc")) {
		ret = -BCH_ERR_ENOMEM_set_nr_journal_buckets;
		goto err;
	}

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

	ret = __bch2_set_nr_journal_buckets(ca, nr, new_fs, NULL);
err:
	bch_err_fn(ca, ret);
	return ret;
}

int bch2_fs_journal_alloc(struct bch_fs *c)
{
	for_each_online_member(c, ca) {
		if (ca->journal.nr)
			continue;

		int ret = bch2_dev_journal_alloc(ca, true);
		if (ret) {
			percpu_ref_put(&ca->io_ref);
			return ret;
		}
	}

	return 0;
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

		if (bch2_bkey_has_device_c(bkey_i_to_s_c(&buf->key), dev_idx))
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
	if (!test_bit(JOURNAL_running, &j->flags))
		return;

	bch2_journal_reclaim_stop(j);
	bch2_journal_flush_all_pins(j);

	wait_event(j->wait, bch2_journal_entry_close(j));

	/*
	 * Always write a new journal entry, to make sure the clock hands are up
	 * to date (and match the superblock)
	 */
	bch2_journal_meta(j);

	journal_quiesce(j);
	cancel_delayed_work_sync(&j->write_work);

	WARN(!bch2_journal_error(j) &&
	     test_bit(JOURNAL_replay_done, &j->flags) &&
	     j->last_empty_seq != journal_cur_seq(j),
	     "journal shutdown error: cur seq %llu but last empty seq %llu",
	     journal_cur_seq(j), j->last_empty_seq);

	if (!bch2_journal_error(j))
		clear_bit(JOURNAL_running, &j->flags);
}

int bch2_fs_journal_start(struct journal *j, u64 cur_seq)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_entry_pin_list *p;
	struct journal_replay *i, **_i;
	struct genradix_iter iter;
	bool had_entries = false;
	u64 last_seq = cur_seq, nr, seq;

	genradix_for_each_reverse(&c->journal_entries, iter, _i) {
		i = *_i;

		if (journal_replay_ignore(i))
			continue;

		last_seq = le64_to_cpu(i->j.last_seq);
		break;
	}

	nr = cur_seq - last_seq;

	if (nr + 1 > j->pin.size) {
		free_fifo(&j->pin);
		init_fifo(&j->pin, roundup_pow_of_two(nr + 1), GFP_KERNEL);
		if (!j->pin.data) {
			bch_err(c, "error reallocating journal fifo (%llu open entries)", nr);
			return -BCH_ERR_ENOMEM_journal_pin_fifo;
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

	fifo_for_each_entry_ptr(p, &j->pin, seq)
		journal_pin_list_init(p, 1);

	genradix_for_each(&c->journal_entries, iter, _i) {
		i = *_i;

		if (journal_replay_ignore(i))
			continue;

		seq = le64_to_cpu(i->j.seq);
		BUG_ON(seq >= cur_seq);

		if (seq < last_seq)
			continue;

		if (journal_entry_empty(&i->j))
			j->last_empty_seq = le64_to_cpu(i->j.seq);

		p = journal_seq_pin(j, seq);

		p->devs.nr = 0;
		darray_for_each(i->ptrs, ptr)
			bch2_dev_list_add_dev(&p->devs, ptr->dev);

		had_entries = true;
	}

	if (!had_entries)
		j->last_empty_seq = cur_seq - 1; /* to match j->seq */

	spin_lock(&j->lock);

	set_bit(JOURNAL_running, &j->flags);
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
	struct journal_device *ja = &ca->journal;

	for (unsigned i = 0; i < ARRAY_SIZE(ja->bio); i++) {
		kfree(ja->bio[i]);
		ja->bio[i] = NULL;
	}

	kfree(ja->buckets);
	kfree(ja->bucket_seq);
	ja->buckets	= NULL;
	ja->bucket_seq	= NULL;
}

int bch2_dev_journal_init(struct bch_dev *ca, struct bch_sb *sb)
{
	struct journal_device *ja = &ca->journal;
	struct bch_sb_field_journal *journal_buckets =
		bch2_sb_field_get(sb, journal);
	struct bch_sb_field_journal_v2 *journal_buckets_v2 =
		bch2_sb_field_get(sb, journal_v2);

	ja->nr = 0;

	if (journal_buckets_v2) {
		unsigned nr = bch2_sb_field_journal_v2_nr_entries(journal_buckets_v2);

		for (unsigned i = 0; i < nr; i++)
			ja->nr += le64_to_cpu(journal_buckets_v2->d[i].nr);
	} else if (journal_buckets) {
		ja->nr = bch2_nr_journal_buckets(journal_buckets);
	}

	ja->bucket_seq = kcalloc(ja->nr, sizeof(u64), GFP_KERNEL);
	if (!ja->bucket_seq)
		return -BCH_ERR_ENOMEM_dev_journal_init;

	unsigned nr_bvecs = DIV_ROUND_UP(JOURNAL_ENTRY_SIZE_MAX, PAGE_SIZE);

	for (unsigned i = 0; i < ARRAY_SIZE(ja->bio); i++) {
		ja->bio[i] = kmalloc(struct_size(ja->bio[i], bio.bi_inline_vecs,
				     nr_bvecs), GFP_KERNEL);
		if (!ja->bio[i])
			return -BCH_ERR_ENOMEM_dev_journal_init;

		ja->bio[i]->ca = ca;
		ja->bio[i]->buf_idx = i;
		bio_init(&ja->bio[i]->bio, NULL, ja->bio[i]->bio.bi_inline_vecs, nr_bvecs, 0);
	}

	ja->buckets = kcalloc(ja->nr, sizeof(u64), GFP_KERNEL);
	if (!ja->buckets)
		return -BCH_ERR_ENOMEM_dev_journal_init;

	if (journal_buckets_v2) {
		unsigned nr = bch2_sb_field_journal_v2_nr_entries(journal_buckets_v2);
		unsigned dst = 0;

		for (unsigned i = 0; i < nr; i++)
			for (unsigned j = 0; j < le64_to_cpu(journal_buckets_v2->d[i].nr); j++)
				ja->buckets[dst++] =
					le64_to_cpu(journal_buckets_v2->d[i].start) + j;
	} else if (journal_buckets) {
		for (unsigned i = 0; i < ja->nr; i++)
			ja->buckets[i] = le64_to_cpu(journal_buckets->buckets[i]);
	}

	return 0;
}

void bch2_fs_journal_exit(struct journal *j)
{
	if (j->wq)
		destroy_workqueue(j->wq);

	darray_exit(&j->early_journal_entries);

	for (unsigned i = 0; i < ARRAY_SIZE(j->buf); i++)
		kvfree(j->buf[i].data);
	free_fifo(&j->pin);
}

int bch2_fs_journal_init(struct journal *j)
{
	static struct lock_class_key res_key;

	mutex_init(&j->buf_lock);
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

	if (!(init_fifo(&j->pin, JOURNAL_PIN, GFP_KERNEL)))
		return -BCH_ERR_ENOMEM_journal_pin_fifo;

	for (unsigned i = 0; i < ARRAY_SIZE(j->buf); i++) {
		j->buf[i].buf_size = JOURNAL_ENTRY_SIZE_MIN;
		j->buf[i].data = kvmalloc(j->buf[i].buf_size, GFP_KERNEL);
		if (!j->buf[i].data)
			return -BCH_ERR_ENOMEM_journal_buf;
		j->buf[i].idx = i;
	}

	j->pin.front = j->pin.back = 1;

	j->wq = alloc_workqueue("bcachefs_journal",
				WQ_HIGHPRI|WQ_FREEZABLE|WQ_UNBOUND|WQ_MEM_RECLAIM, 512);
	if (!j->wq)
		return -BCH_ERR_ENOMEM_fs_other_alloc;
	return 0;
}

/* debug: */

static const char * const bch2_journal_flags_strs[] = {
#define x(n)	#n,
	JOURNAL_FLAGS()
#undef x
	NULL
};

void __bch2_journal_debug_to_text(struct printbuf *out, struct journal *j)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	union journal_res_state s;
	unsigned long now = jiffies;
	u64 nr_writes = j->nr_flush_writes + j->nr_noflush_writes;

	printbuf_tabstops_reset(out);
	printbuf_tabstop_push(out, 28);
	out->atomic++;

	rcu_read_lock();
	s = READ_ONCE(j->reservations);

	prt_printf(out, "flags:\t");
	prt_bitflags(out, bch2_journal_flags_strs, j->flags);
	prt_newline(out);
	prt_printf(out, "dirty journal entries:\t%llu/%llu\n",	fifo_used(&j->pin), j->pin.size);
	prt_printf(out, "seq:\t%llu\n",				journal_cur_seq(j));
	prt_printf(out, "seq_ondisk:\t%llu\n",			j->seq_ondisk);
	prt_printf(out, "last_seq:\t%llu\n",			journal_last_seq(j));
	prt_printf(out, "last_seq_ondisk:\t%llu\n",		j->last_seq_ondisk);
	prt_printf(out, "flushed_seq_ondisk:\t%llu\n",		j->flushed_seq_ondisk);
	prt_printf(out, "watermark:\t%s\n",			bch2_watermarks[j->watermark]);
	prt_printf(out, "each entry reserved:\t%u\n",		j->entry_u64s_reserved);
	prt_printf(out, "nr flush writes:\t%llu\n",		j->nr_flush_writes);
	prt_printf(out, "nr noflush writes:\t%llu\n",		j->nr_noflush_writes);
	prt_printf(out, "average write size:\t");
	prt_human_readable_u64(out, nr_writes ? div64_u64(j->entry_bytes_written, nr_writes) : 0);
	prt_newline(out);
	prt_printf(out, "nr direct reclaim:\t%llu\n",		j->nr_direct_reclaim);
	prt_printf(out, "nr background reclaim:\t%llu\n",	j->nr_background_reclaim);
	prt_printf(out, "reclaim kicked:\t%u\n",		j->reclaim_kicked);
	prt_printf(out, "reclaim runs in:\t%u ms\n",		time_after(j->next_reclaim, now)
	       ? jiffies_to_msecs(j->next_reclaim - jiffies) : 0);
	prt_printf(out, "blocked:\t%u\n",			j->blocked);
	prt_printf(out, "current entry sectors:\t%u\n",		j->cur_entry_sectors);
	prt_printf(out, "current entry error:\t%s\n",		bch2_journal_errors[j->cur_entry_error]);
	prt_printf(out, "current entry:\t");

	switch (s.cur_entry_offset) {
	case JOURNAL_ENTRY_ERROR_VAL:
		prt_printf(out, "error\n");
		break;
	case JOURNAL_ENTRY_CLOSED_VAL:
		prt_printf(out, "closed\n");
		break;
	default:
		prt_printf(out, "%u/%u\n", s.cur_entry_offset, j->cur_entry_u64s);
		break;
	}

	prt_printf(out, "unwritten entries:\n");
	bch2_journal_bufs_to_text(out, j);

	prt_printf(out, "space:\n");
	printbuf_indent_add(out, 2);
	prt_printf(out, "discarded\t%u:%u\n",
	       j->space[journal_space_discarded].next_entry,
	       j->space[journal_space_discarded].total);
	prt_printf(out, "clean ondisk\t%u:%u\n",
	       j->space[journal_space_clean_ondisk].next_entry,
	       j->space[journal_space_clean_ondisk].total);
	prt_printf(out, "clean\t%u:%u\n",
	       j->space[journal_space_clean].next_entry,
	       j->space[journal_space_clean].total);
	prt_printf(out, "total\t%u:%u\n",
	       j->space[journal_space_total].next_entry,
	       j->space[journal_space_total].total);
	printbuf_indent_sub(out, 2);

	for_each_member_device_rcu(c, ca, &c->rw_devs[BCH_DATA_journal]) {
		struct journal_device *ja = &ca->journal;

		if (!test_bit(ca->dev_idx, c->rw_devs[BCH_DATA_journal].d))
			continue;

		if (!ja->nr)
			continue;

		prt_printf(out, "dev %u:\n",			ca->dev_idx);
		printbuf_indent_add(out, 2);
		prt_printf(out, "nr\t%u\n",			ja->nr);
		prt_printf(out, "bucket size\t%u\n",		ca->mi.bucket_size);
		prt_printf(out, "available\t%u:%u\n",		bch2_journal_dev_buckets_available(j, ja, journal_space_discarded), ja->sectors_free);
		prt_printf(out, "discard_idx\t%u\n",		ja->discard_idx);
		prt_printf(out, "dirty_ondisk\t%u (seq %llu)\n",ja->dirty_idx_ondisk,	ja->bucket_seq[ja->dirty_idx_ondisk]);
		prt_printf(out, "dirty_idx\t%u (seq %llu)\n",	ja->dirty_idx,		ja->bucket_seq[ja->dirty_idx]);
		prt_printf(out, "cur_idx\t%u (seq %llu)\n",	ja->cur_idx,		ja->bucket_seq[ja->cur_idx]);
		printbuf_indent_sub(out, 2);
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
	if (!test_bit(JOURNAL_running, &j->flags)) {
		spin_unlock(&j->lock);
		return true;
	}

	*seq = max(*seq, j->pin.front);

	if (*seq >= j->pin.back) {
		spin_unlock(&j->lock);
		return true;
	}

	out->atomic++;

	pin_list = journal_seq_pin(j, *seq);

	prt_printf(out, "%llu: count %u\n", *seq, atomic_read(&pin_list->count));
	printbuf_indent_add(out, 2);

	for (unsigned i = 0; i < ARRAY_SIZE(pin_list->list); i++)
		list_for_each_entry(pin, &pin_list->list[i], list)
			prt_printf(out, "\t%px %ps\n", pin, pin->flush);

	if (!list_empty(&pin_list->flushed))
		prt_printf(out, "flushed:\n");

	list_for_each_entry(pin, &pin_list->flushed, list)
		prt_printf(out, "\t%px %ps\n", pin, pin->flush);

	printbuf_indent_sub(out, 2);

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
