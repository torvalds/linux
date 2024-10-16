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
#include "checksum.h"
#include "disk_accounting.h"
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

	rcu_read_lock();
	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		struct bpos bucket2;
		struct bch_backpointer bp2;

		if (p.ptr.cached)
			continue;

		struct bch_dev *ca = bch2_dev_rcu(c, p.ptr.dev);
		if (!ca)
			continue;

		bch2_extent_ptr_to_bp(c, ca, btree_id, level, k, p, entry, &bucket2, &bp2);
		if (bpos_eq(bucket, bucket2) &&
		    !memcmp(&bp, &bp2, sizeof(bp))) {
			rcu_read_unlock();
			return true;
		}
	}
	rcu_read_unlock();

	return false;
}

int bch2_backpointer_validate(struct bch_fs *c, struct bkey_s_c k,
			      enum bch_validate_flags flags)
{
	struct bkey_s_c_backpointer bp = bkey_s_c_to_backpointer(k);

	rcu_read_lock();
	struct bch_dev *ca = bch2_dev_rcu_noerror(c, bp.k->p.inode);
	if (!ca) {
		/* these will be caught by fsck */
		rcu_read_unlock();
		return 0;
	}

	struct bpos bucket = bp_pos_to_bucket(ca, bp.k->p);
	struct bpos bp_pos = bucket_pos_to_bp_noerror(ca, bucket, bp.v->bucket_offset);
	rcu_read_unlock();
	int ret = 0;

	bkey_fsck_err_on((bp.v->bucket_offset >> MAX_EXTENT_COMPRESS_RATIO_SHIFT) >= ca->mi.bucket_size ||
			 !bpos_eq(bp.k->p, bp_pos),
			 c, backpointer_bucket_offset_wrong,
			 "backpointer bucket_offset wrong");
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
	rcu_read_lock();
	struct bch_dev *ca = bch2_dev_rcu_noerror(c, k.k->p.inode);
	if (ca) {
		struct bpos bucket = bp_pos_to_bucket(ca, k.k->p);
		rcu_read_unlock();
		prt_str(out, "bucket=");
		bch2_bpos_to_text(out, bucket);
		prt_str(out, " ");
	} else {
		rcu_read_unlock();
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
		prt_printf(&buf, "backpointer not found when deleting\n");
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
		return bch2_inconsistent_error(c) ? BCH_ERR_erofs_unfixed_errors : 0;
	} else {
		return 0;
	}
}

int bch2_bucket_backpointer_mod_nowritebuffer(struct btree_trans *trans,
				struct bch_dev *ca,
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
	bp_k->k.p = bucket_pos_to_bp(ca, bucket, bp.bucket_offset);
	bp_k->v = bp;

	if (!insert) {
		bp_k->k.type = KEY_TYPE_deleted;
		set_bkey_val_u64s(&bp_k->k, 0);
	}

	k = bch2_bkey_get_iter(trans, &bp_iter, BTREE_ID_backpointers,
			       bp_k->k.p,
			       BTREE_ITER_intent|
			       BTREE_ITER_slots|
			       BTREE_ITER_with_updates);
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
			      struct bch_dev *ca,
			      struct bpos bucket, int gen,
			      struct bpos *bp_pos,
			      struct bch_backpointer *bp,
			      unsigned iter_flags)
{
	struct bpos bp_end_pos = bucket_pos_to_bp(ca, bpos_nosnap_successor(bucket), 0);
	struct btree_iter alloc_iter = { NULL }, bp_iter = { NULL };
	struct bkey_s_c k;
	int ret = 0;

	if (bpos_ge(*bp_pos, bp_end_pos))
		goto done;

	if (gen >= 0) {
		k = bch2_bkey_get_iter(trans, &alloc_iter, BTREE_ID_alloc,
				       bucket, BTREE_ITER_cached|iter_flags);
		ret = bkey_err(k);
		if (ret)
			goto out;

		if (k.k->type != KEY_TYPE_alloc_v4 ||
		    bkey_s_c_to_alloc_v4(k).v->gen != gen)
			goto done;
	}

	*bp_pos = bpos_max(*bp_pos, bucket_pos_to_bp(ca, bucket, 0));

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

	/*
	 * If we're using the btree write buffer, the backpointer we were
	 * looking at may have already been deleted - failure to find what it
	 * pointed to is not an error:
	 */
	if (likely(!bch2_backpointers_no_use_write_buffer))
		return;

	struct bpos bucket;
	if (!bp_pos_to_bucket_nodev(c, bp_pos, &bucket))
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

		struct bpos bucket;
		if (!bp_pos_to_bucket_nodev(c, bp_pos, &bucket))
			return bkey_s_c_err(-EIO);

		bch2_trans_node_iter_init(trans, iter,
					  bp.btree_id,
					  bp.pos,
					  0, 0,
					  iter_flags);
		struct bkey_s_c k = bch2_btree_iter_peek_slot(iter);
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

	BUG_ON(!bp.level);

	struct bpos bucket;
	if (!bp_pos_to_bucket_nodev(c, bp_pos, &bucket))
		return ERR_PTR(-EIO);

	bch2_trans_node_iter_init(trans, iter,
				  bp.btree_id,
				  bp.pos,
				  0,
				  bp.level - 1,
				  0);
	struct btree *b = bch2_btree_iter_peek_node(iter);
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

	struct bpos bucket;
	if (!bp_pos_to_bucket_nodev_noerror(c, k.k->p, &bucket)) {
		if (fsck_err(trans, backpointer_to_missing_device,
			     "backpointer for missing device:\n%s",
			     (bch2_bkey_val_to_text(&buf, c, k), buf.buf)))
			ret = bch2_btree_delete_at(trans, bp_iter, 0);
		goto out;
	}

	alloc_k = bch2_bkey_get_iter(trans, &alloc_iter, BTREE_ID_alloc, bucket, 0);
	ret = bkey_err(alloc_k);
	if (ret)
		goto out;

	if (fsck_err_on(alloc_k.k->type != KEY_TYPE_alloc_v4,
			trans, backpointer_to_missing_alloc,
			"backpointer for nonexistent alloc key: %llu:%llu:0\n%s",
			alloc_iter.pos.inode, alloc_iter.pos.offset,
			(bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
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

struct extents_to_bp_state {
	struct bpos	bucket_start;
	struct bpos	bucket_end;
	struct bkey_buf last_flushed;
};

static int drop_dev_and_update(struct btree_trans *trans, enum btree_id btree,
			       struct bkey_s_c extent, unsigned dev)
{
	struct bkey_i *n = bch2_bkey_make_mut_noupdate(trans, extent);
	int ret = PTR_ERR_OR_ZERO(n);
	if (ret)
		return ret;

	bch2_bkey_drop_device(bkey_i_to_s(n), dev);
	return bch2_btree_insert_trans(trans, btree, n, 0);
}

static int check_extent_checksum(struct btree_trans *trans,
				 enum btree_id btree, struct bkey_s_c extent,
				 enum btree_id o_btree, struct bkey_s_c extent2, unsigned dev)
{
	struct bch_fs *c = trans->c;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(extent);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	struct printbuf buf = PRINTBUF;
	void *data_buf = NULL;
	struct bio *bio = NULL;
	size_t bytes;
	int ret = 0;

	if (bkey_is_btree_ptr(extent.k))
		return false;

	bkey_for_each_ptr_decode(extent.k, ptrs, p, entry)
		if (p.ptr.dev == dev)
			goto found;
	BUG();
found:
	if (!p.crc.csum_type)
		return false;

	bytes = p.crc.compressed_size << 9;

	struct bch_dev *ca = bch2_dev_get_ioref(c, dev, READ);
	if (!ca)
		return false;

	data_buf = kvmalloc(bytes, GFP_KERNEL);
	if (!data_buf) {
		ret = -ENOMEM;
		goto err;
	}

	bio = bio_alloc(ca->disk_sb.bdev, buf_pages(data_buf, bytes), REQ_OP_READ, GFP_KERNEL);
	bio->bi_iter.bi_sector = p.ptr.offset;
	bch2_bio_map(bio, data_buf, bytes);
	ret = submit_bio_wait(bio);
	if (ret)
		goto err;

	prt_str(&buf, "extents pointing to same space, but first extent checksum bad:");
	prt_printf(&buf, "\n  %s ", bch2_btree_id_str(btree));
	bch2_bkey_val_to_text(&buf, c, extent);
	prt_printf(&buf, "\n  %s ", bch2_btree_id_str(o_btree));
	bch2_bkey_val_to_text(&buf, c, extent2);

	struct nonce nonce = extent_nonce(extent.k->bversion, p.crc);
	struct bch_csum csum = bch2_checksum(c, p.crc.csum_type, nonce, data_buf, bytes);
	if (fsck_err_on(bch2_crc_cmp(csum, p.crc.csum),
			trans, dup_backpointer_to_bad_csum_extent,
			"%s", buf.buf))
		ret = drop_dev_and_update(trans, btree, extent, dev) ?: 1;
fsck_err:
err:
	if (bio)
		bio_put(bio);
	kvfree(data_buf);
	percpu_ref_put(&ca->io_ref);
	printbuf_exit(&buf);
	return ret;
}

static int check_bp_exists(struct btree_trans *trans,
			   struct extents_to_bp_state *s,
			   struct bpos bucket,
			   struct bch_backpointer bp,
			   struct bkey_s_c orig_k)
{
	struct bch_fs *c = trans->c;
	struct btree_iter bp_iter = {};
	struct btree_iter other_extent_iter = {};
	struct printbuf buf = PRINTBUF;
	struct bkey_s_c bp_k;
	int ret = 0;

	struct bch_dev *ca = bch2_dev_bucket_tryget(c, bucket);
	if (!ca) {
		prt_str(&buf, "extent for nonexistent device:bucket ");
		bch2_bpos_to_text(&buf, bucket);
		prt_str(&buf, "\n  ");
		bch2_bkey_val_to_text(&buf, c, orig_k);
		bch_err(c, "%s", buf.buf);
		ret = -BCH_ERR_fsck_repair_unimplemented;
		goto err;
	}

	if (bpos_lt(bucket, s->bucket_start) ||
	    bpos_gt(bucket, s->bucket_end))
		goto out;

	bp_k = bch2_bkey_get_iter(trans, &bp_iter, BTREE_ID_backpointers,
				  bucket_pos_to_bp(ca, bucket, bp.bucket_offset),
				  0);
	ret = bkey_err(bp_k);
	if (ret)
		goto err;

	if (bp_k.k->type != KEY_TYPE_backpointer ||
	    memcmp(bkey_s_c_to_backpointer(bp_k).v, &bp, sizeof(bp))) {
		ret = bch2_btree_write_buffer_maybe_flush(trans, orig_k, &s->last_flushed);
		if (ret)
			goto err;

		goto check_existing_bp;
	}
out:
err:
fsck_err:
	bch2_trans_iter_exit(trans, &other_extent_iter);
	bch2_trans_iter_exit(trans, &bp_iter);
	bch2_dev_put(ca);
	printbuf_exit(&buf);
	return ret;
check_existing_bp:
	/* Do we have a backpointer for a different extent? */
	if (bp_k.k->type != KEY_TYPE_backpointer)
		goto missing;

	struct bch_backpointer other_bp = *bkey_s_c_to_backpointer(bp_k).v;

	struct bkey_s_c other_extent =
		bch2_backpointer_get_key(trans, &other_extent_iter, bp_k.k->p, other_bp, 0);
	ret = bkey_err(other_extent);
	if (ret == -BCH_ERR_backpointer_to_overwritten_btree_node)
		ret = 0;
	if (ret)
		goto err;

	if (!other_extent.k)
		goto missing;

	if (bch2_extents_match(orig_k, other_extent)) {
		printbuf_reset(&buf);
		prt_printf(&buf, "duplicate versions of same extent, deleting smaller\n  ");
		bch2_bkey_val_to_text(&buf, c, orig_k);
		prt_str(&buf, "\n  ");
		bch2_bkey_val_to_text(&buf, c, other_extent);
		bch_err(c, "%s", buf.buf);

		if (other_extent.k->size <= orig_k.k->size) {
			ret = drop_dev_and_update(trans, other_bp.btree_id, other_extent, bucket.inode);
			if (ret)
				goto err;
			goto out;
		} else {
			ret = drop_dev_and_update(trans, bp.btree_id, orig_k, bucket.inode);
			if (ret)
				goto err;
			goto missing;
		}
	}

	ret = check_extent_checksum(trans, other_bp.btree_id, other_extent, bp.btree_id, orig_k, bucket.inode);
	if (ret < 0)
		goto err;
	if (ret) {
		ret = 0;
		goto missing;
	}

	ret = check_extent_checksum(trans, bp.btree_id, orig_k, other_bp.btree_id, other_extent, bucket.inode);
	if (ret < 0)
		goto err;
	if (ret) {
		ret = 0;
		goto out;
	}

	printbuf_reset(&buf);
	prt_printf(&buf, "duplicate extents pointing to same space on dev %llu\n  ", bucket.inode);
	bch2_bkey_val_to_text(&buf, c, orig_k);
	prt_str(&buf, "\n  ");
	bch2_bkey_val_to_text(&buf, c, other_extent);
	bch_err(c, "%s", buf.buf);
	ret = -BCH_ERR_fsck_repair_unimplemented;
	goto err;
missing:
	printbuf_reset(&buf);
	prt_printf(&buf, "missing backpointer for btree=%s l=%u ",
	       bch2_btree_id_str(bp.btree_id), bp.level);
	bch2_bkey_val_to_text(&buf, c, orig_k);
	prt_printf(&buf, "\n  got:   ");
	bch2_bkey_val_to_text(&buf, c, bp_k);

	struct bkey_i_backpointer n_bp_k;
	bkey_backpointer_init(&n_bp_k.k_i);
	n_bp_k.k.p = bucket_pos_to_bp(ca, bucket, bp.bucket_offset);
	n_bp_k.v = bp;
	prt_printf(&buf, "\n  want:  ");
	bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&n_bp_k.k_i));

	if (fsck_err(trans, ptr_to_missing_backpointer, "%s", buf.buf))
		ret = bch2_bucket_backpointer_mod(trans, ca, bucket, bp, orig_k, true);

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
		struct bpos bucket_pos = POS_MIN;
		struct bch_backpointer bp;

		if (p.ptr.cached)
			continue;

		rcu_read_lock();
		struct bch_dev *ca = bch2_dev_rcu_noerror(c, p.ptr.dev);
		if (ca)
			bch2_extent_ptr_to_bp(c, ca, btree, level, k, p, entry, &bucket_pos, &bp);
		rcu_read_unlock();

		if (!ca)
			continue;

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

static u64 mem_may_pin_bytes(struct bch_fs *c)
{
	struct sysinfo i;
	si_meminfo(&i);

	u64 mem_bytes = i.totalram * i.mem_unit;
	return div_u64(mem_bytes * c->opts.fsck_memory_usage_percent, 100);
}

static size_t btree_nodes_fit_in_ram(struct bch_fs *c)
{
	return div_u64(mem_may_pin_bytes(c), c->opts.btree_node_size);
}

static int bch2_get_btree_in_memory_pos(struct btree_trans *trans,
					u64 btree_leaf_mask,
					u64 btree_interior_mask,
					struct bbpos start, struct bbpos *end)
{
	struct bch_fs *c = trans->c;
	s64 mem_may_pin = mem_may_pin_bytes(c);
	int ret = 0;

	bch2_btree_cache_unpin(c);

	btree_interior_mask |= btree_leaf_mask;

	c->btree_cache.pinned_nodes_mask[0]		= btree_leaf_mask;
	c->btree_cache.pinned_nodes_mask[1]		= btree_interior_mask;
	c->btree_cache.pinned_nodes_start		= start;
	c->btree_cache.pinned_nodes_end			= *end = BBPOS_MAX;

	for (enum btree_id btree = start.btree;
	     btree < BTREE_ID_NR && !ret;
	     btree++) {
		unsigned depth = (BIT_ULL(btree) & btree_leaf_mask) ? 0 : 1;

		if (!(BIT_ULL(btree) & btree_leaf_mask) &&
		    !(BIT_ULL(btree) & btree_interior_mask))
			continue;

		ret = __for_each_btree_node(trans, iter, btree,
				      btree == start.btree ? start.pos : POS_MIN,
				      0, depth, BTREE_ITER_prefetch, b, ({
			mem_may_pin -= btree_buf_bytes(b);
			if (mem_may_pin <= 0) {
				c->btree_cache.pinned_nodes_end = *end =
					BBPOS(btree, b->key.k.p);
				break;
			}
			bch2_node_pin(c, b);
			0;
		}));
	}

	return ret;
}

struct progress_indicator_state {
	unsigned long		next_print;
	u64			nodes_seen;
	u64			nodes_total;
	struct btree		*last_node;
};

static inline void progress_init(struct progress_indicator_state *s,
				 struct bch_fs *c,
				 u64 btree_id_mask)
{
	memset(s, 0, sizeof(*s));

	s->next_print = jiffies + HZ * 10;

	for (unsigned i = 0; i < BTREE_ID_NR; i++) {
		if (!(btree_id_mask & BIT_ULL(i)))
			continue;

		struct disk_accounting_pos acc = {
			.type		= BCH_DISK_ACCOUNTING_btree,
			.btree.id	= i,
		};

		u64 v;
		bch2_accounting_mem_read(c, disk_accounting_pos_to_bpos(&acc), &v, 1);
		s->nodes_total += div64_ul(v, btree_sectors(c));
	}
}

static inline bool progress_update_p(struct progress_indicator_state *s)
{
	bool ret = time_after_eq(jiffies, s->next_print);

	if (ret)
		s->next_print = jiffies + HZ * 10;
	return ret;
}

static void progress_update_iter(struct btree_trans *trans,
				 struct progress_indicator_state *s,
				 struct btree_iter *iter,
				 const char *msg)
{
	struct bch_fs *c = trans->c;
	struct btree *b = path_l(btree_iter_path(trans, iter))->b;

	s->nodes_seen += b != s->last_node;
	s->last_node = b;

	if (progress_update_p(s)) {
		struct printbuf buf = PRINTBUF;
		unsigned percent = s->nodes_total
			? div64_u64(s->nodes_seen * 100, s->nodes_total)
			: 0;

		prt_printf(&buf, "%s: %d%%, done %llu/%llu nodes, at ",
			   msg, percent, s->nodes_seen, s->nodes_total);
		bch2_bbpos_to_text(&buf, BBPOS(iter->btree_id, iter->pos));

		bch_info(c, "%s", buf.buf);
		printbuf_exit(&buf);
	}
}

static int bch2_check_extents_to_backpointers_pass(struct btree_trans *trans,
						   struct extents_to_bp_state *s)
{
	struct bch_fs *c = trans->c;
	struct progress_indicator_state progress;
	int ret = 0;

	progress_init(&progress, trans->c, BIT_ULL(BTREE_ID_extents)|BIT_ULL(BTREE_ID_reflink));

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
			bch2_trans_node_iter_init(trans, &iter, btree_id, POS_MIN, 0, level,
						  BTREE_ITER_prefetch);

			ret = for_each_btree_key_continue(trans, iter, 0, k, ({
				progress_update_iter(trans, &progress, &iter, "extents_to_backpointers");
				check_extent_to_backpointers(trans, s, btree_id, level, k) ?:
				bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc);
			}));
			if (ret)
				return ret;

			--level;
		}
	}

	return 0;
}

int bch2_check_extents_to_backpointers(struct bch_fs *c)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct extents_to_bp_state s = { .bucket_start = POS_MIN };
	int ret;

	bch2_bkey_buf_init(&s.last_flushed);
	bkey_init(&s.last_flushed.k->k);

	while (1) {
		struct bbpos end;
		ret = bch2_get_btree_in_memory_pos(trans,
				BIT_ULL(BTREE_ID_backpointers),
				BIT_ULL(BTREE_ID_backpointers),
				BBPOS(BTREE_ID_backpointers, s.bucket_start), &end);
		if (ret)
			break;

		s.bucket_end = end.pos;

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

	bch2_btree_cache_unpin(c);

	bch_err_fn(c, ret);
	return ret;
}

static int check_one_backpointer(struct btree_trans *trans,
				 struct bbpos start,
				 struct bbpos end,
				 struct bkey_s_c_backpointer bp,
				 struct bkey_buf *last_flushed)
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

	if (!k.k) {
		ret = bch2_btree_write_buffer_maybe_flush(trans, bp.s_c, last_flushed);
		if (ret)
			goto out;

		if (fsck_err(trans, backpointer_to_missing_ptr,
			     "backpointer for missing %s\n  %s",
			     bp.v->level ? "btree node" : "extent",
			     (bch2_bkey_val_to_text(&buf, c, bp.s_c), buf.buf))) {
			ret = bch2_btree_delete_at_buffered(trans, BTREE_ID_backpointers, bp.k->p);
			goto out;
		}
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
	struct bch_fs *c = trans->c;
	struct bkey_buf last_flushed;
	struct progress_indicator_state progress;

	bch2_bkey_buf_init(&last_flushed);
	bkey_init(&last_flushed.k->k);
	progress_init(&progress, trans->c, BIT_ULL(BTREE_ID_backpointers));

	int ret = for_each_btree_key_commit(trans, iter, BTREE_ID_backpointers,
				  POS_MIN, BTREE_ITER_prefetch, k,
				  NULL, NULL, BCH_TRANS_COMMIT_no_enospc, ({
			progress_update_iter(trans, &progress, &iter, "backpointers_to_extents");
			check_one_backpointer(trans, start, end,
					      bkey_s_c_to_backpointer(k),
					      &last_flushed);
	}));

	bch2_bkey_buf_exit(&last_flushed, c);
	return ret;
}

int bch2_check_backpointers_to_extents(struct bch_fs *c)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct bbpos start = (struct bbpos) { .btree = 0, .pos = POS_MIN, }, end;
	int ret;

	while (1) {
		ret = bch2_get_btree_in_memory_pos(trans,
						   BIT_ULL(BTREE_ID_extents)|
						   BIT_ULL(BTREE_ID_reflink),
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

	bch2_btree_cache_unpin(c);

	bch_err_fn(c, ret);
	return ret;
}
