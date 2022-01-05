/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_JOURNAL_SEQ_BLACKLIST_H
#define _BCACHEFS_JOURNAL_SEQ_BLACKLIST_H

static inline unsigned
blacklist_nr_entries(struct bch_sb_field_journal_seq_blacklist *bl)
{
	return bl
		? ((vstruct_end(&bl->field) - (void *) &bl->start[0]) /
		   sizeof(struct journal_seq_blacklist_entry))
		: 0;
}

bool bch2_journal_seq_is_blacklisted(struct bch_fs *, u64, bool);
int bch2_journal_seq_blacklist_add(struct bch_fs *c, u64, u64);
int bch2_blacklist_table_initialize(struct bch_fs *);

extern const struct bch_sb_field_ops bch_sb_field_ops_journal_seq_blacklist;

void bch2_blacklist_entries_gc(struct work_struct *);

#endif /* _BCACHEFS_JOURNAL_SEQ_BLACKLIST_H */
