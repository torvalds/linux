// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "bkey_on_stack.h"
#include "btree_update.h"
#include "extents.h"
#include "inode.h"
#include "io.h"
#include "reflink.h"

#include <linux/sched/signal.h>

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
		__bch2_cut_front(l.k->p, _r);
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

static int bch2_make_extent_indirect(struct btree_trans *trans,
				     struct btree_iter *extent_iter,
				     struct bkey_i_extent *e)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *reflink_iter;
	struct bkey_s_c k;
	struct bkey_i_reflink_v *r_v;
	struct bkey_i_reflink_p *r_p;
	int ret;

	for_each_btree_key(trans, reflink_iter, BTREE_ID_REFLINK,
			   POS(0, c->reflink_hint),
			   BTREE_ITER_INTENT|BTREE_ITER_SLOTS, k, ret) {
		if (reflink_iter->pos.inode) {
			bch2_btree_iter_set_pos(reflink_iter, POS_MIN);
			continue;
		}

		if (bkey_deleted(k.k) && e->k.size <= k.k->size)
			break;
	}

	if (ret)
		goto err;

	/* rewind iter to start of hole, if necessary: */
	bch2_btree_iter_set_pos(reflink_iter, bkey_start_pos(k.k));

	r_v = bch2_trans_kmalloc(trans, sizeof(*r_v) + bkey_val_bytes(&e->k));
	ret = PTR_ERR_OR_ZERO(r_v);
	if (ret)
		goto err;

	bkey_reflink_v_init(&r_v->k_i);
	r_v->k.p	= reflink_iter->pos;
	bch2_key_resize(&r_v->k, e->k.size);
	r_v->k.version	= e->k.version;

	set_bkey_val_u64s(&r_v->k, bkey_val_u64s(&r_v->k) +
			  bkey_val_u64s(&e->k));
	r_v->v.refcount	= 0;
	memcpy(r_v->v.start, e->v.start, bkey_val_bytes(&e->k));

	bch2_trans_update(trans, reflink_iter, &r_v->k_i);

	r_p = bch2_trans_kmalloc(trans, sizeof(*r_p));
	if (IS_ERR(r_p))
		return PTR_ERR(r_p);

	e->k.type = KEY_TYPE_reflink_p;
	r_p = bkey_i_to_reflink_p(&e->k_i);
	set_bkey_val_bytes(&r_p->k, sizeof(r_p->v));
	r_p->v.idx = cpu_to_le64(bkey_start_offset(&r_v->k));

	bch2_trans_update(trans, extent_iter, &r_p->k_i);
err:
	if (!IS_ERR(reflink_iter)) {
		c->reflink_hint = reflink_iter->pos.offset;
		bch2_trans_iter_put(trans, reflink_iter);
	}

	return ret;
}

static struct bkey_s_c get_next_src(struct btree_iter *iter, struct bpos end)
{
	struct bkey_s_c k = bch2_btree_iter_peek(iter);
	int ret;

	for_each_btree_key_continue(iter, 0, k, ret) {
		if (bkey_cmp(iter->pos, end) >= 0)
			return bkey_s_c_null;

		if (k.k->type == KEY_TYPE_extent ||
		    k.k->type == KEY_TYPE_reflink_p)
			break;
	}

	return k;
}

s64 bch2_remap_range(struct bch_fs *c,
		     struct bpos dst_start, struct bpos src_start,
		     u64 remap_sectors, u64 *journal_seq,
		     u64 new_i_size, s64 *i_sectors_delta)
{
	struct btree_trans trans;
	struct btree_iter *dst_iter, *src_iter;
	struct bkey_s_c src_k;
	BKEY_PADDED(k) new_dst;
	struct bkey_on_stack new_src;
	struct bpos dst_end = dst_start, src_end = src_start;
	struct bpos dst_want, src_want;
	u64 src_done, dst_done;
	int ret = 0, ret2 = 0;

	if (!percpu_ref_tryget(&c->writes))
		return -EROFS;

	if (!(c->sb.features & (1ULL << BCH_FEATURE_REFLINK))) {
		mutex_lock(&c->sb_lock);
		if (!(c->sb.features & (1ULL << BCH_FEATURE_REFLINK))) {
			c->disk_sb.sb->features[0] |=
				cpu_to_le64(1ULL << BCH_FEATURE_REFLINK);

			bch2_write_super(c);
		}
		mutex_unlock(&c->sb_lock);
	}

	dst_end.offset += remap_sectors;
	src_end.offset += remap_sectors;

	bkey_on_stack_init(&new_src);
	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 4096);

	src_iter = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS, src_start,
				       BTREE_ITER_INTENT);
	dst_iter = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS, dst_start,
				       BTREE_ITER_INTENT);

	while (1) {
		bch2_trans_begin_updates(&trans);
		trans.mem_top = 0;

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto err;
		}

		src_k = get_next_src(src_iter, src_end);
		ret = bkey_err(src_k);
		if (ret)
			goto btree_err;

		src_done = bpos_min(src_iter->pos, src_end).offset -
			src_start.offset;
		dst_want = POS(dst_start.inode, dst_start.offset + src_done);

		if (bkey_cmp(dst_iter->pos, dst_want) < 0) {
			ret = bch2_fpunch_at(&trans, dst_iter, dst_want,
					     journal_seq, i_sectors_delta);
			if (ret)
				goto btree_err;
			continue;
		}

		BUG_ON(bkey_cmp(dst_iter->pos, dst_want));

		if (!bkey_cmp(dst_iter->pos, dst_end))
			break;

		if (src_k.k->type == KEY_TYPE_extent) {
			bkey_on_stack_realloc(&new_src, c, src_k.k->u64s);
			bkey_reassemble(new_src.k, src_k);
			src_k = bkey_i_to_s_c(new_src.k);

			bch2_cut_front(src_iter->pos,	new_src.k);
			bch2_cut_back(src_end,		&new_src.k->k);

			ret = bch2_make_extent_indirect(&trans, src_iter,
						bkey_i_to_extent(new_src.k));
			if (ret)
				goto btree_err;

			BUG_ON(src_k.k->type != KEY_TYPE_reflink_p);
		}

		if (src_k.k->type == KEY_TYPE_reflink_p) {
			struct bkey_s_c_reflink_p src_p =
				bkey_s_c_to_reflink_p(src_k);
			struct bkey_i_reflink_p *dst_p =
				bkey_reflink_p_init(&new_dst.k);

			u64 offset = le64_to_cpu(src_p.v->idx) +
				(src_iter->pos.offset -
				 bkey_start_offset(src_k.k));

			dst_p->v.idx = cpu_to_le64(offset);
		} else {
			BUG();
		}

		new_dst.k.k.p = dst_iter->pos;
		bch2_key_resize(&new_dst.k.k,
				min(src_k.k->p.offset - src_iter->pos.offset,
				    dst_end.offset - dst_iter->pos.offset));

		ret = bch2_extent_update(&trans, dst_iter, &new_dst.k,
					 NULL, journal_seq,
					 new_i_size, i_sectors_delta);
		if (ret)
			goto btree_err;

		dst_done = dst_iter->pos.offset - dst_start.offset;
		src_want = POS(src_start.inode, src_start.offset + dst_done);
		bch2_btree_iter_set_pos(src_iter, src_want);
btree_err:
		if (ret == -EINTR)
			ret = 0;
		if (ret)
			goto err;
	}

	BUG_ON(bkey_cmp(dst_iter->pos, dst_end));
err:
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
				bch2_trans_commit(&trans, NULL, journal_seq,
						  BTREE_INSERT_ATOMIC);
		}
	} while (ret2 == -EINTR);

	ret = bch2_trans_exit(&trans) ?: ret;
	bkey_on_stack_exit(&new_src, c);

	percpu_ref_put(&c->writes);

	return dst_done ?: ret ?: ret2;
}
