// SPDX-License-Identifier: GPL-2.0
/*
 * Some low level IO code, and hacks for various block layer limitations
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "btree_update.h"
#include "buckets.h"
#include "checksum.h"
#include "clock.h"
#include "compress.h"
#include "data_update.h"
#include "disk_groups.h"
#include "ec.h"
#include "error.h"
#include "io_read.h"
#include "io_misc.h"
#include "io_write.h"
#include "subvolume.h"
#include "trace.h"

#include <linux/sched/mm.h>

#ifndef CONFIG_BCACHEFS_NO_LATENCY_ACCT

static bool bch2_target_congested(struct bch_fs *c, u16 target)
{
	const struct bch_devs_mask *devs;
	unsigned d, nr = 0, total = 0;
	u64 now = local_clock(), last;
	s64 congested;
	struct bch_dev *ca;

	if (!target)
		return false;

	rcu_read_lock();
	devs = bch2_target_to_mask(c, target) ?:
		&c->rw_devs[BCH_DATA_user];

	for_each_set_bit(d, devs->d, BCH_SB_MEMBERS_MAX) {
		ca = rcu_dereference(c->devs[d]);
		if (!ca)
			continue;

		congested = atomic_read(&ca->congested);
		last = READ_ONCE(ca->congested_last);
		if (time_after64(now, last))
			congested -= (now - last) >> 12;

		total += max(congested, 0LL);
		nr++;
	}
	rcu_read_unlock();

	return bch2_rand_range(nr * CONGESTED_MAX) < total;
}

#else

static bool bch2_target_congested(struct bch_fs *c, u16 target)
{
	return false;
}

#endif

/* Cache promotion on read */

struct promote_op {
	struct rcu_head		rcu;
	u64			start_time;

	struct rhash_head	hash;
	struct bpos		pos;

	struct data_update	write;
	struct bio_vec		bi_inline_vecs[]; /* must be last */
};

static const struct rhashtable_params bch_promote_params = {
	.head_offset		= offsetof(struct promote_op, hash),
	.key_offset		= offsetof(struct promote_op, pos),
	.key_len		= sizeof(struct bpos),
	.automatic_shrinking	= true,
};

static inline int should_promote(struct bch_fs *c, struct bkey_s_c k,
				  struct bpos pos,
				  struct bch_io_opts opts,
				  unsigned flags,
				  struct bch_io_failures *failed)
{
	if (!failed) {
		BUG_ON(!opts.promote_target);

		if (!(flags & BCH_READ_MAY_PROMOTE))
			return -BCH_ERR_nopromote_may_not;

		if (bch2_bkey_has_target(c, k, opts.promote_target))
			return -BCH_ERR_nopromote_already_promoted;

		if (bkey_extent_is_unwritten(k))
			return -BCH_ERR_nopromote_unwritten;

		if (bch2_target_congested(c, opts.promote_target))
			return -BCH_ERR_nopromote_congested;
	}

	if (rhashtable_lookup_fast(&c->promote_table, &pos,
				   bch_promote_params))
		return -BCH_ERR_nopromote_in_flight;

	return 0;
}

static void promote_free(struct bch_fs *c, struct promote_op *op)
{
	int ret;

	bch2_data_update_exit(&op->write);

	ret = rhashtable_remove_fast(&c->promote_table, &op->hash,
				     bch_promote_params);
	BUG_ON(ret);
	bch2_write_ref_put(c, BCH_WRITE_REF_promote);
	kfree_rcu(op, rcu);
}

static void promote_done(struct bch_write_op *wop)
{
	struct promote_op *op =
		container_of(wop, struct promote_op, write.op);
	struct bch_fs *c = op->write.op.c;

	bch2_time_stats_update(&c->times[BCH_TIME_data_promote],
			       op->start_time);
	promote_free(c, op);
}

static void promote_start(struct promote_op *op, struct bch_read_bio *rbio)
{
	struct bio *bio = &op->write.op.wbio.bio;

	trace_and_count(op->write.op.c, read_promote, &rbio->bio);

	/* we now own pages: */
	BUG_ON(!rbio->bounce);
	BUG_ON(rbio->bio.bi_vcnt > bio->bi_max_vecs);

	memcpy(bio->bi_io_vec, rbio->bio.bi_io_vec,
	       sizeof(struct bio_vec) * rbio->bio.bi_vcnt);
	swap(bio->bi_vcnt, rbio->bio.bi_vcnt);

	bch2_data_update_read_done(&op->write, rbio->pick.crc);
}

static struct promote_op *__promote_alloc(struct btree_trans *trans,
					  enum btree_id btree_id,
					  struct bkey_s_c k,
					  struct bpos pos,
					  struct extent_ptr_decoded *pick,
					  struct bch_io_opts opts,
					  unsigned sectors,
					  struct bch_read_bio **rbio,
					  struct bch_io_failures *failed)
{
	struct bch_fs *c = trans->c;
	struct promote_op *op = NULL;
	struct bio *bio;
	unsigned pages = DIV_ROUND_UP(sectors, PAGE_SECTORS);
	int ret;

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_promote))
		return ERR_PTR(-BCH_ERR_nopromote_no_writes);

	op = kzalloc(struct_size(op, bi_inline_vecs, pages), GFP_KERNEL);
	if (!op) {
		ret = -BCH_ERR_nopromote_enomem;
		goto err;
	}

	op->start_time = local_clock();
	op->pos = pos;

	/*
	 * We don't use the mempool here because extents that aren't
	 * checksummed or compressed can be too big for the mempool:
	 */
	*rbio = kzalloc(sizeof(struct bch_read_bio) +
			sizeof(struct bio_vec) * pages,
			GFP_KERNEL);
	if (!*rbio) {
		ret = -BCH_ERR_nopromote_enomem;
		goto err;
	}

	rbio_init(&(*rbio)->bio, opts);
	bio_init(&(*rbio)->bio, NULL, (*rbio)->bio.bi_inline_vecs, pages, 0);

	if (bch2_bio_alloc_pages(&(*rbio)->bio, sectors << 9, GFP_KERNEL)) {
		ret = -BCH_ERR_nopromote_enomem;
		goto err;
	}

	(*rbio)->bounce		= true;
	(*rbio)->split		= true;
	(*rbio)->kmalloc	= true;

	if (rhashtable_lookup_insert_fast(&c->promote_table, &op->hash,
					  bch_promote_params)) {
		ret = -BCH_ERR_nopromote_in_flight;
		goto err;
	}

	bio = &op->write.op.wbio.bio;
	bio_init(bio, NULL, bio->bi_inline_vecs, pages, 0);

	struct data_update_opts update_opts = {};

	if (!failed) {
		update_opts.target = opts.promote_target;
		update_opts.extra_replicas = 1;
		update_opts.write_flags = BCH_WRITE_ALLOC_NOWAIT|BCH_WRITE_CACHED;
	} else {
		update_opts.target = opts.foreground_target;

		struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
		unsigned i = 0;
		bkey_for_each_ptr(ptrs, ptr) {
			if (bch2_dev_io_failures(failed, ptr->dev))
				update_opts.rewrite_ptrs |= BIT(i);
			i++;
		}
	}

	ret = bch2_data_update_init(trans, NULL, NULL, &op->write,
			writepoint_hashed((unsigned long) current),
			opts,
			update_opts,
			btree_id, k);
	/*
	 * possible errors: -BCH_ERR_nocow_lock_blocked,
	 * -BCH_ERR_ENOSPC_disk_reservation:
	 */
	if (ret) {
		BUG_ON(rhashtable_remove_fast(&c->promote_table, &op->hash,
					      bch_promote_params));
		goto err;
	}

	op->write.op.end_io = promote_done;

	return op;
err:
	if (*rbio)
		bio_free_pages(&(*rbio)->bio);
	kfree(*rbio);
	*rbio = NULL;
	kfree(op);
	bch2_write_ref_put(c, BCH_WRITE_REF_promote);
	return ERR_PTR(ret);
}

noinline
static struct promote_op *promote_alloc(struct btree_trans *trans,
					struct bvec_iter iter,
					struct bkey_s_c k,
					struct extent_ptr_decoded *pick,
					struct bch_io_opts opts,
					unsigned flags,
					struct bch_read_bio **rbio,
					bool *bounce,
					bool *read_full,
					struct bch_io_failures *failed)
{
	struct bch_fs *c = trans->c;
	/*
	 * if failed != NULL we're not actually doing a promote, we're
	 * recovering from an io/checksum error
	 */
	bool promote_full = (failed ||
			     *read_full ||
			     READ_ONCE(c->opts.promote_whole_extents));
	/* data might have to be decompressed in the write path: */
	unsigned sectors = promote_full
		? max(pick->crc.compressed_size, pick->crc.live_size)
		: bvec_iter_sectors(iter);
	struct bpos pos = promote_full
		? bkey_start_pos(k.k)
		: POS(k.k->p.inode, iter.bi_sector);
	struct promote_op *promote;
	int ret;

	ret = should_promote(c, k, pos, opts, flags, failed);
	if (ret)
		goto nopromote;

	promote = __promote_alloc(trans,
				  k.k->type == KEY_TYPE_reflink_v
				  ? BTREE_ID_reflink
				  : BTREE_ID_extents,
				  k, pos, pick, opts, sectors, rbio, failed);
	ret = PTR_ERR_OR_ZERO(promote);
	if (ret)
		goto nopromote;

	*bounce		= true;
	*read_full	= promote_full;
	return promote;
nopromote:
	trace_read_nopromote(c, ret);
	return NULL;
}

/* Read */

#define READ_RETRY_AVOID	1
#define READ_RETRY		2
#define READ_ERR		3

enum rbio_context {
	RBIO_CONTEXT_NULL,
	RBIO_CONTEXT_HIGHPRI,
	RBIO_CONTEXT_UNBOUND,
};

static inline struct bch_read_bio *
bch2_rbio_parent(struct bch_read_bio *rbio)
{
	return rbio->split ? rbio->parent : rbio;
}

__always_inline
static void bch2_rbio_punt(struct bch_read_bio *rbio, work_func_t fn,
			   enum rbio_context context,
			   struct workqueue_struct *wq)
{
	if (context <= rbio->context) {
		fn(&rbio->work);
	} else {
		rbio->work.func		= fn;
		rbio->context		= context;
		queue_work(wq, &rbio->work);
	}
}

static inline struct bch_read_bio *bch2_rbio_free(struct bch_read_bio *rbio)
{
	BUG_ON(rbio->bounce && !rbio->split);

	if (rbio->promote)
		promote_free(rbio->c, rbio->promote);
	rbio->promote = NULL;

	if (rbio->bounce)
		bch2_bio_free_pages_pool(rbio->c, &rbio->bio);

	if (rbio->split) {
		struct bch_read_bio *parent = rbio->parent;

		if (rbio->kmalloc)
			kfree(rbio);
		else
			bio_put(&rbio->bio);

		rbio = parent;
	}

	return rbio;
}

/*
 * Only called on a top level bch_read_bio to complete an entire read request,
 * not a split:
 */
static void bch2_rbio_done(struct bch_read_bio *rbio)
{
	if (rbio->start_time)
		bch2_time_stats_update(&rbio->c->times[BCH_TIME_data_read],
				       rbio->start_time);
	bio_endio(&rbio->bio);
}

static void bch2_read_retry_nodecode(struct bch_fs *c, struct bch_read_bio *rbio,
				     struct bvec_iter bvec_iter,
				     struct bch_io_failures *failed,
				     unsigned flags)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	struct bkey_buf sk;
	struct bkey_s_c k;
	int ret;

	flags &= ~BCH_READ_LAST_FRAGMENT;
	flags |= BCH_READ_MUST_CLONE;

	bch2_bkey_buf_init(&sk);

	bch2_trans_iter_init(trans, &iter, rbio->data_btree,
			     rbio->read_pos, BTREE_ITER_slots);
retry:
	bch2_trans_begin(trans);
	rbio->bio.bi_status = 0;

	k = bch2_btree_iter_peek_slot(&iter);
	if (bkey_err(k))
		goto err;

	bch2_bkey_buf_reassemble(&sk, c, k);
	k = bkey_i_to_s_c(sk.k);

	if (!bch2_bkey_matches_ptr(c, k,
				   rbio->pick.ptr,
				   rbio->data_pos.offset -
				   rbio->pick.crc.offset)) {
		/* extent we wanted to read no longer exists: */
		rbio->hole = true;
		goto out;
	}

	ret = __bch2_read_extent(trans, rbio, bvec_iter,
				 rbio->read_pos,
				 rbio->data_btree,
				 k, 0, failed, flags);
	if (ret == READ_RETRY)
		goto retry;
	if (ret)
		goto err;
out:
	bch2_rbio_done(rbio);
	bch2_trans_iter_exit(trans, &iter);
	bch2_trans_put(trans);
	bch2_bkey_buf_exit(&sk, c);
	return;
err:
	rbio->bio.bi_status = BLK_STS_IOERR;
	goto out;
}

static void bch2_rbio_retry(struct work_struct *work)
{
	struct bch_read_bio *rbio =
		container_of(work, struct bch_read_bio, work);
	struct bch_fs *c	= rbio->c;
	struct bvec_iter iter	= rbio->bvec_iter;
	unsigned flags		= rbio->flags;
	subvol_inum inum = {
		.subvol = rbio->subvol,
		.inum	= rbio->read_pos.inode,
	};
	struct bch_io_failures failed = { .nr = 0 };

	trace_and_count(c, read_retry, &rbio->bio);

	if (rbio->retry == READ_RETRY_AVOID)
		bch2_mark_io_failure(&failed, &rbio->pick);

	rbio->bio.bi_status = 0;

	rbio = bch2_rbio_free(rbio);

	flags |= BCH_READ_IN_RETRY;
	flags &= ~BCH_READ_MAY_PROMOTE;

	if (flags & BCH_READ_NODECODE) {
		bch2_read_retry_nodecode(c, rbio, iter, &failed, flags);
	} else {
		flags &= ~BCH_READ_LAST_FRAGMENT;
		flags |= BCH_READ_MUST_CLONE;

		__bch2_read(c, rbio, iter, inum, &failed, flags);
	}
}

static void bch2_rbio_error(struct bch_read_bio *rbio, int retry,
			    blk_status_t error)
{
	rbio->retry = retry;

	if (rbio->flags & BCH_READ_IN_RETRY)
		return;

	if (retry == READ_ERR) {
		rbio = bch2_rbio_free(rbio);

		rbio->bio.bi_status = error;
		bch2_rbio_done(rbio);
	} else {
		bch2_rbio_punt(rbio, bch2_rbio_retry,
			       RBIO_CONTEXT_UNBOUND, system_unbound_wq);
	}
}

static int __bch2_rbio_narrow_crcs(struct btree_trans *trans,
				   struct bch_read_bio *rbio)
{
	struct bch_fs *c = rbio->c;
	u64 data_offset = rbio->data_pos.offset - rbio->pick.crc.offset;
	struct bch_extent_crc_unpacked new_crc;
	struct btree_iter iter;
	struct bkey_i *new;
	struct bkey_s_c k;
	int ret = 0;

	if (crc_is_compressed(rbio->pick.crc))
		return 0;

	k = bch2_bkey_get_iter(trans, &iter, rbio->data_btree, rbio->data_pos,
			       BTREE_ITER_slots|BTREE_ITER_intent);
	if ((ret = bkey_err(k)))
		goto out;

	if (bversion_cmp(k.k->bversion, rbio->version) ||
	    !bch2_bkey_matches_ptr(c, k, rbio->pick.ptr, data_offset))
		goto out;

	/* Extent was merged? */
	if (bkey_start_offset(k.k) < data_offset ||
	    k.k->p.offset > data_offset + rbio->pick.crc.uncompressed_size)
		goto out;

	if (bch2_rechecksum_bio(c, &rbio->bio, rbio->version,
			rbio->pick.crc, NULL, &new_crc,
			bkey_start_offset(k.k) - data_offset, k.k->size,
			rbio->pick.crc.csum_type)) {
		bch_err(c, "error verifying existing checksum while narrowing checksum (memory corruption?)");
		ret = 0;
		goto out;
	}

	/*
	 * going to be temporarily appending another checksum entry:
	 */
	new = bch2_trans_kmalloc(trans, bkey_bytes(k.k) +
				 sizeof(struct bch_extent_crc128));
	if ((ret = PTR_ERR_OR_ZERO(new)))
		goto out;

	bkey_reassemble(new, k);

	if (!bch2_bkey_narrow_crcs(new, new_crc))
		goto out;

	ret = bch2_trans_update(trans, &iter, new,
				BTREE_UPDATE_internal_snapshot_node);
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static noinline void bch2_rbio_narrow_crcs(struct bch_read_bio *rbio)
{
	bch2_trans_do(rbio->c, NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
		      __bch2_rbio_narrow_crcs(trans, rbio));
}

/* Inner part that may run in process context */
static void __bch2_read_endio(struct work_struct *work)
{
	struct bch_read_bio *rbio =
		container_of(work, struct bch_read_bio, work);
	struct bch_fs *c	= rbio->c;
	struct bio *src		= &rbio->bio;
	struct bio *dst		= &bch2_rbio_parent(rbio)->bio;
	struct bvec_iter dst_iter = rbio->bvec_iter;
	struct bch_extent_crc_unpacked crc = rbio->pick.crc;
	struct nonce nonce = extent_nonce(rbio->version, crc);
	unsigned nofs_flags;
	struct bch_csum csum;
	int ret;

	nofs_flags = memalloc_nofs_save();

	/* Reset iterator for checksumming and copying bounced data: */
	if (rbio->bounce) {
		src->bi_iter.bi_size		= crc.compressed_size << 9;
		src->bi_iter.bi_idx		= 0;
		src->bi_iter.bi_bvec_done	= 0;
	} else {
		src->bi_iter			= rbio->bvec_iter;
	}

	csum = bch2_checksum_bio(c, crc.csum_type, nonce, src);
	if (bch2_crc_cmp(csum, rbio->pick.crc.csum) && !c->opts.no_data_io)
		goto csum_err;

	/*
	 * XXX
	 * We need to rework the narrow_crcs path to deliver the read completion
	 * first, and then punt to a different workqueue, otherwise we're
	 * holding up reads while doing btree updates which is bad for memory
	 * reclaim.
	 */
	if (unlikely(rbio->narrow_crcs))
		bch2_rbio_narrow_crcs(rbio);

	if (rbio->flags & BCH_READ_NODECODE)
		goto nodecode;

	/* Adjust crc to point to subset of data we want: */
	crc.offset     += rbio->offset_into_extent;
	crc.live_size	= bvec_iter_sectors(rbio->bvec_iter);

	if (crc_is_compressed(crc)) {
		ret = bch2_encrypt_bio(c, crc.csum_type, nonce, src);
		if (ret)
			goto decrypt_err;

		if (bch2_bio_uncompress(c, src, dst, dst_iter, crc) &&
		    !c->opts.no_data_io)
			goto decompression_err;
	} else {
		/* don't need to decrypt the entire bio: */
		nonce = nonce_add(nonce, crc.offset << 9);
		bio_advance(src, crc.offset << 9);

		BUG_ON(src->bi_iter.bi_size < dst_iter.bi_size);
		src->bi_iter.bi_size = dst_iter.bi_size;

		ret = bch2_encrypt_bio(c, crc.csum_type, nonce, src);
		if (ret)
			goto decrypt_err;

		if (rbio->bounce) {
			struct bvec_iter src_iter = src->bi_iter;

			bio_copy_data_iter(dst, &dst_iter, src, &src_iter);
		}
	}

	if (rbio->promote) {
		/*
		 * Re encrypt data we decrypted, so it's consistent with
		 * rbio->crc:
		 */
		ret = bch2_encrypt_bio(c, crc.csum_type, nonce, src);
		if (ret)
			goto decrypt_err;

		promote_start(rbio->promote, rbio);
		rbio->promote = NULL;
	}
nodecode:
	if (likely(!(rbio->flags & BCH_READ_IN_RETRY))) {
		rbio = bch2_rbio_free(rbio);
		bch2_rbio_done(rbio);
	}
out:
	memalloc_nofs_restore(nofs_flags);
	return;
csum_err:
	/*
	 * Checksum error: if the bio wasn't bounced, we may have been
	 * reading into buffers owned by userspace (that userspace can
	 * scribble over) - retry the read, bouncing it this time:
	 */
	if (!rbio->bounce && (rbio->flags & BCH_READ_USER_MAPPED)) {
		rbio->flags |= BCH_READ_MUST_BOUNCE;
		bch2_rbio_error(rbio, READ_RETRY, BLK_STS_IOERR);
		goto out;
	}

	struct printbuf buf = PRINTBUF;
	buf.atomic++;
	prt_str(&buf, "data ");
	bch2_csum_err_msg(&buf, crc.csum_type, rbio->pick.crc.csum, csum);

	struct bch_dev *ca = rbio->have_ioref ? bch2_dev_have_ref(c, rbio->pick.ptr.dev) : NULL;
	if (ca) {
		bch_err_inum_offset_ratelimited(ca,
			rbio->read_pos.inode,
			rbio->read_pos.offset << 9,
			"data %s", buf.buf);
		bch2_io_error(ca, BCH_MEMBER_ERROR_checksum);
	}
	printbuf_exit(&buf);
	bch2_rbio_error(rbio, READ_RETRY_AVOID, BLK_STS_IOERR);
	goto out;
decompression_err:
	bch_err_inum_offset_ratelimited(c, rbio->read_pos.inode,
					rbio->read_pos.offset << 9,
					"decompression error");
	bch2_rbio_error(rbio, READ_ERR, BLK_STS_IOERR);
	goto out;
decrypt_err:
	bch_err_inum_offset_ratelimited(c, rbio->read_pos.inode,
					rbio->read_pos.offset << 9,
					"decrypt error");
	bch2_rbio_error(rbio, READ_ERR, BLK_STS_IOERR);
	goto out;
}

static void bch2_read_endio(struct bio *bio)
{
	struct bch_read_bio *rbio =
		container_of(bio, struct bch_read_bio, bio);
	struct bch_fs *c	= rbio->c;
	struct bch_dev *ca = rbio->have_ioref ? bch2_dev_have_ref(c, rbio->pick.ptr.dev) : NULL;
	struct workqueue_struct *wq = NULL;
	enum rbio_context context = RBIO_CONTEXT_NULL;

	if (rbio->have_ioref) {
		bch2_latency_acct(ca, rbio->submit_time, READ);
		percpu_ref_put(&ca->io_ref);
	}

	if (!rbio->split)
		rbio->bio.bi_end_io = rbio->end_io;

	if (bio->bi_status) {
		if (ca) {
			bch_err_inum_offset_ratelimited(ca,
				rbio->read_pos.inode,
				rbio->read_pos.offset,
				"data read error: %s",
				bch2_blk_status_to_str(bio->bi_status));
			bch2_io_error(ca, BCH_MEMBER_ERROR_read);
		}
		bch2_rbio_error(rbio, READ_RETRY_AVOID, bio->bi_status);
		return;
	}

	if (((rbio->flags & BCH_READ_RETRY_IF_STALE) && race_fault()) ||
	    (ca && dev_ptr_stale(ca, &rbio->pick.ptr))) {
		trace_and_count(c, read_reuse_race, &rbio->bio);

		if (rbio->flags & BCH_READ_RETRY_IF_STALE)
			bch2_rbio_error(rbio, READ_RETRY, BLK_STS_AGAIN);
		else
			bch2_rbio_error(rbio, READ_ERR, BLK_STS_AGAIN);
		return;
	}

	if (rbio->narrow_crcs ||
	    rbio->promote ||
	    crc_is_compressed(rbio->pick.crc) ||
	    bch2_csum_type_is_encryption(rbio->pick.crc.csum_type))
		context = RBIO_CONTEXT_UNBOUND,	wq = system_unbound_wq;
	else if (rbio->pick.crc.csum_type)
		context = RBIO_CONTEXT_HIGHPRI,	wq = system_highpri_wq;

	bch2_rbio_punt(rbio, __bch2_read_endio, context, wq);
}

int __bch2_read_indirect_extent(struct btree_trans *trans,
				unsigned *offset_into_extent,
				struct bkey_buf *orig_k)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	u64 reflink_offset;
	int ret;

	reflink_offset = le64_to_cpu(bkey_i_to_reflink_p(orig_k->k)->v.idx) +
		*offset_into_extent;

	k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_reflink,
			       POS(0, reflink_offset), 0);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (k.k->type != KEY_TYPE_reflink_v &&
	    k.k->type != KEY_TYPE_indirect_inline_data) {
		bch_err_inum_offset_ratelimited(trans->c,
			orig_k->k->k.p.inode,
			orig_k->k->k.p.offset << 9,
			"%llu len %u points to nonexistent indirect extent %llu",
			orig_k->k->k.p.offset,
			orig_k->k->k.size,
			reflink_offset);
		bch2_inconsistent_error(trans->c);
		ret = -BCH_ERR_missing_indirect_extent;
		goto err;
	}

	*offset_into_extent = iter.pos.offset - bkey_start_offset(k.k);
	bch2_bkey_buf_reassemble(orig_k, trans->c, k);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static noinline void read_from_stale_dirty_pointer(struct btree_trans *trans,
						   struct bch_dev *ca,
						   struct bkey_s_c k,
						   struct bch_extent_ptr ptr)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct printbuf buf = PRINTBUF;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_alloc,
			     PTR_BUCKET_POS(ca, &ptr),
			     BTREE_ITER_cached);

	u8 *gen = bucket_gen(ca, iter.pos.offset);
	if (gen) {

		prt_printf(&buf, "Attempting to read from stale dirty pointer:\n");
		printbuf_indent_add(&buf, 2);

		bch2_bkey_val_to_text(&buf, c, k);
		prt_newline(&buf);

		prt_printf(&buf, "memory gen: %u", *gen);

		ret = lockrestart_do(trans, bkey_err(k = bch2_btree_iter_peek_slot(&iter)));
		if (!ret) {
			prt_newline(&buf);
			bch2_bkey_val_to_text(&buf, c, k);
		}
	} else {
		prt_printf(&buf, "Attempting to read from invalid bucket %llu:%llu:\n",
			   iter.pos.inode, iter.pos.offset);
		printbuf_indent_add(&buf, 2);

		prt_printf(&buf, "first bucket %u nbuckets %llu\n",
			   ca->mi.first_bucket, ca->mi.nbuckets);

		bch2_bkey_val_to_text(&buf, c, k);
		prt_newline(&buf);
	}

	bch2_fs_inconsistent(c, "%s", buf.buf);

	bch2_trans_iter_exit(trans, &iter);
	printbuf_exit(&buf);
}

int __bch2_read_extent(struct btree_trans *trans, struct bch_read_bio *orig,
		       struct bvec_iter iter, struct bpos read_pos,
		       enum btree_id data_btree, struct bkey_s_c k,
		       unsigned offset_into_extent,
		       struct bch_io_failures *failed, unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct extent_ptr_decoded pick;
	struct bch_read_bio *rbio = NULL;
	struct promote_op *promote = NULL;
	bool bounce = false, read_full = false, narrow_crcs = false;
	struct bpos data_pos = bkey_start_pos(k.k);
	int pick_ret;

	if (bkey_extent_is_inline_data(k.k)) {
		unsigned bytes = min_t(unsigned, iter.bi_size,
				       bkey_inline_data_bytes(k.k));

		swap(iter.bi_size, bytes);
		memcpy_to_bio(&orig->bio, iter, bkey_inline_data_p(k));
		swap(iter.bi_size, bytes);
		bio_advance_iter(&orig->bio, &iter, bytes);
		zero_fill_bio_iter(&orig->bio, iter);
		goto out_read_done;
	}
retry_pick:
	pick_ret = bch2_bkey_pick_read_device(c, k, failed, &pick);

	/* hole or reservation - just zero fill: */
	if (!pick_ret)
		goto hole;

	if (pick_ret < 0) {
		struct printbuf buf = PRINTBUF;
		bch2_bkey_val_to_text(&buf, c, k);

		bch_err_inum_offset_ratelimited(c,
				read_pos.inode, read_pos.offset << 9,
				"no device to read from: %s\n  %s",
				bch2_err_str(pick_ret),
				buf.buf);
		printbuf_exit(&buf);
		goto err;
	}

	struct bch_dev *ca = bch2_dev_get_ioref(c, pick.ptr.dev, READ);

	/*
	 * Stale dirty pointers are treated as IO errors, but @failed isn't
	 * allocated unless we're in the retry path - so if we're not in the
	 * retry path, don't check here, it'll be caught in bch2_read_endio()
	 * and we'll end up in the retry path:
	 */
	if ((flags & BCH_READ_IN_RETRY) &&
	    !pick.ptr.cached &&
	    ca &&
	    unlikely(dev_ptr_stale(ca, &pick.ptr))) {
		read_from_stale_dirty_pointer(trans, ca, k, pick.ptr);
		bch2_mark_io_failure(failed, &pick);
		percpu_ref_put(&ca->io_ref);
		goto retry_pick;
	}

	/*
	 * Unlock the iterator while the btree node's lock is still in
	 * cache, before doing the IO:
	 */
	bch2_trans_unlock(trans);

	if (flags & BCH_READ_NODECODE) {
		/*
		 * can happen if we retry, and the extent we were going to read
		 * has been merged in the meantime:
		 */
		if (pick.crc.compressed_size > orig->bio.bi_vcnt * PAGE_SECTORS) {
			if (ca)
				percpu_ref_put(&ca->io_ref);
			goto hole;
		}

		iter.bi_size	= pick.crc.compressed_size << 9;
		goto get_bio;
	}

	if (!(flags & BCH_READ_LAST_FRAGMENT) ||
	    bio_flagged(&orig->bio, BIO_CHAIN))
		flags |= BCH_READ_MUST_CLONE;

	narrow_crcs = !(flags & BCH_READ_IN_RETRY) &&
		bch2_can_narrow_extent_crcs(k, pick.crc);

	if (narrow_crcs && (flags & BCH_READ_USER_MAPPED))
		flags |= BCH_READ_MUST_BOUNCE;

	EBUG_ON(offset_into_extent + bvec_iter_sectors(iter) > k.k->size);

	if (crc_is_compressed(pick.crc) ||
	    (pick.crc.csum_type != BCH_CSUM_none &&
	     (bvec_iter_sectors(iter) != pick.crc.uncompressed_size ||
	      (bch2_csum_type_is_encryption(pick.crc.csum_type) &&
	       (flags & BCH_READ_USER_MAPPED)) ||
	      (flags & BCH_READ_MUST_BOUNCE)))) {
		read_full = true;
		bounce = true;
	}

	if (orig->opts.promote_target)// || failed)
		promote = promote_alloc(trans, iter, k, &pick, orig->opts, flags,
					&rbio, &bounce, &read_full, failed);

	if (!read_full) {
		EBUG_ON(crc_is_compressed(pick.crc));
		EBUG_ON(pick.crc.csum_type &&
			(bvec_iter_sectors(iter) != pick.crc.uncompressed_size ||
			 bvec_iter_sectors(iter) != pick.crc.live_size ||
			 pick.crc.offset ||
			 offset_into_extent));

		data_pos.offset += offset_into_extent;
		pick.ptr.offset += pick.crc.offset +
			offset_into_extent;
		offset_into_extent		= 0;
		pick.crc.compressed_size	= bvec_iter_sectors(iter);
		pick.crc.uncompressed_size	= bvec_iter_sectors(iter);
		pick.crc.offset			= 0;
		pick.crc.live_size		= bvec_iter_sectors(iter);
	}
get_bio:
	if (rbio) {
		/*
		 * promote already allocated bounce rbio:
		 * promote needs to allocate a bio big enough for uncompressing
		 * data in the write path, but we're not going to use it all
		 * here:
		 */
		EBUG_ON(rbio->bio.bi_iter.bi_size <
		       pick.crc.compressed_size << 9);
		rbio->bio.bi_iter.bi_size =
			pick.crc.compressed_size << 9;
	} else if (bounce) {
		unsigned sectors = pick.crc.compressed_size;

		rbio = rbio_init(bio_alloc_bioset(NULL,
						  DIV_ROUND_UP(sectors, PAGE_SECTORS),
						  0,
						  GFP_NOFS,
						  &c->bio_read_split),
				 orig->opts);

		bch2_bio_alloc_pages_pool(c, &rbio->bio, sectors << 9);
		rbio->bounce	= true;
		rbio->split	= true;
	} else if (flags & BCH_READ_MUST_CLONE) {
		/*
		 * Have to clone if there were any splits, due to error
		 * reporting issues (if a split errored, and retrying didn't
		 * work, when it reports the error to its parent (us) we don't
		 * know if the error was from our bio, and we should retry, or
		 * from the whole bio, in which case we don't want to retry and
		 * lose the error)
		 */
		rbio = rbio_init(bio_alloc_clone(NULL, &orig->bio, GFP_NOFS,
						 &c->bio_read_split),
				 orig->opts);
		rbio->bio.bi_iter = iter;
		rbio->split	= true;
	} else {
		rbio = orig;
		rbio->bio.bi_iter = iter;
		EBUG_ON(bio_flagged(&rbio->bio, BIO_CHAIN));
	}

	EBUG_ON(bio_sectors(&rbio->bio) != pick.crc.compressed_size);

	rbio->c			= c;
	rbio->submit_time	= local_clock();
	if (rbio->split)
		rbio->parent	= orig;
	else
		rbio->end_io	= orig->bio.bi_end_io;
	rbio->bvec_iter		= iter;
	rbio->offset_into_extent= offset_into_extent;
	rbio->flags		= flags;
	rbio->have_ioref	= ca != NULL;
	rbio->narrow_crcs	= narrow_crcs;
	rbio->hole		= 0;
	rbio->retry		= 0;
	rbio->context		= 0;
	/* XXX: only initialize this if needed */
	rbio->devs_have		= bch2_bkey_devs(k);
	rbio->pick		= pick;
	rbio->subvol		= orig->subvol;
	rbio->read_pos		= read_pos;
	rbio->data_btree	= data_btree;
	rbio->data_pos		= data_pos;
	rbio->version		= k.k->bversion;
	rbio->promote		= promote;
	INIT_WORK(&rbio->work, NULL);

	if (flags & BCH_READ_NODECODE)
		orig->pick = pick;

	rbio->bio.bi_opf	= orig->bio.bi_opf;
	rbio->bio.bi_iter.bi_sector = pick.ptr.offset;
	rbio->bio.bi_end_io	= bch2_read_endio;

	if (rbio->bounce)
		trace_and_count(c, read_bounce, &rbio->bio);

	this_cpu_add(c->counters[BCH_COUNTER_io_read], bio_sectors(&rbio->bio));
	bch2_increment_clock(c, bio_sectors(&rbio->bio), READ);

	/*
	 * If it's being moved internally, we don't want to flag it as a cache
	 * hit:
	 */
	if (ca && pick.ptr.cached && !(flags & BCH_READ_NODECODE))
		bch2_bucket_io_time_reset(trans, pick.ptr.dev,
			PTR_BUCKET_NR(ca, &pick.ptr), READ);

	if (!(flags & (BCH_READ_IN_RETRY|BCH_READ_LAST_FRAGMENT))) {
		bio_inc_remaining(&orig->bio);
		trace_and_count(c, read_split, &orig->bio);
	}

	if (!rbio->pick.idx) {
		if (!rbio->have_ioref) {
			bch_err_inum_offset_ratelimited(c,
					read_pos.inode,
					read_pos.offset << 9,
					"no device to read from");
			bch2_rbio_error(rbio, READ_RETRY_AVOID, BLK_STS_IOERR);
			goto out;
		}

		this_cpu_add(ca->io_done->sectors[READ][BCH_DATA_user],
			     bio_sectors(&rbio->bio));
		bio_set_dev(&rbio->bio, ca->disk_sb.bdev);

		if (unlikely(c->opts.no_data_io)) {
			if (likely(!(flags & BCH_READ_IN_RETRY)))
				bio_endio(&rbio->bio);
		} else {
			if (likely(!(flags & BCH_READ_IN_RETRY)))
				submit_bio(&rbio->bio);
			else
				submit_bio_wait(&rbio->bio);
		}

		/*
		 * We just submitted IO which may block, we expect relock fail
		 * events and shouldn't count them:
		 */
		trans->notrace_relock_fail = true;
	} else {
		/* Attempting reconstruct read: */
		if (bch2_ec_read_extent(trans, rbio, k)) {
			bch2_rbio_error(rbio, READ_RETRY_AVOID, BLK_STS_IOERR);
			goto out;
		}

		if (likely(!(flags & BCH_READ_IN_RETRY)))
			bio_endio(&rbio->bio);
	}
out:
	if (likely(!(flags & BCH_READ_IN_RETRY))) {
		return 0;
	} else {
		int ret;

		rbio->context = RBIO_CONTEXT_UNBOUND;
		bch2_read_endio(&rbio->bio);

		ret = rbio->retry;
		rbio = bch2_rbio_free(rbio);

		if (ret == READ_RETRY_AVOID) {
			bch2_mark_io_failure(failed, &pick);
			ret = READ_RETRY;
		}

		if (!ret)
			goto out_read_done;

		return ret;
	}

err:
	if (flags & BCH_READ_IN_RETRY)
		return READ_ERR;

	orig->bio.bi_status = BLK_STS_IOERR;
	goto out_read_done;

hole:
	/*
	 * won't normally happen in the BCH_READ_NODECODE
	 * (bch2_move_extent()) path, but if we retry and the extent we wanted
	 * to read no longer exists we have to signal that:
	 */
	if (flags & BCH_READ_NODECODE)
		orig->hole = true;

	zero_fill_bio_iter(&orig->bio, iter);
out_read_done:
	if (flags & BCH_READ_LAST_FRAGMENT)
		bch2_rbio_done(orig);
	return 0;
}

void __bch2_read(struct bch_fs *c, struct bch_read_bio *rbio,
		 struct bvec_iter bvec_iter, subvol_inum inum,
		 struct bch_io_failures *failed, unsigned flags)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	struct bkey_buf sk;
	struct bkey_s_c k;
	int ret;

	BUG_ON(flags & BCH_READ_NODECODE);

	bch2_bkey_buf_init(&sk);
	bch2_trans_iter_init(trans, &iter, BTREE_ID_extents,
			     POS(inum.inum, bvec_iter.bi_sector),
			     BTREE_ITER_slots);

	while (1) {
		unsigned bytes, sectors, offset_into_extent;
		enum btree_id data_btree = BTREE_ID_extents;

		bch2_trans_begin(trans);

		u32 snapshot;
		ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
		if (ret)
			goto err;

		bch2_btree_iter_set_snapshot(&iter, snapshot);

		bch2_btree_iter_set_pos(&iter,
				POS(inum.inum, bvec_iter.bi_sector));

		k = bch2_btree_iter_peek_slot(&iter);
		ret = bkey_err(k);
		if (ret)
			goto err;

		offset_into_extent = iter.pos.offset -
			bkey_start_offset(k.k);
		sectors = k.k->size - offset_into_extent;

		bch2_bkey_buf_reassemble(&sk, c, k);

		ret = bch2_read_indirect_extent(trans, &data_btree,
					&offset_into_extent, &sk);
		if (ret)
			goto err;

		k = bkey_i_to_s_c(sk.k);

		/*
		 * With indirect extents, the amount of data to read is the min
		 * of the original extent and the indirect extent:
		 */
		sectors = min(sectors, k.k->size - offset_into_extent);

		bytes = min(sectors, bvec_iter_sectors(bvec_iter)) << 9;
		swap(bvec_iter.bi_size, bytes);

		if (bvec_iter.bi_size == bytes)
			flags |= BCH_READ_LAST_FRAGMENT;

		ret = __bch2_read_extent(trans, rbio, bvec_iter, iter.pos,
					 data_btree, k,
					 offset_into_extent, failed, flags);
		if (ret)
			goto err;

		if (flags & BCH_READ_LAST_FRAGMENT)
			break;

		swap(bvec_iter.bi_size, bytes);
		bio_advance_iter(&rbio->bio, &bvec_iter, bytes);
err:
		if (ret &&
		    !bch2_err_matches(ret, BCH_ERR_transaction_restart) &&
		    ret != READ_RETRY &&
		    ret != READ_RETRY_AVOID)
			break;
	}

	bch2_trans_iter_exit(trans, &iter);
	bch2_trans_put(trans);
	bch2_bkey_buf_exit(&sk, c);

	if (ret) {
		bch_err_inum_offset_ratelimited(c, inum.inum,
						bvec_iter.bi_sector << 9,
						"read error %i from btree lookup", ret);
		rbio->bio.bi_status = BLK_STS_IOERR;
		bch2_rbio_done(rbio);
	}
}

void bch2_fs_io_read_exit(struct bch_fs *c)
{
	if (c->promote_table.tbl)
		rhashtable_destroy(&c->promote_table);
	bioset_exit(&c->bio_read_split);
	bioset_exit(&c->bio_read);
}

int bch2_fs_io_read_init(struct bch_fs *c)
{
	if (bioset_init(&c->bio_read, 1, offsetof(struct bch_read_bio, bio),
			BIOSET_NEED_BVECS))
		return -BCH_ERR_ENOMEM_bio_read_init;

	if (bioset_init(&c->bio_read_split, 1, offsetof(struct bch_read_bio, bio),
			BIOSET_NEED_BVECS))
		return -BCH_ERR_ENOMEM_bio_read_split_init;

	if (rhashtable_init(&c->promote_table, &bch_promote_params))
		return -BCH_ERR_ENOMEM_promote_table_init;

	return 0;
}
