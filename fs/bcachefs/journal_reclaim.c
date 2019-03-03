// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "journal.h"
#include "journal_io.h"
#include "journal_reclaim.h"
#include "replicas.h"
#include "super.h"

/* Free space calculations: */

unsigned bch2_journal_dev_buckets_available(struct journal *j,
					    struct journal_device *ja)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	unsigned next = (ja->cur_idx + 1) % ja->nr;
	unsigned available = (ja->discard_idx + ja->nr - next) % ja->nr;

	/*
	 * Allocator startup needs some journal space before we can do journal
	 * replay:
	 */
	if (available && test_bit(BCH_FS_ALLOCATOR_STARTED, &c->flags))
		--available;

	/*
	 * Don't use the last bucket unless writing the new last_seq
	 * will make another bucket available:
	 */
	if (available && ja->dirty_idx_ondisk == ja->dirty_idx)
		--available;

	return available;
}

void bch2_journal_space_available(struct journal *j)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct bch_dev *ca;
	unsigned sectors_next_entry	= UINT_MAX;
	unsigned sectors_total		= UINT_MAX;
	unsigned max_entry_size		= min(j->buf[0].buf_size >> 9,
					      j->buf[1].buf_size >> 9);
	unsigned i, nr_online = 0, nr_devs = 0;
	unsigned unwritten_sectors = j->reservations.prev_buf_unwritten
		? journal_prev_buf(j)->sectors
		: 0;
	int ret = 0;

	lockdep_assert_held(&j->lock);

	rcu_read_lock();
	for_each_member_device_rcu(ca, c, i,
				   &c->rw_devs[BCH_DATA_JOURNAL]) {
		struct journal_device *ja = &ca->journal;

		if (!ja->nr)
			continue;

		while (ja->dirty_idx != ja->cur_idx &&
		       ja->bucket_seq[ja->dirty_idx] < journal_last_seq(j))
			ja->dirty_idx = (ja->dirty_idx + 1) % ja->nr;

		while (ja->dirty_idx_ondisk != ja->dirty_idx &&
		       ja->bucket_seq[ja->dirty_idx_ondisk] < j->last_seq_ondisk)
			ja->dirty_idx_ondisk = (ja->dirty_idx_ondisk + 1) % ja->nr;

		nr_online++;
	}

	if (nr_online < c->opts.metadata_replicas_required) {
		ret = -EROFS;
		sectors_next_entry = 0;
		goto out;
	}

	for_each_member_device_rcu(ca, c, i,
				   &c->rw_devs[BCH_DATA_JOURNAL]) {
		struct journal_device *ja = &ca->journal;
		unsigned buckets_this_device, sectors_this_device;

		if (!ja->nr)
			continue;

		buckets_this_device = bch2_journal_dev_buckets_available(j, ja);
		sectors_this_device = ja->sectors_free;

		/*
		 * We that we don't allocate the space for a journal entry
		 * until we write it out - thus, account for it here:
		 */
		if (unwritten_sectors >= sectors_this_device) {
			if (!buckets_this_device)
				continue;

			buckets_this_device--;
			sectors_this_device = ca->mi.bucket_size;
		}

		sectors_this_device -= unwritten_sectors;

		if (sectors_this_device < ca->mi.bucket_size &&
		    buckets_this_device) {
			buckets_this_device--;
			sectors_this_device = ca->mi.bucket_size;
		}

		if (!sectors_this_device)
			continue;

		sectors_next_entry = min(sectors_next_entry,
					 sectors_this_device);

		sectors_total = min(sectors_total,
			buckets_this_device * ca->mi.bucket_size +
			sectors_this_device);

		max_entry_size = min_t(unsigned, max_entry_size,
				       ca->mi.bucket_size);

		nr_devs++;
	}

	if (!sectors_next_entry ||
	    nr_devs < min_t(unsigned, nr_online, c->opts.metadata_replicas)) {
		ret = -ENOSPC;
		sectors_next_entry = 0;
	} else if (!fifo_free(&j->pin)) {
		ret = -ENOSPC;
		sectors_next_entry = 0;
	}
out:
	rcu_read_unlock();

	j->cur_entry_sectors	= sectors_next_entry;
	j->cur_entry_error	= ret;

	if (!ret)
		journal_wake(j);
}

/* Discards - last part of journal reclaim: */

static bool should_discard_bucket(struct journal *j, struct journal_device *ja)
{
	bool ret;

	spin_lock(&j->lock);
	ret = ja->discard_idx != ja->dirty_idx_ondisk;
	spin_unlock(&j->lock);

	return ret;
}

/*
 * Advance ja->discard_idx as long as it points to buckets that are no longer
 * dirty, issuing discards if necessary:
 */
static void bch2_journal_do_discards(struct journal *j)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct bch_dev *ca;
	unsigned iter;

	mutex_lock(&j->discard_lock);

	for_each_rw_member(ca, c, iter) {
		struct journal_device *ja = &ca->journal;

		while (should_discard_bucket(j, ja)) {
			if (ca->mi.discard &&
			    bdev_max_discard_sectors(ca->disk_sb.bdev))
				blkdev_issue_discard(ca->disk_sb.bdev,
					bucket_to_sector(ca,
						ja->buckets[ja->discard_idx]),
					ca->mi.bucket_size, GFP_NOIO);

			spin_lock(&j->lock);
			ja->discard_idx = (ja->discard_idx + 1) % ja->nr;

			bch2_journal_space_available(j);
			spin_unlock(&j->lock);
		}
	}

	mutex_unlock(&j->discard_lock);
}

/*
 * Journal entry pinning - machinery for holding a reference on a given journal
 * entry, holding it open to ensure it gets replayed during recovery:
 */

static void bch2_journal_reclaim_fast(struct journal *j)
{
	struct journal_entry_pin_list temp;
	bool popped = false;

	lockdep_assert_held(&j->lock);

	/*
	 * Unpin journal entries whose reference counts reached zero, meaning
	 * all btree nodes got written out
	 */
	while (!fifo_empty(&j->pin) &&
	       !atomic_read(&fifo_peek_front(&j->pin).count)) {
		BUG_ON(!list_empty(&fifo_peek_front(&j->pin).list));
		BUG_ON(!fifo_pop(&j->pin, temp));
		popped = true;
	}

	if (popped)
		bch2_journal_space_available(j);
}

void bch2_journal_pin_put(struct journal *j, u64 seq)
{
	struct journal_entry_pin_list *pin_list = journal_seq_pin(j, seq);

	if (atomic_dec_and_test(&pin_list->count)) {
		spin_lock(&j->lock);
		bch2_journal_reclaim_fast(j);
		spin_unlock(&j->lock);
	}
}

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

	list_add(&pin->list, flush_fn ? &pin_list->list : &pin_list->flushed);

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
	else if (fifo_used(&j->pin) == 1 &&
		 atomic_read(&pin_list->count) == 1)
		journal_wake(j);
}

void bch2_journal_pin_drop(struct journal *j,
			   struct journal_entry_pin *pin)
{
	spin_lock(&j->lock);
	__journal_pin_drop(j, pin);
	spin_unlock(&j->lock);
}

void bch2_journal_pin_update(struct journal *j, u64 seq,
			     struct journal_entry_pin *pin,
			     journal_pin_flush_fn flush_fn)
{
	spin_lock(&j->lock);

	if (pin->seq != seq) {
		__journal_pin_drop(j, pin);
		__journal_pin_add(j, seq, pin, flush_fn);
	} else {
		struct journal_entry_pin_list *pin_list =
			journal_seq_pin(j, seq);

		list_move(&pin->list, &pin_list->list);
	}

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

static struct journal_entry_pin *
journal_get_next_pin(struct journal *j, u64 max_seq, u64 *seq)
{
	struct journal_entry_pin_list *pin_list;
	struct journal_entry_pin *ret = NULL;

	spin_lock(&j->lock);

	fifo_for_each_entry_ptr(pin_list, &j->pin, *seq)
		if (*seq > max_seq ||
		    (ret = list_first_entry_or_null(&pin_list->list,
				struct journal_entry_pin, list)))
			break;

	if (ret) {
		list_move(&ret->list, &pin_list->flushed);
		BUG_ON(j->flush_in_progress);
		j->flush_in_progress = ret;
		j->last_flushed = jiffies;
	}

	spin_unlock(&j->lock);

	return ret;
}

static void journal_flush_pins(struct journal *j, u64 seq_to_flush,
			       unsigned min_nr)
{
	struct journal_entry_pin *pin;
	u64 seq;

	lockdep_assert_held(&j->reclaim_lock);

	while ((pin = journal_get_next_pin(j, min_nr
				? U64_MAX : seq_to_flush, &seq))) {
		if (min_nr)
			min_nr--;

		pin->flush(j, pin, seq);

		BUG_ON(j->flush_in_progress != pin);
		j->flush_in_progress = NULL;
		wake_up(&j->pin_flush_wait);
	}
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
	unsigned iter, bucket_to_flush, min_nr = 0;
	u64 seq_to_flush = 0;

	bch2_journal_do_discards(j);

	mutex_lock(&j->reclaim_lock);
	spin_lock(&j->lock);

	for_each_rw_member(ca, c, iter) {
		struct journal_device *ja = &ca->journal;

		if (!ja->nr)
			continue;


		/* Try to keep the journal at most half full: */
		bucket_to_flush = (ja->cur_idx + (ja->nr >> 1)) % ja->nr;
		seq_to_flush = max_t(u64, seq_to_flush,
				     ja->bucket_seq[bucket_to_flush]);
	}

	/* Also flush if the pin fifo is more than half full */
	seq_to_flush = max_t(s64, seq_to_flush,
			     (s64) journal_cur_seq(j) -
			     (j->pin.size >> 1));
	spin_unlock(&j->lock);

	/*
	 * If it's been longer than j->reclaim_delay_ms since we last flushed,
	 * make sure to flush at least one journal pin:
	 */
	if (time_after(jiffies, j->last_flushed +
		       msecs_to_jiffies(j->reclaim_delay_ms)))
		min_nr = 1;

	journal_flush_pins(j, seq_to_flush, min_nr);

	mutex_unlock(&j->reclaim_lock);

	if (!test_bit(BCH_FS_RO, &c->flags))
		queue_delayed_work(c->journal_reclaim_wq, &j->reclaim_work,
				   msecs_to_jiffies(j->reclaim_delay_ms));
}

static int journal_flush_done(struct journal *j, u64 seq_to_flush)
{
	int ret;

	ret = bch2_journal_error(j);
	if (ret)
		return ret;

	mutex_lock(&j->reclaim_lock);

	journal_flush_pins(j, seq_to_flush, 0);

	spin_lock(&j->lock);
	/*
	 * If journal replay hasn't completed, the unreplayed journal entries
	 * hold refs on their corresponding sequence numbers
	 */
	ret = !test_bit(JOURNAL_REPLAY_DONE, &j->flags) ||
		journal_last_seq(j) > seq_to_flush ||
		(fifo_used(&j->pin) == 1 &&
		 atomic_read(&fifo_peek_front(&j->pin).count) == 1);

	spin_unlock(&j->lock);
	mutex_unlock(&j->reclaim_lock);

	return ret;
}

void bch2_journal_flush_pins(struct journal *j, u64 seq_to_flush)
{
	if (!test_bit(JOURNAL_STARTED, &j->flags))
		return;

	closure_wait_event(&j->async_wait, journal_flush_done(j, seq_to_flush));
}

int bch2_journal_flush_device_pins(struct journal *j, int dev_idx)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_entry_pin_list *p;
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
		struct bch_replicas_padded replicas;

		seq = max(seq, journal_last_seq(j));
		bch2_devlist_to_replicas(&replicas.e, BCH_DATA_JOURNAL,
					 journal_seq_pin(j, seq)->devs);
		seq++;

		spin_unlock(&j->lock);
		ret = bch2_mark_replicas(c, &replicas.e);
		spin_lock(&j->lock);
	}
	spin_unlock(&j->lock);

	ret = bch2_replicas_gc_end(c, ret);
	mutex_unlock(&c->replicas_gc_lock);

	return ret;
}
