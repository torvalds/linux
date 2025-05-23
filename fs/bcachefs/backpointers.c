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
#include "progress.h"

#include <linux/mm.h>

int bch2_backpointer_validate(struct bch_fs *c, struct bkey_s_c k,
			      struct bkey_validate_context from)
{
	struct bkey_s_c_backpointer bp = bkey_s_c_to_backpointer(k);
	int ret = 0;

	bkey_fsck_err_on(bp.v->level > BTREE_MAX_DEPTH,
			 c, backpointer_level_bad,
			 "backpointer level bad: %u >= %u",
			 bp.v->level, BTREE_MAX_DEPTH);

	bkey_fsck_err_on(bp.k->p.inode == BCH_SB_MEMBER_INVALID,
			 c, backpointer_dev_bad,
			 "backpointer for BCH_SB_MEMBER_INVALID");
fsck_err:
	return ret;
}

void bch2_backpointer_to_text(struct printbuf *out, struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_backpointer bp = bkey_s_c_to_backpointer(k);

	rcu_read_lock();
	struct bch_dev *ca = bch2_dev_rcu_noerror(c, bp.k->p.inode);
	if (ca) {
		u32 bucket_offset;
		struct bpos bucket = bp_pos_to_bucket_and_offset(ca, bp.k->p, &bucket_offset);
		rcu_read_unlock();
		prt_printf(out, "bucket=%llu:%llu:%u ", bucket.inode, bucket.offset, bucket_offset);
	} else {
		rcu_read_unlock();
		prt_printf(out, "sector=%llu:%llu ", bp.k->p.inode, bp.k->p.offset >> MAX_EXTENT_COMPRESS_RATIO_SHIFT);
	}

	bch2_btree_id_level_to_text(out, bp.v->btree_id, bp.v->level);
	prt_str(out, " data_type=");
	bch2_prt_data_type(out, bp.v->data_type);
	prt_printf(out, " suboffset=%u len=%u gen=%u pos=",
		   (u32) bp.k->p.offset & ~(~0U << MAX_EXTENT_COMPRESS_RATIO_SHIFT),
		   bp.v->bucket_len,
		   bp.v->bucket_gen);
	bch2_bpos_to_text(out, bp.v->pos);
}

void bch2_backpointer_swab(struct bkey_s k)
{
	struct bkey_s_backpointer bp = bkey_s_to_backpointer(k);

	bp.v->bucket_len	= swab32(bp.v->bucket_len);
	bch2_bpos_swab(&bp.v->pos);
}

static bool extent_matches_bp(struct bch_fs *c,
			      enum btree_id btree_id, unsigned level,
			      struct bkey_s_c k,
			      struct bkey_s_c_backpointer bp)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		struct bkey_i_backpointer bp2;
		bch2_extent_ptr_to_bp(c, btree_id, level, k, p, entry, &bp2);

		if (bpos_eq(bp.k->p, bp2.k.p) &&
		    !memcmp(bp.v, &bp2.v, sizeof(bp2.v)))
			return true;
	}

	return false;
}

static noinline int backpointer_mod_err(struct btree_trans *trans,
					struct bkey_s_c orig_k,
					struct bkey_i_backpointer *new_bp,
					struct bkey_s_c found_bp,
					bool insert)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	if (insert) {
		prt_printf(&buf, "existing backpointer found when inserting ");
		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&new_bp->k_i));
		prt_newline(&buf);
		printbuf_indent_add(&buf, 2);

		prt_printf(&buf, "found ");
		bch2_bkey_val_to_text(&buf, c, found_bp);
		prt_newline(&buf);

		prt_printf(&buf, "for ");
		bch2_bkey_val_to_text(&buf, c, orig_k);

		bch_err(c, "%s", buf.buf);
	} else if (c->curr_recovery_pass > BCH_RECOVERY_PASS_check_extents_to_backpointers) {
		prt_printf(&buf, "backpointer not found when deleting\n");
		printbuf_indent_add(&buf, 2);

		prt_printf(&buf, "searching for ");
		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&new_bp->k_i));
		prt_newline(&buf);

		prt_printf(&buf, "got ");
		bch2_bkey_val_to_text(&buf, c, found_bp);
		prt_newline(&buf);

		prt_printf(&buf, "for ");
		bch2_bkey_val_to_text(&buf, c, orig_k);
	}

	if (c->curr_recovery_pass > BCH_RECOVERY_PASS_check_extents_to_backpointers &&
	    __bch2_inconsistent_error(c, &buf))
		ret = -BCH_ERR_erofs_unfixed_errors;

	bch_err(c, "%s", buf.buf);
	printbuf_exit(&buf);
	return ret;
}

int bch2_bucket_backpointer_mod_nowritebuffer(struct btree_trans *trans,
				struct bkey_s_c orig_k,
				struct bkey_i_backpointer *bp,
				bool insert)
{
	struct btree_iter bp_iter;
	struct bkey_s_c k = bch2_bkey_get_iter(trans, &bp_iter, BTREE_ID_backpointers,
			       bp->k.p,
			       BTREE_ITER_intent|
			       BTREE_ITER_slots|
			       BTREE_ITER_with_updates);
	int ret = bkey_err(k);
	if (ret)
		return ret;

	if (insert
	    ? k.k->type
	    : (k.k->type != KEY_TYPE_backpointer ||
	       memcmp(bkey_s_c_to_backpointer(k).v, &bp->v, sizeof(bp->v)))) {
		ret = backpointer_mod_err(trans, orig_k, bp, k, insert);
		if (ret)
			goto err;
	}

	if (!insert) {
		bp->k.type = KEY_TYPE_deleted;
		set_bkey_val_u64s(&bp->k, 0);
	}

	ret = bch2_trans_update(trans, &bp_iter, &bp->k_i, 0);
err:
	bch2_trans_iter_exit(trans, &bp_iter);
	return ret;
}

static int bch2_backpointer_del(struct btree_trans *trans, struct bpos pos)
{
	return (likely(!bch2_backpointers_no_use_write_buffer)
		? bch2_btree_delete_at_buffered(trans, BTREE_ID_backpointers, pos)
		: bch2_btree_delete(trans, BTREE_ID_backpointers, pos, 0)) ?:
		 bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc);
}

static inline int bch2_backpointers_maybe_flush(struct btree_trans *trans,
					 struct bkey_s_c visiting_k,
					 struct bkey_buf *last_flushed)
{
	return likely(!bch2_backpointers_no_use_write_buffer)
		? bch2_btree_write_buffer_maybe_flush(trans, visiting_k, last_flushed)
		: 0;
}

static int backpointer_target_not_found(struct btree_trans *trans,
				  struct bkey_s_c_backpointer bp,
				  struct bkey_s_c target_k,
				  struct bkey_buf *last_flushed,
				  bool commit)
{
	struct bch_fs *c = trans->c;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	/*
	 * If we're using the btree write buffer, the backpointer we were
	 * looking at may have already been deleted - failure to find what it
	 * pointed to is not an error:
	 */
	ret = last_flushed
		? bch2_backpointers_maybe_flush(trans, bp.s_c, last_flushed)
		: 0;
	if (ret)
		return ret;

	prt_printf(&buf, "backpointer doesn't match %s it points to:\n",
		   bp.v->level ? "btree node" : "extent");
	bch2_bkey_val_to_text(&buf, c, bp.s_c);

	prt_newline(&buf);
	bch2_bkey_val_to_text(&buf, c, target_k);

	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(target_k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	bkey_for_each_ptr_decode(target_k.k, ptrs, p, entry)
		if (p.ptr.dev == bp.k->p.inode) {
			prt_newline(&buf);
			struct bkey_i_backpointer bp2;
			bch2_extent_ptr_to_bp(c, bp.v->btree_id, bp.v->level, target_k, p, entry, &bp2);
			bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&bp2.k_i));
		}

	if (fsck_err(trans, backpointer_to_missing_ptr,
		     "%s", buf.buf)) {
		ret = bch2_backpointer_del(trans, bp.k->p);
		if (ret || !commit)
			goto out;

		/*
		 * Normally, on transaction commit from inside a transaction,
		 * we'll return -BCH_ERR_transaction_restart_nested, since a
		 * transaction commit invalidates pointers given out by peek().
		 *
		 * However, since we're updating a write buffer btree, if we
		 * return a transaction restart and loop we won't see that the
		 * backpointer has been deleted without an additional write
		 * buffer flush - and those are expensive.
		 *
		 * So we're relying on the caller immediately advancing to the
		 * next backpointer and starting a new transaction immediately
		 * after backpointer_get_key() returns NULL:
		 */
		ret = bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc);
	}
out:
fsck_err:
	printbuf_exit(&buf);
	return ret;
}

static struct btree *__bch2_backpointer_get_node(struct btree_trans *trans,
						 struct bkey_s_c_backpointer bp,
						 struct btree_iter *iter,
						 struct bkey_buf *last_flushed,
						 bool commit)
{
	struct bch_fs *c = trans->c;

	BUG_ON(!bp.v->level);

	bch2_trans_node_iter_init(trans, iter,
				  bp.v->btree_id,
				  bp.v->pos,
				  0,
				  bp.v->level - 1,
				  0);
	struct btree *b = bch2_btree_iter_peek_node(trans, iter);
	if (IS_ERR_OR_NULL(b))
		goto err;

	BUG_ON(b->c.level != bp.v->level - 1);

	if (extent_matches_bp(c, bp.v->btree_id, bp.v->level,
			      bkey_i_to_s_c(&b->key), bp))
		return b;

	if (btree_node_will_make_reachable(b)) {
		b = ERR_PTR(-BCH_ERR_backpointer_to_overwritten_btree_node);
	} else {
		int ret = backpointer_target_not_found(trans, bp, bkey_i_to_s_c(&b->key),
						       last_flushed, commit);
		b = ret ? ERR_PTR(ret) : NULL;
	}
err:
	bch2_trans_iter_exit(trans, iter);
	return b;
}

static struct bkey_s_c __bch2_backpointer_get_key(struct btree_trans *trans,
						  struct bkey_s_c_backpointer bp,
						  struct btree_iter *iter,
						  unsigned iter_flags,
						  struct bkey_buf *last_flushed,
						  bool commit)
{
	struct bch_fs *c = trans->c;

	if (unlikely(bp.v->btree_id >= btree_id_nr_alive(c)))
		return bkey_s_c_null;

	bch2_trans_node_iter_init(trans, iter,
				  bp.v->btree_id,
				  bp.v->pos,
				  0,
				  bp.v->level,
				  iter_flags);
	struct bkey_s_c k = bch2_btree_iter_peek_slot(trans, iter);
	if (bkey_err(k)) {
		bch2_trans_iter_exit(trans, iter);
		return k;
	}

	/*
	 * peek_slot() doesn't normally return NULL - except when we ask for a
	 * key at a btree level that doesn't exist.
	 *
	 * We may want to revisit this and change peek_slot():
	 */
	if (!k.k) {
		bkey_init(&iter->k);
		iter->k.p = bp.v->pos;
		k.k = &iter->k;
	}

	if (k.k &&
	    extent_matches_bp(c, bp.v->btree_id, bp.v->level, k, bp))
		return k;

	bch2_trans_iter_exit(trans, iter);

	if (!bp.v->level) {
		int ret = backpointer_target_not_found(trans, bp, k, last_flushed, commit);
		return ret ? bkey_s_c_err(ret) : bkey_s_c_null;
	} else {
		struct btree *b = __bch2_backpointer_get_node(trans, bp, iter, last_flushed, commit);
		if (b == ERR_PTR(-BCH_ERR_backpointer_to_overwritten_btree_node))
			return bkey_s_c_null;
		if (IS_ERR_OR_NULL(b))
			return ((struct bkey_s_c) { .k = ERR_CAST(b) });

		return bkey_i_to_s_c(&b->key);
	}
}

struct btree *bch2_backpointer_get_node(struct btree_trans *trans,
					struct bkey_s_c_backpointer bp,
					struct btree_iter *iter,
					struct bkey_buf *last_flushed)
{
	return __bch2_backpointer_get_node(trans, bp, iter, last_flushed, true);
}

struct bkey_s_c bch2_backpointer_get_key(struct btree_trans *trans,
					 struct bkey_s_c_backpointer bp,
					 struct btree_iter *iter,
					 unsigned iter_flags,
					 struct bkey_buf *last_flushed)
{
	return __bch2_backpointer_get_key(trans, bp, iter, iter_flags, last_flushed, true);
}

static int bch2_check_backpointer_has_valid_bucket(struct btree_trans *trans, struct bkey_s_c k,
						   struct bkey_buf *last_flushed)
{
	if (k.k->type != KEY_TYPE_backpointer)
		return 0;

	struct bch_fs *c = trans->c;
	struct btree_iter alloc_iter = {};
	struct bkey_s_c alloc_k;
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	struct bpos bucket;
	if (!bp_pos_to_bucket_nodev_noerror(c, k.k->p, &bucket)) {
		ret = bch2_backpointers_maybe_flush(trans, k, last_flushed);
		if (ret)
			goto out;

		if (fsck_err(trans, backpointer_to_missing_device,
			     "backpointer for missing device:\n%s",
			     (bch2_bkey_val_to_text(&buf, c, k), buf.buf)))
			ret = bch2_backpointer_del(trans, k.k->p);
		goto out;
	}

	alloc_k = bch2_bkey_get_iter(trans, &alloc_iter, BTREE_ID_alloc, bucket, 0);
	ret = bkey_err(alloc_k);
	if (ret)
		goto out;

	if (alloc_k.k->type != KEY_TYPE_alloc_v4) {
		ret = bch2_backpointers_maybe_flush(trans, k, last_flushed);
		if (ret)
			goto out;

		if (fsck_err(trans, backpointer_to_missing_alloc,
			     "backpointer for nonexistent alloc key: %llu:%llu:0\n%s",
			     alloc_iter.pos.inode, alloc_iter.pos.offset,
			     (bch2_bkey_val_to_text(&buf, c, k), buf.buf)))
			ret = bch2_backpointer_del(trans, k.k->p);
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
	struct bkey_buf last_flushed;
	bch2_bkey_buf_init(&last_flushed);
	bkey_init(&last_flushed.k->k);

	int ret = bch2_trans_run(c,
		for_each_btree_key_commit(trans, iter,
			BTREE_ID_backpointers, POS_MIN, 0, k,
			NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
		  bch2_check_backpointer_has_valid_bucket(trans, k, &last_flushed)));

	bch2_bkey_buf_exit(&last_flushed, c);
	bch_err_fn(c, ret);
	return ret;
}

struct extents_to_bp_state {
	struct bpos	bp_start;
	struct bpos	bp_end;
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

	prt_printf(&buf, "extents pointing to same space, but first extent checksum bad:\n");
	bch2_btree_id_to_text(&buf, btree);
	prt_str(&buf, " ");
	bch2_bkey_val_to_text(&buf, c, extent);
	prt_newline(&buf);
	bch2_btree_id_to_text(&buf, o_btree);
	prt_str(&buf, " ");
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
	percpu_ref_put(&ca->io_ref[READ]);
	printbuf_exit(&buf);
	return ret;
}

static int check_bp_exists(struct btree_trans *trans,
			   struct extents_to_bp_state *s,
			   struct bkey_i_backpointer *bp,
			   struct bkey_s_c orig_k)
{
	struct bch_fs *c = trans->c;
	struct btree_iter other_extent_iter = {};
	struct printbuf buf = PRINTBUF;

	if (bpos_lt(bp->k.p, s->bp_start) ||
	    bpos_gt(bp->k.p, s->bp_end))
		return 0;

	struct btree_iter bp_iter;
	struct bkey_s_c bp_k = bch2_bkey_get_iter(trans, &bp_iter, BTREE_ID_backpointers, bp->k.p, 0);
	int ret = bkey_err(bp_k);
	if (ret)
		goto err;

	if (bp_k.k->type != KEY_TYPE_backpointer ||
	    memcmp(bkey_s_c_to_backpointer(bp_k).v, &bp->v, sizeof(bp->v))) {
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
	printbuf_exit(&buf);
	return ret;
check_existing_bp:
	/* Do we have a backpointer for a different extent? */
	if (bp_k.k->type != KEY_TYPE_backpointer)
		goto missing;

	struct bkey_s_c_backpointer other_bp = bkey_s_c_to_backpointer(bp_k);

	struct bkey_s_c other_extent =
		__bch2_backpointer_get_key(trans, other_bp, &other_extent_iter, 0, NULL, false);
	ret = bkey_err(other_extent);
	if (ret == -BCH_ERR_backpointer_to_overwritten_btree_node)
		ret = 0;
	if (ret)
		goto err;

	if (!other_extent.k)
		goto missing;

	rcu_read_lock();
	struct bch_dev *ca = bch2_dev_rcu_noerror(c, bp->k.p.inode);
	if (ca) {
		struct bkey_ptrs_c other_extent_ptrs = bch2_bkey_ptrs_c(other_extent);
		bkey_for_each_ptr(other_extent_ptrs, ptr)
			if (ptr->dev == bp->k.p.inode &&
			    dev_ptr_stale_rcu(ca, ptr)) {
				ret = drop_dev_and_update(trans, other_bp.v->btree_id,
							  other_extent, bp->k.p.inode);
				if (ret)
					goto err;
				goto out;
			}
	}
	rcu_read_unlock();

	if (bch2_extents_match(orig_k, other_extent)) {
		printbuf_reset(&buf);
		prt_printf(&buf, "duplicate versions of same extent, deleting smaller\n");
		bch2_bkey_val_to_text(&buf, c, orig_k);
		prt_newline(&buf);
		bch2_bkey_val_to_text(&buf, c, other_extent);
		bch_err(c, "%s", buf.buf);

		if (other_extent.k->size <= orig_k.k->size) {
			ret = drop_dev_and_update(trans, other_bp.v->btree_id,
						  other_extent, bp->k.p.inode);
			if (ret)
				goto err;
			goto out;
		} else {
			ret = drop_dev_and_update(trans, bp->v.btree_id, orig_k, bp->k.p.inode);
			if (ret)
				goto err;
			goto missing;
		}
	}

	ret = check_extent_checksum(trans,
				    other_bp.v->btree_id, other_extent,
				    bp->v.btree_id, orig_k,
				    bp->k.p.inode);
	if (ret < 0)
		goto err;
	if (ret) {
		ret = 0;
		goto missing;
	}

	ret = check_extent_checksum(trans, bp->v.btree_id, orig_k,
				    other_bp.v->btree_id, other_extent, bp->k.p.inode);
	if (ret < 0)
		goto err;
	if (ret) {
		ret = 0;
		goto out;
	}

	printbuf_reset(&buf);
	prt_printf(&buf, "duplicate extents pointing to same space on dev %llu\n", bp->k.p.inode);
	bch2_bkey_val_to_text(&buf, c, orig_k);
	prt_newline(&buf);
	bch2_bkey_val_to_text(&buf, c, other_extent);
	bch_err(c, "%s", buf.buf);
	ret = -BCH_ERR_fsck_repair_unimplemented;
	goto err;
missing:
	printbuf_reset(&buf);
	prt_str(&buf, "missing backpointer\nfor:  ");
	bch2_bkey_val_to_text(&buf, c, orig_k);
	prt_printf(&buf, "\nwant: ");
	bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&bp->k_i));
	prt_printf(&buf, "\ngot:  ");
	bch2_bkey_val_to_text(&buf, c, bp_k);

	if (fsck_err(trans, ptr_to_missing_backpointer, "%s", buf.buf))
		ret = bch2_bucket_backpointer_mod(trans, orig_k, bp, true);

	goto out;
}

static int check_extent_to_backpointers(struct btree_trans *trans,
					struct extents_to_bp_state *s,
					enum btree_id btree, unsigned level,
					struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		if (p.ptr.dev == BCH_SB_MEMBER_INVALID)
			continue;

		rcu_read_lock();
		struct bch_dev *ca = bch2_dev_rcu_noerror(c, p.ptr.dev);
		bool check = ca && test_bit(PTR_BUCKET_NR(ca, &p.ptr), ca->bucket_backpointer_mismatches);
		bool empty = ca && test_bit(PTR_BUCKET_NR(ca, &p.ptr), ca->bucket_backpointer_empty);

		bool stale = p.ptr.cached && (!ca || dev_ptr_stale_rcu(ca, &p.ptr));
		rcu_read_unlock();

		if ((check || empty) && !stale) {
			struct bkey_i_backpointer bp;
			bch2_extent_ptr_to_bp(c, btree, level, k, p, entry, &bp);

			int ret = check
				? check_bp_exists(trans, s, &bp, k)
				: bch2_bucket_backpointer_mod(trans, k, &bp, true);
			if (ret)
				return ret;
		}
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
	b = bch2_btree_iter_peek_node(trans, &iter);
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

static int bch2_check_extents_to_backpointers_pass(struct btree_trans *trans,
						   struct extents_to_bp_state *s)
{
	struct bch_fs *c = trans->c;
	struct progress_indicator_state progress;
	int ret = 0;

	bch2_progress_init(&progress, trans->c, BIT_ULL(BTREE_ID_extents)|BIT_ULL(BTREE_ID_reflink));

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
				bch2_progress_update_iter(trans, &progress, &iter, "extents_to_backpointers");
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

enum alloc_sector_counter {
	ALLOC_dirty,
	ALLOC_cached,
	ALLOC_stripe,
	ALLOC_SECTORS_NR
};

static int data_type_to_alloc_counter(enum bch_data_type t)
{
	switch (t) {
	case BCH_DATA_btree:
	case BCH_DATA_user:
		return ALLOC_dirty;
	case BCH_DATA_cached:
		return ALLOC_cached;
	case BCH_DATA_stripe:
	case BCH_DATA_parity:
		return ALLOC_stripe;
	default:
		return -1;
	}
}

static int check_bucket_backpointers_to_extents(struct btree_trans *, struct bch_dev *, struct bpos);

static int check_bucket_backpointer_mismatch(struct btree_trans *trans, struct bkey_s_c alloc_k,
					     struct bkey_buf *last_flushed)
{
	struct bch_fs *c = trans->c;
	struct bch_alloc_v4 a_convert;
	const struct bch_alloc_v4 *a = bch2_alloc_to_v4(alloc_k, &a_convert);
	bool need_commit = false;

	if (a->data_type == BCH_DATA_sb ||
	    a->data_type == BCH_DATA_journal ||
	    a->data_type == BCH_DATA_parity)
		return 0;

	u32 sectors[ALLOC_SECTORS_NR];
	memset(sectors, 0, sizeof(sectors));

	struct bch_dev *ca = bch2_dev_bucket_tryget_noerror(trans->c, alloc_k.k->p);
	if (!ca)
		return 0;

	struct btree_iter iter;
	struct bkey_s_c bp_k;
	int ret = 0;
	for_each_btree_key_max_norestart(trans, iter, BTREE_ID_backpointers,
				bucket_pos_to_bp_start(ca, alloc_k.k->p),
				bucket_pos_to_bp_end(ca, alloc_k.k->p), 0, bp_k, ret) {
		if (bp_k.k->type != KEY_TYPE_backpointer)
			continue;

		struct bkey_s_c_backpointer bp = bkey_s_c_to_backpointer(bp_k);

		if (c->sb.version_upgrade_complete >= bcachefs_metadata_version_backpointer_bucket_gen &&
		    (bp.v->bucket_gen != a->gen ||
		     bp.v->pad)) {
			ret = bch2_backpointer_del(trans, bp_k.k->p);
			if (ret)
				break;

			need_commit = true;
			continue;
		}

		if (bp.v->bucket_gen != a->gen)
			continue;

		int alloc_counter = data_type_to_alloc_counter(bp.v->data_type);
		if (alloc_counter < 0)
			continue;

		sectors[alloc_counter] += bp.v->bucket_len;
	};
	bch2_trans_iter_exit(trans, &iter);
	if (ret)
		goto err;

	if (need_commit) {
		ret = bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc);
		if (ret)
			goto err;
	}

	if (sectors[ALLOC_dirty]  != a->dirty_sectors ||
	    sectors[ALLOC_cached] != a->cached_sectors ||
	    sectors[ALLOC_stripe] != a->stripe_sectors) {
		if (c->sb.version_upgrade_complete >= bcachefs_metadata_version_backpointer_bucket_gen) {
			ret = bch2_backpointers_maybe_flush(trans, alloc_k, last_flushed);
			if (ret)
				goto err;
		}

		if (sectors[ALLOC_dirty]  > a->dirty_sectors ||
		    sectors[ALLOC_cached] > a->cached_sectors ||
		    sectors[ALLOC_stripe] > a->stripe_sectors) {
			ret = check_bucket_backpointers_to_extents(trans, ca, alloc_k.k->p) ?:
				-BCH_ERR_transaction_restart_nested;
			goto err;
		}

		if (!sectors[ALLOC_dirty] &&
		    !sectors[ALLOC_stripe] &&
		    !sectors[ALLOC_cached])
			__set_bit(alloc_k.k->p.offset, ca->bucket_backpointer_empty);
		else
			__set_bit(alloc_k.k->p.offset, ca->bucket_backpointer_mismatches);
	}
err:
	bch2_dev_put(ca);
	return ret;
}

static bool backpointer_node_has_missing(struct bch_fs *c, struct bkey_s_c k)
{
	switch (k.k->type) {
	case KEY_TYPE_btree_ptr_v2: {
		bool ret = false;

		rcu_read_lock();
		struct bpos pos = bkey_s_c_to_btree_ptr_v2(k).v->min_key;
		while (pos.inode <= k.k->p.inode) {
			if (pos.inode >= c->sb.nr_devices)
				break;

			struct bch_dev *ca = bch2_dev_rcu_noerror(c, pos.inode);
			if (!ca)
				goto next;

			struct bpos bucket = bp_pos_to_bucket(ca, pos);
			bucket.offset = find_next_bit(ca->bucket_backpointer_mismatches,
						      ca->mi.nbuckets, bucket.offset);
			if (bucket.offset == ca->mi.nbuckets)
				goto next;

			ret = bpos_le(bucket_pos_to_bp_end(ca, bucket), k.k->p);
			if (ret)
				break;
next:
			pos = SPOS(pos.inode + 1, 0, 0);
		}
		rcu_read_unlock();

		return ret;
	}
	case KEY_TYPE_btree_ptr:
		return true;
	default:
		return false;
	}
}

static int btree_node_get_and_pin(struct btree_trans *trans, struct bkey_i *k,
				  enum btree_id btree, unsigned level)
{
	struct btree_iter iter;
	bch2_trans_node_iter_init(trans, &iter, btree, k->k.p, 0, level, 0);
	struct btree *b = bch2_btree_iter_peek_node(trans, &iter);
	int ret = PTR_ERR_OR_ZERO(b);
	if (ret)
		goto err;

	if (b)
		bch2_node_pin(trans->c, b);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int bch2_pin_backpointer_nodes_with_missing(struct btree_trans *trans,
						   struct bpos start, struct bpos *end)
{
	struct bch_fs *c = trans->c;
	int ret = 0;

	struct bkey_buf tmp;
	bch2_bkey_buf_init(&tmp);

	bch2_btree_cache_unpin(c);

	*end = SPOS_MAX;

	s64 mem_may_pin = mem_may_pin_bytes(c);
	struct btree_iter iter;
	bch2_trans_node_iter_init(trans, &iter, BTREE_ID_backpointers, start,
				  0, 1, BTREE_ITER_prefetch);
	ret = for_each_btree_key_continue(trans, iter, 0, k, ({
		if (!backpointer_node_has_missing(c, k))
			continue;

		mem_may_pin -= c->opts.btree_node_size;
		if (mem_may_pin <= 0)
			break;

		bch2_bkey_buf_reassemble(&tmp, c, k);
		struct btree_path *path = btree_iter_path(trans, &iter);

		BUG_ON(path->level != 1);

		bch2_btree_node_prefetch(trans, path, tmp.k, path->btree_id, path->level - 1);
	}));
	if (ret)
		return ret;

	struct bpos pinned = SPOS_MAX;
	mem_may_pin = mem_may_pin_bytes(c);
	bch2_trans_node_iter_init(trans, &iter, BTREE_ID_backpointers, start,
				  0, 1, BTREE_ITER_prefetch);
	ret = for_each_btree_key_continue(trans, iter, 0, k, ({
		if (!backpointer_node_has_missing(c, k))
			continue;

		mem_may_pin -= c->opts.btree_node_size;
		if (mem_may_pin <= 0) {
			*end = pinned;
			break;
		}

		bch2_bkey_buf_reassemble(&tmp, c, k);
		struct btree_path *path = btree_iter_path(trans, &iter);

		BUG_ON(path->level != 1);

		int ret2 = btree_node_get_and_pin(trans, tmp.k, path->btree_id, path->level - 1);

		if (!ret2)
			pinned = tmp.k->k.p;

		ret;
	}));
	if (ret)
		return ret;

	return ret;
}

int bch2_check_extents_to_backpointers(struct bch_fs *c)
{
	int ret = 0;

	/*
	 * Can't allow devices to come/go/resize while we have bucket bitmaps
	 * allocated
	 */
	down_read(&c->state_lock);

	for_each_member_device(c, ca) {
		BUG_ON(ca->bucket_backpointer_mismatches);
		ca->bucket_backpointer_mismatches = kvcalloc(BITS_TO_LONGS(ca->mi.nbuckets),
							     sizeof(unsigned long),
							     GFP_KERNEL);
		ca->bucket_backpointer_empty = kvcalloc(BITS_TO_LONGS(ca->mi.nbuckets),
							sizeof(unsigned long),
							GFP_KERNEL);
		if (!ca->bucket_backpointer_mismatches ||
		    !ca->bucket_backpointer_empty) {
			bch2_dev_put(ca);
			ret = -BCH_ERR_ENOMEM_backpointer_mismatches_bitmap;
			goto err_free_bitmaps;
		}
	}

	struct btree_trans *trans = bch2_trans_get(c);
	struct extents_to_bp_state s = { .bp_start = POS_MIN };

	bch2_bkey_buf_init(&s.last_flushed);
	bkey_init(&s.last_flushed.k->k);

	ret = for_each_btree_key(trans, iter, BTREE_ID_alloc,
				 POS_MIN, BTREE_ITER_prefetch, k, ({
		check_bucket_backpointer_mismatch(trans, k, &s.last_flushed);
	}));
	if (ret)
		goto err;

	u64 nr_buckets = 0, nr_mismatches = 0, nr_empty = 0;
	for_each_member_device(c, ca) {
		nr_buckets	+= ca->mi.nbuckets;
		nr_mismatches	+= bitmap_weight(ca->bucket_backpointer_mismatches, ca->mi.nbuckets);
		nr_empty	+= bitmap_weight(ca->bucket_backpointer_empty, ca->mi.nbuckets);
	}

	if (!nr_mismatches && !nr_empty)
		goto err;

	bch_info(c, "scanning for missing backpointers in %llu/%llu buckets",
		 nr_mismatches + nr_empty, nr_buckets);

	while (1) {
		ret = bch2_pin_backpointer_nodes_with_missing(trans, s.bp_start, &s.bp_end);
		if (ret)
			break;

		if ( bpos_eq(s.bp_start, POS_MIN) &&
		    !bpos_eq(s.bp_end, SPOS_MAX))
			bch_verbose(c, "%s(): alloc info does not fit in ram, running in multiple passes with %zu nodes per pass",
				    __func__, btree_nodes_fit_in_ram(c));

		if (!bpos_eq(s.bp_start, POS_MIN) ||
		    !bpos_eq(s.bp_end, SPOS_MAX)) {
			struct printbuf buf = PRINTBUF;

			prt_str(&buf, "check_extents_to_backpointers(): ");
			bch2_bpos_to_text(&buf, s.bp_start);
			prt_str(&buf, "-");
			bch2_bpos_to_text(&buf, s.bp_end);

			bch_verbose(c, "%s", buf.buf);
			printbuf_exit(&buf);
		}

		ret = bch2_check_extents_to_backpointers_pass(trans, &s);
		if (ret || bpos_eq(s.bp_end, SPOS_MAX))
			break;

		s.bp_start = bpos_successor(s.bp_end);
	}
err:
	bch2_trans_put(trans);
	bch2_bkey_buf_exit(&s.last_flushed, c);
	bch2_btree_cache_unpin(c);
err_free_bitmaps:
	for_each_member_device(c, ca) {
		kvfree(ca->bucket_backpointer_empty);
		ca->bucket_backpointer_empty = NULL;
		kvfree(ca->bucket_backpointer_mismatches);
		ca->bucket_backpointer_mismatches = NULL;
	}

	up_read(&c->state_lock);
	bch_err_fn(c, ret);
	return ret;
}

static int check_one_backpointer(struct btree_trans *trans,
				 struct bbpos start,
				 struct bbpos end,
				 struct bkey_s_c bp_k,
				 struct bkey_buf *last_flushed)
{
	if (bp_k.k->type != KEY_TYPE_backpointer)
		return 0;

	struct bkey_s_c_backpointer bp = bkey_s_c_to_backpointer(bp_k);
	struct bbpos pos = bp_to_bbpos(*bp.v);

	if (bbpos_cmp(pos, start) < 0 ||
	    bbpos_cmp(pos, end) > 0)
		return 0;

	struct btree_iter iter;
	struct bkey_s_c k = bch2_backpointer_get_key(trans, bp, &iter, 0, last_flushed);
	int ret = bkey_err(k);
	if (ret == -BCH_ERR_backpointer_to_overwritten_btree_node)
		return 0;
	if (ret)
		return ret;

	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int check_bucket_backpointers_to_extents(struct btree_trans *trans,
						struct bch_dev *ca, struct bpos bucket)
{
	u32 restart_count = trans->restart_count;
	struct bkey_buf last_flushed;
	bch2_bkey_buf_init(&last_flushed);
	bkey_init(&last_flushed.k->k);

	int ret = for_each_btree_key_max(trans, iter, BTREE_ID_backpointers,
				      bucket_pos_to_bp_start(ca, bucket),
				      bucket_pos_to_bp_end(ca, bucket),
				      0, k,
		check_one_backpointer(trans, BBPOS_MIN, BBPOS_MAX, k, &last_flushed)
	);

	bch2_bkey_buf_exit(&last_flushed, trans->c);
	return ret ?: trans_was_restarted(trans, restart_count);
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
	bch2_progress_init(&progress, trans->c, BIT_ULL(BTREE_ID_backpointers));

	int ret = for_each_btree_key(trans, iter, BTREE_ID_backpointers,
				     POS_MIN, BTREE_ITER_prefetch, k, ({
			bch2_progress_update_iter(trans, &progress, &iter, "backpointers_to_extents");
			check_one_backpointer(trans, start, end, k, &last_flushed);
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
