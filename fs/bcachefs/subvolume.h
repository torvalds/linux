/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SUBVOLUME_H
#define _BCACHEFS_SUBVOLUME_H

#include "darray.h"
#include "subvolume_types.h"

enum bch_validate_flags;

int bch2_check_subvols(struct bch_fs *);
int bch2_check_subvol_children(struct bch_fs *);

int bch2_subvolume_validate(struct bch_fs *, struct bkey_s_c, enum bch_validate_flags);
void bch2_subvolume_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);
int bch2_subvolume_trigger(struct btree_trans *, enum btree_id, unsigned,
			   struct bkey_s_c, struct bkey_s,
			   enum btree_iter_update_trigger_flags);

#define bch2_bkey_ops_subvolume ((struct bkey_ops) {		\
	.key_validate	= bch2_subvolume_validate,		\
	.val_to_text	= bch2_subvolume_to_text,		\
	.trigger	= bch2_subvolume_trigger,		\
	.min_val_size	= 16,					\
})

int bch2_subvol_has_children(struct btree_trans *, u32);
int bch2_subvolume_get(struct btree_trans *, unsigned,
		       bool, int, struct bch_subvolume *);
int bch2_subvolume_get_snapshot(struct btree_trans *, u32, u32 *);

int bch2_subvol_is_ro_trans(struct btree_trans *, u32);
int bch2_subvol_is_ro(struct bch_fs *, u32);

int bch2_delete_dead_snapshots(struct bch_fs *);
void bch2_delete_dead_snapshots_async(struct bch_fs *);

int bch2_subvolume_unlink(struct btree_trans *, u32);
int bch2_subvolume_create(struct btree_trans *, u64, u32, u32, u32 *, u32 *, bool);

int bch2_initialize_subvolumes(struct bch_fs *);
int bch2_fs_upgrade_for_subvolumes(struct bch_fs *);

int bch2_fs_subvolumes_init(struct bch_fs *);

#endif /* _BCACHEFS_SUBVOLUME_H */
