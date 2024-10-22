// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "bkey_buf.h"
#include "btree_update.h"
#include "buckets.h"
#include "error.h"
#include "extents.h"
#include "inode.h"
#include "io_misc.h"
#include "io_write.h"
#include "rebalance.h"
#include "reflink.h"
#include "subvolume.h"
#include "super-io.h"

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

int bch2_reflink_p_validate(struct bch_fs *c, struct bkey_s_c k,
			    enum bch_validate_flags flags)
{
	struct bkey_s_c_reflink_p p = bkey_s_c_to_reflink_p(k);
	int ret = 0;

	bkey_fsck_err_on(le64_to_cpu(p.v->idx) < le32_to_cpu(p.v->front_pad),
			 c, reflink_p_front_pad_bad,
			 "idx < front_pad (%llu < %u)",
			 le64_to_cpu(p.v->idx), le32_to_cpu(p.v->front_pad));
fsck_err:
	return ret;
}

void bch2_reflink_p_to_text(struct printbuf *out, struct bch_fs *c,
			    struct bkey_s_c k)
{
	struct bkey_s_c_reflink_p p = bkey_s_c_to_reflink_p(k);

	prt_printf(out, "idx %llu front_pad %u back_pad %u",
	       le64_to_cpu(p.v->idx),
	       le32_to_cpu(p.v->front_pad),
	       le32_to_cpu(p.v->back_pad));
}

bool bch2_reflink_p_merge(struct bch_fs *c, struct bkey_s _l, struct bkey_s_c _r)
{
	struct bkey_s_reflink_p l = bkey_s_to_reflink_p(_l);
	struct bkey_s_c_reflink_p r = bkey_s_c_to_reflink_p(_r);

	/*
	 * Disabled for now, the triggers code needs to be reworked for merging
	 * of reflink pointers to work:
	 */
	return false;

	if (le64_to_cpu(l.v->idx) + l.k->size != le64_to_cpu(r.v->idx))
		return false;

	bch2_key_resize(l.k, l.k->size + r.k->size);
	return true;
}

static int trans_trigger_reflink_p_segment(struct btree_trans *trans,
			struct bkey_s_c_reflink_p p, u64 *idx,
			enum btree_iter_update_trigger_flags flags)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_i *k;
	__le64 *refcount;
	int add = !(flags & BTREE_TRIGGER_overwrite) ? 1 : -1;
	struct printbuf buf = PRINTBUF;
	int ret;

	k = bch2_bkey_get_mut_noupdate(trans, &iter,
			BTREE_ID_reflink, POS(0, *idx),
			BTREE_ITER_with_updates);
	ret = PTR_ERR_OR_ZERO(k);
	if (ret)
		goto err;

	refcount = bkey_refcount(bkey_i_to_s(k));
	if (!refcount) {
		bch2_bkey_val_to_text(&buf, c, p.s_c);
		bch2_trans_inconsistent(trans,
			"nonexistent indirect extent at %llu while marking\n  %s",
			*idx, buf.buf);
		ret = -EIO;
		goto err;
	}

	if (!*refcount && (flags & BTREE_TRIGGER_overwrite)) {
		bch2_bkey_val_to_text(&buf, c, p.s_c);
		bch2_trans_inconsistent(trans,
			"indirect extent refcount underflow at %llu while marking\n  %s",
			*idx, buf.buf);
		ret = -EIO;
		goto err;
	}

	if (flags & BTREE_TRIGGER_insert) {
		struct bch_reflink_p *v = (struct bch_reflink_p *) p.v;
		u64 pad;

		pad = max_t(s64, le32_to_cpu(v->front_pad),
			    le64_to_cpu(v->idx) - bkey_start_offset(&k->k));
		BUG_ON(pad > U32_MAX);
		v->front_pad = cpu_to_le32(pad);

		pad = max_t(s64, le32_to_cpu(v->back_pad),
			    k->k.p.offset - p.k->size - le64_to_cpu(v->idx));
		BUG_ON(pad > U32_MAX);
		v->back_pad = cpu_to_le32(pad);
	}

	le64_add_cpu(refcount, add);

	bch2_btree_iter_set_pos_to_extent_start(&iter);
	ret = bch2_trans_update(trans, &iter, k, 0);
	if (ret)
		goto err;

	*idx = k->k.p.offset;
err:
	bch2_trans_iter_exit(trans, &iter);
	printbuf_exit(&buf);
	return ret;
}

static s64 gc_trigger_reflink_p_segment(struct btree_trans *trans,
				struct bkey_s_c_reflink_p p, u64 *idx,
				enum btree_iter_update_trigger_flags flags,
				size_t r_idx)
{
	struct bch_fs *c = trans->c;
	struct reflink_gc *r;
	int add = !(flags & BTREE_TRIGGER_overwrite) ? 1 : -1;
	u64 start = le64_to_cpu(p.v->idx);
	u64 end = le64_to_cpu(p.v->idx) + p.k->size;
	u64 next_idx = end + le32_to_cpu(p.v->back_pad);
	s64 ret = 0;
	struct printbuf buf = PRINTBUF;

	if (r_idx >= c->reflink_gc_nr)
		goto not_found;

	r = genradix_ptr(&c->reflink_gc_table, r_idx);
	next_idx = min(next_idx, r->offset - r->size);
	if (*idx < next_idx)
		goto not_found;

	BUG_ON((s64) r->refcount + add < 0);

	if (flags & BTREE_TRIGGER_gc)
		r->refcount += add;
	*idx = r->offset;
	return 0;
not_found:
	BUG_ON(!(flags & BTREE_TRIGGER_check_repair));

	if (fsck_err(trans, reflink_p_to_missing_reflink_v,
		     "pointer to missing indirect extent\n"
		     "  %s\n"
		     "  missing range %llu-%llu",
		     (bch2_bkey_val_to_text(&buf, c, p.s_c), buf.buf),
		     *idx, next_idx)) {
		struct bkey_i *update = bch2_bkey_make_mut_noupdate(trans, p.s_c);
		ret = PTR_ERR_OR_ZERO(update);
		if (ret)
			goto err;

		if (next_idx <= start) {
			bkey_i_to_reflink_p(update)->v.front_pad = cpu_to_le32(start - next_idx);
		} else if (*idx >= end) {
			bkey_i_to_reflink_p(update)->v.back_pad = cpu_to_le32(*idx - end);
		} else {
			bkey_error_init(update);
			update->k.p		= p.k->p;
			update->k.size		= p.k->size;
			set_bkey_val_u64s(&update->k, 0);
		}

		ret = bch2_btree_insert_trans(trans, BTREE_ID_extents, update, BTREE_TRIGGER_norun);
	}

	*idx = next_idx;
err:
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

static int __trigger_reflink_p(struct btree_trans *trans,
		enum btree_id btree_id, unsigned level, struct bkey_s_c k,
		enum btree_iter_update_trigger_flags flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c_reflink_p p = bkey_s_c_to_reflink_p(k);
	int ret = 0;

	u64 idx = le64_to_cpu(p.v->idx) - le32_to_cpu(p.v->front_pad);
	u64 end = le64_to_cpu(p.v->idx) + p.k->size + le32_to_cpu(p.v->back_pad);

	if (flags & BTREE_TRIGGER_transactional) {
		while (idx < end && !ret)
			ret = trans_trigger_reflink_p_segment(trans, p, &idx, flags);
	}

	if (flags & (BTREE_TRIGGER_check_repair|BTREE_TRIGGER_gc)) {
		size_t l = 0, r = c->reflink_gc_nr;

		while (l < r) {
			size_t m = l + (r - l) / 2;
			struct reflink_gc *ref = genradix_ptr(&c->reflink_gc_table, m);
			if (ref->offset <= idx)
				l = m + 1;
			else
				r = m;
		}

		while (idx < end && !ret)
			ret = gc_trigger_reflink_p_segment(trans, p, &idx, flags, l++);
	}

	return ret;
}

int bch2_trigger_reflink_p(struct btree_trans *trans,
			   enum btree_id btree_id, unsigned level,
			   struct bkey_s_c old,
			   struct bkey_s new,
			   enum btree_iter_update_trigger_flags flags)
{
	if ((flags & BTREE_TRIGGER_transactional) &&
	    (flags & BTREE_TRIGGER_insert)) {
		struct bch_reflink_p *v = bkey_s_to_reflink_p(new).v;

		v->front_pad = v->back_pad = 0;
	}

	return trigger_run_overwrite_then_insert(__trigger_reflink_p, trans, btree_id, level, old, new, flags);
}

/* indirect extents */

int bch2_reflink_v_validate(struct bch_fs *c, struct bkey_s_c k,
			    enum bch_validate_flags flags)
{
	return bch2_bkey_ptrs_validate(c, k, flags);
}

void bch2_reflink_v_to_text(struct printbuf *out, struct bch_fs *c,
			    struct bkey_s_c k)
{
	struct bkey_s_c_reflink_v r = bkey_s_c_to_reflink_v(k);

	prt_printf(out, "refcount: %llu ", le64_to_cpu(r.v->refcount));

	bch2_bkey_ptrs_to_text(out, c, k);
}

#if 0
Currently disabled, needs to be debugged:

bool bch2_reflink_v_merge(struct bch_fs *c, struct bkey_s _l, struct bkey_s_c _r)
{
	struct bkey_s_reflink_v   l = bkey_s_to_reflink_v(_l);
	struct bkey_s_c_reflink_v r = bkey_s_c_to_reflink_v(_r);

	return l.v->refcount == r.v->refcount && bch2_extent_merge(c, _l, _r);
}
#endif

static inline void
check_indirect_extent_deleting(struct bkey_s new,
			       enum btree_iter_update_trigger_flags *flags)
{
	if ((*flags & BTREE_TRIGGER_insert) && !*bkey_refcount(new)) {
		new.k->type = KEY_TYPE_deleted;
		new.k->size = 0;
		set_bkey_val_u64s(new.k, 0);
		*flags &= ~BTREE_TRIGGER_insert;
	}
}

int bch2_trigger_reflink_v(struct btree_trans *trans,
			   enum btree_id btree_id, unsigned level,
			   struct bkey_s_c old, struct bkey_s new,
			   enum btree_iter_update_trigger_flags flags)
{
	if ((flags & BTREE_TRIGGER_transactional) &&
	    (flags & BTREE_TRIGGER_insert))
		check_indirect_extent_deleting(new, &flags);

	return bch2_trigger_extent(trans, btree_id, level, old, new, flags);
}

/* indirect inline data */

int bch2_indirect_inline_data_validate(struct bch_fs *c, struct bkey_s_c k,
				      enum bch_validate_flags flags)
{
	return 0;
}

void bch2_indirect_inline_data_to_text(struct printbuf *out,
				       struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_indirect_inline_data d = bkey_s_c_to_indirect_inline_data(k);
	unsigned datalen = bkey_inline_data_bytes(k.k);

	prt_printf(out, "refcount %llu datalen %u: %*phN",
	       le64_to_cpu(d.v->refcount), datalen,
	       min(datalen, 32U), d.v->data);
}

int bch2_trigger_indirect_inline_data(struct btree_trans *trans,
			      enum btree_id btree_id, unsigned level,
			      struct bkey_s_c old, struct bkey_s new,
			      enum btree_iter_update_trigger_flags flags)
{
	check_indirect_extent_deleting(new, &flags);

	return 0;
}

static int bch2_make_extent_indirect(struct btree_trans *trans,
				     struct btree_iter *extent_iter,
				     struct bkey_i *orig)
{
	struct bch_fs *c = trans->c;
	struct btree_iter reflink_iter = { NULL };
	struct bkey_s_c k;
	struct bkey_i *r_v;
	struct bkey_i_reflink_p *r_p;
	__le64 *refcount;
	int ret;

	if (orig->k.type == KEY_TYPE_inline_data)
		bch2_check_set_feature(c, BCH_FEATURE_reflink_inline_data);

	bch2_trans_iter_init(trans, &reflink_iter, BTREE_ID_reflink, POS_MAX,
			     BTREE_ITER_intent);
	k = bch2_btree_iter_peek_prev(&reflink_iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	r_v = bch2_trans_kmalloc(trans, sizeof(__le64) + bkey_bytes(&orig->k));
	ret = PTR_ERR_OR_ZERO(r_v);
	if (ret)
		goto err;

	bkey_init(&r_v->k);
	r_v->k.type	= bkey_type_to_indirect(&orig->k);
	r_v->k.p	= reflink_iter.pos;
	bch2_key_resize(&r_v->k, orig->k.size);
	r_v->k.version	= orig->k.version;

	set_bkey_val_bytes(&r_v->k, sizeof(__le64) + bkey_val_bytes(&orig->k));

	refcount	= bkey_refcount(bkey_i_to_s(r_v));
	*refcount	= 0;
	memcpy(refcount + 1, &orig->v, bkey_val_bytes(&orig->k));

	ret = bch2_trans_update(trans, &reflink_iter, r_v, 0);
	if (ret)
		goto err;

	/*
	 * orig is in a bkey_buf which statically allocates 5 64s for the val,
	 * so we know it will be big enough:
	 */
	orig->k.type = KEY_TYPE_reflink_p;
	r_p = bkey_i_to_reflink_p(orig);
	set_bkey_val_bytes(&r_p->k, sizeof(r_p->v));

	/* FORTIFY_SOURCE is broken here, and doesn't provide unsafe_memset() */
#if !defined(__NO_FORTIFY) && defined(__OPTIMIZE__) && defined(CONFIG_FORTIFY_SOURCE)
	__underlying_memset(&r_p->v, 0, sizeof(r_p->v));
#else
	memset(&r_p->v, 0, sizeof(r_p->v));
#endif

	r_p->v.idx = cpu_to_le64(bkey_start_offset(&r_v->k));

	ret = bch2_trans_update(trans, extent_iter, &r_p->k_i,
				BTREE_UPDATE_internal_snapshot_node);
err:
	bch2_trans_iter_exit(trans, &reflink_iter);

	return ret;
}

static struct bkey_s_c get_next_src(struct btree_iter *iter, struct bpos end)
{
	struct bkey_s_c k;
	int ret;

	for_each_btree_key_upto_continue_norestart(*iter, end, 0, k, ret) {
		if (bkey_extent_is_unwritten(k))
			continue;

		if (bkey_extent_is_data(k.k))
			return k;
	}

	if (bkey_ge(iter->pos, end))
		bch2_btree_iter_set_pos(iter, end);
	return ret ? bkey_s_c_err(ret) : bkey_s_c_null;
}

s64 bch2_remap_range(struct bch_fs *c,
		     subvol_inum dst_inum, u64 dst_offset,
		     subvol_inum src_inum, u64 src_offset,
		     u64 remap_sectors,
		     u64 new_i_size, s64 *i_sectors_delta)
{
	struct btree_trans *trans;
	struct btree_iter dst_iter, src_iter;
	struct bkey_s_c src_k;
	struct bkey_buf new_dst, new_src;
	struct bpos dst_start = POS(dst_inum.inum, dst_offset);
	struct bpos src_start = POS(src_inum.inum, src_offset);
	struct bpos dst_end = dst_start, src_end = src_start;
	struct bch_io_opts opts;
	struct bpos src_want;
	u64 dst_done = 0;
	u32 dst_snapshot, src_snapshot;
	int ret = 0, ret2 = 0;

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_reflink))
		return -BCH_ERR_erofs_no_writes;

	bch2_check_set_feature(c, BCH_FEATURE_reflink);

	dst_end.offset += remap_sectors;
	src_end.offset += remap_sectors;

	bch2_bkey_buf_init(&new_dst);
	bch2_bkey_buf_init(&new_src);
	trans = bch2_trans_get(c);

	ret = bch2_inum_opts_get(trans, src_inum, &opts);
	if (ret)
		goto err;

	bch2_trans_iter_init(trans, &src_iter, BTREE_ID_extents, src_start,
			     BTREE_ITER_intent);
	bch2_trans_iter_init(trans, &dst_iter, BTREE_ID_extents, dst_start,
			     BTREE_ITER_intent);

	while ((ret == 0 ||
		bch2_err_matches(ret, BCH_ERR_transaction_restart)) &&
	       bkey_lt(dst_iter.pos, dst_end)) {
		struct disk_reservation disk_res = { 0 };

		bch2_trans_begin(trans);

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		ret = bch2_subvolume_get_snapshot(trans, src_inum.subvol,
						  &src_snapshot);
		if (ret)
			continue;

		bch2_btree_iter_set_snapshot(&src_iter, src_snapshot);

		ret = bch2_subvolume_get_snapshot(trans, dst_inum.subvol,
						  &dst_snapshot);
		if (ret)
			continue;

		bch2_btree_iter_set_snapshot(&dst_iter, dst_snapshot);

		if (dst_inum.inum < src_inum.inum) {
			/* Avoid some lock cycle transaction restarts */
			ret = bch2_btree_iter_traverse(&dst_iter);
			if (ret)
				continue;
		}

		dst_done = dst_iter.pos.offset - dst_start.offset;
		src_want = POS(src_start.inode, src_start.offset + dst_done);
		bch2_btree_iter_set_pos(&src_iter, src_want);

		src_k = get_next_src(&src_iter, src_end);
		ret = bkey_err(src_k);
		if (ret)
			continue;

		if (bkey_lt(src_want, src_iter.pos)) {
			ret = bch2_fpunch_at(trans, &dst_iter, dst_inum,
					min(dst_end.offset,
					    dst_iter.pos.offset +
					    src_iter.pos.offset - src_want.offset),
					i_sectors_delta);
			continue;
		}

		if (src_k.k->type != KEY_TYPE_reflink_p) {
			bch2_btree_iter_set_pos_to_extent_start(&src_iter);

			bch2_bkey_buf_reassemble(&new_src, c, src_k);
			src_k = bkey_i_to_s_c(new_src.k);

			ret = bch2_make_extent_indirect(trans, &src_iter,
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

		new_dst.k->k.p = dst_iter.pos;
		bch2_key_resize(&new_dst.k->k,
				min(src_k.k->p.offset - src_want.offset,
				    dst_end.offset - dst_iter.pos.offset));

		ret =   bch2_bkey_set_needs_rebalance(c, new_dst.k, &opts) ?:
			bch2_extent_update(trans, dst_inum, &dst_iter,
					new_dst.k, &disk_res,
					new_i_size, i_sectors_delta,
					true);
		bch2_disk_reservation_put(c, &disk_res);
	}
	bch2_trans_iter_exit(trans, &dst_iter);
	bch2_trans_iter_exit(trans, &src_iter);

	BUG_ON(!ret && !bkey_eq(dst_iter.pos, dst_end));
	BUG_ON(bkey_gt(dst_iter.pos, dst_end));

	dst_done = dst_iter.pos.offset - dst_start.offset;
	new_i_size = min(dst_iter.pos.offset << 9, new_i_size);

	do {
		struct bch_inode_unpacked inode_u;
		struct btree_iter inode_iter = { NULL };

		bch2_trans_begin(trans);

		ret2 = bch2_inode_peek(trans, &inode_iter, &inode_u,
				       dst_inum, BTREE_ITER_intent);

		if (!ret2 &&
		    inode_u.bi_size < new_i_size) {
			inode_u.bi_size = new_i_size;
			ret2  = bch2_inode_write(trans, &inode_iter, &inode_u) ?:
				bch2_trans_commit(trans, NULL, NULL,
						  BCH_TRANS_COMMIT_no_enospc);
		}

		bch2_trans_iter_exit(trans, &inode_iter);
	} while (bch2_err_matches(ret2, BCH_ERR_transaction_restart));
err:
	bch2_trans_put(trans);
	bch2_bkey_buf_exit(&new_src, c);
	bch2_bkey_buf_exit(&new_dst, c);

	bch2_write_ref_put(c, BCH_WRITE_REF_reflink);

	return dst_done ?: ret ?: ret2;
}
