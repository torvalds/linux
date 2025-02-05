/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SB_DOWNGRADE_H
#define _BCACHEFS_SB_DOWNGRADE_H

extern const struct bch_sb_field_ops bch_sb_field_ops_downgrade;

int bch2_sb_downgrade_update(struct bch_fs *);
void bch2_sb_set_upgrade(struct bch_fs *, unsigned, unsigned);
int bch2_sb_set_upgrade_extra(struct bch_fs *);
void bch2_sb_set_downgrade(struct bch_fs *, unsigned, unsigned);

#endif /* _BCACHEFS_SB_DOWNGRADE_H */
