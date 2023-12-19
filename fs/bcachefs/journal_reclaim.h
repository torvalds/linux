/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_JOURNAL_RECLAIM_H
#define _BCACHEFS_JOURNAL_RECLAIM_H

#define JOURNAL_PIN	(32 * 1024)

static inline void journal_reclaim_kick(struct journal *j)
{
	struct task_struct *p = READ_ONCE(j->reclaim_thread);

	j->reclaim_kicked = true;
	if (p)
		wake_up_process(p);
}

unsigned bch2_journal_dev_buckets_available(struct journal *,
					    struct journal_device *,
					    enum journal_space_from);
void bch2_journal_space_available(struct journal *);

static inline bool journal_pin_active(struct journal_entry_pin *pin)
{
	return pin->seq != 0;
}

static inline struct journal_entry_pin_list *
journal_seq_pin(struct journal *j, u64 seq)
{
	EBUG_ON(seq < j->pin.front || seq >= j->pin.back);

	return &j->pin.data[seq & j->pin.mask];
}

void bch2_journal_reclaim_fast(struct journal *);
bool __bch2_journal_pin_put(struct journal *, u64);
void bch2_journal_pin_put(struct journal *, u64);
void bch2_journal_pin_drop(struct journal *, struct journal_entry_pin *);

void bch2_journal_pin_set(struct journal *, u64, struct journal_entry_pin *,
			  journal_pin_flush_fn);

static inline void bch2_journal_pin_add(struct journal *j, u64 seq,
					struct journal_entry_pin *pin,
					journal_pin_flush_fn flush_fn)
{
	if (unlikely(!journal_pin_active(pin) || pin->seq > seq))
		bch2_journal_pin_set(j, seq, pin, flush_fn);
}

static inline void bch2_journal_pin_copy(struct journal *j,
					 struct journal_entry_pin *dst,
					 struct journal_entry_pin *src,
					 journal_pin_flush_fn flush_fn)
{
	/* Guard against racing with journal_pin_drop(src): */
	u64 seq = READ_ONCE(src->seq);

	if (seq)
		bch2_journal_pin_add(j, seq, dst, flush_fn);
}

static inline void bch2_journal_pin_update(struct journal *j, u64 seq,
					   struct journal_entry_pin *pin,
					   journal_pin_flush_fn flush_fn)
{
	if (unlikely(!journal_pin_active(pin) || pin->seq < seq))
		bch2_journal_pin_set(j, seq, pin, flush_fn);
}

void bch2_journal_pin_flush(struct journal *, struct journal_entry_pin *);

void bch2_journal_do_discards(struct journal *);
int bch2_journal_reclaim(struct journal *);

void bch2_journal_reclaim_stop(struct journal *);
int bch2_journal_reclaim_start(struct journal *);

bool bch2_journal_flush_pins(struct journal *, u64);

static inline bool bch2_journal_flush_all_pins(struct journal *j)
{
	return bch2_journal_flush_pins(j, U64_MAX);
}

int bch2_journal_flush_device_pins(struct journal *, int);

#endif /* _BCACHEFS_JOURNAL_RECLAIM_H */
