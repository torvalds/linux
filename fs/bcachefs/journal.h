/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_JOURNAL_H
#define _BCACHEFS_JOURNAL_H

/*
 * THE JOURNAL:
 *
 * The primary purpose of the journal is to log updates (insertions) to the
 * b-tree, to avoid having to do synchronous updates to the b-tree on disk.
 *
 * Without the journal, the b-tree is always internally consistent on
 * disk - and in fact, in the earliest incarnations bcache didn't have a journal
 * but did handle unclean shutdowns by doing all index updates synchronously
 * (with coalescing).
 *
 * Updates to interior nodes still happen synchronously and without the journal
 * (for simplicity) - this may change eventually but updates to interior nodes
 * are rare enough it's not a huge priority.
 *
 * This means the journal is relatively separate from the b-tree; it consists of
 * just a list of keys and journal replay consists of just redoing those
 * insertions in same order that they appear in the journal.
 *
 * PERSISTENCE:
 *
 * For synchronous updates (where we're waiting on the index update to hit
 * disk), the journal entry will be written out immediately (or as soon as
 * possible, if the write for the previous journal entry was still in flight).
 *
 * Synchronous updates are specified by passing a closure (@flush_cl) to
 * bch2_btree_insert() or bch_btree_insert_node(), which then pass that parameter
 * down to the journalling code. That closure will will wait on the journal
 * write to complete (via closure_wait()).
 *
 * If the index update wasn't synchronous, the journal entry will be
 * written out after 10 ms have elapsed, by default (the delay_ms field
 * in struct journal).
 *
 * JOURNAL ENTRIES:
 *
 * A journal entry is variable size (struct jset), it's got a fixed length
 * header and then a variable number of struct jset_entry entries.
 *
 * Journal entries are identified by monotonically increasing 64 bit sequence
 * numbers - jset->seq; other places in the code refer to this sequence number.
 *
 * A jset_entry entry contains one or more bkeys (which is what gets inserted
 * into the b-tree). We need a container to indicate which b-tree the key is
 * for; also, the roots of the various b-trees are stored in jset_entry entries
 * (one for each b-tree) - this lets us add new b-tree types without changing
 * the on disk format.
 *
 * We also keep some things in the journal header that are logically part of the
 * superblock - all the things that are frequently updated. This is for future
 * bcache on raw flash support; the superblock (which will become another
 * journal) can't be moved or wear leveled, so it contains just enough
 * information to find the main journal, and the superblock only has to be
 * rewritten when we want to move/wear level the main journal.
 *
 * JOURNAL LAYOUT ON DISK:
 *
 * The journal is written to a ringbuffer of buckets (which is kept in the
 * superblock); the individual buckets are not necessarily contiguous on disk
 * which means that journal entries are not allowed to span buckets, but also
 * that we can resize the journal at runtime if desired (unimplemented).
 *
 * The journal buckets exist in the same pool as all the other buckets that are
 * managed by the allocator and garbage collection - garbage collection marks
 * the journal buckets as metadata buckets.
 *
 * OPEN/DIRTY JOURNAL ENTRIES:
 *
 * Open/dirty journal entries are journal entries that contain b-tree updates
 * that have not yet been written out to the b-tree on disk. We have to track
 * which journal entries are dirty, and we also have to avoid wrapping around
 * the journal and overwriting old but still dirty journal entries with new
 * journal entries.
 *
 * On disk, this is represented with the "last_seq" field of struct jset;
 * last_seq is the first sequence number that journal replay has to replay.
 *
 * To avoid overwriting dirty journal entries on disk, we keep a mapping (in
 * journal_device->seq) of for each journal bucket, the highest sequence number
 * any journal entry it contains. Then, by comparing that against last_seq we
 * can determine whether that journal bucket contains dirty journal entries or
 * not.
 *
 * To track which journal entries are dirty, we maintain a fifo of refcounts
 * (where each entry corresponds to a specific sequence number) - when a ref
 * goes to 0, that journal entry is no longer dirty.
 *
 * Journalling of index updates is done at the same time as the b-tree itself is
 * being modified (see btree_insert_key()); when we add the key to the journal
 * the pending b-tree write takes a ref on the journal entry the key was added
 * to. If a pending b-tree write would need to take refs on multiple dirty
 * journal entries, it only keeps the ref on the oldest one (since a newer
 * journal entry will still be replayed if an older entry was dirty).
 *
 * JOURNAL FILLING UP:
 *
 * There are two ways the journal could fill up; either we could run out of
 * space to write to, or we could have too many open journal entries and run out
 * of room in the fifo of refcounts. Since those refcounts are decremented
 * without any locking we can't safely resize that fifo, so we handle it the
 * same way.
 *
 * If the journal fills up, we start flushing dirty btree nodes until we can
 * allocate space for a journal write again - preferentially flushing btree
 * nodes that are pinning the oldest journal entries first.
 */

#include <linux/hash.h>

#include "journal_types.h"

struct bch_fs;

static inline void journal_wake(struct journal *j)
{
	wake_up(&j->wait);
	closure_wake_up(&j->async_wait);
}

static inline struct journal_buf *journal_cur_buf(struct journal *j)
{
	return j->buf + j->reservations.idx;
}

static inline struct journal_buf *journal_prev_buf(struct journal *j)
{
	return j->buf + !j->reservations.idx;
}

/* Sequence number of oldest dirty journal entry */

static inline u64 journal_last_seq(struct journal *j)
{
	return j->pin.front;
}

static inline u64 journal_cur_seq(struct journal *j)
{
	BUG_ON(j->pin.back - 1 != atomic64_read(&j->seq));

	return j->pin.back - 1;
}

u64 bch2_inode_journal_seq(struct journal *, u64);

static inline int journal_state_count(union journal_res_state s, int idx)
{
	return idx == 0 ? s.buf0_count : s.buf1_count;
}

static inline void journal_state_inc(union journal_res_state *s)
{
	s->buf0_count += s->idx == 0;
	s->buf1_count += s->idx == 1;
}

static inline void bch2_journal_set_has_inode(struct journal *j,
					      struct journal_res *res,
					      u64 inum)
{
	struct journal_buf *buf = &j->buf[res->idx];
	unsigned long bit = hash_64(inum, ilog2(sizeof(buf->has_inode) * 8));

	/* avoid atomic op if possible */
	if (unlikely(!test_bit(bit, buf->has_inode)))
		set_bit(bit, buf->has_inode);
}

/*
 * Amount of space that will be taken up by some keys in the journal (i.e.
 * including the jset header)
 */
static inline unsigned jset_u64s(unsigned u64s)
{
	return u64s + sizeof(struct jset_entry) / sizeof(u64);
}

static inline struct jset_entry *
bch2_journal_add_entry_noreservation(struct journal_buf *buf, size_t u64s)
{
	struct jset *jset = buf->data;
	struct jset_entry *entry = vstruct_idx(jset, le32_to_cpu(jset->u64s));

	memset(entry, 0, sizeof(*entry));
	entry->u64s = cpu_to_le16(u64s);

	le32_add_cpu(&jset->u64s, jset_u64s(u64s));

	return entry;
}

static inline void bch2_journal_add_entry(struct journal *j, struct journal_res *res,
					  unsigned type, enum btree_id id,
					  unsigned level,
					  const void *data, unsigned u64s)
{
	struct journal_buf *buf = &j->buf[res->idx];
	struct jset_entry *entry = vstruct_idx(buf->data, res->offset);
	unsigned actual = jset_u64s(u64s);

	EBUG_ON(!res->ref);
	EBUG_ON(actual > res->u64s);

	res->offset	+= actual;
	res->u64s	-= actual;

	entry->u64s	= cpu_to_le16(u64s);
	entry->btree_id = id;
	entry->level	= level;
	entry->type	= type;
	entry->pad[0]	= 0;
	entry->pad[1]	= 0;
	entry->pad[2]	= 0;
	memcpy_u64s(entry->_data, data, u64s);
}

static inline void bch2_journal_add_keys(struct journal *j, struct journal_res *res,
					enum btree_id id, const struct bkey_i *k)
{
	bch2_journal_add_entry(j, res, BCH_JSET_ENTRY_btree_keys,
			       id, 0, k, k->k.u64s);
}

void bch2_journal_buf_put_slowpath(struct journal *, bool);

static inline void bch2_journal_buf_put(struct journal *j, unsigned idx,
				       bool need_write_just_set)
{
	union journal_res_state s;

	s.v = atomic64_sub_return(((union journal_res_state) {
				    .buf0_count = idx == 0,
				    .buf1_count = idx == 1,
				    }).v, &j->reservations.counter);

	EBUG_ON(s.idx != idx && !s.prev_buf_unwritten);

	/*
	 * Do not initiate a journal write if the journal is in an error state
	 * (previous journal entry write may have failed)
	 */
	if (s.idx != idx &&
	    !journal_state_count(s, idx) &&
	    s.cur_entry_offset != JOURNAL_ENTRY_ERROR_VAL)
		bch2_journal_buf_put_slowpath(j, need_write_just_set);
}

/*
 * This function releases the journal write structure so other threads can
 * then proceed to add their keys as well.
 */
static inline void bch2_journal_res_put(struct journal *j,
				       struct journal_res *res)
{
	if (!res->ref)
		return;

	lock_release(&j->res_map, _RET_IP_);

	while (res->u64s)
		bch2_journal_add_entry(j, res,
				       BCH_JSET_ENTRY_btree_keys,
				       0, 0, NULL, 0);

	bch2_journal_buf_put(j, res->idx, false);

	res->ref = 0;
}

int bch2_journal_res_get_slowpath(struct journal *, struct journal_res *,
				 unsigned, unsigned);

static inline int journal_res_get_fast(struct journal *j,
				       struct journal_res *res,
				       unsigned u64s_min,
				       unsigned u64s_max)
{
	union journal_res_state old, new;
	u64 v = atomic64_read(&j->reservations.counter);

	do {
		old.v = new.v = v;

		/*
		 * Check if there is still room in the current journal
		 * entry:
		 */
		if (old.cur_entry_offset + u64s_min > j->cur_entry_u64s)
			return 0;

		res->offset	= old.cur_entry_offset;
		res->u64s	= min(u64s_max, j->cur_entry_u64s -
				      old.cur_entry_offset);

		journal_state_inc(&new);
		new.cur_entry_offset += res->u64s;
	} while ((v = atomic64_cmpxchg(&j->reservations.counter,
				       old.v, new.v)) != old.v);

	res->ref = true;
	res->idx = new.idx;
	res->seq = le64_to_cpu(j->buf[res->idx].data->seq);
	return 1;
}

static inline int bch2_journal_res_get(struct journal *j, struct journal_res *res,
				      unsigned u64s_min, unsigned u64s_max)
{
	int ret;

	EBUG_ON(res->ref);
	EBUG_ON(u64s_max < u64s_min);
	EBUG_ON(!test_bit(JOURNAL_STARTED, &j->flags));

	if (journal_res_get_fast(j, res, u64s_min, u64s_max))
		goto out;

	ret = bch2_journal_res_get_slowpath(j, res, u64s_min, u64s_max);
	if (ret)
		return ret;
out:
	lock_acquire_shared(&j->res_map, 0, 0, NULL, _THIS_IP_);
	EBUG_ON(!res->ref);
	return 0;
}

u64 bch2_journal_last_unwritten_seq(struct journal *);
int bch2_journal_open_seq_async(struct journal *, u64, struct closure *);

void bch2_journal_wait_on_seq(struct journal *, u64, struct closure *);
void bch2_journal_flush_seq_async(struct journal *, u64, struct closure *);
void bch2_journal_flush_async(struct journal *, struct closure *);
void bch2_journal_meta_async(struct journal *, struct closure *);

int bch2_journal_flush_seq(struct journal *, u64);
int bch2_journal_flush(struct journal *);
int bch2_journal_meta(struct journal *);

void bch2_journal_halt(struct journal *);

static inline int bch2_journal_error(struct journal *j)
{
	return j->reservations.cur_entry_offset == JOURNAL_ENTRY_ERROR_VAL
		? -EIO : 0;
}

struct bch_dev;

static inline bool journal_flushes_device(struct bch_dev *ca)
{
	return true;
}

static inline void bch2_journal_set_replay_done(struct journal *j)
{
	BUG_ON(!test_bit(JOURNAL_STARTED, &j->flags));
	set_bit(JOURNAL_REPLAY_DONE, &j->flags);
}

ssize_t bch2_journal_print_debug(struct journal *, char *);
ssize_t bch2_journal_print_pins(struct journal *, char *);

int bch2_set_nr_journal_buckets(struct bch_fs *, struct bch_dev *,
				unsigned nr);
int bch2_dev_journal_alloc(struct bch_dev *);

void bch2_dev_journal_stop(struct journal *, struct bch_dev *);
void bch2_fs_journal_stop(struct journal *);
void bch2_fs_journal_start(struct journal *);
void bch2_dev_journal_exit(struct bch_dev *);
int bch2_dev_journal_init(struct bch_dev *, struct bch_sb *);
void bch2_fs_journal_exit(struct journal *);
int bch2_fs_journal_init(struct journal *);

#endif /* _BCACHEFS_JOURNAL_H */
