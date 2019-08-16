// SPDX-License-Identifier: GPL-2.0
/*
 * Code for moving data off a device.
 */

#include "bcachefs.h"
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
	BKEY_PADDED(key) tmp;
	int ret = 0;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	iter = bch2_trans_get_iter(&trans, btree_id, POS_MIN,
				   BTREE_ITER_PREFETCH);

	while ((k = bch2_btree_iter_peek(iter)).k &&
	       !(ret = bkey_err(k))) {
		if (!bch2_bkey_has_device(k, dev_idx)) {
			ret = bch2_mark_bkey_replicas(c, k);
			if (ret)
				break;
			bch2_btree_iter_next(iter);
			continue;
		}

		bkey_reassemble(&tmp.key, k);

		ret = drop_dev_ptrs(c, bkey_i_to_s(&tmp.key),
				    dev_idx, flags, false);
		if (ret)
			break;

		/*
		 * If the new extent no longer has any pointers, bch2_extent_normalize()
		 * will do the appropriate thing with it (turning it into a
		 * KEY_TYPE_error key, or just a discard if it was a cached extent)
		 */
		bch2_extent_normalize(c, bkey_i_to_s(&tmp.key));

		/* XXX not sketchy at all */
		iter->pos = bkey_start_pos(&tmp.key.k);

		bch2_trans_update(&trans, BTREE_INSERT_ENTRY(iter, &tmp.key));

		ret = bch2_trans_commit(&trans, NULL, NULL,
					BTREE_INSERT_ATOMIC|
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

	ret = bch2_trans_exit(&trans) ?: ret;

	BUG_ON(ret == -EINTR);

	return ret;
}

static int bch2_dev_usrdata_drop(struct bch_fs *c, unsigned dev_idx, int flags)
{
	return  __bch2_dev_usrdata_drop(c, dev_idx, flags, BTREE_ID_EXTENTS) ?:
		__bch2_dev_usrdata_drop(c, dev_idx, flags, BTREE_ID_REFLINK);
}

static int bch2_dev_metadata_drop(struct bch_fs *c, unsigned dev_idx, int flags)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct closure cl;
	struct btree *b;
	unsigned id;
	int ret;

	/* don't handle this yet: */
	if (flags & BCH_FORCE_IF_METADATA_LOST)
		return -EINVAL;

	bch2_trans_init(&trans, c, 0, 0);
	closure_init_stack(&cl);

	for (id = 0; id < BTREE_ID_NR; id++) {
		for_each_btree_node(&trans, iter, id, POS_MIN,
				    BTREE_ITER_PREFETCH, b) {
			__BKEY_PADDED(k, BKEY_BTREE_PTR_VAL_U64s_MAX) tmp;
			struct bkey_i_btree_ptr *new_key;
retry:
			if (!bch2_bkey_has_device(bkey_i_to_s_c(&b->key),
						  dev_idx)) {
				/*
				 * we might have found a btree node key we
				 * needed to update, and then tried to update it
				 * but got -EINTR after upgrading the iter, but
				 * then raced and the node is now gone:
				 */
				bch2_btree_iter_downgrade(iter);

				ret = bch2_mark_bkey_replicas(c, bkey_i_to_s_c(&b->key));
				if (ret)
					goto err;
			} else {
				bkey_copy(&tmp.k, &b->key);
				new_key = bkey_i_to_btree_ptr(&tmp.k);

				ret = drop_dev_ptrs(c, bkey_i_to_s(&new_key->k_i),
						    dev_idx, flags, true);
				if (ret)
					goto err;

				ret = bch2_btree_node_update_key(c, iter, b, new_key);
				if (ret == -EINTR) {
					b = bch2_btree_iter_peek_node(iter);
					goto retry;
				}
				if (ret)
					goto err;
			}
		}
		bch2_trans_iter_free(&trans, iter);
	}

	/* flush relevant btree updates */
	while (1) {
		closure_wait_event(&c->btree_interior_update_wait,
				   !bch2_btree_interior_updates_nr_pending(c) ||
				   c->btree_roots_dirty);
		if (!bch2_btree_interior_updates_nr_pending(c))
			break;
		bch2_journal_meta(&c->journal);
	}

	ret = 0;
err:
	ret = bch2_trans_exit(&trans) ?: ret;

	BUG_ON(ret == -EINTR);

	return ret;
}

int bch2_dev_data_drop(struct bch_fs *c, unsigned dev_idx, int flags)
{
	return bch2_dev_usrdata_drop(c, dev_idx, flags) ?:
		bch2_dev_metadata_drop(c, dev_idx, flags) ?:
		bch2_replicas_gc2(c);
}
