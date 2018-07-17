// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "journal.h"
#include "journal_reclaim.h"
#include "replicas.h"
#include "super.h"

/*
 * Journal entry pinning - machinery for holding a reference on a given journal
 * entry, holding it open to ensure it gets replayed during recovery:
 */

static inline void __journal_pin_add(struct journal *j,
				     u64 seq,
				     struct journal_entry_pin *pin,
				     journal_pin_flush_fn flush_fn)
{
	struct journal_entry_pin_list *pin_list = journal_seq_pin(j, seq);

	BUG_ON(journal_pin_active(pin));
	BUG_ON(!atomic_read(&pin_list->count));

	atomic_inc(&pin_list->count);
	pin->seq	= seq;
	pin->flush	= flush_fn;

	if (flush_fn)
		list_add(&pin->list, &pin_list->list);
	else
		INIT_LIST_HEAD(&pin->list);

	/*
	 * If the journal is currently full,  we might want to call flush_fn
	 * immediately:
	 */
	journal_wake(j);
}

void bch2_journal_pin_add(struct journal *j, u64 seq,
			  struct journal_entry_pin *pin,
			  journal_pin_flush_fn flush_fn)
{
	spin_lock(&j->lock);
	__journal_pin_add(j, seq, pin, flush_fn);
	spin_unlock(&j->lock);
}

static inline void __journal_pin_drop(struct journal *j,
				      struct journal_entry_pin *pin)
{
	struct journal_entry_pin_list *pin_list;

	if (!journal_pin_active(pin))
		return;

	pin_list = journal_seq_pin(j, pin->seq);
	pin->seq = 0;
	list_del_init(&pin->list);

	/*
	 * Unpinning a journal entry make make journal_next_bucket() succeed, if
	 * writing a new last_seq will now make another bucket available:
	 */
	if (atomic_dec_and_test(&pin_list->count) &&
	    pin_list == &fifo_peek_front(&j->pin))
		bch2_journal_reclaim_fast(j);
}

void bch2_journal_pin_drop(struct journal *j,
			   struct journal_entry_pin *pin)
{
	spin_lock(&j->lock);
	__journal_pin_drop(j, pin);
	spin_unlock(&j->lock);
}

void bch2_journal_pin_add_if_older(struct journal *j,
				  struct journal_entry_pin *src_pin,
				  struct journal_entry_pin *pin,
				  journal_pin_flush_fn flush_fn)
{
	spin_lock(&j->lock);

	if (journal_pin_active(src_pin) &&
	    (!journal_pin_active(pin) ||
	     src_pin->seq < pin->seq)) {
		__journal_pin_drop(j, pin);
		__journal_pin_add(j, src_pin->seq, pin, flush_fn);
	}

	spin_unlock(&j->lock);
}

void bch2_journal_pin_flush(struct journal *j, struct journal_entry_pin *pin)
{
	BUG_ON(journal_pin_active(pin));

	wait_event(j->pin_flush_wait, j->flush_in_progress != pin);
}

/*
 * Journal reclaim: flush references to open journal entries to reclaim space in
 * the journal
 *
 * May be done by the journal code in the background as needed to free up space
 * for more journal entries, or as part of doing a clean shutdown, or to migrate
 * data off of a specific device:
 */

/**
 * bch2_journal_reclaim_fast - do the fast part of journal reclaim
 *
 * Called from IO submission context, does not block. Cleans up after btree
 * write completions by advancing the journal pin and each cache's last_idx,
 * kicking off discards and background reclaim as necessary.
 */
void bch2_journal_reclaim_fast(struct journal *j)
{
	struct journal_entry_pin_list temp;
	bool popped = false;

	lockdep_assert_held(&j->lock);

	/*
	 * Unpin journal entries whose reference counts reached zero, meaning
	 * all btree nodes got written out
	 */
	while (!atomic_read(&fifo_peek_front(&j->pin).count)) {
		BUG_ON(!list_empty(&fifo_peek_front(&j->pin).list));
		BUG_ON(!fifo_pop(&j->pin, temp));
		popped = true;
	}

	if (popped)
		journal_wake(j);
}

static void journal_pin_mark_flushing(struct journal *j,
				      struct journal_entry_pin *pin,
				      u64 seq)
{
	lockdep_assert_held(&j->reclaim_lock);

	list_move(&pin->list, &journal_seq_pin(j, seq)->flushed);
	BUG_ON(j->flush_in_progress);
	j->flush_in_progress = pin;
}

static void journal_pin_flush(struct journal *j,
			      struct journal_entry_pin *pin,
			      u64 seq)
{
	pin->flush(j, pin, seq);

	BUG_ON(j->flush_in_progress != pin);
	j->flush_in_progress = NULL;
	wake_up(&j->pin_flush_wait);
}

static struct journal_entry_pin *
journal_get_next_pin(struct journal *j, u64 seq_to_flush, u64 *seq)
{
	struct journal_entry_pin_list *pin_list;
	struct journal_entry_pin *ret = NULL;

	/* no need to iterate over empty fifo entries: */
	bch2_journal_reclaim_fast(j);

	fifo_for_each_entry_ptr(pin_list, &j->pin, *seq)
		if (*seq > seq_to_flush ||
		    (ret = list_first_entry_or_null(&pin_list->list,
				struct journal_entry_pin, list)))
			break;

	return ret;
}

static bool should_discard_bucket(struct journal *j, struct journal_device *ja)
{
	bool ret;

	spin_lock(&j->lock);
	ret = ja->nr &&
		(ja->last_idx != ja->cur_idx &&
		 ja->bucket_seq[ja->last_idx] < j->last_seq_ondisk);
	spin_unlock(&j->lock);

	return ret;
}

/**
 * bch2_journal_reclaim_work - free up journal buckets
 *
 * Background journal reclaim writes out btree nodes. It should be run
 * early enough so that we never completely run out of journal buckets.
 *
 * High watermarks for triggering background reclaim:
 * - FIFO has fewer than 512 entries left
 * - fewer than 25% journal buckets free
 *
 * Background reclaim runs until low watermarks are reached:
 * - FIFO has more than 1024 entries left
 * - more than 50% journal buckets free
 *
 * As long as a reclaim can complete in the time it takes to fill up
 * 512 journal entries or 25% of all journal buckets, then
 * journal_next_bucket() should not stall.
 */
void bch2_journal_reclaim_work(struct work_struct *work)
{
	struct bch_fs *c = container_of(to_delayed_work(work),
				struct bch_fs, journal.reclaim_work);
	struct journal *j = &c->journal;
	struct bch_dev *ca;
	struct journal_entry_pin *pin;
	u64 seq, seq_to_flush = 0;
	unsigned iter, bucket_to_flush;
	unsigned long next_flush;
	bool reclaim_lock_held = false, need_flush;

	/*
	 * Advance last_idx to point to the oldest journal entry containing
	 * btree node updates that have not yet been written out
	 */
	for_each_rw_member(ca, c, iter) {
		struct journal_device *ja = &ca->journal;

		if (!ja->nr)
			continue;

		while (should_discard_bucket(j, ja)) {
			if (!reclaim_lock_held) {
				/*
				 * ugh:
				 * might be called from __journal_res_get()
				 * under wait_event() - have to go back to
				 * TASK_RUNNING before doing something that
				 * would block, but only if we're doing work:
				 */
				__set_current_state(TASK_RUNNING);

				mutex_lock(&j->reclaim_lock);
				reclaim_lock_held = true;
				/* recheck under reclaim_lock: */
				continue;
			}

			if (ca->mi.discard &&
			    bdev_max_discard_sectors(ca->disk_sb.bdev))
				blkdev_issue_discard(ca->disk_sb.bdev,
					bucket_to_sector(ca,
						ja->buckets[ja->last_idx]),
					ca->mi.bucket_size, GFP_NOIO);

			spin_lock(&j->lock);
			ja->last_idx = (ja->last_idx + 1) % ja->nr;
			spin_unlock(&j->lock);

			journal_wake(j);
		}

		/*
		 * Write out enough btree nodes to free up 50% journal
		 * buckets
		 */
		spin_lock(&j->lock);
		bucket_to_flush = (ja->cur_idx + (ja->nr >> 1)) % ja->nr;
		seq_to_flush = max_t(u64, seq_to_flush,
				     ja->bucket_seq[bucket_to_flush]);
		spin_unlock(&j->lock);
	}

	/* Also flush if the pin fifo is more than half full */
	spin_lock(&j->lock);
	seq_to_flush = max_t(s64, seq_to_flush,
			     (s64) journal_cur_seq(j) -
			     (j->pin.size >> 1));

	/*
	 * If it's been longer than j->reclaim_delay_ms since we last flushed,
	 * make sure to flush at least one journal pin:
	 */
	next_flush = j->last_flushed + msecs_to_jiffies(j->reclaim_delay_ms);
	need_flush = time_after(jiffies, next_flush);

	while ((pin = journal_get_next_pin(j, need_flush
					   ? U64_MAX
					   : seq_to_flush, &seq))) {
		if (!reclaim_lock_held) {
			spin_unlock(&j->lock);
			__set_current_state(TASK_RUNNING);
			mutex_lock(&j->reclaim_lock);
			reclaim_lock_held = true;
			spin_lock(&j->lock);
			continue;
		}

		journal_pin_mark_flushing(j, pin, seq);
		spin_unlock(&j->lock);

		journal_pin_flush(j, pin, seq);

		need_flush = false;
		j->last_flushed = jiffies;

		spin_lock(&j->lock);
	}

	spin_unlock(&j->lock);

	if (reclaim_lock_held)
		mutex_unlock(&j->reclaim_lock);

	if (!test_bit(BCH_FS_RO, &c->flags))
		queue_delayed_work(system_freezable_wq, &j->reclaim_work,
				   msecs_to_jiffies(j->reclaim_delay_ms));
}

static int journal_flush_done(struct journal *j, u64 seq_to_flush,
			      struct journal_entry_pin **pin,
			      u64 *pin_seq)
{
	int ret;

	*pin = NULL;

	ret = bch2_journal_error(j);
	if (ret)
		return ret;

	spin_lock(&j->lock);
	/*
	 * If journal replay hasn't completed, the unreplayed journal entries
	 * hold refs on their corresponding sequence numbers
	 */
	ret = (*pin = journal_get_next_pin(j, seq_to_flush, pin_seq)) != NULL ||
		!test_bit(JOURNAL_REPLAY_DONE, &j->flags) ||
		journal_last_seq(j) > seq_to_flush ||
		(fifo_used(&j->pin) == 1 &&
		 atomic_read(&fifo_peek_front(&j->pin).count) == 1);
	if (*pin)
		journal_pin_mark_flushing(j, *pin, *pin_seq);

	spin_unlock(&j->lock);

	return ret;
}

void bch2_journal_flush_pins(struct journal *j, u64 seq_to_flush)
{
	struct journal_entry_pin *pin;
	u64 pin_seq;

	if (!test_bit(JOURNAL_STARTED, &j->flags))
		return;

	mutex_lock(&j->reclaim_lock);

	while (1) {
		wait_event(j->wait, journal_flush_done(j, seq_to_flush,
						       &pin, &pin_seq));
		if (!pin)
			break;

		journal_pin_flush(j, pin, pin_seq);
	}

	mutex_unlock(&j->reclaim_lock);
}

int bch2_journal_flush_device_pins(struct journal *j, int dev_idx)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_entry_pin_list *p;
	struct bch_devs_list devs;
	u64 iter, seq = 0;
	int ret = 0;

	spin_lock(&j->lock);
	fifo_for_each_entry_ptr(p, &j->pin, iter)
		if (dev_idx >= 0
		    ? bch2_dev_list_has_dev(p->devs, dev_idx)
		    : p->devs.nr < c->opts.metadata_replicas)
			seq = iter;
	spin_unlock(&j->lock);

	bch2_journal_flush_pins(j, seq);

	ret = bch2_journal_error(j);
	if (ret)
		return ret;

	mutex_lock(&c->replicas_gc_lock);
	bch2_replicas_gc_start(c, 1 << BCH_DATA_JOURNAL);

	seq = 0;

	spin_lock(&j->lock);
	while (!ret && seq < j->pin.back) {
		seq = max(seq, journal_last_seq(j));
		devs = journal_seq_pin(j, seq)->devs;
		seq++;

		spin_unlock(&j->lock);
		ret = bch2_mark_replicas(c, BCH_DATA_JOURNAL, devs);
		spin_lock(&j->lock);
	}
	spin_unlock(&j->lock);

	ret = bch2_replicas_gc_end(c, ret);
	mutex_unlock(&c->replicas_gc_lock);

	return ret;
}
