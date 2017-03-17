// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "error.h"
#include "journal.h"
#include "journal_io.h"
#include "journal_reclaim.h"
#include "journal_seq_blacklist.h"

/*
 * journal_seq_blacklist machinery:
 *
 * To guarantee order of btree updates after a crash, we need to detect when a
 * btree node entry (bset) is newer than the newest journal entry that was
 * successfully written, and ignore it - effectively ignoring any btree updates
 * that didn't make it into the journal.
 *
 * If we didn't do this, we might have two btree nodes, a and b, both with
 * updates that weren't written to the journal yet: if b was updated after a,
 * but b was flushed and not a - oops; on recovery we'll find that the updates
 * to b happened, but not the updates to a that happened before it.
 *
 * Ignoring bsets that are newer than the newest journal entry is always safe,
 * because everything they contain will also have been journalled - and must
 * still be present in the journal on disk until a journal entry has been
 * written _after_ that bset was written.
 *
 * To accomplish this, bsets record the newest journal sequence number they
 * contain updates for; then, on startup, the btree code queries the journal
 * code to ask "Is this sequence number newer than the newest journal entry? If
 * so, ignore it."
 *
 * When this happens, we must blacklist that journal sequence number: the
 * journal must not write any entries with that sequence number, and it must
 * record that it was blacklisted so that a) on recovery we don't think we have
 * missing journal entries and b) so that the btree code continues to ignore
 * that bset, until that btree node is rewritten.
 *
 * Blacklisted journal sequence numbers are themselves recorded as entries in
 * the journal.
 */

/*
 * Called when journal needs to evict a blacklist entry to reclaim space: find
 * any btree nodes that refer to the blacklist journal sequence numbers, and
 * rewrite them:
 */
static void journal_seq_blacklist_flush(struct journal *j,
					struct journal_entry_pin *pin, u64 seq)
{
	struct bch_fs *c =
		container_of(j, struct bch_fs, journal);
	struct journal_seq_blacklist *bl =
		container_of(pin, struct journal_seq_blacklist, pin);
	struct blacklisted_node n;
	struct closure cl;
	unsigned i;
	int ret;

	closure_init_stack(&cl);

	for (i = 0;; i++) {
		struct btree_iter iter;
		struct btree *b;

		mutex_lock(&j->blacklist_lock);
		if (i >= bl->nr_entries) {
			mutex_unlock(&j->blacklist_lock);
			break;
		}
		n = bl->entries[i];
		mutex_unlock(&j->blacklist_lock);

		__bch2_btree_iter_init(&iter, c, n.btree_id, n.pos,
				       0, 0, BTREE_ITER_NODES);

		b = bch2_btree_iter_peek_node(&iter);

		/* The node might have already been rewritten: */

		if (b->data->keys.seq == n.seq) {
			ret = bch2_btree_node_rewrite(c, &iter, n.seq, 0);
			if (ret) {
				bch2_btree_iter_unlock(&iter);
				bch2_fs_fatal_error(c,
					"error %i rewriting btree node with blacklisted journal seq",
					ret);
				bch2_journal_halt(j);
				return;
			}
		}

		bch2_btree_iter_unlock(&iter);
	}

	for (i = 0;; i++) {
		struct btree_update *as;
		struct pending_btree_node_free *d;

		mutex_lock(&j->blacklist_lock);
		if (i >= bl->nr_entries) {
			mutex_unlock(&j->blacklist_lock);
			break;
		}
		n = bl->entries[i];
		mutex_unlock(&j->blacklist_lock);
redo_wait:
		mutex_lock(&c->btree_interior_update_lock);

		/*
		 * Is the node on the list of pending interior node updates -
		 * being freed? If so, wait for that to finish:
		 */
		for_each_pending_btree_node_free(c, as, d)
			if (n.seq	== d->seq &&
			    n.btree_id	== d->btree_id &&
			    !d->level &&
			    !bkey_cmp(n.pos, d->key.k.p)) {
				closure_wait(&as->wait, &cl);
				mutex_unlock(&c->btree_interior_update_lock);
				closure_sync(&cl);
				goto redo_wait;
			}

		mutex_unlock(&c->btree_interior_update_lock);
	}

	mutex_lock(&j->blacklist_lock);

	bch2_journal_pin_drop(j, &bl->pin);
	list_del(&bl->list);
	kfree(bl->entries);
	kfree(bl);

	mutex_unlock(&j->blacklist_lock);
}

/*
 * Determine if a particular sequence number is blacklisted - if so, return
 * blacklist entry:
 */
struct journal_seq_blacklist *
bch2_journal_seq_blacklist_find(struct journal *j, u64 seq)
{
	struct journal_seq_blacklist *bl;

	lockdep_assert_held(&j->blacklist_lock);

	list_for_each_entry(bl, &j->seq_blacklist, list)
		if (seq >= bl->start && seq <= bl->end)
			return bl;

	return NULL;
}

/*
 * Allocate a new, in memory blacklist entry:
 */
static struct journal_seq_blacklist *
bch2_journal_seq_blacklisted_new(struct journal *j, u64 start, u64 end)
{
	struct journal_seq_blacklist *bl;

	lockdep_assert_held(&j->blacklist_lock);

	/*
	 * When we start the journal, bch2_journal_start() will skip over @seq:
	 */

	bl = kzalloc(sizeof(*bl), GFP_KERNEL);
	if (!bl)
		return NULL;

	bl->start	= start;
	bl->end		= end;

	list_add_tail(&bl->list, &j->seq_blacklist);
	return bl;
}

/*
 * Returns true if @seq is newer than the most recent journal entry that got
 * written, and data corresponding to @seq should be ignored - also marks @seq
 * as blacklisted so that on future restarts the corresponding data will still
 * be ignored:
 */
int bch2_journal_seq_should_ignore(struct bch_fs *c, u64 seq, struct btree *b)
{
	struct journal *j = &c->journal;
	struct journal_seq_blacklist *bl = NULL;
	struct blacklisted_node *n;
	u64 journal_seq;
	int ret = 0;

	if (!seq)
		return 0;

	spin_lock(&j->lock);
	journal_seq = journal_cur_seq(j);
	spin_unlock(&j->lock);

	/* Interier updates aren't journalled: */
	BUG_ON(b->level);
	BUG_ON(seq > journal_seq && test_bit(BCH_FS_INITIAL_GC_DONE, &c->flags));

	/*
	 * Decrease this back to j->seq + 2 when we next rev the on disk format:
	 * increasing it temporarily to work around bug in old kernels
	 */
	fsck_err_on(seq > journal_seq + 4, c,
		    "bset journal seq too far in the future: %llu > %llu",
		    seq, journal_seq);

	if (seq <= journal_seq &&
	    list_empty_careful(&j->seq_blacklist))
		return 0;

	mutex_lock(&j->blacklist_lock);

	if (seq <= journal_seq) {
		bl = bch2_journal_seq_blacklist_find(j, seq);
		if (!bl)
			goto out;
	} else {
		bch_verbose(c, "btree node %u:%llu:%llu has future journal sequence number %llu, blacklisting",
			    b->btree_id, b->key.k.p.inode, b->key.k.p.offset, seq);

		if (!j->new_blacklist) {
			j->new_blacklist = bch2_journal_seq_blacklisted_new(j,
						journal_seq + 1,
						journal_seq + 1);
			if (!j->new_blacklist) {
				ret = -ENOMEM;
				goto out;
			}
		}
		bl = j->new_blacklist;
		bl->end = max(bl->end, seq);
	}

	for (n = bl->entries; n < bl->entries + bl->nr_entries; n++)
		if (b->data->keys.seq	== n->seq &&
		    b->btree_id		== n->btree_id &&
		    !bkey_cmp(b->key.k.p, n->pos))
			goto found_entry;

	if (!bl->nr_entries ||
	    is_power_of_2(bl->nr_entries)) {
		n = krealloc(bl->entries,
			     max_t(size_t, bl->nr_entries * 2, 8) * sizeof(*n),
			     GFP_KERNEL);
		if (!n) {
			ret = -ENOMEM;
			goto out;
		}
		bl->entries = n;
	}

	bl->entries[bl->nr_entries++] = (struct blacklisted_node) {
		.seq		= b->data->keys.seq,
		.btree_id	= b->btree_id,
		.pos		= b->key.k.p,
	};
found_entry:
	ret = 1;
out:
fsck_err:
	mutex_unlock(&j->blacklist_lock);
	return ret;
}

static int __bch2_journal_seq_blacklist_read(struct journal *j,
					     struct journal_replay *i,
					     u64 start, u64 end)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct journal_seq_blacklist *bl;

	bch_verbose(c, "blacklisting existing journal seq %llu-%llu",
		    start, end);

	bl = bch2_journal_seq_blacklisted_new(j, start, end);
	if (!bl)
		return -ENOMEM;

	bch2_journal_pin_add(j, le64_to_cpu(i->j.seq), &bl->pin,
			     journal_seq_blacklist_flush);
	return 0;
}

/*
 * After reading the journal, find existing journal seq blacklist entries and
 * read them into memory:
 */
int bch2_journal_seq_blacklist_read(struct journal *j,
				    struct journal_replay *i)
{
	struct jset_entry *entry;
	int ret = 0;

	vstruct_for_each(&i->j, entry) {
		switch (entry->type) {
		case BCH_JSET_ENTRY_blacklist: {
			struct jset_entry_blacklist *bl_entry =
				container_of(entry, struct jset_entry_blacklist, entry);

			ret = __bch2_journal_seq_blacklist_read(j, i,
					le64_to_cpu(bl_entry->seq),
					le64_to_cpu(bl_entry->seq));
			break;
		}
		case BCH_JSET_ENTRY_blacklist_v2: {
			struct jset_entry_blacklist_v2 *bl_entry =
				container_of(entry, struct jset_entry_blacklist_v2, entry);

			ret = __bch2_journal_seq_blacklist_read(j, i,
					le64_to_cpu(bl_entry->start),
					le64_to_cpu(bl_entry->end));
			break;
		}
		}

		if (ret)
			break;
	}

	return ret;
}

/*
 * After reading the journal and walking the btree, we might have new journal
 * sequence numbers to blacklist - add entries to the next journal entry to be
 * written:
 */
void bch2_journal_seq_blacklist_write(struct journal *j)
{
	struct journal_seq_blacklist *bl = j->new_blacklist;
	struct jset_entry_blacklist_v2 *bl_entry;
	struct jset_entry *entry;

	if (!bl)
		return;

	entry = bch2_journal_add_entry_noreservation(journal_cur_buf(j),
			(sizeof(*bl_entry) - sizeof(*entry)) / sizeof(u64));

	bl_entry = container_of(entry, struct jset_entry_blacklist_v2, entry);
	bl_entry->entry.type	= BCH_JSET_ENTRY_blacklist_v2;
	bl_entry->start		= cpu_to_le64(bl->start);
	bl_entry->end		= cpu_to_le64(bl->end);

	bch2_journal_pin_add(j,
			     journal_cur_seq(j),
			     &bl->pin,
			     journal_seq_blacklist_flush);

	j->new_blacklist = NULL;
}
