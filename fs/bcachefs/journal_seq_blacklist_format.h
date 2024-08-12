/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_JOURNAL_SEQ_BLACKLIST_FORMAT_H
#define _BCACHEFS_JOURNAL_SEQ_BLACKLIST_FORMAT_H

struct journal_seq_blacklist_entry {
	__le64			start;
	__le64			end;
};

struct bch_sb_field_journal_seq_blacklist {
	struct bch_sb_field	field;
	struct journal_seq_blacklist_entry start[];
};

#endif /* _BCACHEFS_JOURNAL_SEQ_BLACKLIST_FORMAT_H */
