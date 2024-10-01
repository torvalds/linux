/* SPDX-License-Identifier: GPL-2.0 */

#include "super-io.h"
#include "vstructs.h"

static inline unsigned bch2_nr_journal_buckets(struct bch_sb_field_journal *j)
{
	return j
		? (__le64 *) vstruct_end(&j->field) - j->buckets
		: 0;
}

static inline unsigned bch2_sb_field_journal_v2_nr_entries(struct bch_sb_field_journal_v2 *j)
{
	if (!j)
		return 0;

	return (struct bch_sb_field_journal_v2_entry *) vstruct_end(&j->field) - &j->d[0];
}

extern const struct bch_sb_field_ops bch_sb_field_ops_journal;
extern const struct bch_sb_field_ops bch_sb_field_ops_journal_v2;

int bch2_journal_buckets_to_sb(struct bch_fs *, struct bch_dev *, u64 *, unsigned);
