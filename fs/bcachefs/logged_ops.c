// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_buf.h"
#include "btree_update.h"
#include "error.h"
#include "io_misc.h"
#include "logged_ops.h"
#include "super.h"

struct bch_logged_op_fn {
	u8		type;
	int		(*resume)(struct btree_trans *, struct bkey_i *);
};

static const struct bch_logged_op_fn logged_op_fns[] = {
#define x(n)		{					\
	.type		= KEY_TYPE_logged_op_##n,		\
	.resume		= bch2_resume_logged_op_##n,		\
},
	BCH_LOGGED_OPS()
#undef x
};

static const struct bch_logged_op_fn *logged_op_fn(enum bch_bkey_type type)
{
	for (unsigned i = 0; i < ARRAY_SIZE(logged_op_fns); i++)
		if (logged_op_fns[i].type == type)
			return logged_op_fns + i;
	return NULL;
}

static int resume_logged_op(struct btree_trans *trans, struct btree_iter *iter,
			    struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	const struct bch_logged_op_fn *fn = logged_op_fn(k.k->type);
	struct bkey_buf sk;
	u32 restart_count = trans->restart_count;
	int ret;

	if (!fn)
		return 0;

	bch2_bkey_buf_init(&sk);
	bch2_bkey_buf_reassemble(&sk, c, k);

	ret =   drop_locks_do(trans, (bch2_fs_lazy_rw(c), 0)) ?:
		fn->resume(trans, sk.k) ?: trans_was_restarted(trans, restart_count);

	bch2_bkey_buf_exit(&sk, c);
	return ret;
}

int bch2_resume_logged_ops(struct bch_fs *c)
{
	int ret = bch2_trans_run(c,
		for_each_btree_key(trans, iter,
				   BTREE_ID_logged_ops, POS_MIN,
				   BTREE_ITER_PREFETCH, k,
			resume_logged_op(trans, &iter, k)));
	bch_err_fn(c, ret);
	return ret;
}

static int __bch2_logged_op_start(struct btree_trans *trans, struct bkey_i *k)
{
	struct btree_iter iter;
	int ret;

	ret = bch2_bkey_get_empty_slot(trans, &iter, BTREE_ID_logged_ops, POS_MAX);
	if (ret)
		return ret;

	k->k.p = iter.pos;

	ret = bch2_trans_update(trans, &iter, k, 0);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_logged_op_start(struct btree_trans *trans, struct bkey_i *k)
{
	return commit_do(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			 __bch2_logged_op_start(trans, k));
}

void bch2_logged_op_finish(struct btree_trans *trans, struct bkey_i *k)
{
	int ret = commit_do(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			    bch2_btree_delete(trans, BTREE_ID_logged_ops, k->k.p, 0));
	/*
	 * This needs to be a fatal error because we've left an unfinished
	 * operation in the logged ops btree.
	 *
	 * We should only ever see an error here if the filesystem has already
	 * been shut down, but make sure of that here:
	 */
	if (ret) {
		struct bch_fs *c = trans->c;
		struct printbuf buf = PRINTBUF;

		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(k));
		bch2_fs_fatal_error(c, "%s: error deleting logged operation %s: %s",
				     __func__, buf.buf, bch2_err_str(ret));
		printbuf_exit(&buf);
	}
}
