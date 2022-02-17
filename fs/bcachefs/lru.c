// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_background.h"
#include "btree_iter.h"
#include "btree_update.h"
#include "error.h"
#include "lru.h"
#include "recovery.h"

const char *bch2_lru_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	const struct bch_lru *lru = bkey_s_c_to_lru(k).v;

	if (bkey_val_bytes(k.k) < sizeof(*lru))
		return "incorrect value size";

	return NULL;
}

void bch2_lru_to_text(struct printbuf *out, struct bch_fs *c,
		      struct bkey_s_c k)
{
	const struct bch_lru *lru = bkey_s_c_to_lru(k).v;

	pr_buf(out, "idx %llu", le64_to_cpu(lru->idx));
}

static int lru_delete(struct btree_trans *trans, u64 id, u64 idx, u64 time)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	u64 existing_idx;
	int ret = 0;

	if (!time)
		return 0;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_lru,
			     POS(id, time),
			     BTREE_ITER_INTENT|
			     BTREE_ITER_WITH_UPDATES);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (k.k->type != KEY_TYPE_lru) {
		bch2_fs_inconsistent(c,
			"pointer to nonexistent lru %llu:%llu",
			id, time);
		ret = -EIO;
		goto err;
	}

	existing_idx = le64_to_cpu(bkey_s_c_to_lru(k).v->idx);
	if (existing_idx != idx) {
		bch2_fs_inconsistent(c,
			"lru %llu:%llu with wrong backpointer: got %llu, should be %llu",
			id, time, existing_idx, idx);
		ret = -EIO;
		goto err;
	}

	ret = bch2_btree_delete_at(trans, &iter, 0);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int lru_set(struct btree_trans *trans, u64 lru_id, u64 idx, u64 *time)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_i_lru *lru;
	int ret = 0;

	if (!*time)
		return 0;

	for_each_btree_key_norestart(trans, iter, BTREE_ID_lru,
			POS(lru_id, *time),
			BTREE_ITER_SLOTS|
			BTREE_ITER_INTENT|
			BTREE_ITER_WITH_UPDATES, k, ret)
		if (bkey_deleted(k.k))
			break;

	if (ret)
		goto err;

	BUG_ON(iter.pos.inode != lru_id);
	*time = iter.pos.offset;

	lru = bch2_trans_kmalloc(trans, sizeof(*lru));
	ret = PTR_ERR_OR_ZERO(lru);
	if (ret)
		goto err;

	bkey_lru_init(&lru->k_i);
	lru->k.p	= iter.pos;
	lru->v.idx	= cpu_to_le64(idx);

	ret = bch2_trans_update(trans, &iter, &lru->k_i, 0);
	if (ret)
		goto err;
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_lru_change(struct btree_trans *trans, u64 id, u64 idx,
		    u64 old_time, u64 *new_time)
{
	if (old_time == *new_time)
		return 0;

	return  lru_delete(trans, id, idx, old_time) ?:
		lru_set(trans, id, idx, new_time);
}

static int bch2_check_lru_key(struct btree_trans *trans,
			      struct btree_iter *lru_iter, bool initial)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c lru_k, k;
	struct bch_alloc_v4 a;
	struct printbuf buf1 = PRINTBUF;
	struct printbuf buf2 = PRINTBUF;
	u64 idx;
	int ret;

	lru_k = bch2_btree_iter_peek(lru_iter);
	if (!lru_k.k)
		return 0;

	ret = bkey_err(lru_k);
	if (ret)
		return ret;

	idx = le64_to_cpu(bkey_s_c_to_lru(lru_k).v->idx);

	bch2_trans_iter_init(trans, &iter, BTREE_ID_alloc,
			     POS(lru_k.k->p.inode, idx), 0);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	bch2_alloc_to_v4(k, &a);

	if (fsck_err_on(bucket_state(a) != BUCKET_cached ||
			a.io_time[READ] != lru_k.k->p.offset, c,
			"incorrect lru entry %s\n"
			"  for %s",
			(bch2_bkey_val_to_text(&buf1, c, lru_k), buf1.buf),
			(bch2_bkey_val_to_text(&buf2, c, k), buf2.buf))) {
		struct bkey_i *update =
			bch2_trans_kmalloc(trans, sizeof(*update));

		ret = PTR_ERR_OR_ZERO(update);
		if (ret)
			goto err;

		bkey_init(&update->k);
		update->k.p = lru_iter->pos;

		ret = bch2_trans_update(trans, lru_iter, update, 0);
		if (ret)
			goto err;
	}
err:
fsck_err:
	bch2_trans_iter_exit(trans, &iter);
	printbuf_exit(&buf2);
	printbuf_exit(&buf1);
	return ret;
}

int bch2_check_lrus(struct bch_fs *c, bool initial)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_lru, POS_MIN,
			   BTREE_ITER_PREFETCH, k, ret) {
		ret = __bch2_trans_do(&trans, NULL, NULL, 0,
			bch2_check_lru_key(&trans, &iter, initial));
		if (ret)
			break;
	}
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);
	return ret;

}
