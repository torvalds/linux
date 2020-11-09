// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "journal.h"
#include "journal_io.h"
#include "journal_reclaim.h"
#include "replicas.h"
#include "super.h"

/* Free space calculations: */

static unsigned journal_space_from(struct journal_device *ja,
				   enum journal_space_from from)
{
	switch (from) {
	case journal_space_discarded:
		return ja->discard_idx;
	case journal_space_clean_ondisk:
		return ja->dirty_idx_ondisk;
	case journal_space_clean:
		return ja->dirty_idx;
	default:
		BUG();
	}
}

unsigned bch2_journal_dev_buckets_available(struct journal *j,
					    struct journal_device *ja,
					    enum journal_space_from from)
{
	unsigned available = (journal_space_from(ja, from) -
			      ja->cur_idx - 1 + ja->nr) % ja->nr;

	/*
	 * Don't use the last bucket unless writing the new last_seq
	 * will make another bucket available:
	 */
	if (available && ja->dirty_idx_ondisk == ja->dirty_idx)
		--available;

	return available;
}

static void journal_set_remaining(struct journal *j, unsigned u64s_remaining)
{
	union journal_preres_state old, new;
	u64 v = atomic64_read(&j->prereserved.counter);

	do {
		old.v = new.v = v;
		new.remaining = u64s_remaining;
	} while ((v = atomic64_cmpxchg(&j->prereserved.counter,
				       old.v, new.v)) != old.v);
}

static struct journal_space {
	unsigned	next_entry;
	unsigned	remaining;
} __journal_space_available(struct journal *j, unsigned nr_devs_want,
			    enum journal_space_from from)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct bch_dev *ca;
	unsigned sectors_next_entry	= UINT_MAX;
	unsigned sectors_total		= UINT_MAX;
	unsigned i, nr_devs = 0;
	unsigned unwritten_sectors = j->reservations.prev_buf_unwritten
		? journal_prev_buf(j)->sectors
		: 0;

	rcu_read_lock();
	for_each_member_device_rcu(ca, c, i,
				   &c->rw_devs[BCH_DATA_journal]) {
		struct journal_device *ja = &ca->journal;
		unsigned buckets_this_device, sectors_this_device;

		if (!ja->nr)
			continue;

		buckets_this_device = bch2_journal_dev_buckets_available(j, ja, from);
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

		nr_devs++;
	}
	rcu_read_unlock();

	if (nr_devs < nr_devs_want)
		return (struct journal_space) { 0, 0 };

	return (struct journal_space) {
		.next_entry	= sectors_next_entry,
		.remaining	= max_t(int, 0, sectors_total - sectors_next_entry),
	};
}

void bch2_journal_space_available(struct journal *j)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct bch_dev *ca;
	struct journal_space discarded, clean_ondisk, clean;
	unsigned overhead, u64s_remaining = 0;
	unsigned max_entry_size	 = min(j->buf[0].buf_size >> 9,
				       j->buf[1].buf_size >> 9);
	unsigned i, nr_online = 0, nr_devs_want;
	bool can_discard = false;
	int ret = 0;

	lockdep_assert_held(&j->lock);

	rcu_read_lock();
	for_each_member_device_rcu(ca, c, i,
				   &c->rw_devs[BCH_DATA_journal]) {
		struct journal_device *ja = &ca->journal;

		if (!ja->nr)
			continue;

		while (ja->dirty_idx != ja->cur_idx &&
		       ja->bucket_seq[ja->dirty_idx] < journal_last_seq(j))
			ja->dirty_idx = (ja->dirty_idx + 1) % ja->nr;

		while (ja->dirty_idx_ondisk != ja->dirty_idx &&
		       ja->bucket_seq[ja->dirty_idx_ondisk] < j->last_seq_ondisk)
			ja->dirty_idx_ondisk = (ja->dirty_idx_ondisk + 1) % ja->nr;

		if (ja->discard_idx != ja->dirty_idx_ondisk)
			can_discard = true;

		max_entry_size = min_t(unsigned, max_entry_size, ca->mi.bucket_size);
		nr_online++;
	}
	rcu_read_unlock();

	j->can_discard = can_discard;

	if (nr_online < c->opts.metadata_replicas_required) {
		ret = -EROFS;
		goto out;
	}

	if (!fifo_free(&j->pin)) {
		ret = -ENOSPC;
		goto out;
	}

	nr_devs_want = min_t(unsigned, nr_online, c->opts.metadata_replicas);

	discarded	= __journal_space_available(j, nr_devs_want, journal_space_discarded);
	clean_ondisk	= __journal_space_available(j, nr_devs_want, journal_space_clean_ondisk);
	clean		= __journal_space_available(j, nr_devs_want, journal_space_clean);

	if (!discarded.next_entry)
		ret = -ENOSPC;

	overhead = DIV_ROUND_UP(clean.remaining, max_entry_size) *
		journal_entry_overhead(j);
	u64s_remaining = clean.remaining << 6;
	u64s_remaining = max_t(int, 0, u64s_remaining - overhead);
	u64s_remaining /= 4;
out:
	j->cur_entry_sectors	= !ret ? discarded.next_entry : 0;
	j->cur_entry_error	= ret;
	journal_set_remaining(j, u64s_remaining);
	journal_check_may_get_unreserved(j);

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
void bch2_journal_do_discards(struct journal *j)
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

static void bch2_journal_pin_add_locked(struct journal *j, u64 seq,
			    struct journal_entry_pin *pin,
			    journal_pin_flush_fn flush_fn)
{
	struct journal_entry_pin_list *pin_list = journal_seq_pin(j, seq);

	__journal_pin_drop(j, pin);

	BUG_ON(!atomic_read(&pin_list->count) && seq == journal_last_seq(j));

	atomic_inc(&pin_list->count);
	pin->seq	= seq;
	pin->flush	= flush_fn;

	list_add(&pin->list, flush_fn ? &pin_list->list : &pin_list->flushed);
}

void __bch2_journal_pin_add(struct journal *j, u64 seq,
			    struct journal_entry_pin *pin,
			    journal_pin_flush_fn flush_fn)
{
	spin_lock(&j->lock);
	bch2_journal_pin_add_locked(j, seq, pin, flush_fn);
	spin_unlock(&j->lock);

	/*
	 * If the journal is currently full,  we might want to call flush_fn
	 * immediately:
	 */
	journal_wake(j);
}

void bch2_journal_pin_update(struct journal *j, u64 seq,
			     struct journal_entry_pin *pin,
			     journal_pin_flush_fn flush_fn)
{
	if (journal_pin_active(pin) && pin->seq < seq)
		return;

	spin_lock(&j->lock);

	if (pin->seq != seq) {
		bch2_journal_pin_add_locked(j, seq, pin, flush_fn);
	} else {
		struct journal_entry_pin_list *pin_list =
			journal_seq_pin(j, seq);

		/*
		 * If the pin is already pinning the right sequence number, it
		 * still might've already been flushed:
		 */
		list_move(&pin->list, &pin_list->list);
	}

	spin_unlock(&j->lock);

	/*
	 * If the journal is currently full,  we might want to call flush_fn
	 * immediately:
	 */
	journal_wake(j);
}

void bch2_journal_pin_copy(struct journal *j,
			   struct journal_entry_pin *dst,
			   struct journal_entry_pin *src,
			   journal_pin_flush_fn flush_fn)
{
	spin_lock(&j->lock);

	if (journal_pin_active(src) &&
	    (!journal_pin_active(dst) || src->seq < dst->seq))
		bch2_journal_pin_add_locked(j, src->seq, dst, flush_fn);

	spin_unlock(&j->lock);
}

/**
 * bch2_journal_pin_flush: ensure journal pin callback is no longer running
 */
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

	if (!test_bit(JOURNAL_RECLAIM_STARTED, &j->flags))
		return NULL;

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

/* returns true if we did work */
static bool journal_flush_pins(struct journal *j, u64 seq_to_flush,
			       unsigned min_nr)
{
	struct journal_entry_pin *pin;
	bool ret = false;
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
		ret = true;
	}

	return ret;
}

static u64 journal_seq_to_flush(struct journal *j)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct bch_dev *ca;
	u64 seq_to_flush = 0;
	unsigned iter;

	spin_lock(&j->lock);

	for_each_rw_member(ca, c, iter) {
		struct journal_device *ja = &ca->journal;
		unsigned nr_buckets, bucket_to_flush;

		if (!ja->nr)
			continue;

		/* Try to keep the journal at most half full: */
		nr_buckets = ja->nr / 2;

		/* And include pre-reservations: */
		nr_buckets += DIV_ROUND_UP(j->prereserved.reserved,
					   (ca->mi.bucket_size << 6) -
					   journal_entry_overhead(j));

		nr_buckets = min(nr_buckets, ja->nr);

		bucket_to_flush = (ja->cur_idx + nr_buckets) % ja->nr;
		seq_to_flush = max(seq_to_flush,
				   ja->bucket_seq[bucket_to_flush]);
	}

	/* Also flush if the pin fifo is more than half full */
	seq_to_flush = max_t(s64, seq_to_flush,
			     (s64) journal_cur_seq(j) -
			     (j->pin.size >> 1));
	spin_unlock(&j->lock);

	return seq_to_flush;
}

/**
 * bch2_journal_reclaim - free up journal buckets
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
void bch2_journal_reclaim(struct journal *j)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	unsigned min_nr = 0;
	u64 seq_to_flush = 0;

	lockdep_assert_held(&j->reclaim_lock);

	do {
		bch2_journal_do_discards(j);

		seq_to_flush = journal_seq_to_flush(j);
		min_nr = 0;

		/*
		 * If it's been longer than j->reclaim_delay_ms since we last flushed,
		 * make sure to flush at least one journal pin:
		 */
		if (time_after(jiffies, j->last_flushed +
			       msecs_to_jiffies(j->reclaim_delay_ms)))
			min_nr = 1;

		if (j->prereserved.reserved * 2 > j->prereserved.remaining)
			min_nr = 1;

		if ((atomic_read(&c->btree_cache.dirty) * 4 >
		     c->btree_cache.used  * 3) ||
		    (c->btree_key_cache.nr_dirty * 4 >
		     c->btree_key_cache.nr_keys))
			min_nr = 1;
	} while (journal_flush_pins(j, seq_to_flush, min_nr));

	if (!bch2_journal_error(j))
		queue_delayed_work(c->journal_reclaim_wq, &j->reclaim_work,
				   msecs_to_jiffies(j->reclaim_delay_ms));
}

void bch2_journal_reclaim_work(struct work_struct *work)
{
	struct journal *j = container_of(to_delayed_work(work),
				struct journal, reclaim_work);

	mutex_lock(&j->reclaim_lock);
	bch2_journal_reclaim(j);
	mutex_unlock(&j->reclaim_lock);
}

static int journal_flush_done(struct journal *j, u64 seq_to_flush,
			      bool *did_work)
{
	int ret;

	ret = bch2_journal_error(j);
	if (ret)
		return ret;

	mutex_lock(&j->reclaim_lock);

	*did_work = journal_flush_pins(j, seq_to_flush, 0);

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

bool bch2_journal_flush_pins(struct journal *j, u64 seq_to_flush)
{
	bool did_work = false;

	if (!test_bit(JOURNAL_STARTED, &j->flags))
		return false;

	closure_wait_event(&j->async_wait,
		journal_flush_done(j, seq_to_flush, &did_work));

	return did_work;
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
	bch2_replicas_gc_start(c, 1 << BCH_DATA_journal);

	seq = 0;

	spin_lock(&j->lock);
	while (!ret && seq < j->pin.back) {
		struct bch_replicas_padded replicas;

		seq = max(seq, journal_last_seq(j));
		bch2_devlist_to_replicas(&replicas.e, BCH_DATA_journal,
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
