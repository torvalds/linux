// SPDX-License-Identifier: GPL-2.0
/*
 * Code for moving data off a device.
 */

#include "bcachefs.h"
#include "bkey_buf.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "buckets.h"
#include "extents.h"
#include "io.h"
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
		return -EINVAL;

	return 0;
}

static int __bch2_dev_usrdata_drop(struct bch_fs *c, unsigned dev_idx, int flags,
				   enum btree_id btree_id)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_buf sk;
	int ret = 0;

	bch2_bkey_buf_init(&sk);
	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	iter = bch2_trans_get_iter(&trans, btree_id, POS_MIN,
				   BTREE_ITER_PREFETCH);

	while ((k = bch2_btree_iter_peek(iter)).k &&
	       !(ret = bkey_err(k))) {
		if (!bch2_bkey_has_device(k, dev_idx)) {
			bch2_btree_iter_advance(iter);
			continue;
		}

		bch2_bkey_buf_reassemble(&sk, c, k);

		ret = drop_dev_ptrs(c, bkey_i_to_s(sk.k),
				    dev_idx, flags, false);
		if (ret)
			break;

		/*
		 * If the new extent no longer has any pointers, bch2_extent_normalize()
		 * will do the appropriate thing with it (turning it into a
		 * KEY_TYPE_error key, or just a discard if it was a cached extent)
		 */
		bch2_extent_normalize(c, bkey_i_to_s(sk.k));

		bch2_btree_iter_set_pos(iter, bkey_start_pos(&sk.k->k));

		ret   = bch2_btree_iter_traverse(iter) ?:
			bch2_trans_update(&trans, iter, sk.k, 0) ?:
			bch2_trans_commit(&trans, NULL, NULL,
					BTREE_INSERT_NOFAIL);

		/*
		 * don't want to leave ret == -EINTR, since if we raced and
		 * something else overwrote the key we could spuriously return
		 * -EINTR below:
		 */
		if (ret == -EINTR)
			ret = 0;
		if (ret)
			break;
	}
	bch2_trans_iter_put(&trans, iter);

	ret = bch2_trans_exit(&trans) ?: ret;
	bch2_bkey_buf_exit(&sk, c);

	BUG_ON(ret == -EINTR);

	return ret;
}

static int bch2_dev_usrdata_drop(struct bch_fs *c, unsigned dev_idx, int flags)
{
	return  __bch2_dev_usrdata_drop(c, dev_idx, flags, BTREE_ID_extents) ?:
		__bch2_dev_usrdata_drop(c, dev_idx, flags, BTREE_ID_reflink);
}

static int bch2_dev_metadata_drop(struct bch_fs *c, unsigned dev_idx, int flags)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct closure cl;
	struct btree *b;
	struct bkey_buf k;
	unsigned id;
	int ret;

	/* don't handle this yet: */
	if (flags & BCH_FORCE_IF_METADATA_LOST)
		return -EINVAL;

	bch2_bkey_buf_init(&k);
	bch2_trans_init(&trans, c, 0, 0);
	closure_init_stack(&cl);

	for (id = 0; id < BTREE_ID_NR; id++) {
		for_each_btree_node(&trans, iter, id, POS_MIN,
				    BTREE_ITER_PREFETCH, b) {
retry:
			if (!bch2_bkey_has_device(bkey_i_to_s_c(&b->key),
						  dev_idx))
				continue;

			bch2_bkey_buf_copy(&k, c, &b->key);

			ret = drop_dev_ptrs(c, bkey_i_to_s(k.k),
					    dev_idx, flags, true);
			if (ret) {
				bch_err(c, "Cannot drop device without losing data");
				break;
			}

			ret = bch2_btree_node_update_key(c, iter, b, k.k);
			if (ret == -EINTR) {
				b = bch2_btree_iter_peek_node(iter);
				ret = 0;
				goto retry;
			}
			if (ret) {
				bch_err(c, "Error updating btree node key: %i", ret);
				break;
			}
		}
		bch2_trans_iter_free(&trans, iter);

		if (ret)
			goto err;
	}

	/* flush relevant btree updates */
	closure_wait_event(&c->btree_interior_update_wait,
			   !bch2_btree_interior_updates_nr_pending(c));

	ret = 0;
err:
	ret = bch2_trans_exit(&trans) ?: ret;
	bch2_bkey_buf_exit(&k, c);

	BUG_ON(ret == -EINTR);

	return ret;
}

int bch2_dev_data_drop(struct bch_fs *c, unsigned dev_idx, int flags)
{
	return bch2_dev_usrdata_drop(c, dev_idx, flags) ?:
		bch2_dev_metadata_drop(c, dev_idx, flags);
}
