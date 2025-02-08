// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_background.h"
#include "bkey_buf.h"
#include "btree_iter.h"
#include "btree_update.h"
#include "btree_write_buffer.h"
#include "ec.h"
#include "error.h"
#include "lru.h"
#include "recovery.h"

/* KEY_TYPE_lru is obsolete: */
int bch2_lru_validate(struct bch_fs *c, struct bkey_s_c k,
		      struct bkey_validate_context from)
{
	int ret = 0;

	bkey_fsck_err_on(!lru_pos_time(k.k->p),
			 c, lru_entry_at_time_0,
			 "lru entry at time=0");
fsck_err:
	return ret;
}

void bch2_lru_to_text(struct printbuf *out, struct bch_fs *c,
		      struct bkey_s_c k)
{
	const struct bch_lru *lru = bkey_s_c_to_lru(k).v;

	prt_printf(out, "idx %llu", le64_to_cpu(lru->idx));
}

void bch2_lru_pos_to_text(struct printbuf *out, struct bpos lru)
{
	prt_printf(out, "%llu:%llu -> %llu:%llu",
		   lru_pos_id(lru),
		   lru_pos_time(lru),
		   u64_to_bucket(lru.offset).inode,
		   u64_to_bucket(lru.offset).offset);
}

static int __bch2_lru_set(struct btree_trans *trans, u16 lru_id,
			  u64 dev_bucket, u64 time, bool set)
{
	return time
		? bch2_btree_bit_mod_buffered(trans, BTREE_ID_lru,
					      lru_pos(lru_id, dev_bucket, time), set)
		: 0;
}

int bch2_lru_del(struct btree_trans *trans, u16 lru_id, u64 dev_bucket, u64 time)
{
	return __bch2_lru_set(trans, lru_id, dev_bucket, time, KEY_TYPE_deleted);
}

int bch2_lru_set(struct btree_trans *trans, u16 lru_id, u64 dev_bucket, u64 time)
{
	return __bch2_lru_set(trans, lru_id, dev_bucket, time, KEY_TYPE_set);
}

int __bch2_lru_change(struct btree_trans *trans,
		      u16 lru_id, u64 dev_bucket,
		      u64 old_time, u64 new_time)
{
	if (old_time == new_time)
		return 0;

	return  bch2_lru_del(trans, lru_id, dev_bucket, old_time) ?:
		bch2_lru_set(trans, lru_id, dev_bucket, new_time);
}

static const char * const bch2_lru_types[] = {
#define x(n) #n,
	BCH_LRU_TYPES()
#undef x
	NULL
};

int bch2_lru_check_set(struct btree_trans *trans,
		       u16 lru_id,
		       u64 dev_bucket,
		       u64 time,
		       struct bkey_s_c referring_k,
		       struct bkey_buf *last_flushed)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	struct btree_iter lru_iter;
	struct bkey_s_c lru_k =
		bch2_bkey_get_iter(trans, &lru_iter, BTREE_ID_lru,
				   lru_pos(lru_id, dev_bucket, time), 0);
	int ret = bkey_err(lru_k);
	if (ret)
		return ret;

	if (lru_k.k->type != KEY_TYPE_set) {
		ret = bch2_btree_write_buffer_maybe_flush(trans, referring_k, last_flushed);
		if (ret)
			goto err;

		if (fsck_err(trans, alloc_key_to_missing_lru_entry,
			     "missing %s lru entry\n"
			     "  %s",
			     bch2_lru_types[lru_type(lru_k)],
			     (bch2_bkey_val_to_text(&buf, c, referring_k), buf.buf))) {
			ret = bch2_lru_set(trans, lru_id, dev_bucket, time);
			if (ret)
				goto err;
		}
	}
err:
fsck_err:
	bch2_trans_iter_exit(trans, &lru_iter);
	printbuf_exit(&buf);
	return ret;
}

static struct bbpos lru_pos_to_bp(struct bkey_s_c lru_k)
{
	enum bch_lru_type type = lru_type(lru_k);

	switch (type) {
	case BCH_LRU_read:
	case BCH_LRU_fragmentation:
		return BBPOS(BTREE_ID_alloc, u64_to_bucket(lru_k.k->p.offset));
	case BCH_LRU_stripes:
		return BBPOS(BTREE_ID_stripes, POS(0, lru_k.k->p.offset));
	default:
		BUG();
	}
}

static u64 bkey_lru_type_idx(struct bch_fs *c,
			     enum bch_lru_type type,
			     struct bkey_s_c k)
{
	struct bch_alloc_v4 a_convert;
	const struct bch_alloc_v4 *a;

	switch (type) {
	case BCH_LRU_read:
		a = bch2_alloc_to_v4(k, &a_convert);
		return alloc_lru_idx_read(*a);
	case BCH_LRU_fragmentation: {
		a = bch2_alloc_to_v4(k, &a_convert);

		rcu_read_lock();
		struct bch_dev *ca = bch2_dev_rcu_noerror(c, k.k->p.inode);
		u64 idx = ca
			? alloc_lru_idx_fragmentation(*a, ca)
			: 0;
		rcu_read_unlock();
		return idx;
	}
	case BCH_LRU_stripes:
		return k.k->type == KEY_TYPE_stripe
			? stripe_lru_pos(bkey_s_c_to_stripe(k).v)
			: 0;
	default:
		BUG();
	}
}

static int bch2_check_lru_key(struct btree_trans *trans,
			      struct btree_iter *lru_iter,
			      struct bkey_s_c lru_k,
			      struct bkey_buf *last_flushed)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf1 = PRINTBUF;
	struct printbuf buf2 = PRINTBUF;

	struct bbpos bp = lru_pos_to_bp(lru_k);

	struct btree_iter iter;
	struct bkey_s_c k = bch2_bkey_get_iter(trans, &iter, bp.btree, bp.pos, 0);
	int ret = bkey_err(k);
	if (ret)
		goto err;

	enum bch_lru_type type = lru_type(lru_k);
	u64 idx = bkey_lru_type_idx(c, type, k);

	if (lru_pos_time(lru_k.k->p) != idx) {
		ret = bch2_btree_write_buffer_maybe_flush(trans, lru_k, last_flushed);
		if (ret)
			goto err;

		if (fsck_err(trans, lru_entry_bad,
			     "incorrect lru entry: lru %s time %llu\n"
			     "  %s\n"
			     "  for %s",
			     bch2_lru_types[type],
			     lru_pos_time(lru_k.k->p),
			     (bch2_bkey_val_to_text(&buf1, c, lru_k), buf1.buf),
			     (bch2_bkey_val_to_text(&buf2, c, k), buf2.buf)))
			ret = bch2_btree_bit_mod_buffered(trans, BTREE_ID_lru, lru_iter->pos, false);
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
	struct bkey_buf last_flushed;

	bch2_bkey_buf_init(&last_flushed);
	bkey_init(&last_flushed.k->k);

	int ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter,
				BTREE_ID_lru, POS_MIN, BTREE_ITER_prefetch, k,
				NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			bch2_check_lru_key(trans, &iter, k, &last_flushed)));

	bch2_bkey_buf_exit(&last_flushed, c);
	bch_err_fn(c, ret);
	return ret;

}
