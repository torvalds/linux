// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "bbpos.h"
#include "alloc_background.h"
#include "backpointers.h"
#include "bkey_buf.h"
#include "btree_cache.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_write_buffer.h"
#include "error.h"

#include <linux/mm.h>

static bool extent_matches_bp(struct bch_fs *c,
			      enum btree_id btree_id, unsigned level,
			      struct bkey_s_c k,
			      struct bpos bucket,
			      struct bch_backpointer bp)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		struct bpos bucket2;
		struct bch_backpointer bp2;

		if (p.ptr.cached)
			continue;

		bch2_extent_ptr_to_bp(c, btree_id, level, k, p,
				      &bucket2, &bp2);
		if (bpos_eq(bucket, bucket2) &&
		    !memcmp(&bp, &bp2, sizeof(bp)))
			return true;
	}

	return false;
}

int bch2_backpointer_invalid(struct bch_fs *c, struct bkey_s_c k,
			     enum bkey_invalid_flags flags,
			     struct printbuf *err)
{
	struct bkey_s_c_backpointer bp = bkey_s_c_to_backpointer(k);
	struct bpos bucket = bp_pos_to_bucket(c, bp.k->p);
	int ret = 0;

	bkey_fsck_err_on(!bpos_eq(bp.k->p, bucket_pos_to_bp(c, bucket, bp.v->bucket_offset)),
			 c, err,
			 backpointer_pos_wrong,
			 "backpointer at wrong pos");
fsck_err:
	return ret;
}

void bch2_backpointer_to_text(struct printbuf *out, const struct bch_backpointer *bp)
{
	prt_printf(out, "btree=%s l=%u offset=%llu:%u len=%u pos=",
	       bch2_btree_id_str(bp->btree_id),
	       bp->level,
	       (u64) (bp->bucket_offset >> MAX_EXTENT_COMPRESS_RATIO_SHIFT),
	       (u32) bp->bucket_offset & ~(~0U << MAX_EXTENT_COMPRESS_RATIO_SHIFT),
	       bp->bucket_len);
	bch2_bpos_to_text(out, bp->pos);
}

void bch2_backpointer_k_to_text(struct printbuf *out, struct bch_fs *c, struct bkey_s_c k)
{
	if (bch2_dev_exists2(c, k.k->p.inode)) {
		prt_str(out, "bucket=");
		bch2_bpos_to_text(out, bp_pos_to_bucket(c, k.k->p));
		prt_str(out, " ");
	}

	bch2_backpointer_to_text(out, bkey_s_c_to_backpointer(k).v);
}

void bch2_backpointer_swab(struct bkey_s k)
{
	struct bkey_s_backpointer bp = bkey_s_to_backpointer(k);

	bp.v->bucket_offset	= swab40(bp.v->bucket_offset);
	bp.v->bucket_len	= swab32(bp.v->bucket_len);
	bch2_bpos_swab(&bp.v->pos);
}

static noinline int backpointer_mod_err(struct btree_trans *trans,
					struct bch_backpointer bp,
					struct bkey_s_c bp_k,
					struct bkey_s_c orig_k,
					bool insert)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;

	if (insert) {
		prt_printf(&buf, "existing backpointer found when inserting ");
		bch2_backpointer_to_text(&buf, &bp);
		prt_newline(&buf);
		printbuf_indent_add(&buf, 2);

		prt_printf(&buf, "found ");
		bch2_bkey_val_to_text(&buf, c, bp_k);
		prt_newline(&buf);

		prt_printf(&buf, "for ");
		bch2_bkey_val_to_text(&buf, c, orig_k);

		bch_err(c, "%s", buf.buf);
	} else if (c->curr_recovery_pass > BCH_RECOVERY_PASS_check_extents_to_backpointers) {
		prt_printf(&buf, "backpointer not found when deleting");
		prt_newline(&buf);
		printbuf_indent_add(&buf, 2);

		prt_printf(&buf, "searching for ");
		bch2_backpointer_to_text(&buf, &bp);
		prt_newline(&buf);

		prt_printf(&buf, "got ");
		bch2_bkey_val_to_text(&buf, c, bp_k);
		prt_newline(&buf);

		prt_printf(&buf, "for ");
		bch2_bkey_val_to_text(&buf, c, orig_k);

		bch_err(c, "%s", buf.buf);
	}

	printbuf_exit(&buf);

	if (c->curr_recovery_pass > BCH_RECOVERY_PASS_check_extents_to_backpointers) {
		bch2_inconsistent_error(c);
		return -EIO;
	} else {
		return 0;
	}
}

int bch2_bucket_backpointer_mod_nowritebuffer(struct btree_trans *trans,
				struct bpos bucket,
				struct bch_backpointer bp,
				struct bkey_s_c orig_k,
				bool insert)
{
	struct btree_iter bp_iter;
	struct bkey_s_c k;
	struct bkey_i_backpointer *bp_k;
	int ret;

	bp_k = bch2_trans_kmalloc_nomemzero(trans, sizeof(struct bkey_i_backpointer));
	ret = PTR_ERR_OR_ZERO(bp_k);
	if (ret)
		return ret;

	bkey_backpointer_init(&bp_k->k_i);
	bp_k->k.p = bucket_pos_to_bp(trans->c, bucket, bp.bucket_offset);
	bp_k->v = bp;

	if (!insert) {
		bp_k->k.type = KEY_TYPE_deleted;
		set_bkey_val_u64s(&bp_k->k, 0);
	}

	k = bch2_bkey_get_iter(trans, &bp_iter, BTREE_ID_backpointers,
			       bp_k->k.p,
			       BTREE_ITER_INTENT|
			       BTREE_ITER_SLOTS|
			       BTREE_ITER_WITH_UPDATES);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (insert
	    ? k.k->type
	    : (k.k->type != KEY_TYPE_backpointer ||
	       memcmp(bkey_s_c_to_backpointer(k).v, &bp, sizeof(bp)))) {
		ret = backpointer_mod_err(trans, bp, k, orig_k, insert);
		if (ret)
			goto err;
	}

	ret = bch2_trans_update(trans, &bp_iter, &bp_k->k_i, 0);
err:
	bch2_trans_iter_exit(trans, &bp_iter);
	return ret;
}

/*
 * Find the next backpointer >= *bp_offset:
 */
int bch2_get_next_backpointer(struct btree_trans *trans,
			      struct bpos bucket, int gen,
			      struct bpos *bp_pos,
			      struct bch_backpointer *bp,
			      unsigned iter_flags)
{
	struct bch_fs *c = trans->c;
	struct bpos bp_end_pos = bucket_pos_to_bp(c, bpos_nosnap_successor(bucket), 0);
	struct btree_iter alloc_iter = { NULL }, bp_iter = { NULL };
	struct bkey_s_c k;
	int ret = 0;

	if (bpos_ge(*bp_pos, bp_end_pos))
		goto done;

	if (gen >= 0) {
		k = bch2_bkey_get_iter(trans, &alloc_iter, BTREE_ID_alloc,
				       bucket, BTREE_ITER_CACHED|iter_flags);
		ret = bkey_err(k);
		if (ret)
			goto out;

		if (k.k->type != KEY_TYPE_alloc_v4 ||
		    bkey_s_c_to_alloc_v4(k).v->gen != gen)
			goto done;
	}

	*bp_pos = bpos_max(*bp_pos, bucket_pos_to_bp(c, bucket, 0));

	for_each_btree_key_norestart(trans, bp_iter, BTREE_ID_backpointers,
				     *bp_pos, iter_flags, k, ret) {
		if (bpos_ge(k.k->p, bp_end_pos))
			break;

		*bp_pos = k.k->p;
		*bp = *bkey_s_c_to_backpointer(k).v;
		goto out;
	}
done:
	*bp_pos = SPOS_MAX;
out:
	bch2_trans_iter_exit(trans, &bp_iter);
	bch2_trans_iter_exit(trans, &alloc_iter);
	return ret;
}

static void backpointer_not_found(struct btree_trans *trans,
				  struct bpos bp_pos,
				  struct bch_backpointer bp,
				  struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	struct bpos bucket = bp_pos_to_bucket(c, bp_pos);

	/*
	 * If we're using the btree write buffer, the backpointer we were
	 * looking at may have already been deleted - failure to find what it
	 * pointed to is not an error:
	 */
	if (likely(!bch2_backpointers_no_use_write_buffer))
		return;

	prt_printf(&buf, "backpointer doesn't match %s it points to:\n  ",
		   bp.level ? "btree node" : "extent");
	prt_printf(&buf, "bucket: ");
	bch2_bpos_to_text(&buf, bucket);
	prt_printf(&buf, "\n  ");

	prt_printf(&buf, "backpointer pos: ");
	bch2_bpos_to_text(&buf, bp_pos);
	prt_printf(&buf, "\n  ");

	bch2_backpointer_to_text(&buf, &bp);
	prt_printf(&buf, "\n  ");
	bch2_bkey_val_to_text(&buf, c, k);
	if (c->curr_recovery_pass >= BCH_RECOVERY_PASS_check_extents_to_backpointers)
		bch_err_ratelimited(c, "%s", buf.buf);
	else
		bch2_trans_inconsistent(trans, "%s", buf.buf);

	printbuf_exit(&buf);
}

struct bkey_s_c bch2_backpointer_get_key(struct btree_trans *trans,
					 struct btree_iter *iter,
					 struct bpos bp_pos,
					 struct bch_backpointer bp,
					 unsigned iter_flags)
{
	if (likely(!bp.level)) {
		struct bch_fs *c = trans->c;
		struct bpos bucket = bp_pos_to_bucket(c, bp_pos);
		struct bkey_s_c k;

		bch2_trans_node_iter_init(trans, iter,
					  bp.btree_id,
					  bp.pos,
					  0, 0,
					  iter_flags);
		k = bch2_btree_iter_peek_slot(iter);
		if (bkey_err(k)) {
			bch2_trans_iter_exit(trans, iter);
			return k;
		}

		if (k.k && extent_matches_bp(c, bp.btree_id, bp.level, k, bucket, bp))
			return k;

		bch2_trans_iter_exit(trans, iter);
		backpointer_not_found(trans, bp_pos, bp, k);
		return bkey_s_c_null;
	} else {
		struct btree *b = bch2_backpointer_get_node(trans, iter, bp_pos, bp);

		if (IS_ERR_OR_NULL(b)) {
			bch2_trans_iter_exit(trans, iter);
			return IS_ERR(b) ? bkey_s_c_err(PTR_ERR(b)) : bkey_s_c_null;
		}
		return bkey_i_to_s_c(&b->key);
	}
}

struct btree *bch2_backpointer_get_node(struct btree_trans *trans,
					struct btree_iter *iter,
					struct bpos bp_pos,
					struct bch_backpointer bp)
{
	struct bch_fs *c = trans->c;
	struct bpos bucket = bp_pos_to_bucket(c, bp_pos);
	struct btree *b;

	BUG_ON(!bp.level);

	bch2_trans_node_iter_init(trans, iter,
				  bp.btree_id,
				  bp.pos,
				  0,
				  bp.level - 1,
				  0);
	b = bch2_btree_iter_peek_node(iter);
	if (IS_ERR_OR_NULL(b))
		goto err;

	BUG_ON(b->c.level != bp.level - 1);

	if (extent_matches_bp(c, bp.btree_id, bp.level,
			      bkey_i_to_s_c(&b->key),
			      bucket, bp))
		return b;

	if (btree_node_will_make_reachable(b)) {
		b = ERR_PTR(-BCH_ERR_backpointer_to_overwritten_btree_node);
	} else {
		backpointer_not_found(trans, bp_pos, bp, bkey_i_to_s_c(&b->key));
		b = NULL;
	}
err:
	bch2_trans_iter_exit(trans, iter);
	return b;
}

static int bch2_check_btree_backpointer(struct btree_trans *trans, struct btree_iter *bp_iter,
					struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct btree_iter alloc_iter = { NULL };
	struct bkey_s_c alloc_k;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	if (fsck_err_on(!bch2_dev_exists2(c, k.k->p.inode), c,
			backpointer_to_missing_device,
			"backpointer for missing device:\n%s",
			(bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		ret = bch2_btree_delete_at(trans, bp_iter, 0);
		goto out;
	}

	alloc_k = bch2_bkey_get_iter(trans, &alloc_iter, BTREE_ID_alloc,
				     bp_pos_to_bucket(c, k.k->p), 0);
	ret = bkey_err(alloc_k);
	if (ret)
		goto out;

	if (fsck_err_on(alloc_k.k->type != KEY_TYPE_alloc_v4, c,
			backpointer_to_missing_alloc,
			"backpointer for nonexistent alloc key: %llu:%llu:0\n%s",
			alloc_iter.pos.inode, alloc_iter.pos.offset,
			(bch2_bkey_val_to_text(&buf, c, alloc_k), buf.buf))) {
		ret = bch2_btree_delete_at(trans, bp_iter, 0);
		goto out;
	}
out:
fsck_err:
	bch2_trans_iter_exit(trans, &alloc_iter);
	printbuf_exit(&buf);
	return ret;
}

/* verify that every backpointer has a corresponding alloc key */
int bch2_check_btree_backpointers(struct bch_fs *c)
{
	int ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter,
			BTREE_ID_backpointers, POS_MIN, 0, k,
			NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
		  bch2_check_btree_backpointer(trans, &iter, k)));
	bch_err_fn(c, ret);
	return ret;
}

static inline bool bkey_and_val_eq(struct bkey_s_c l, struct bkey_s_c r)
{
	return bpos_eq(l.k->p, r.k->p) &&
		bkey_bytes(l.k) == bkey_bytes(r.k) &&
		!memcmp(l.v, r.v, bkey_val_bytes(l.k));
}

struct extents_to_bp_state {
	struct bpos	bucket_start;
	struct bpos	bucket_end;
	struct bkey_buf last_flushed;
};

static int check_bp_exists(struct btree_trans *trans,
			   struct extents_to_bp_state *s,
			   struct bpos bucket,
			   struct bch_backpointer bp,
			   struct bkey_s_c orig_k)
{
	struct bch_fs *c = trans->c;
	struct btree_iter bp_iter = { NULL };
	struct printbuf buf = PRINTBUF;
	struct bkey_s_c bp_k;
	struct bkey_buf tmp;
	int ret;

	bch2_bkey_buf_init(&tmp);

	if (bpos_lt(bucket, s->bucket_start) ||
	    bpos_gt(bucket, s->bucket_end))
		return 0;

	if (!bch2_dev_bucket_exists(c, bucket))
		goto missing;

	bp_k = bch2_bkey_get_iter(trans, &bp_iter, BTREE_ID_backpointers,
				  bucket_pos_to_bp(c, bucket, bp.bucket_offset),
				  0);
	ret = bkey_err(bp_k);
	if (ret)
		goto err;

	if (bp_k.k->type != KEY_TYPE_backpointer ||
	    memcmp(bkey_s_c_to_backpointer(bp_k).v, &bp, sizeof(bp))) {
		bch2_bkey_buf_reassemble(&tmp, c, orig_k);

		if (!bkey_and_val_eq(orig_k, bkey_i_to_s_c(s->last_flushed.k))) {
			if (bp.level) {
				bch2_trans_unlock(trans);
				bch2_btree_interior_updates_flush(c);
			}

			ret = bch2_btree_write_buffer_flush_sync(trans);
			if (ret)
				goto err;

			bch2_bkey_buf_copy(&s->last_flushed, c, tmp.k);
			ret = -BCH_ERR_transaction_restart_write_buffer_flush;
			goto out;
		}
		goto missing;
	}
out:
err:
fsck_err:
	bch2_trans_iter_exit(trans, &bp_iter);
	bch2_bkey_buf_exit(&tmp, c);
	printbuf_exit(&buf);
	return ret;
missing:
	prt_printf(&buf, "missing backpointer for btree=%s l=%u ",
	       bch2_btree_id_str(bp.btree_id), bp.level);
	bch2_bkey_val_to_text(&buf, c, orig_k);
	prt_printf(&buf, "\nbp pos ");
	bch2_bpos_to_text(&buf, bp_iter.pos);

	if (c->opts.reconstruct_alloc ||
	    fsck_err(c, ptr_to_missing_backpointer, "%s", buf.buf))
		ret = bch2_bucket_backpointer_mod(trans, bucket, bp, orig_k, true);

	goto out;
}

static int check_extent_to_backpointers(struct btree_trans *trans,
					struct extents_to_bp_state *s,
					enum btree_id btree, unsigned level,
					struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct bkey_ptrs_c ptrs;
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	int ret;

	ptrs = bch2_bkey_ptrs_c(k);
	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		struct bpos bucket_pos;
		struct bch_backpointer bp;

		if (p.ptr.cached)
			continue;

		bch2_extent_ptr_to_bp(c, btree, level,
				      k, p, &bucket_pos, &bp);

		ret = check_bp_exists(trans, s, bucket_pos, bp, k);
		if (ret)
			return ret;
	}

	return 0;
}

static int check_btree_root_to_backpointers(struct btree_trans *trans,
					    struct extents_to_bp_state *s,
					    enum btree_id btree_id,
					    int *level)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct btree *b;
	struct bkey_s_c k;
	int ret;
retry:
	bch2_trans_node_iter_init(trans, &iter, btree_id, POS_MIN,
				  0, bch2_btree_id_root(c, btree_id)->b->c.level, 0);
	b = bch2_btree_iter_peek_node(&iter);
	ret = PTR_ERR_OR_ZERO(b);
	if (ret)
		goto err;

	if (b != btree_node_root(c, b)) {
		bch2_trans_iter_exit(trans, &iter);
		goto retry;
	}

	*level = b->c.level;

	k = bkey_i_to_s_c(&b->key);
	ret = check_extent_to_backpointers(trans, s, btree_id, b->c.level + 1, k);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static inline struct bbpos bp_to_bbpos(struct bch_backpointer bp)
{
	return (struct bbpos) {
		.btree	= bp.btree_id,
		.pos	= bp.pos,
	};
}

static size_t btree_nodes_fit_in_ram(struct bch_fs *c)
{
	struct sysinfo i;
	u64 mem_bytes;

	si_meminfo(&i);
	mem_bytes = i.totalram * i.mem_unit;
	return div_u64(mem_bytes >> 1, c->opts.btree_node_size);
}

static int bch2_get_btree_in_memory_pos(struct btree_trans *trans,
					unsigned btree_leaf_mask,
					unsigned btree_interior_mask,
					struct bbpos start, struct bbpos *end)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	size_t btree_nodes = btree_nodes_fit_in_ram(trans->c);
	enum btree_id btree;
	int ret = 0;

	for (btree = start.btree; btree < BTREE_ID_NR && !ret; btree++) {
		unsigned depth = ((1U << btree) & btree_leaf_mask) ? 1 : 2;

		if (!((1U << btree) & btree_leaf_mask) &&
		    !((1U << btree) & btree_interior_mask))
			continue;

		bch2_trans_node_iter_init(trans, &iter, btree,
					  btree == start.btree ? start.pos : POS_MIN,
					  0, depth, 0);
		/*
		 * for_each_btree_key_contineu() doesn't check the return value
		 * from bch2_btree_iter_advance(), which is needed when
		 * iterating over interior nodes where we'll see keys at
		 * SPOS_MAX:
		 */
		do {
			k = __bch2_btree_iter_peek_and_restart(trans, &iter, 0);
			ret = bkey_err(k);
			if (!k.k || ret)
				break;

			--btree_nodes;
			if (!btree_nodes) {
				*end = BBPOS(btree, k.k->p);
				bch2_trans_iter_exit(trans, &iter);
				return 0;
			}
		} while (bch2_btree_iter_advance(&iter));
		bch2_trans_iter_exit(trans, &iter);
	}

	*end = BBPOS_MAX;
	return ret;
}

static int bch2_check_extents_to_backpointers_pass(struct btree_trans *trans,
						   struct extents_to_bp_state *s)
{
	struct bch_fs *c = trans->c;
	int ret = 0;

	for (enum btree_id btree_id = 0;
	     btree_id < btree_id_nr_alive(c);
	     btree_id++) {
		int level, depth = btree_type_has_ptrs(btree_id) ? 0 : 1;

		ret = commit_do(trans, NULL, NULL,
				BCH_TRANS_COMMIT_no_enospc,
				check_btree_root_to_backpointers(trans, s, btree_id, &level));
		if (ret)
			return ret;

		while (level >= depth) {
			struct btree_iter iter;
			bch2_trans_node_iter_init(trans, &iter, btree_id, POS_MIN, 0,
						  level,
						  BTREE_ITER_PREFETCH);
			while (1) {
				bch2_trans_begin(trans);

				struct bkey_s_c k = bch2_btree_iter_peek(&iter);
				if (!k.k)
					break;
				ret = bkey_err(k) ?:
					check_extent_to_backpointers(trans, s, btree_id, level, k) ?:
					bch2_trans_commit(trans, NULL, NULL,
							  BCH_TRANS_COMMIT_no_enospc);
				if (bch2_err_matches(ret, BCH_ERR_transaction_restart)) {
					ret = 0;
					continue;
				}
				if (ret)
					break;
				if (bpos_eq(iter.pos, SPOS_MAX))
					break;
				bch2_btree_iter_advance(&iter);
			}
			bch2_trans_iter_exit(trans, &iter);

			if (ret)
				return ret;

			--level;
		}
	}

	return 0;
}

static struct bpos bucket_pos_to_bp_safe(const struct bch_fs *c,
					 struct bpos bucket)
{
	return bch2_dev_exists2(c, bucket.inode)
		? bucket_pos_to_bp(c, bucket, 0)
		: bucket;
}

static int bch2_get_alloc_in_memory_pos(struct btree_trans *trans,
					struct bpos start, struct bpos *end)
{
	struct btree_iter alloc_iter;
	struct btree_iter bp_iter;
	struct bkey_s_c alloc_k, bp_k;
	size_t btree_nodes = btree_nodes_fit_in_ram(trans->c);
	bool alloc_end = false, bp_end = false;
	int ret = 0;

	bch2_trans_node_iter_init(trans, &alloc_iter, BTREE_ID_alloc,
				  start, 0, 1, 0);
	bch2_trans_node_iter_init(trans, &bp_iter, BTREE_ID_backpointers,
				  bucket_pos_to_bp_safe(trans->c, start), 0, 1, 0);
	while (1) {
		alloc_k = !alloc_end
			? __bch2_btree_iter_peek_and_restart(trans, &alloc_iter, 0)
			: bkey_s_c_null;
		bp_k = !bp_end
			? __bch2_btree_iter_peek_and_restart(trans, &bp_iter, 0)
			: bkey_s_c_null;

		ret = bkey_err(alloc_k) ?: bkey_err(bp_k);
		if ((!alloc_k.k && !bp_k.k) || ret) {
			*end = SPOS_MAX;
			break;
		}

		--btree_nodes;
		if (!btree_nodes) {
			*end = alloc_k.k ? alloc_k.k->p : SPOS_MAX;
			break;
		}

		if (bpos_lt(alloc_iter.pos, SPOS_MAX) &&
		    bpos_lt(bucket_pos_to_bp_safe(trans->c, alloc_iter.pos), bp_iter.pos)) {
			if (!bch2_btree_iter_advance(&alloc_iter))
				alloc_end = true;
		} else {
			if (!bch2_btree_iter_advance(&bp_iter))
				bp_end = true;
		}
	}
	bch2_trans_iter_exit(trans, &bp_iter);
	bch2_trans_iter_exit(trans, &alloc_iter);
	return ret;
}

int bch2_check_extents_to_backpointers(struct bch_fs *c)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct extents_to_bp_state s = { .bucket_start = POS_MIN };
	int ret;

	bch2_bkey_buf_init(&s.last_flushed);
	bkey_init(&s.last_flushed.k->k);

	while (1) {
		ret = bch2_get_alloc_in_memory_pos(trans, s.bucket_start, &s.bucket_end);
		if (ret)
			break;

		if ( bpos_eq(s.bucket_start, POS_MIN) &&
		    !bpos_eq(s.bucket_end, SPOS_MAX))
			bch_verbose(c, "%s(): alloc info does not fit in ram, running in multiple passes with %zu nodes per pass",
				    __func__, btree_nodes_fit_in_ram(c));

		if (!bpos_eq(s.bucket_start, POS_MIN) ||
		    !bpos_eq(s.bucket_end, SPOS_MAX)) {
			struct printbuf buf = PRINTBUF;

			prt_str(&buf, "check_extents_to_backpointers(): ");
			bch2_bpos_to_text(&buf, s.bucket_start);
			prt_str(&buf, "-");
			bch2_bpos_to_text(&buf, s.bucket_end);

			bch_verbose(c, "%s", buf.buf);
			printbuf_exit(&buf);
		}

		ret = bch2_check_extents_to_backpointers_pass(trans, &s);
		if (ret || bpos_eq(s.bucket_end, SPOS_MAX))
			break;

		s.bucket_start = bpos_successor(s.bucket_end);
	}
	bch2_trans_put(trans);
	bch2_bkey_buf_exit(&s.last_flushed, c);

	bch_err_fn(c, ret);
	return ret;
}

static int check_one_backpointer(struct btree_trans *trans,
				 struct bbpos start,
				 struct bbpos end,
				 struct bkey_s_c_backpointer bp,
				 struct bpos *last_flushed_pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bbpos pos = bp_to_bbpos(*bp.v);
	struct bkey_s_c k;
	struct printbuf buf = PRINTBUF;
	int ret;

	if (bbpos_cmp(pos, start) < 0 ||
	    bbpos_cmp(pos, end) > 0)
		return 0;

	k = bch2_backpointer_get_key(trans, &iter, bp.k->p, *bp.v, 0);
	ret = bkey_err(k);
	if (ret == -BCH_ERR_backpointer_to_overwritten_btree_node)
		return 0;
	if (ret)
		return ret;

	if (!k.k && !bpos_eq(*last_flushed_pos, bp.k->p)) {
		*last_flushed_pos = bp.k->p;
		ret = bch2_btree_write_buffer_flush_sync(trans) ?:
			-BCH_ERR_transaction_restart_write_buffer_flush;
		goto out;
	}

	if (fsck_err_on(!k.k, c,
			backpointer_to_missing_ptr,
			"backpointer for missing %s\n  %s",
			bp.v->level ? "btree node" : "extent",
			(bch2_bkey_val_to_text(&buf, c, bp.s_c), buf.buf))) {
		ret = bch2_btree_delete_at_buffered(trans, BTREE_ID_backpointers, bp.k->p);
		goto out;
	}
out:
fsck_err:
	bch2_trans_iter_exit(trans, &iter);
	printbuf_exit(&buf);
	return ret;
}

static int bch2_check_backpointers_to_extents_pass(struct btree_trans *trans,
						   struct bbpos start,
						   struct bbpos end)
{
	struct bpos last_flushed_pos = SPOS_MAX;

	return for_each_btree_key_commit(trans, iter, BTREE_ID_backpointers,
				  POS_MIN, BTREE_ITER_PREFETCH, k,
				  NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
		check_one_backpointer(trans, start, end,
				      bkey_s_c_to_backpointer(k),
				      &last_flushed_pos));
}

int bch2_check_backpointers_to_extents(struct bch_fs *c)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct bbpos start = (struct bbpos) { .btree = 0, .pos = POS_MIN, }, end;
	int ret;

	while (1) {
		ret = bch2_get_btree_in_memory_pos(trans,
						   (1U << BTREE_ID_extents)|
						   (1U << BTREE_ID_reflink),
						   ~0,
						   start, &end);
		if (ret)
			break;

		if (!bbpos_cmp(start, BBPOS_MIN) &&
		    bbpos_cmp(end, BBPOS_MAX))
			bch_verbose(c, "%s(): extents do not fit in ram, running in multiple passes with %zu nodes per pass",
				    __func__, btree_nodes_fit_in_ram(c));

		if (bbpos_cmp(start, BBPOS_MIN) ||
		    bbpos_cmp(end, BBPOS_MAX)) {
			struct printbuf buf = PRINTBUF;

			prt_str(&buf, "check_backpointers_to_extents(): ");
			bch2_bbpos_to_text(&buf, start);
			prt_str(&buf, "-");
			bch2_bbpos_to_text(&buf, end);

			bch_verbose(c, "%s", buf.buf);
			printbuf_exit(&buf);
		}

		ret = bch2_check_backpointers_to_extents_pass(trans, start, end);
		if (ret || !bbpos_cmp(end, BBPOS_MAX))
			break;

		start = bbpos_successor(end);
	}
	bch2_trans_put(trans);

	bch_err_fn(c, ret);
	return ret;
}
