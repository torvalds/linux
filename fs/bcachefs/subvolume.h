/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_SUBVOLUME_H
#define _BCACHEFS_SUBVOLUME_H

#include "darray.h"
#include "subvolume_types.h"

enum bkey_invalid_flags;

int bch2_check_subvols(struct bch_fs *);

int bch2_subvolume_invalid(const struct bch_fs *, struct bkey_s_c,
			   enum bkey_invalid_flags, struct printbuf *);
void bch2_subvolume_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_subvolume ((struct bkey_ops) {		\
	.key_invalid	= bch2_subvolume_invalid,		\
	.val_to_text	= bch2_subvolume_to_text,		\
	.min_val_size	= 16,					\
})

int bch2_subvolume_get(struct btree_trans *, unsigned,
		       bool, int, struct bch_subvolume *);
int bch2_subvolume_get_snapshot(struct btree_trans *, u32, u32 *);

int bch2_delete_dead_snapshots(struct bch_fs *);
void bch2_delete_dead_snapshots_async(struct bch_fs *);

int bch2_subvolume_unlink(struct btree_trans *, u32);
int bch2_subvolume_create(struct btree_trans *, u64, u32,
			  u32 *, u32 *, bool);

int bch2_fs_subvolumes_init(struct bch_fs *);

#endif /* _BCACHEFS_SUBVOLUME_H */
