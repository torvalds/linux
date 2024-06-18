/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SB_CLEAN_H
#define _BCACHEFS_SB_CLEAN_H

int bch2_sb_clean_validate_late(struct bch_fs *, struct bch_sb_field_clean *, int);
int bch2_verify_superblock_clean(struct bch_fs *, struct bch_sb_field_clean **,
				 struct jset *);
struct bch_sb_field_clean *bch2_read_superblock_clean(struct bch_fs *);
void bch2_journal_super_entries_add_common(struct bch_fs *, struct jset_entry **, u64);

extern const struct bch_sb_field_ops bch_sb_field_ops_clean;

int bch2_fs_mark_dirty(struct bch_fs *);
void bch2_fs_mark_clean(struct bch_fs *);

#endif /* _BCACHEFS_SB_CLEAN_H */
