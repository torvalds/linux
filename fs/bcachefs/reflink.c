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

static inline bool bkey_extent_is_reflink_data(const struct bkey *k)
{
	switch (k->type) {
	case KEY_TYPE_reflink_v:
	case KEY_TYPE_indirect_inline_data:
		return true;
	default:
		return false;
	}
}

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
			    struct bkey_validate_context from)
{
	struct bkey_s_c_reflink_p p = bkey_s_c_to_reflink_p(k);
	int ret = 0;

	bkey_fsck_err_on(REFLINK_P_IDX(p.v) < le32_to_cpu(p.v->front_pad),
			 c, reflink_p_front_pad_bad,
			 "idx < front_pad (%llu < %u)",
			 REFLINK_P_IDX(p.v), le32_to_cpu(p.v->front_pad));
fsck_err:
	return ret;
}

void bch2_reflink_p_to_text(struct printbuf *out, struct bch_fs *c,
			    struct bkey_s_c k)
{
	struct bkey_s_c_reflink_p p = bkey_s_c_to_reflink_p(k);

	prt_printf(out, "idx %llu front_pad %u back_pad %u",
	       REFLINK_P_IDX(p.v),
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

	if (REFLINK_P_IDX(l.v) + l.k->size != REFLINK_P_IDX(r.v))
		return false;

	if (REFLINK_P_ERROR(l.v) != REFLINK_P_ERROR(r.v))
		return false;

	bch2_key_resize(l.k, l.k->size + r.k->size);
	return true;
}

/* indirect extents */

int bch2_reflink_v_validate(struct bch_fs *c, struct bkey_s_c k,
			    struct bkey_validate_context from)
{
	int ret = 0;

	bkey_fsck_err_on(bkey_gt(k.k->p, POS(0, REFLINK_P_IDX_MAX)),
			 c, reflink_v_pos_bad,
			 "indirect extent above maximum position 0:%llu",
			 REFLINK_P_IDX_MAX);

	ret = bch2_bkey_ptrs_validate(c, k, from);
fsck_err:
	return ret;
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

/* indirect inline data */

int bch2_indirect_inline_data_validate(struct bch_fs *c, struct bkey_s_c k,
				       struct bkey_validate_context from)
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

/* lookup */

static int bch2_indirect_extent_not_missing(struct btree_trans *trans, struct bkey_s_c_reflink_p p,
					    bool should_commit)
{
	struct bkey_i_reflink_p *new = bch2_bkey_make_mut_noupdate_typed(trans, p.s_c, reflink_p);
	int ret = PTR_ERR_OR_ZERO(new);
	if (ret)
		return ret;

	SET_REFLINK_P_ERROR(&new->v, false);
	ret = bch2_btree_insert_trans(trans, BTREE_ID_extents, &new->k_i, BTREE_TRIGGER_norun);
	if (ret)
		return ret;

	if (!should_commit)
		return 0;

	return bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc) ?:
		-BCH_ERR_transaction_restart_nested;
}

static int bch2_indirect_extent_missing_error(struct btree_trans *trans,
					      struct bkey_s_c_reflink_p p,
					      u64 missing_start, u64 missing_end,
					      bool should_commit)
{
	if (REFLINK_P_ERROR(p.v))
		return 0;

	struct bch_fs *c = trans->c;
	u64 live_start	= REFLINK_P_IDX(p.v);
	u64 live_end	= REFLINK_P_IDX(p.v) + p.k->size;
	u64 refd_start	= live_start	- le32_to_cpu(p.v->front_pad);
	u64 refd_end	= live_end	+ le32_to_cpu(p.v->back_pad);
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	BUG_ON(missing_start	< refd_start);
	BUG_ON(missing_end	> refd_end);

	struct bpos missing_pos = bkey_start_pos(p.k);
	missing_pos.offset += missing_start - live_start;

	prt_printf(&buf, "pointer to missing indirect extent in ");
	ret = bch2_inum_snap_offset_err_msg_trans(trans, &buf, missing_pos);
	if (ret)
		goto err;

	prt_printf(&buf, "-%llu\n", (missing_pos.offset + (missing_end - missing_start)) << 9);
	bch2_bkey_val_to_text(&buf, c, p.s_c);

	prt_printf(&buf, "\nmissing reflink btree range %llu-%llu",
		   missing_start, missing_end);

	if (fsck_err(trans, reflink_p_to_missing_reflink_v, "%s", buf.buf)) {
		struct bkey_i_reflink_p *new = bch2_bkey_make_mut_noupdate_typed(trans, p.s_c, reflink_p);
		ret = PTR_ERR_OR_ZERO(new);
		if (ret)
			goto err;

		/*
		 * Is the missing range not actually needed?
		 *
		 * p.v->idx refers to the data that we actually want, but if the
		 * indirect extent we point to was bigger, front_pad and back_pad
		 * indicate the range we took a reference on.
		 */

		if (missing_end <= live_start) {
			new->v.front_pad = cpu_to_le32(live_start - missing_end);
		} else if (missing_start >= live_end) {
			new->v.back_pad = cpu_to_le32(missing_start - live_end);
		} else {
			struct bpos new_start	= bkey_start_pos(&new->k);
			struct bpos new_end	= new->k.p;

			if (missing_start > live_start)
				new_start.offset += missing_start - live_start;
			if (missing_end < live_end)
				new_end.offset -= live_end - missing_end;

			bch2_cut_front(new_start, &new->k_i);
			bch2_cut_back(new_end, &new->k_i);

			SET_REFLINK_P_ERROR(&new->v, true);
		}

		ret = bch2_btree_insert_trans(trans, BTREE_ID_extents, &new->k_i, BTREE_TRIGGER_norun);
		if (ret)
			goto err;

		if (should_commit)
			ret =   bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc) ?:
				-BCH_ERR_transaction_restart_nested;
	}
err:
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

/*
 * This is used from the read path, which doesn't expect to have to do a
 * transaction commit, and from triggers, which should not be doing a commit:
 */
struct bkey_s_c bch2_lookup_indirect_extent(struct btree_trans *trans,
					    struct btree_iter *iter,
					    s64 *offset_into_extent,
					    struct bkey_s_c_reflink_p p,
					    bool should_commit,
					    unsigned iter_flags)
{
	BUG_ON(*offset_into_extent < -((s64) le32_to_cpu(p.v->front_pad)));
	BUG_ON(*offset_into_extent >= p.k->size + le32_to_cpu(p.v->back_pad));

	u64 reflink_offset = REFLINK_P_IDX(p.v) + *offset_into_extent;

	struct bkey_s_c k = bch2_bkey_get_iter(trans, iter, BTREE_ID_reflink,
				       POS(0, reflink_offset), iter_flags);
	if (bkey_err(k))
		return k;

	if (unlikely(!bkey_extent_is_reflink_data(k.k))) {
		unsigned size = min((u64) k.k->size,
				    REFLINK_P_IDX(p.v) + p.k->size + le32_to_cpu(p.v->back_pad) -
				    reflink_offset);
		bch2_key_resize(&iter->k, size);

		int ret = bch2_indirect_extent_missing_error(trans, p, reflink_offset,
							     k.k->p.offset, should_commit);
		if (ret) {
			bch2_trans_iter_exit(trans, iter);
			return bkey_s_c_err(ret);
		}
	} else if (unlikely(REFLINK_P_ERROR(p.v))) {
		int ret = bch2_indirect_extent_not_missing(trans, p, should_commit);
		if (ret) {
			bch2_trans_iter_exit(trans, iter);
			return bkey_s_c_err(ret);
		}
	}

	*offset_into_extent = reflink_offset - bkey_start_offset(k.k);
	return k;
}

/* reflink pointer trigger */

static int trans_trigger_reflink_p_segment(struct btree_trans *trans,
			struct bkey_s_c_reflink_p p, u64 *idx,
			enum btree_iter_update_trigger_flags flags)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;

	s64 offset_into_extent = *idx - REFLINK_P_IDX(p.v);
	struct btree_iter iter;
	struct bkey_s_c k = bch2_lookup_indirect_extent(trans, &iter, &offset_into_extent, p, false,
							BTREE_ITER_intent|
							BTREE_ITER_with_updates);
	int ret = bkey_err(k);
	if (ret)
		return ret;

	if (!bkey_refcount_c(k)) {
		if (!(flags & BTREE_TRIGGER_overwrite))
			ret = -BCH_ERR_missing_indirect_extent;
		goto next;
	}

	struct bkey_i *new = bch2_bkey_make_mut_noupdate(trans, k);
	ret = PTR_ERR_OR_ZERO(new);
	if (ret)
		goto err;

	__le64 *refcount = bkey_refcount(bkey_i_to_s(new));
	if (!*refcount && (flags & BTREE_TRIGGER_overwrite)) {
		bch2_bkey_val_to_text(&buf, c, p.s_c);
		prt_newline(&buf);
		bch2_bkey_val_to_text(&buf, c, k);
		log_fsck_err(trans, reflink_refcount_underflow,
			     "indirect extent refcount underflow while marking\n%s",
			   buf.buf);
		goto next;
	}

	if (flags & BTREE_TRIGGER_insert) {
		struct bch_reflink_p *v = (struct bch_reflink_p *) p.v;
		u64 pad;

		pad = max_t(s64, le32_to_cpu(v->front_pad),
			    REFLINK_P_IDX(v) - bkey_start_offset(&new->k));
		BUG_ON(pad > U32_MAX);
		v->front_pad = cpu_to_le32(pad);

		pad = max_t(s64, le32_to_cpu(v->back_pad),
			    new->k.p.offset - p.k->size - REFLINK_P_IDX(v));
		BUG_ON(pad > U32_MAX);
		v->back_pad = cpu_to_le32(pad);
	}

	le64_add_cpu(refcount, !(flags & BTREE_TRIGGER_overwrite) ? 1 : -1);

	bch2_btree_iter_set_pos_to_extent_start(&iter);
	ret = bch2_trans_update(trans, &iter, new, 0);
	if (ret)
		goto err;
next:
	*idx = k.k->p.offset;
err:
fsck_err:
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
	u64 next_idx = REFLINK_P_IDX(p.v) + p.k->size + le32_to_cpu(p.v->back_pad);
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
	if (flags & BTREE_TRIGGER_check_repair) {
		ret = bch2_indirect_extent_missing_error(trans, p, *idx, next_idx, false);
		if (ret)
			goto err;
	}

	*idx = next_idx;
err:
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

	u64 idx = REFLINK_P_IDX(p.v) - le32_to_cpu(p.v->front_pad);
	u64 end = REFLINK_P_IDX(p.v) + p.k->size + le32_to_cpu(p.v->back_pad);

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

/* indirect extent trigger */

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

int bch2_trigger_indirect_inline_data(struct btree_trans *trans,
			      enum btree_id btree_id, unsigned level,
			      struct bkey_s_c old, struct bkey_s new,
			      enum btree_iter_update_trigger_flags flags)
{
	check_indirect_extent_deleting(new, &flags);

	return 0;
}

/* create */

static int bch2_make_extent_indirect(struct btree_trans *trans,
				     struct btree_iter *extent_iter,
				     struct bkey_i *orig,
				     bool reflink_p_may_update_opts_field)
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

	/*
	 * XXX: we're assuming that 56 bits will be enough for the life of the
	 * filesystem: we need to implement wraparound, with a cursor in the
	 * logged ops btree:
	 */
	if (bkey_ge(reflink_iter.pos, POS(0, REFLINK_P_IDX_MAX - orig->k.size)))
		return -ENOSPC;

	r_v = bch2_trans_kmalloc(trans, sizeof(__le64) + bkey_bytes(&orig->k));
	ret = PTR_ERR_OR_ZERO(r_v);
	if (ret)
		goto err;

	bkey_init(&r_v->k);
	r_v->k.type	= bkey_type_to_indirect(&orig->k);
	r_v->k.p	= reflink_iter.pos;
	bch2_key_resize(&r_v->k, orig->k.size);
	r_v->k.bversion	= orig->k.bversion;

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

	SET_REFLINK_P_IDX(&r_p->v, bkey_start_offset(&r_v->k));

	if (reflink_p_may_update_opts_field)
		SET_REFLINK_P_MAY_UPDATE_OPTIONS(&r_p->v, true);

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

	for_each_btree_key_max_continue_norestart(*iter, end, 0, k, ret) {
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
		     u64 new_i_size, s64 *i_sectors_delta,
		     bool may_change_src_io_path_opts)
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
	bool reflink_p_may_update_opts_field =
		!bch2_request_incompat_feature(c, bcachefs_metadata_version_reflink_p_may_update_opts);
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
						new_src.k,
						reflink_p_may_update_opts_field);
			if (ret)
				continue;

			BUG_ON(src_k.k->type != KEY_TYPE_reflink_p);
		}

		if (src_k.k->type == KEY_TYPE_reflink_p) {
			struct bkey_s_c_reflink_p src_p =
				bkey_s_c_to_reflink_p(src_k);
			struct bkey_i_reflink_p *dst_p =
				bkey_reflink_p_init(new_dst.k);

			u64 offset = REFLINK_P_IDX(src_p.v) +
				(src_want.offset -
				 bkey_start_offset(src_k.k));

			SET_REFLINK_P_IDX(&dst_p->v, offset);

			if (reflink_p_may_update_opts_field &&
			    may_change_src_io_path_opts)
				SET_REFLINK_P_MAY_UPDATE_OPTIONS(&dst_p->v, true);
		} else {
			BUG();
		}

		new_dst.k->k.p = dst_iter.pos;
		bch2_key_resize(&new_dst.k->k,
				min(src_k.k->p.offset - src_want.offset,
				    dst_end.offset - dst_iter.pos.offset));

		ret =   bch2_bkey_set_needs_rebalance(c, &opts, new_dst.k) ?:
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

/* fsck */

static int bch2_gc_write_reflink_key(struct btree_trans *trans,
				     struct btree_iter *iter,
				     struct bkey_s_c k,
				     size_t *idx)
{
	struct bch_fs *c = trans->c;
	const __le64 *refcount = bkey_refcount_c(k);
	struct printbuf buf = PRINTBUF;
	struct reflink_gc *r;
	int ret = 0;

	if (!refcount)
		return 0;

	while ((r = genradix_ptr(&c->reflink_gc_table, *idx)) &&
	       r->offset < k.k->p.offset)
		++*idx;

	if (!r ||
	    r->offset != k.k->p.offset ||
	    r->size != k.k->size) {
		bch_err(c, "unexpected inconsistency walking reflink table at gc finish");
		return -EINVAL;
	}

	if (fsck_err_on(r->refcount != le64_to_cpu(*refcount),
			trans, reflink_v_refcount_wrong,
			"reflink key has wrong refcount:\n"
			"%s\n"
			"should be %u",
			(bch2_bkey_val_to_text(&buf, c, k), buf.buf),
			r->refcount)) {
		struct bkey_i *new = bch2_bkey_make_mut_noupdate(trans, k);
		ret = PTR_ERR_OR_ZERO(new);
		if (ret)
			goto out;

		if (!r->refcount)
			new->k.type = KEY_TYPE_deleted;
		else
			*bkey_refcount(bkey_i_to_s(new)) = cpu_to_le64(r->refcount);
		ret = bch2_trans_update(trans, iter, new, 0);
	}
out:
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

int bch2_gc_reflink_done(struct bch_fs *c)
{
	size_t idx = 0;

	int ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter,
				BTREE_ID_reflink, POS_MIN,
				BTREE_ITER_prefetch, k,
				NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			bch2_gc_write_reflink_key(trans, &iter, k, &idx)));
	c->reflink_gc_nr = 0;
	return ret;
}

int bch2_gc_reflink_start(struct bch_fs *c)
{
	c->reflink_gc_nr = 0;

	int ret = bch2_trans_run(c,
		for_each_btree_key(trans, iter, BTREE_ID_reflink, POS_MIN,
				   BTREE_ITER_prefetch, k, ({
			const __le64 *refcount = bkey_refcount_c(k);

			if (!refcount)
				continue;

			struct reflink_gc *r = genradix_ptr_alloc(&c->reflink_gc_table,
							c->reflink_gc_nr++, GFP_KERNEL);
			if (!r) {
				ret = -BCH_ERR_ENOMEM_gc_reflink_start;
				break;
			}

			r->offset	= k.k->p.offset;
			r->size		= k.k->size;
			r->refcount	= 0;
			0;
		})));

	bch_err_fn(c, ret);
	return ret;
}
