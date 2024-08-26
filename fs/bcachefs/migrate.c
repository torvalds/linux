// SPDX-License-Identifier: GPL-2.0
/*
 * Code for moving data off a device.
 */

#include "bcachefs.h"
#include "bkey_buf.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "buckets.h"
#include "errcode.h"
#include "extents.h"
#include "io_write.h"
#include "journal.h"
#include "keylist.h"
#include "migrate.h"
#include "move.h"
#include "replicas.h"
#include "super-io.h"

static int drop_dev_ptrs(struct bch_fs *c, struct bkey_s k,
			 unsigned dev_idx, int flags, bool metadata)
{
	unsigned replicas = metadata ? c->opts.metadata_replicas : c->opts.data_replicas;
	unsigned lost = metadata ? BCH_FORCE_IF_METADATA_LOST : BCH_FORCE_IF_DATA_LOST;
	unsigned degraded = metadata ? BCH_FORCE_IF_METADATA_DEGRADED : BCH_FORCE_IF_DATA_DEGRADED;
	unsigned nr_good;

	bch2_bkey_drop_device(k, dev_idx);

	nr_good = bch2_bkey_durability(c, k.s_c);
	if ((!nr_good && !(flags & lost)) ||
	    (nr_good < replicas && !(flags & degraded)))
		return -BCH_ERR_remove_would_lose_data;

	return 0;
}

static int bch2_dev_usrdata_drop_key(struct btree_trans *trans,
				     struct btree_iter *iter,
				     struct bkey_s_c k,
				     unsigned dev_idx,
				     int flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_i *n;
	int ret;

	if (!bch2_bkey_has_device_c(k, dev_idx))
		return 0;

	n = bch2_bkey_make_mut(trans, iter, &k, BTREE_UPDATE_internal_snapshot_node);
	ret = PTR_ERR_OR_ZERO(n);
	if (ret)
		return ret;

	ret = drop_dev_ptrs(c, bkey_i_to_s(n), dev_idx, flags, false);
	if (ret)
		return ret;

	/*
	 * If the new extent no longer has any pointers, bch2_extent_normalize()
	 * will do the appropriate thing with it (turning it into a
	 * KEY_TYPE_error key, or just a discard if it was a cached extent)
	 */
	bch2_extent_normalize(c, bkey_i_to_s(n));

	/*
	 * Since we're not inserting through an extent iterator
	 * (BTREE_ITER_all_snapshots iterators aren't extent iterators),
	 * we aren't using the extent overwrite path to delete, we're
	 * just using the normal key deletion path:
	 */
	if (bkey_deleted(&n->k))
		n->k.size = 0;
	return 0;
}

static int bch2_dev_usrdata_drop(struct bch_fs *c, unsigned dev_idx, int flags)
{
	struct btree_trans *trans = bch2_trans_get(c);
	enum btree_id id;
	int ret = 0;

	for (id = 0; id < BTREE_ID_NR; id++) {
		if (!btree_type_has_ptrs(id))
			continue;

		ret = for_each_btree_key_commit(trans, iter, id, POS_MIN,
				BTREE_ITER_prefetch|BTREE_ITER_all_snapshots, k,
				NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			bch2_dev_usrdata_drop_key(trans, &iter, k, dev_idx, flags));
		if (ret)
			break;
	}

	bch2_trans_put(trans);

	return ret;
}

static int bch2_dev_metadata_drop(struct bch_fs *c, unsigned dev_idx, int flags)
{
	struct btree_trans *trans;
	struct btree_iter iter;
	struct closure cl;
	struct btree *b;
	struct bkey_buf k;
	unsigned id;
	int ret;

	/* don't handle this yet: */
	if (flags & BCH_FORCE_IF_METADATA_LOST)
		return -BCH_ERR_remove_with_metadata_missing_unimplemented;

	trans = bch2_trans_get(c);
	bch2_bkey_buf_init(&k);
	closure_init_stack(&cl);

	for (id = 0; id < BTREE_ID_NR; id++) {
		bch2_trans_node_iter_init(trans, &iter, id, POS_MIN, 0, 0,
					  BTREE_ITER_prefetch);
retry:
		ret = 0;
		while (bch2_trans_begin(trans),
		       (b = bch2_btree_iter_peek_node(&iter)) &&
		       !(ret = PTR_ERR_OR_ZERO(b))) {
			if (!bch2_bkey_has_device_c(bkey_i_to_s_c(&b->key), dev_idx))
				goto next;

			bch2_bkey_buf_copy(&k, c, &b->key);

			ret = drop_dev_ptrs(c, bkey_i_to_s(k.k),
					    dev_idx, flags, true);
			if (ret)
				break;

			ret = bch2_btree_node_update_key(trans, &iter, b, k.k, 0, false);
			if (bch2_err_matches(ret, BCH_ERR_transaction_restart)) {
				ret = 0;
				continue;
			}

			bch_err_msg(c, ret, "updating btree node key");
			if (ret)
				break;
next:
			bch2_btree_iter_next_node(&iter);
		}
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			goto retry;

		bch2_trans_iter_exit(trans, &iter);

		if (ret)
			goto err;
	}

	bch2_btree_interior_updates_flush(c);
	ret = 0;
err:
	bch2_bkey_buf_exit(&k, c);
	bch2_trans_put(trans);

	BUG_ON(bch2_err_matches(ret, BCH_ERR_transaction_restart));

	return ret;
}

int bch2_dev_data_drop(struct bch_fs *c, unsigned dev_idx, int flags)
{
	return bch2_dev_usrdata_drop(c, dev_idx, flags) ?:
		bch2_dev_metadata_drop(c, dev_idx, flags);
}
