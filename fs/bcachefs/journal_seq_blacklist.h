/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_JOURNAL_SEQ_BLACKLIST_H
#define _BCACHEFS_JOURNAL_SEQ_BLACKLIST_H

struct journal_replay;

struct journal_seq_blacklist *
bch2_journal_seq_blacklist_find(struct journal *, u64);
int bch2_journal_seq_should_ignore(struct bch_fs *, u64, struct btree *);
int bch2_journal_seq_blacklist_read(struct journal *,
				    struct journal_replay *);
void bch2_journal_seq_blacklist_write(struct journal *);

#endif /* _BCACHEFS_JOURNAL_SEQ_BLACKLIST_H */
