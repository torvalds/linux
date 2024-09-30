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

static inline struct bkey_s_c
bch2_btree_iter_peek_in_subvolume_upto_type(struct btree_iter *iter, struct bpos end,
					    u32 subvolid, unsigned flags)
{
	u32 snapshot;
	int ret = bch2_subvolume_get_snapshot(iter->trans, subvolid, &snapshot);
	if (ret)
		return bkey_s_c_err(ret);

	bch2_btree_iter_set_snapshot(iter, snapshot);
	return bch2_btree_iter_peek_upto_type(iter, end, flags);
}

#define for_each_btree_key_in_subvolume_upto_continue(_trans, _iter,		\
					 _end, _subvolid, _flags, _k, _do)	\
({										\
	struct bkey_s_c _k;							\
	int _ret3 = 0;								\
										\
	do {									\
		_ret3 = lockrestart_do(_trans, ({				\
			(_k) = bch2_btree_iter_peek_in_subvolume_upto_type(&(_iter),	\
						_end, _subvolid, (_flags));	\
			if (!(_k).k)						\
				break;						\
										\
			bkey_err(_k) ?: (_do);					\
		}));								\
	} while (!_ret3 && bch2_btree_iter_advance(&(_iter)));			\
										\
	bch2_trans_iter_exit((_trans), &(_iter));				\
	_ret3;									\
})

#define for_each_btree_key_in_subvolume_upto(_trans, _iter, _btree_id,		\
				_start, _end, _subvolid, _flags, _k, _do)	\
({										\
	struct btree_iter _iter;						\
	bch2_trans_iter_init((_trans), &(_iter), (_btree_id),			\
			     (_start), (_flags));				\
										\
	for_each_btree_key_in_subvolume_upto_continue(_trans, _iter,		\
					_end, _subvolid, _flags, _k, _do);	\
})

int bch2_delete_dead_snapshots(struct bch_fs *);
void bch2_delete_dead_snapshots_async(struct bch_fs *);

int bch2_subvolume_unlink(struct btree_trans *, u32);
int bch2_subvolume_create(struct btree_trans *, u64, u32, u32, u32 *, u32 *, bool);

int bch2_initialize_subvolumes(struct bch_fs *);
int bch2_fs_upgrade_for_subvolumes(struct bch_fs *);

int bch2_fs_subvolumes_init(struct bch_fs *);

#endif /* _BCACHEFS_SUBVOLUME_H */
