/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_EXTENT_UPDATE_H
#define _BCACHEFS_EXTENT_UPDATE_H

#include "bcachefs.h"

int bch2_extent_atomic_end(struct btree_iter *, struct bkey_i *,
			   struct bpos *);
int bch2_extent_trim_atomic(struct bkey_i *, struct btree_iter *);
int bch2_extent_is_atomic(struct bkey_i *, struct btree_iter *);

#endif /* _BCACHEFS_EXTENT_UPDATE_H */
