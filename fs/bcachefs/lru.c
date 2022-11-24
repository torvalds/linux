// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_background.h"
#include "btree_iter.h"
#include "btree_update.h"
#include "error.h"
#include "lru.h"
#include "recovery.h"

int bch2_lru_invalid(const struct bch_fs *c, struct bkey_s_c k,
		     int rw, struct printbuf *err)
{
	const struct bch_lru *lru = bkey_s_c_to_lru(k).v;

	if (bkey_val_bytes(k.k) < sizeof(*lru)) {
		prt_printf(err, "incorrect value size (%zu < %zu)",
		       bkey_val_bytes(k.k), sizeof(*lru));
		return -BCH_ERR_invalid_bkey;
	}

	return 0;
}

void bch2_lru_to_text(struct printbuf *out, struct bch_fs *c,
		      struct bkey_s_c k)
{
	const struct bch_lru *lru = bkey_s_c_to_lru(k).v;

	prt_printf(out, "idx %llu", le64_to_cpu(lru->idx));
}

int bch2_lru_delete(struct btree_trans *trans, u64 id, u64 idx, u64 time,
		    struct bkey_s_c orig_k)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	u64 existing_idx;
	struct printbuf buf = PRINTBUF;
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
		bch2_bkey_val_to_text(&buf, trans->c, orig_k);
		bch2_trans_inconsistent(trans,
			"pointer to nonexistent lru %llu:%llu\n%s",
			id, time, buf.buf);
		ret = -EIO;
		goto err;
	}

	existing_idx = le64_to_cpu(bkey_s_c_to_lru(k).v->idx);
	if (existing_idx != idx) {
		bch2_bkey_val_to_text(&buf, trans->c, orig_k);
		bch2_trans_inconsistent(trans,
			"lru %llu:%llu with wrong backpointer: got %llu, should be %llu\n%s",
			id, time, existing_idx, idx, buf.buf);
		ret = -EIO;
		goto err;
	}

	ret = bch2_btree_delete_at(trans, &iter, 0);
err:
	bch2_trans_iter_exit(trans, &iter);
	printbuf_exit(&buf);
	return ret;
}

int bch2_lru_set(struct btree_trans *trans, u64 lru_id, u64 idx, u64 *time)
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

	lru = bch2_bkey_alloc(trans, &iter, lru);
	ret = PTR_ERR_OR_ZERO(lru);
	if (ret)
		goto err;

	lru->v.idx = cpu_to_le64(idx);

	ret = bch2_trans_update(trans, &iter, &lru->k_i, 0);
	if (ret)
		goto err;
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_lru_change(struct btree_trans *trans, u64 id, u64 idx,
		    u64 old_time, u64 *new_time,
		    struct bkey_s_c k)
{
	if (old_time == *new_time)
		return 0;

	return  bch2_lru_delete(trans, id, idx, old_time, k) ?:
		bch2_lru_set(trans, id, idx, new_time);
}

static int bch2_check_lru_key(struct btree_trans *trans,
			      struct btree_iter *lru_iter,
			      struct bkey_s_c lru_k)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bch_alloc_v4 a;
	struct printbuf buf1 = PRINTBUF;
	struct printbuf buf2 = PRINTBUF;
	struct bpos alloc_pos;
	int ret;

	alloc_pos = POS(lru_k.k->p.inode,
			le64_to_cpu(bkey_s_c_to_lru(lru_k).v->idx));

	if (fsck_err_on(!bch2_dev_bucket_exists(c, alloc_pos), c,
			"lru key points to nonexistent device:bucket %llu:%llu",
			alloc_pos.inode, alloc_pos.offset))
		return bch2_btree_delete_at(trans, lru_iter, 0);

	bch2_trans_iter_init(trans, &iter, BTREE_ID_alloc, alloc_pos, 0);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	bch2_alloc_to_v4(k, &a);

	if (fsck_err_on(a.data_type != BCH_DATA_cached ||
			a.io_time[READ] != lru_k.k->p.offset, c,
			"incorrect lru entry %s\n"
			"  for %s",
			(bch2_bkey_val_to_text(&buf1, c, lru_k), buf1.buf),
			(bch2_bkey_val_to_text(&buf2, c, k), buf2.buf))) {
		ret = bch2_btree_delete_at(trans, lru_iter, 0);
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

int bch2_check_lrus(struct bch_fs *c)
{
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	bch2_trans_init(&trans, c, 0, 0);

	ret = for_each_btree_key_commit(&trans, iter,
			BTREE_ID_lru, POS_MIN, BTREE_ITER_PREFETCH, k,
			NULL, NULL, BTREE_INSERT_NOFAIL|BTREE_INSERT_LAZY_RW,
		bch2_check_lru_key(&trans, &iter, k));

	bch2_trans_exit(&trans);
	return ret;

}
