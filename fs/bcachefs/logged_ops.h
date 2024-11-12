/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_LOGGED_OPS_H
#define _BCACHEFS_LOGGED_OPS_H

#include "bkey.h"

#define BCH_LOGGED_OPS()			\
	x(truncate)				\
	x(finsert)

static inline int bch2_logged_op_update(struct btree_trans *trans, struct bkey_i *op)
{
	return bch2_btree_insert_nonextent(trans, BTREE_ID_logged_ops, op, 0);
}

int bch2_resume_logged_ops(struct bch_fs *);
int bch2_logged_op_start(struct btree_trans *, struct bkey_i *);
int bch2_logged_op_finish(struct btree_trans *, struct bkey_i *);

#endif /* _BCACHEFS_LOGGED_OPS_H */
