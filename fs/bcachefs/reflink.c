// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "bkey_buf.h"
#include "btree_update.h"
#include "buckets.h"
#include "extents.h"
#include "inode.h"
#include "io.h"
#include "reflink.h"

#include <linux/sched/signal.h>

static inline unsigned bkey_type_to_indirect(const struct bkey *k)
{
	switch (k->type) {
	case KEY_TYPE_extent:
		return KEY_TYPE_reflink_v;
	case KEY_TYPE_inline_data:
		return KEY_TYPE_indirect_inline_data;
	default:
		return 0;
	}
}

/* reflink pointers */

const char *bch2_reflink_p_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_reflink_p p = bkey_s_c_to_reflink_p(k);

	if (bkey_val_bytes(p.k) != sizeof(*p.v))
		return "incorrect value size";

	return NULL;
}

void bch2_reflink_p_to_text(struct printbuf *out, struct bch_fs *c,
			    struct bkey_s_c k)
{
	struct bkey_s_c_reflink_p p = bkey_s_c_to_reflink_p(k);

	pr_buf(out, "idx %llu", le64_to_cpu(p.v->idx));
}

enum merge_result bch2_reflink_p_merge(struct bch_fs *c,
				       struct bkey_s _l, struct bkey_s _r)
{
	struct bkey_s_reflink_p l = bkey_s_to_reflink_p(_l);
	struct bkey_s_reflink_p r = bkey_s_to_reflink_p(_r);

	if (le64_to_cpu(l.v->idx) + l.k->size != le64_to_cpu(r.v->idx))
		return BCH_MERGE_NOMERGE;

	if ((u64) l.k->size + r.k->size > KEY_SIZE_MAX) {
		bch2_key_resize(l.k, KEY_SIZE_MAX);
		bch2_cut_front_s(l.k->p, _r);
		return BCH_MERGE_PARTIAL;
	}

	bch2_key_resize(l.k, l.k->size + r.k->size);

	return BCH_MERGE_MERGE;
}

/* indirect extents */

const char *bch2_reflink_v_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_reflink_v r = bkey_s_c_to_reflink_v(k);

	if (bkey_val_bytes(r.k) < sizeof(*r.v))
		return "incorrect value size";

	return bch2_bkey_ptrs_invalid(c, k);
}

void bch2_reflink_v_to_text(struct printbuf *out, struct bch_fs *c,
			    struct bkey_s_c k)
{
	struct bkey_s_c_reflink_v r = bkey_s_c_to_reflink_v(k);

	pr_buf(out, "refcount: %llu ", le64_to_cpu(r.v->refcount));

	bch2_bkey_ptrs_to_text(out, c, k);
}

/* indirect inline data */

const char *bch2_indirect_inline_data_invalid(const struct bch_fs *c,
					      struct bkey_s_c k)
{
	if (bkey_val_bytes(k.k) < sizeof(struct bch_indirect_inline_data))
		return "incorrect value size";
	return NULL;
}

void bch2_indirect_inline_data_to_text(struct printbuf *out,
					struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_indirect_inline_data d = bkey_s_c_to_indirect_inline_data(k);
	unsigned datalen = bkey_inline_data_bytes(k.k);

	pr_buf(out, "refcount %llu datalen %u: %*phN",
	       le64_to_cpu(d.v->refcount), datalen,
	       min(datalen, 32U), d.v->data);
}

static int bch2_make_extent_indirect(struct btree_trans *trans,
				     struct btree_iter *extent_iter,
				     struct bkey_i *orig)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *reflink_iter;
	struct bkey_s_c k;
	struct bkey_i *r_v;
	struct bkey_i_reflink_p *r_p;
	__le64 *refcount;
	int ret;

	if (orig->k.type == KEY_TYPE_inline_data)
		bch2_check_set_feature(c, BCH_FEATURE_reflink_inline_data);

	for_each_btree_key(trans, reflink_iter, BTREE_ID_reflink,
			   POS(0, c->reflink_hint),
			   BTREE_ITER_INTENT|BTREE_ITER_SLOTS, k, ret) {
		if (reflink_iter->pos.inode) {
			bch2_btree_iter_set_pos(reflink_iter, POS_MIN);
			continue;
		}

		if (bkey_deleted(k.k) && orig->k.size <= k.k->size)
			break;
	}

	if (ret)
		goto err;

	/* rewind iter to start of hole, if necessary: */
	bch2_btree_iter_set_pos(reflink_iter, bkey_start_pos(k.k));

	r_v = bch2_trans_kmalloc(trans, sizeof(__le64) + bkey_val_bytes(&orig->k));
	ret = PTR_ERR_OR_ZERO(r_v);
	if (ret)
		goto err;

	bkey_init(&r_v->k);
	r_v->k.type	= bkey_type_to_indirect(&orig->k);
	r_v->k.p	= reflink_iter->pos;
	bch2_key_resize(&r_v->k, orig->k.size);
	r_v->k.version	= orig->k.version;

	set_bkey_val_bytes(&r_v->k, sizeof(__le64) + bkey_val_bytes(&orig->k));

	refcount	= bkey_refcount(r_v);
	*refcount	= 0;
	memcpy(refcount + 1, &orig->v, bkey_val_bytes(&orig->k));

	ret = bch2_trans_update(trans, reflink_iter, r_v, 0);
	if (ret)
		goto err;

	r_p = bch2_trans_kmalloc(trans, sizeof(*r_p));
	if (IS_ERR(r_p)) {
		ret = PTR_ERR(r_p);
		goto err;
	}

	orig->k.type = KEY_TYPE_reflink_p;
	r_p = bkey_i_to_reflink_p(orig);
	set_bkey_val_bytes(&r_p->k, sizeof(r_p->v));
	r_p->v.idx = cpu_to_le64(bkey_start_offset(&r_v->k));

	ret = bch2_trans_update(trans, extent_iter, &r_p->k_i, 0);
err:
	if (!IS_ERR(reflink_iter))
		c->reflink_hint = reflink_iter->pos.offset;
	bch2_trans_iter_put(trans, reflink_iter);

	return ret;
}

static struct bkey_s_c get_next_src(struct btree_iter *iter, struct bpos end)
{
	struct bkey_s_c k;
	int ret;

	for_each_btree_key_continue(iter, 0, k, ret) {
		if (bkey_cmp(iter->pos, end) >= 0)
			break;

		if (bkey_extent_is_data(k.k))
			return k;
	}

	bch2_btree_iter_set_pos(iter, end);
	return bkey_s_c_null;
}

s64 bch2_remap_range(struct bch_fs *c,
		     struct bpos dst_start, struct bpos src_start,
		     u64 remap_sectors, u64 *journal_seq,
		     u64 new_i_size, s64 *i_sectors_delta)
{
	struct btree_trans trans;
	struct btree_iter *dst_iter, *src_iter;
	struct bkey_s_c src_k;
	struct bkey_buf new_dst, new_src;
	struct bpos dst_end = dst_start, src_end = src_start;
	struct bpos src_want;
	u64 dst_done;
	int ret = 0, ret2 = 0;

	if (!percpu_ref_tryget(&c->writes))
		return -EROFS;

	bch2_check_set_feature(c, BCH_FEATURE_reflink);

	dst_end.offset += remap_sectors;
	src_end.offset += remap_sectors;

	bch2_bkey_buf_init(&new_dst);
	bch2_bkey_buf_init(&new_src);
	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 4096);

	src_iter = bch2_trans_get_iter(&trans, BTREE_ID_extents, src_start,
				       BTREE_ITER_INTENT);
	dst_iter = bch2_trans_get_iter(&trans, BTREE_ID_extents, dst_start,
				       BTREE_ITER_INTENT);

	while ((ret == 0 || ret == -EINTR) &&
	       bkey_cmp(dst_iter->pos, dst_end) < 0) {
		struct disk_reservation disk_res = { 0 };

		bch2_trans_begin(&trans);

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		dst_done = dst_iter->pos.offset - dst_start.offset;
		src_want = POS(src_start.inode, src_start.offset + dst_done);
		bch2_btree_iter_set_pos(src_iter, src_want);

		src_k = get_next_src(src_iter, src_end);
		ret = bkey_err(src_k);
		if (ret)
			continue;

		if (bkey_cmp(src_want, src_iter->pos) < 0) {
			ret = bch2_fpunch_at(&trans, dst_iter,
					bpos_min(dst_end,
						 POS(dst_iter->pos.inode, dst_iter->pos.offset +
						     src_iter->pos.offset - src_want.offset)),
						 journal_seq, i_sectors_delta);
			continue;
		}

		if (src_k.k->type != KEY_TYPE_reflink_p) {
			bch2_bkey_buf_reassemble(&new_src, c, src_k);
			src_k = bkey_i_to_s_c(new_src.k);

			bch2_btree_iter_set_pos(src_iter, bkey_start_pos(src_k.k));

			ret = bch2_make_extent_indirect(&trans, src_iter,
						new_src.k);
			if (ret)
				continue;

			BUG_ON(src_k.k->type != KEY_TYPE_reflink_p);
		}

		if (src_k.k->type == KEY_TYPE_reflink_p) {
			struct bkey_s_c_reflink_p src_p =
				bkey_s_c_to_reflink_p(src_k);
			struct bkey_i_reflink_p *dst_p =
				bkey_reflink_p_init(new_dst.k);

			u64 offset = le64_to_cpu(src_p.v->idx) +
				(src_want.offset -
				 bkey_start_offset(src_k.k));

			dst_p->v.idx = cpu_to_le64(offset);
		} else {
			BUG();
		}

		new_dst.k->k.p = dst_iter->pos;
		bch2_key_resize(&new_dst.k->k,
				min(src_k.k->p.offset - src_want.offset,
				    dst_end.offset - dst_iter->pos.offset));
		ret = bch2_extent_update(&trans, dst_iter, new_dst.k,
					 &disk_res, journal_seq,
					 new_i_size, i_sectors_delta,
					 true);
		bch2_disk_reservation_put(c, &disk_res);
	}
	bch2_trans_iter_put(&trans, dst_iter);
	bch2_trans_iter_put(&trans, src_iter);

	BUG_ON(!ret && bkey_cmp(dst_iter->pos, dst_end));
	BUG_ON(bkey_cmp(dst_iter->pos, dst_end) > 0);

	dst_done = dst_iter->pos.offset - dst_start.offset;
	new_i_size = min(dst_iter->pos.offset << 9, new_i_size);

	bch2_trans_begin(&trans);

	do {
		struct bch_inode_unpacked inode_u;
		struct btree_iter *inode_iter;

		inode_iter = bch2_inode_peek(&trans, &inode_u,
				dst_start.inode, BTREE_ITER_INTENT);
		ret2 = PTR_ERR_OR_ZERO(inode_iter);

		if (!ret2 &&
		    inode_u.bi_size < new_i_size) {
			inode_u.bi_size = new_i_size;
			ret2  = bch2_inode_write(&trans, inode_iter, &inode_u) ?:
				bch2_trans_commit(&trans, NULL, journal_seq, 0);
		}

		bch2_trans_iter_put(&trans, inode_iter);
	} while (ret2 == -EINTR);

	ret = bch2_trans_exit(&trans) ?: ret;
	bch2_bkey_buf_exit(&new_src, c);
	bch2_bkey_buf_exit(&new_dst, c);

	percpu_ref_put(&c->writes);

	return dst_done ?: ret ?: ret2;
}
