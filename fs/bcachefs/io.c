// SPDX-License-Identifier: GPL-2.0
/*
 * Some low level IO code, and hacks for various block layer limitations
 *
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "bset.h"
#include "btree_update.h"
#include "buckets.h"
#include "checksum.h"
#include "compress.h"
#include "clock.h"
#include "debug.h"
#include "disk_groups.h"
#include "ec.h"
#include "error.h"
#include "extents.h"
#include "io.h"
#include "journal.h"
#include "keylist.h"
#include "move.h"
#include "rebalance.h"
#include "super.h"
#include "super-io.h"
#include "trace.h"

#include <linux/blkdev.h>
#include <linux/random.h>

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
	devs = bch2_target_to_mask(c, target);
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

static inline void bch2_congested_acct(struct bch_dev *ca, u64 io_latency,
				       u64 now, int rw)
{
	u64 latency_capable =
		ca->io_latency[rw].quantiles.entries[QUANTILE_IDX(1)].m;
	/* ideally we'd be taking into account the device's variance here: */
	u64 latency_threshold = latency_capable << (rw == READ ? 2 : 3);
	s64 latency_over = io_latency - latency_threshold;

	if (latency_threshold && latency_over > 0) {
		/*
		 * bump up congested by approximately latency_over * 4 /
		 * latency_threshold - we don't need much accuracy here so don't
		 * bother with the divide:
		 */
		if (atomic_read(&ca->congested) < CONGESTED_MAX)
			atomic_add(latency_over >>
				   max_t(int, ilog2(latency_threshold) - 2, 0),
				   &ca->congested);

		ca->congested_last = now;
	} else if (atomic_read(&ca->congested) > 0) {
		atomic_dec(&ca->congested);
	}
}

void bch2_latency_acct(struct bch_dev *ca, u64 submit_time, int rw)
{
	atomic64_t *latency = &ca->cur_latency[rw];
	u64 now = local_clock();
	u64 io_latency = time_after64(now, submit_time)
		? now - submit_time
		: 0;
	u64 old, new, v = atomic64_read(latency);

	do {
		old = v;

		/*
		 * If the io latency was reasonably close to the current
		 * latency, skip doing the update and atomic operation - most of
		 * the time:
		 */
		if (abs((int) (old - io_latency)) < (old >> 1) &&
		    now & ~(~0 << 5))
			break;

		new = ewma_add(old, io_latency, 5);
	} while ((v = atomic64_cmpxchg(latency, old, new)) != old);

	bch2_congested_acct(ca, io_latency, now, rw);

	__bch2_time_stats_update(&ca->io_latency[rw], submit_time, now);
}

#else

static bool bch2_target_congested(struct bch_fs *c, u16 target)
{
	return false;
}

#endif

/* Allocate, free from mempool: */

void bch2_bio_free_pages_pool(struct bch_fs *c, struct bio *bio)
{
	struct bvec_iter_all iter;
	struct bio_vec *bv;

	bio_for_each_segment_all(bv, bio, iter)
		if (bv->bv_page != ZERO_PAGE(0))
			mempool_free(bv->bv_page, &c->bio_bounce_pages);
	bio->bi_vcnt = 0;
}

static struct page *__bio_alloc_page_pool(struct bch_fs *c, bool *using_mempool)
{
	struct page *page;

	if (likely(!*using_mempool)) {
		page = alloc_page(GFP_NOIO);
		if (unlikely(!page)) {
			mutex_lock(&c->bio_bounce_pages_lock);
			*using_mempool = true;
			goto pool_alloc;

		}
	} else {
pool_alloc:
		page = mempool_alloc(&c->bio_bounce_pages, GFP_NOIO);
	}

	return page;
}

void bch2_bio_alloc_pages_pool(struct bch_fs *c, struct bio *bio,
			       size_t size)
{
	bool using_mempool = false;

	while (size) {
		struct page *page = __bio_alloc_page_pool(c, &using_mempool);
		unsigned len = min(PAGE_SIZE, size);

		BUG_ON(!bio_add_page(bio, page, len, 0));
		size -= len;
	}

	if (using_mempool)
		mutex_unlock(&c->bio_bounce_pages_lock);
}

/* Writes */

void bch2_submit_wbio_replicas(struct bch_write_bio *wbio, struct bch_fs *c,
			       enum bch_data_type type,
			       const struct bkey_i *k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(bkey_i_to_s_c(k));
	const struct bch_extent_ptr *ptr;
	struct bch_write_bio *n;
	struct bch_dev *ca;

	BUG_ON(c->opts.nochanges);

	bkey_for_each_ptr(ptrs, ptr) {
		BUG_ON(ptr->dev >= BCH_SB_MEMBERS_MAX ||
		       !c->devs[ptr->dev]);

		ca = bch_dev_bkey_exists(c, ptr->dev);

		if (to_entry(ptr + 1) < ptrs.end) {
			n = to_wbio(bio_alloc_clone(NULL, &wbio->bio,
						GFP_NOIO, &ca->replica_set));

			n->bio.bi_end_io	= wbio->bio.bi_end_io;
			n->bio.bi_private	= wbio->bio.bi_private;
			n->parent		= wbio;
			n->split		= true;
			n->bounce		= false;
			n->put_bio		= true;
			n->bio.bi_opf		= wbio->bio.bi_opf;
			bio_inc_remaining(&wbio->bio);
		} else {
			n = wbio;
			n->split		= false;
		}

		n->c			= c;
		n->dev			= ptr->dev;
		n->have_ioref		= bch2_dev_get_ioref(ca, WRITE);
		n->submit_time		= local_clock();
		n->bio.bi_iter.bi_sector = ptr->offset;

		if (!journal_flushes_device(ca))
			n->bio.bi_opf |= REQ_FUA;

		if (likely(n->have_ioref)) {
			this_cpu_add(ca->io_done->sectors[WRITE][type],
				     bio_sectors(&n->bio));

			bio_set_dev(&n->bio, ca->disk_sb.bdev);

			if (type != BCH_DATA_BTREE && unlikely(c->opts.no_data_io)) {
				bio_endio(&n->bio);
				continue;
			}

			submit_bio(&n->bio);
		} else {
			n->bio.bi_status	= BLK_STS_REMOVED;
			bio_endio(&n->bio);
		}
	}
}

static void __bch2_write(struct closure *);

static void bch2_write_done(struct closure *cl)
{
	struct bch_write_op *op = container_of(cl, struct bch_write_op, cl);
	struct bch_fs *c = op->c;

	if (!op->error && (op->flags & BCH_WRITE_FLUSH))
		op->error = bch2_journal_error(&c->journal);

	if (!(op->flags & BCH_WRITE_NOPUT_RESERVATION))
		bch2_disk_reservation_put(c, &op->res);
	percpu_ref_put(&c->writes);
	bch2_keylist_free(&op->insert_keys, op->inline_keys);

	bch2_time_stats_update(&c->times[BCH_TIME_data_write], op->start_time);

	closure_return(cl);
}

int bch2_write_index_default(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct btree_trans trans;
	struct btree_iter *iter;
	struct keylist *keys = &op->insert_keys;
	int ret;

	BUG_ON(bch2_keylist_empty(keys));
	bch2_verify_keylist_sorted(keys);

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 256);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS,
				   bkey_start_pos(&bch2_keylist_front(keys)->k),
				   BTREE_ITER_INTENT);

	do {
		BKEY_PADDED(k) split;

		bkey_copy(&split.k, bch2_keylist_front(keys));

		bch2_extent_trim_atomic(&split.k, iter);

		bch2_trans_update(&trans,
				  BTREE_INSERT_ENTRY(iter, &split.k));

		ret = bch2_trans_commit(&trans, &op->res, op_journal_seq(op),
					BTREE_INSERT_NOFAIL|
					BTREE_INSERT_USE_RESERVE);
		if (ret)
			break;

		if (bkey_cmp(iter->pos, bch2_keylist_front(keys)->k.p) < 0)
			bch2_cut_front(iter->pos, bch2_keylist_front(keys));
		else
			bch2_keylist_pop_front(keys);
	} while (!bch2_keylist_empty(keys));

	bch2_trans_exit(&trans);

	return ret;
}

/**
 * bch_write_index - after a write, update index to point to new data
 */
static void __bch2_write_index(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct keylist *keys = &op->insert_keys;
	struct bch_extent_ptr *ptr;
	struct bkey_i *src, *dst = keys->keys, *n, *k;
	unsigned dev;
	int ret;

	for (src = keys->keys; src != keys->top; src = n) {
		n = bkey_next(src);
		bkey_copy(dst, src);

		bch2_bkey_drop_ptrs(bkey_i_to_s(dst), ptr,
			test_bit(ptr->dev, op->failed.d));

		if (!bch2_bkey_nr_ptrs(bkey_i_to_s_c(dst))) {
			ret = -EIO;
			goto err;
		}

		dst = bkey_next(dst);
	}

	keys->top = dst;

	/*
	 * probably not the ideal place to hook this in, but I don't
	 * particularly want to plumb io_opts all the way through the btree
	 * update stack right now
	 */
	for_each_keylist_key(keys, k)
		bch2_rebalance_add_key(c, bkey_i_to_s_c(k), &op->opts);

	if (!bch2_keylist_empty(keys)) {
		u64 sectors_start = keylist_sectors(keys);
		int ret = op->index_update_fn(op);

		BUG_ON(keylist_sectors(keys) && !ret);

		op->written += sectors_start - keylist_sectors(keys);

		if (ret) {
			__bcache_io_error(c, "btree IO error %i", ret);
			op->error = ret;
		}
	}
out:
	/* If some a bucket wasn't written, we can't erasure code it: */
	for_each_set_bit(dev, op->failed.d, BCH_SB_MEMBERS_MAX)
		bch2_open_bucket_write_error(c, &op->open_buckets, dev);

	bch2_open_buckets_put(c, &op->open_buckets);
	return;
err:
	keys->top = keys->keys;
	op->error = ret;
	goto out;
}

static void bch2_write_index(struct closure *cl)
{
	struct bch_write_op *op = container_of(cl, struct bch_write_op, cl);
	struct bch_fs *c = op->c;

	__bch2_write_index(op);

	if (!op->error && (op->flags & BCH_WRITE_FLUSH)) {
		bch2_journal_flush_seq_async(&c->journal,
					     *op_journal_seq(op),
					     cl);
		continue_at(cl, bch2_write_done, index_update_wq(op));
	} else {
		continue_at_nobarrier(cl, bch2_write_done, NULL);
	}
}

static void bch2_write_endio(struct bio *bio)
{
	struct closure *cl		= bio->bi_private;
	struct bch_write_op *op		= container_of(cl, struct bch_write_op, cl);
	struct bch_write_bio *wbio	= to_wbio(bio);
	struct bch_write_bio *parent	= wbio->split ? wbio->parent : NULL;
	struct bch_fs *c		= wbio->c;
	struct bch_dev *ca		= bch_dev_bkey_exists(c, wbio->dev);

	if (bch2_dev_io_err_on(bio->bi_status, ca, "data write"))
		set_bit(wbio->dev, op->failed.d);

	if (wbio->have_ioref) {
		bch2_latency_acct(ca, wbio->submit_time, WRITE);
		percpu_ref_put(&ca->io_ref);
	}

	if (wbio->bounce)
		bch2_bio_free_pages_pool(c, bio);

	if (wbio->put_bio)
		bio_put(bio);

	if (parent)
		bio_endio(&parent->bio);
	else
		closure_put(cl);
}

static void init_append_extent(struct bch_write_op *op,
			       struct write_point *wp,
			       struct bversion version,
			       struct bch_extent_crc_unpacked crc)
{
	struct bkey_i_extent *e = bkey_extent_init(op->insert_keys.top);
	struct bch_extent_ptr *ptr;

	op->pos.offset += crc.uncompressed_size;
	e->k.p		= op->pos;
	e->k.size	= crc.uncompressed_size;
	e->k.version	= version;

	if (crc.csum_type ||
	    crc.compression_type ||
	    crc.nonce)
		bch2_extent_crc_append(e, crc);

	bch2_alloc_sectors_append_ptrs(op->c, wp, &e->k_i, crc.compressed_size);

	if (op->flags & BCH_WRITE_CACHED)
		extent_for_each_ptr(extent_i_to_s(e), ptr)
			ptr->cached = true;

	bch2_keylist_push(&op->insert_keys);
}

static struct bio *bch2_write_bio_alloc(struct bch_fs *c,
					struct write_point *wp,
					struct bio *src,
					bool *page_alloc_failed,
					void *buf)
{
	struct bch_write_bio *wbio;
	struct bio *bio;
	unsigned output_available =
		min(wp->sectors_free << 9, src->bi_iter.bi_size);
	unsigned pages = DIV_ROUND_UP(output_available, PAGE_SIZE);

	bio = bio_alloc_bioset(NULL, pages, 0,
			       GFP_NOIO, &c->bio_write);
	wbio			= wbio_init(bio);
	wbio->put_bio		= true;
	/* copy WRITE_SYNC flag */
	wbio->bio.bi_opf	= src->bi_opf;

	if (buf) {
		bch2_bio_map(bio, buf, output_available);
		return bio;
	}

	wbio->bounce		= true;

	/*
	 * We can't use mempool for more than c->sb.encoded_extent_max
	 * worth of pages, but we'd like to allocate more if we can:
	 */
	bch2_bio_alloc_pages_pool(c, bio,
				  min_t(unsigned, output_available,
					c->sb.encoded_extent_max << 9));

	if (bio->bi_iter.bi_size < output_available)
		*page_alloc_failed =
			bch2_bio_alloc_pages(bio,
					     output_available -
					     bio->bi_iter.bi_size,
					     GFP_NOFS) != 0;

	return bio;
}

static int bch2_write_rechecksum(struct bch_fs *c,
				 struct bch_write_op *op,
				 unsigned new_csum_type)
{
	struct bio *bio = &op->wbio.bio;
	struct bch_extent_crc_unpacked new_crc;
	int ret;

	/* bch2_rechecksum_bio() can't encrypt or decrypt data: */

	if (bch2_csum_type_is_encryption(op->crc.csum_type) !=
	    bch2_csum_type_is_encryption(new_csum_type))
		new_csum_type = op->crc.csum_type;

	ret = bch2_rechecksum_bio(c, bio, op->version, op->crc,
				  NULL, &new_crc,
				  op->crc.offset, op->crc.live_size,
				  new_csum_type);
	if (ret)
		return ret;

	bio_advance(bio, op->crc.offset << 9);
	bio->bi_iter.bi_size = op->crc.live_size << 9;
	op->crc = new_crc;
	return 0;
}

static int bch2_write_decrypt(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct nonce nonce = extent_nonce(op->version, op->crc);
	struct bch_csum csum;

	if (!bch2_csum_type_is_encryption(op->crc.csum_type))
		return 0;

	/*
	 * If we need to decrypt data in the write path, we'll no longer be able
	 * to verify the existing checksum (poly1305 mac, in this case) after
	 * it's decrypted - this is the last point we'll be able to reverify the
	 * checksum:
	 */
	csum = bch2_checksum_bio(c, op->crc.csum_type, nonce, &op->wbio.bio);
	if (bch2_crc_cmp(op->crc.csum, csum))
		return -EIO;

	bch2_encrypt_bio(c, op->crc.csum_type, nonce, &op->wbio.bio);
	op->crc.csum_type = 0;
	op->crc.csum = (struct bch_csum) { 0, 0 };
	return 0;
}

static enum prep_encoded_ret {
	PREP_ENCODED_OK,
	PREP_ENCODED_ERR,
	PREP_ENCODED_CHECKSUM_ERR,
	PREP_ENCODED_DO_WRITE,
} bch2_write_prep_encoded_data(struct bch_write_op *op, struct write_point *wp)
{
	struct bch_fs *c = op->c;
	struct bio *bio = &op->wbio.bio;

	if (!(op->flags & BCH_WRITE_DATA_ENCODED))
		return PREP_ENCODED_OK;

	BUG_ON(bio_sectors(bio) != op->crc.compressed_size);

	/* Can we just write the entire extent as is? */
	if (op->crc.uncompressed_size == op->crc.live_size &&
	    op->crc.compressed_size <= wp->sectors_free &&
	    op->crc.compression_type == op->compression_type) {
		if (!op->crc.compression_type &&
		    op->csum_type != op->crc.csum_type &&
		    bch2_write_rechecksum(c, op, op->csum_type))
			return PREP_ENCODED_CHECKSUM_ERR;

		return PREP_ENCODED_DO_WRITE;
	}

	/*
	 * If the data is compressed and we couldn't write the entire extent as
	 * is, we have to decompress it:
	 */
	if (op->crc.compression_type) {
		struct bch_csum csum;

		if (bch2_write_decrypt(op))
			return PREP_ENCODED_CHECKSUM_ERR;

		/* Last point we can still verify checksum: */
		csum = bch2_checksum_bio(c, op->crc.csum_type,
					 extent_nonce(op->version, op->crc),
					 bio);
		if (bch2_crc_cmp(op->crc.csum, csum))
			return PREP_ENCODED_CHECKSUM_ERR;

		if (bch2_bio_uncompress_inplace(c, bio, &op->crc))
			return PREP_ENCODED_ERR;
	}

	/*
	 * No longer have compressed data after this point - data might be
	 * encrypted:
	 */

	/*
	 * If the data is checksummed and we're only writing a subset,
	 * rechecksum and adjust bio to point to currently live data:
	 */
	if ((op->crc.live_size != op->crc.uncompressed_size ||
	     op->crc.csum_type != op->csum_type) &&
	    bch2_write_rechecksum(c, op, op->csum_type))
		return PREP_ENCODED_CHECKSUM_ERR;

	/*
	 * If we want to compress the data, it has to be decrypted:
	 */
	if ((op->compression_type ||
	     bch2_csum_type_is_encryption(op->crc.csum_type) !=
	     bch2_csum_type_is_encryption(op->csum_type)) &&
	    bch2_write_decrypt(op))
		return PREP_ENCODED_CHECKSUM_ERR;

	return PREP_ENCODED_OK;
}

static int bch2_write_extent(struct bch_write_op *op, struct write_point *wp)
{
	struct bch_fs *c = op->c;
	struct bio *src = &op->wbio.bio, *dst = src;
	struct bvec_iter saved_iter;
	struct bkey_i *key_to_write;
	void *ec_buf;
	unsigned key_to_write_offset = op->insert_keys.top_p -
		op->insert_keys.keys_p;
	unsigned total_output = 0, total_input = 0;
	bool bounce = false;
	bool page_alloc_failed = false;
	int ret, more = 0;

	BUG_ON(!bio_sectors(src));

	ec_buf = bch2_writepoint_ec_buf(c, wp);

	switch (bch2_write_prep_encoded_data(op, wp)) {
	case PREP_ENCODED_OK:
		break;
	case PREP_ENCODED_ERR:
		ret = -EIO;
		goto err;
	case PREP_ENCODED_CHECKSUM_ERR:
		goto csum_err;
	case PREP_ENCODED_DO_WRITE:
		if (ec_buf) {
			dst = bch2_write_bio_alloc(c, wp, src,
						   &page_alloc_failed,
						   ec_buf);
			bio_copy_data(dst, src);
			bounce = true;
		}
		init_append_extent(op, wp, op->version, op->crc);
		goto do_write;
	}

	if (ec_buf ||
	    op->compression_type ||
	    (op->csum_type &&
	     !(op->flags & BCH_WRITE_PAGES_STABLE)) ||
	    (bch2_csum_type_is_encryption(op->csum_type) &&
	     !(op->flags & BCH_WRITE_PAGES_OWNED))) {
		dst = bch2_write_bio_alloc(c, wp, src,
					   &page_alloc_failed,
					   ec_buf);
		bounce = true;
	}

	saved_iter = dst->bi_iter;

	do {
		struct bch_extent_crc_unpacked crc =
			(struct bch_extent_crc_unpacked) { 0 };
		struct bversion version = op->version;
		size_t dst_len, src_len;

		if (page_alloc_failed &&
		    bio_sectors(dst) < wp->sectors_free &&
		    bio_sectors(dst) < c->sb.encoded_extent_max)
			break;

		BUG_ON(op->compression_type &&
		       (op->flags & BCH_WRITE_DATA_ENCODED) &&
		       bch2_csum_type_is_encryption(op->crc.csum_type));
		BUG_ON(op->compression_type && !bounce);

		crc.compression_type = op->compression_type
			?  bch2_bio_compress(c, dst, &dst_len, src, &src_len,
					     op->compression_type)
			: 0;
		if (!crc.compression_type) {
			dst_len = min(dst->bi_iter.bi_size, src->bi_iter.bi_size);
			dst_len = min_t(unsigned, dst_len, wp->sectors_free << 9);

			if (op->csum_type)
				dst_len = min_t(unsigned, dst_len,
						c->sb.encoded_extent_max << 9);

			if (bounce) {
				swap(dst->bi_iter.bi_size, dst_len);
				bio_copy_data(dst, src);
				swap(dst->bi_iter.bi_size, dst_len);
			}

			src_len = dst_len;
		}

		BUG_ON(!src_len || !dst_len);

		if (bch2_csum_type_is_encryption(op->csum_type)) {
			if (bversion_zero(version)) {
				version.lo = atomic64_inc_return(&c->key_version) + 1;
			} else {
				crc.nonce = op->nonce;
				op->nonce += src_len >> 9;
			}
		}

		if ((op->flags & BCH_WRITE_DATA_ENCODED) &&
		    !crc.compression_type &&
		    bch2_csum_type_is_encryption(op->crc.csum_type) ==
		    bch2_csum_type_is_encryption(op->csum_type)) {
			/*
			 * Note: when we're using rechecksum(), we need to be
			 * checksumming @src because it has all the data our
			 * existing checksum covers - if we bounced (because we
			 * were trying to compress), @dst will only have the
			 * part of the data the new checksum will cover.
			 *
			 * But normally we want to be checksumming post bounce,
			 * because part of the reason for bouncing is so the
			 * data can't be modified (by userspace) while it's in
			 * flight.
			 */
			if (bch2_rechecksum_bio(c, src, version, op->crc,
					&crc, &op->crc,
					src_len >> 9,
					bio_sectors(src) - (src_len >> 9),
					op->csum_type))
				goto csum_err;
		} else {
			if ((op->flags & BCH_WRITE_DATA_ENCODED) &&
			    bch2_rechecksum_bio(c, src, version, op->crc,
					NULL, &op->crc,
					src_len >> 9,
					bio_sectors(src) - (src_len >> 9),
					op->crc.csum_type))
				goto csum_err;

			crc.compressed_size	= dst_len >> 9;
			crc.uncompressed_size	= src_len >> 9;
			crc.live_size		= src_len >> 9;

			swap(dst->bi_iter.bi_size, dst_len);
			bch2_encrypt_bio(c, op->csum_type,
					 extent_nonce(version, crc), dst);
			crc.csum = bch2_checksum_bio(c, op->csum_type,
					 extent_nonce(version, crc), dst);
			crc.csum_type = op->csum_type;
			swap(dst->bi_iter.bi_size, dst_len);
		}

		init_append_extent(op, wp, version, crc);

		if (dst != src)
			bio_advance(dst, dst_len);
		bio_advance(src, src_len);
		total_output	+= dst_len;
		total_input	+= src_len;
	} while (dst->bi_iter.bi_size &&
		 src->bi_iter.bi_size &&
		 wp->sectors_free &&
		 !bch2_keylist_realloc(&op->insert_keys,
				      op->inline_keys,
				      ARRAY_SIZE(op->inline_keys),
				      BKEY_EXTENT_U64s_MAX));

	more = src->bi_iter.bi_size != 0;

	dst->bi_iter = saved_iter;

	if (dst == src && more) {
		BUG_ON(total_output != total_input);

		dst = bio_split(src, total_input >> 9,
				GFP_NOIO, &c->bio_write);
		wbio_init(dst)->put_bio	= true;
		/* copy WRITE_SYNC flag */
		dst->bi_opf		= src->bi_opf;
	}

	dst->bi_iter.bi_size = total_output;
do_write:
	/* might have done a realloc... */

	key_to_write = (void *) (op->insert_keys.keys_p + key_to_write_offset);

	bch2_ec_add_backpointer(c, wp,
				bkey_start_pos(&key_to_write->k),
				total_input >> 9);

	dst->bi_end_io	= bch2_write_endio;
	dst->bi_private	= &op->cl;
	dst->bi_opf	= REQ_OP_WRITE;

	closure_get(dst->bi_private);

	bch2_submit_wbio_replicas(to_wbio(dst), c, BCH_DATA_USER,
				  key_to_write);
	return more;
csum_err:
	bch_err(c, "error verifying existing checksum while "
		"rewriting existing data (memory corruption?)");
	ret = -EIO;
err:
	if (to_wbio(dst)->bounce)
		bch2_bio_free_pages_pool(c, dst);
	if (to_wbio(dst)->put_bio)
		bio_put(dst);

	return ret;
}

static void __bch2_write(struct closure *cl)
{
	struct bch_write_op *op = container_of(cl, struct bch_write_op, cl);
	struct bch_fs *c = op->c;
	struct write_point *wp;
	int ret;
again:
	memset(&op->failed, 0, sizeof(op->failed));

	do {
		/* +1 for possible cache device: */
		if (op->open_buckets.nr + op->nr_replicas + 1 >
		    ARRAY_SIZE(op->open_buckets.v))
			goto flush_io;

		if (bch2_keylist_realloc(&op->insert_keys,
					op->inline_keys,
					ARRAY_SIZE(op->inline_keys),
					BKEY_EXTENT_U64s_MAX))
			goto flush_io;

		wp = bch2_alloc_sectors_start(c,
			op->target,
			op->opts.erasure_code,
			op->write_point,
			&op->devs_have,
			op->nr_replicas,
			op->nr_replicas_required,
			op->alloc_reserve,
			op->flags,
			(op->flags & BCH_WRITE_ALLOC_NOWAIT) ? NULL : cl);
		EBUG_ON(!wp);

		if (unlikely(IS_ERR(wp))) {
			if (unlikely(PTR_ERR(wp) != -EAGAIN)) {
				ret = PTR_ERR(wp);
				goto err;
			}

			goto flush_io;
		}

		ret = bch2_write_extent(op, wp);

		bch2_open_bucket_get(c, wp, &op->open_buckets);
		bch2_alloc_sectors_done(c, wp);

		if (ret < 0)
			goto err;
	} while (ret);

	continue_at(cl, bch2_write_index, index_update_wq(op));
	return;
err:
	op->error = ret;

	continue_at(cl, !bch2_keylist_empty(&op->insert_keys)
		    ? bch2_write_index
		    : bch2_write_done, index_update_wq(op));
	return;
flush_io:
	closure_sync(cl);

	if (!bch2_keylist_empty(&op->insert_keys)) {
		__bch2_write_index(op);

		if (op->error) {
			continue_at_nobarrier(cl, bch2_write_done, NULL);
			return;
		}
	}

	goto again;
}

/**
 * bch_write - handle a write to a cache device or flash only volume
 *
 * This is the starting point for any data to end up in a cache device; it could
 * be from a normal write, or a writeback write, or a write to a flash only
 * volume - it's also used by the moving garbage collector to compact data in
 * mostly empty buckets.
 *
 * It first writes the data to the cache, creating a list of keys to be inserted
 * (if the data won't fit in a single open bucket, there will be multiple keys);
 * after the data is written it calls bch_journal, and after the keys have been
 * added to the next journal write they're inserted into the btree.
 *
 * If op->discard is true, instead of inserting the data it invalidates the
 * region of the cache represented by op->bio and op->inode.
 */
void bch2_write(struct closure *cl)
{
	struct bch_write_op *op = container_of(cl, struct bch_write_op, cl);
	struct bch_fs *c = op->c;

	BUG_ON(!op->nr_replicas);
	BUG_ON(!op->write_point.v);
	BUG_ON(!bkey_cmp(op->pos, POS_MAX));

	op->start_time = local_clock();

	bch2_keylist_init(&op->insert_keys, op->inline_keys);
	wbio_init(&op->wbio.bio)->put_bio = false;

	if (c->opts.nochanges ||
	    !percpu_ref_tryget(&c->writes)) {
		__bcache_io_error(c, "read only");
		op->error = -EROFS;
		if (!(op->flags & BCH_WRITE_NOPUT_RESERVATION))
			bch2_disk_reservation_put(c, &op->res);
		closure_return(cl);
		return;
	}

	bch2_increment_clock(c, bio_sectors(&op->wbio.bio), WRITE);

	continue_at_nobarrier(cl, __bch2_write, NULL);
}

/* Cache promotion on read */

struct promote_op {
	struct closure		cl;
	struct rcu_head		rcu;
	u64			start_time;

	struct rhash_head	hash;
	struct bpos		pos;

	struct migrate_write	write;
	struct bio_vec		bi_inline_vecs[0]; /* must be last */
};

static const struct rhashtable_params bch_promote_params = {
	.head_offset	= offsetof(struct promote_op, hash),
	.key_offset	= offsetof(struct promote_op, pos),
	.key_len	= sizeof(struct bpos),
};

static inline bool should_promote(struct bch_fs *c, struct bkey_s_c k,
				  struct bpos pos,
				  struct bch_io_opts opts,
				  unsigned flags)
{
	if (!bkey_extent_is_data(k.k))
		return false;

	if (!(flags & BCH_READ_MAY_PROMOTE))
		return false;

	if (!opts.promote_target)
		return false;

	if (bch2_extent_has_target(c, bkey_s_c_to_extent(k),
				   opts.promote_target))
		return false;

	if (bch2_target_congested(c, opts.promote_target)) {
		/* XXX trace this */
		return false;
	}

	if (rhashtable_lookup_fast(&c->promote_table, &pos,
				   bch_promote_params))
		return false;

	return true;
}

static void promote_free(struct bch_fs *c, struct promote_op *op)
{
	int ret;

	ret = rhashtable_remove_fast(&c->promote_table, &op->hash,
				     bch_promote_params);
	BUG_ON(ret);
	percpu_ref_put(&c->writes);
	kfree_rcu(op, rcu);
}

static void promote_done(struct closure *cl)
{
	struct promote_op *op =
		container_of(cl, struct promote_op, cl);
	struct bch_fs *c = op->write.op.c;

	bch2_time_stats_update(&c->times[BCH_TIME_data_promote],
			       op->start_time);

	bch2_bio_free_pages_pool(c, &op->write.op.wbio.bio);
	promote_free(c, op);
}

static void promote_start(struct promote_op *op, struct bch_read_bio *rbio)
{
	struct bch_fs *c = rbio->c;
	struct closure *cl = &op->cl;
	struct bio *bio = &op->write.op.wbio.bio;

	trace_promote(&rbio->bio);

	/* we now own pages: */
	BUG_ON(!rbio->bounce);
	BUG_ON(rbio->bio.bi_vcnt > bio->bi_max_vecs);

	memcpy(bio->bi_io_vec, rbio->bio.bi_io_vec,
	       sizeof(struct bio_vec) * rbio->bio.bi_vcnt);
	swap(bio->bi_vcnt, rbio->bio.bi_vcnt);

	bch2_migrate_read_done(&op->write, rbio);

	closure_init(cl, NULL);
	closure_call(&op->write.op.cl, bch2_write, c->wq, cl);
	closure_return_with_destructor(cl, promote_done);
}

noinline
static struct promote_op *__promote_alloc(struct bch_fs *c,
					  struct bpos pos,
					  struct extent_ptr_decoded *pick,
					  struct bch_io_opts opts,
					  unsigned sectors,
					  struct bch_read_bio **rbio)
{
	struct promote_op *op = NULL;
	struct bio *bio;
	unsigned pages = DIV_ROUND_UP(sectors, PAGE_SECTORS);
	int ret;

	if (!percpu_ref_tryget(&c->writes))
		return NULL;

	op = kzalloc(sizeof(*op) + sizeof(struct bio_vec) * pages, GFP_NOIO);
	if (!op)
		goto err;

	op->start_time = local_clock();
	op->pos = pos;

	/*
	 * We don't use the mempool here because extents that aren't
	 * checksummed or compressed can be too big for the mempool:
	 */
	*rbio = kzalloc(sizeof(struct bch_read_bio) +
			sizeof(struct bio_vec) * pages,
			GFP_NOIO);
	if (!*rbio)
		goto err;

	rbio_init(&(*rbio)->bio, opts);
	bio_init(&(*rbio)->bio, NULL, (*rbio)->bio.bi_inline_vecs, pages, 0);

	if (bch2_bio_alloc_pages(&(*rbio)->bio, sectors << 9,
				 GFP_NOIO))
		goto err;

	(*rbio)->bounce		= true;
	(*rbio)->split		= true;
	(*rbio)->kmalloc	= true;

	if (rhashtable_lookup_insert_fast(&c->promote_table, &op->hash,
					  bch_promote_params))
		goto err;

	bio = &op->write.op.wbio.bio;
	bio_init(bio, NULL, bio->bi_inline_vecs, pages, 0);

	ret = bch2_migrate_write_init(c, &op->write,
			writepoint_hashed((unsigned long) current),
			opts,
			DATA_PROMOTE,
			(struct data_opts) {
				.target = opts.promote_target
			},
			bkey_s_c_null);
	BUG_ON(ret);

	return op;
err:
	if (*rbio)
		bio_free_pages(&(*rbio)->bio);
	kfree(*rbio);
	*rbio = NULL;
	kfree(op);
	percpu_ref_put(&c->writes);
	return NULL;
}

static inline struct promote_op *promote_alloc(struct bch_fs *c,
					       struct bvec_iter iter,
					       struct bkey_s_c k,
					       struct extent_ptr_decoded *pick,
					       struct bch_io_opts opts,
					       unsigned flags,
					       struct bch_read_bio **rbio,
					       bool *bounce,
					       bool *read_full)
{
	bool promote_full = *read_full || READ_ONCE(c->promote_whole_extents);
	/* data might have to be decompressed in the write path: */
	unsigned sectors = promote_full
		? max(pick->crc.compressed_size, pick->crc.live_size)
		: bvec_iter_sectors(iter);
	struct bpos pos = promote_full
		? bkey_start_pos(k.k)
		: POS(k.k->p.inode, iter.bi_sector);
	struct promote_op *promote;

	if (!should_promote(c, k, pos, opts, flags))
		return NULL;

	promote = __promote_alloc(c, pos, pick, opts, sectors, rbio);
	if (!promote)
		return NULL;

	*bounce		= true;
	*read_full	= promote_full;
	return promote;
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

static void bch2_rbio_done(struct bch_read_bio *rbio)
{
	bch2_time_stats_update(&rbio->c->times[BCH_TIME_data_read],
			       rbio->start_time);
	bio_endio(&rbio->bio);
}

static void bch2_read_retry_nodecode(struct bch_fs *c, struct bch_read_bio *rbio,
				     struct bvec_iter bvec_iter, u64 inode,
				     struct bch_io_failures *failed,
				     unsigned flags)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	BKEY_PADDED(k) tmp;
	struct bkey_s_c k;
	int ret;

	flags &= ~BCH_READ_LAST_FRAGMENT;

	bch2_trans_init(&trans, c, 0, 0);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS,
				   rbio->pos, BTREE_ITER_SLOTS);
retry:
	rbio->bio.bi_status = 0;

	k = bch2_btree_iter_peek_slot(iter);
	if (bkey_err(k))
		goto err;

	bkey_reassemble(&tmp.k, k);
	k = bkey_i_to_s_c(&tmp.k);
	bch2_trans_unlock(&trans);

	if (!bkey_extent_is_data(k.k) ||
	    !bch2_extent_matches_ptr(c, bkey_i_to_s_c_extent(&tmp.k),
				     rbio->pick.ptr,
				     rbio->pos.offset -
				     rbio->pick.crc.offset)) {
		/* extent we wanted to read no longer exists: */
		rbio->hole = true;
		goto out;
	}

	ret = __bch2_read_extent(c, rbio, bvec_iter, k, failed, flags);
	if (ret == READ_RETRY)
		goto retry;
	if (ret)
		goto err;
out:
	bch2_rbio_done(rbio);
	bch2_trans_exit(&trans);
	return;
err:
	rbio->bio.bi_status = BLK_STS_IOERR;
	goto out;
}

static void bch2_read_retry(struct bch_fs *c, struct bch_read_bio *rbio,
			    struct bvec_iter bvec_iter, u64 inode,
			    struct bch_io_failures *failed, unsigned flags)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	flags &= ~BCH_READ_LAST_FRAGMENT;
	flags |= BCH_READ_MUST_CLONE;
retry:
	for_each_btree_key(&trans, iter, BTREE_ID_EXTENTS,
			   POS(inode, bvec_iter.bi_sector),
			   BTREE_ITER_SLOTS, k, ret) {
		BKEY_PADDED(k) tmp;
		unsigned bytes;

		bkey_reassemble(&tmp.k, k);
		k = bkey_i_to_s_c(&tmp.k);
		bch2_trans_unlock(&trans);

		bytes = min_t(unsigned, bvec_iter.bi_size,
			      (k.k->p.offset - bvec_iter.bi_sector) << 9);
		swap(bvec_iter.bi_size, bytes);

		ret = __bch2_read_extent(c, rbio, bvec_iter, k, failed, flags);
		switch (ret) {
		case READ_RETRY:
			goto retry;
		case READ_ERR:
			goto err;
		};

		if (bytes == bvec_iter.bi_size)
			goto out;

		swap(bvec_iter.bi_size, bytes);
		bio_advance_iter(&rbio->bio, &bvec_iter, bytes);
	}

	/*
	 * If we get here, it better have been because there was an error
	 * reading a btree node
	 */
	BUG_ON(!ret);
	__bcache_io_error(c, "btree IO error: %i", ret);
err:
	rbio->bio.bi_status = BLK_STS_IOERR;
out:
	bch2_trans_exit(&trans);
	bch2_rbio_done(rbio);
}

static void bch2_rbio_retry(struct work_struct *work)
{
	struct bch_read_bio *rbio =
		container_of(work, struct bch_read_bio, work);
	struct bch_fs *c	= rbio->c;
	struct bvec_iter iter	= rbio->bvec_iter;
	unsigned flags		= rbio->flags;
	u64 inode		= rbio->pos.inode;
	struct bch_io_failures failed = { .nr = 0 };

	trace_read_retry(&rbio->bio);

	if (rbio->retry == READ_RETRY_AVOID)
		bch2_mark_io_failure(&failed, &rbio->pick);

	rbio->bio.bi_status = 0;

	rbio = bch2_rbio_free(rbio);

	flags |= BCH_READ_IN_RETRY;
	flags &= ~BCH_READ_MAY_PROMOTE;

	if (flags & BCH_READ_NODECODE)
		bch2_read_retry_nodecode(c, rbio, iter, inode, &failed, flags);
	else
		bch2_read_retry(c, rbio, iter, inode, &failed, flags);
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

static void bch2_rbio_narrow_crcs(struct bch_read_bio *rbio)
{
	struct bch_fs *c = rbio->c;
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_i_extent *e;
	BKEY_PADDED(k) new;
	struct bch_extent_crc_unpacked new_crc;
	u64 data_offset = rbio->pos.offset - rbio->pick.crc.offset;
	int ret;

	if (rbio->pick.crc.compression_type)
		return;

	bch2_trans_init(&trans, c, 0, 0);
retry:
	bch2_trans_begin(&trans);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_EXTENTS, rbio->pos,
				   BTREE_ITER_INTENT);
	k = bch2_btree_iter_peek(iter);
	if (IS_ERR_OR_NULL(k.k))
		goto out;

	if (!bkey_extent_is_data(k.k))
		goto out;

	bkey_reassemble(&new.k, k);
	e = bkey_i_to_extent(&new.k);

	if (!bch2_extent_matches_ptr(c, extent_i_to_s_c(e),
				     rbio->pick.ptr, data_offset) ||
	    bversion_cmp(e->k.version, rbio->version))
		goto out;

	/* Extent was merged? */
	if (bkey_start_offset(&e->k) < data_offset ||
	    e->k.p.offset > data_offset + rbio->pick.crc.uncompressed_size)
		goto out;

	if (bch2_rechecksum_bio(c, &rbio->bio, rbio->version,
			rbio->pick.crc, NULL, &new_crc,
			bkey_start_offset(&e->k) - data_offset, e->k.size,
			rbio->pick.crc.csum_type)) {
		bch_err(c, "error verifying existing checksum while narrowing checksum (memory corruption?)");
		goto out;
	}

	if (!bch2_extent_narrow_crcs(e, new_crc))
		goto out;

	bch2_trans_update(&trans, BTREE_INSERT_ENTRY(iter, &e->k_i));
	ret = bch2_trans_commit(&trans, NULL, NULL,
				BTREE_INSERT_ATOMIC|
				BTREE_INSERT_NOFAIL|
				BTREE_INSERT_NOWAIT);
	if (ret == -EINTR)
		goto retry;
out:
	bch2_trans_exit(&trans);
}

static bool should_narrow_crcs(struct bkey_s_c k,
			       struct extent_ptr_decoded *pick,
			       unsigned flags)
{
	return !(flags & BCH_READ_IN_RETRY) &&
		bkey_extent_is_data(k.k) &&
		bch2_can_narrow_extent_crcs(bkey_s_c_to_extent(k), pick->crc);
}

/* Inner part that may run in process context */
static void __bch2_read_endio(struct work_struct *work)
{
	struct bch_read_bio *rbio =
		container_of(work, struct bch_read_bio, work);
	struct bch_fs *c	= rbio->c;
	struct bch_dev *ca	= bch_dev_bkey_exists(c, rbio->pick.ptr.dev);
	struct bio *src		= &rbio->bio;
	struct bio *dst		= &bch2_rbio_parent(rbio)->bio;
	struct bvec_iter dst_iter = rbio->bvec_iter;
	struct bch_extent_crc_unpacked crc = rbio->pick.crc;
	struct nonce nonce = extent_nonce(rbio->version, crc);
	struct bch_csum csum;

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

	if (unlikely(rbio->narrow_crcs))
		bch2_rbio_narrow_crcs(rbio);

	if (rbio->flags & BCH_READ_NODECODE)
		goto nodecode;

	/* Adjust crc to point to subset of data we want: */
	crc.offset     += rbio->bvec_iter.bi_sector - rbio->pos.offset;
	crc.live_size	= bvec_iter_sectors(rbio->bvec_iter);

	if (crc.compression_type != BCH_COMPRESSION_NONE) {
		bch2_encrypt_bio(c, crc.csum_type, nonce, src);
		if (bch2_bio_uncompress(c, src, dst, dst_iter, crc))
			goto decompression_err;
	} else {
		/* don't need to decrypt the entire bio: */
		nonce = nonce_add(nonce, crc.offset << 9);
		bio_advance(src, crc.offset << 9);

		BUG_ON(src->bi_iter.bi_size < dst_iter.bi_size);
		src->bi_iter.bi_size = dst_iter.bi_size;

		bch2_encrypt_bio(c, crc.csum_type, nonce, src);

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
		bch2_encrypt_bio(c, crc.csum_type, nonce, src);
		promote_start(rbio->promote, rbio);
		rbio->promote = NULL;
	}
nodecode:
	if (likely(!(rbio->flags & BCH_READ_IN_RETRY))) {
		rbio = bch2_rbio_free(rbio);
		bch2_rbio_done(rbio);
	}
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
		return;
	}

	bch2_dev_io_error(ca,
		"data checksum error, inode %llu offset %llu: expected %0llx:%0llx got %0llx:%0llx (type %u)",
		rbio->pos.inode, (u64) rbio->bvec_iter.bi_sector,
		rbio->pick.crc.csum.hi, rbio->pick.crc.csum.lo,
		csum.hi, csum.lo, crc.csum_type);
	bch2_rbio_error(rbio, READ_RETRY_AVOID, BLK_STS_IOERR);
	return;
decompression_err:
	__bcache_io_error(c, "decompression error, inode %llu offset %llu",
			  rbio->pos.inode,
			  (u64) rbio->bvec_iter.bi_sector);
	bch2_rbio_error(rbio, READ_ERR, BLK_STS_IOERR);
	return;
}

static void bch2_read_endio(struct bio *bio)
{
	struct bch_read_bio *rbio =
		container_of(bio, struct bch_read_bio, bio);
	struct bch_fs *c	= rbio->c;
	struct bch_dev *ca	= bch_dev_bkey_exists(c, rbio->pick.ptr.dev);
	struct workqueue_struct *wq = NULL;
	enum rbio_context context = RBIO_CONTEXT_NULL;

	if (rbio->have_ioref) {
		bch2_latency_acct(ca, rbio->submit_time, READ);
		percpu_ref_put(&ca->io_ref);
	}

	if (!rbio->split)
		rbio->bio.bi_end_io = rbio->end_io;

	if (bch2_dev_io_err_on(bio->bi_status, ca, "data read")) {
		bch2_rbio_error(rbio, READ_RETRY_AVOID, bio->bi_status);
		return;
	}

	if (rbio->pick.ptr.cached &&
	    (((rbio->flags & BCH_READ_RETRY_IF_STALE) && race_fault()) ||
	     ptr_stale(ca, &rbio->pick.ptr))) {
		atomic_long_inc(&c->read_realloc_races);

		if (rbio->flags & BCH_READ_RETRY_IF_STALE)
			bch2_rbio_error(rbio, READ_RETRY, BLK_STS_AGAIN);
		else
			bch2_rbio_error(rbio, READ_ERR, BLK_STS_AGAIN);
		return;
	}

	if (rbio->narrow_crcs ||
	    rbio->pick.crc.compression_type ||
	    bch2_csum_type_is_encryption(rbio->pick.crc.csum_type))
		context = RBIO_CONTEXT_UNBOUND,	wq = system_unbound_wq;
	else if (rbio->pick.crc.csum_type)
		context = RBIO_CONTEXT_HIGHPRI,	wq = system_highpri_wq;

	bch2_rbio_punt(rbio, __bch2_read_endio, context, wq);
}

int __bch2_read_extent(struct bch_fs *c, struct bch_read_bio *orig,
		       struct bvec_iter iter, struct bkey_s_c k,
		       struct bch_io_failures *failed, unsigned flags)
{
	struct extent_ptr_decoded pick;
	struct bch_read_bio *rbio = NULL;
	struct bch_dev *ca;
	struct promote_op *promote = NULL;
	bool bounce = false, read_full = false, narrow_crcs = false;
	struct bpos pos = bkey_start_pos(k.k);
	int pick_ret;

	pick_ret = bch2_bkey_pick_read_device(c, k, failed, &pick);

	/* hole or reservation - just zero fill: */
	if (!pick_ret)
		goto hole;

	if (pick_ret < 0) {
		__bcache_io_error(c, "no device to read from");
		goto err;
	}

	if (pick_ret > 0)
		ca = bch_dev_bkey_exists(c, pick.ptr.dev);

	if (flags & BCH_READ_NODECODE) {
		/*
		 * can happen if we retry, and the extent we were going to read
		 * has been merged in the meantime:
		 */
		if (pick.crc.compressed_size > orig->bio.bi_vcnt * PAGE_SECTORS)
			goto hole;

		iter.bi_sector	= pos.offset;
		iter.bi_size	= pick.crc.compressed_size << 9;
		goto noclone;
	}

	if (!(flags & BCH_READ_LAST_FRAGMENT) ||
	    bio_flagged(&orig->bio, BIO_CHAIN))
		flags |= BCH_READ_MUST_CLONE;

	narrow_crcs = should_narrow_crcs(k, &pick, flags);

	if (narrow_crcs && (flags & BCH_READ_USER_MAPPED))
		flags |= BCH_READ_MUST_BOUNCE;

	EBUG_ON(bkey_start_offset(k.k) > iter.bi_sector ||
		k.k->p.offset < bvec_iter_end_sector(iter));

	if (pick.crc.compression_type != BCH_COMPRESSION_NONE ||
	    (pick.crc.csum_type != BCH_CSUM_NONE &&
	     (bvec_iter_sectors(iter) != pick.crc.uncompressed_size ||
	      (bch2_csum_type_is_encryption(pick.crc.csum_type) &&
	       (flags & BCH_READ_USER_MAPPED)) ||
	      (flags & BCH_READ_MUST_BOUNCE)))) {
		read_full = true;
		bounce = true;
	}

	promote = promote_alloc(c, iter, k, &pick, orig->opts, flags,
				&rbio, &bounce, &read_full);

	if (!read_full) {
		EBUG_ON(pick.crc.compression_type);
		EBUG_ON(pick.crc.csum_type &&
			(bvec_iter_sectors(iter) != pick.crc.uncompressed_size ||
			 bvec_iter_sectors(iter) != pick.crc.live_size ||
			 pick.crc.offset ||
			 iter.bi_sector != pos.offset));

		pick.ptr.offset += pick.crc.offset +
			(iter.bi_sector - pos.offset);
		pick.crc.compressed_size	= bvec_iter_sectors(iter);
		pick.crc.uncompressed_size	= bvec_iter_sectors(iter);
		pick.crc.offset			= 0;
		pick.crc.live_size		= bvec_iter_sectors(iter);
		pos.offset			= iter.bi_sector;
	}

	if (rbio) {
		/*
		 * promote already allocated bounce rbio:
		 * promote needs to allocate a bio big enough for uncompressing
		 * data in the write path, but we're not going to use it all
		 * here:
		 */
		BUG_ON(rbio->bio.bi_iter.bi_size <
		       pick.crc.compressed_size << 9);
		rbio->bio.bi_iter.bi_size =
			pick.crc.compressed_size << 9;
	} else if (bounce) {
		unsigned sectors = pick.crc.compressed_size;

		rbio = rbio_init(bio_alloc_bioset(NULL,
						  DIV_ROUND_UP(sectors, PAGE_SECTORS),
						  0,
						  GFP_NOIO,
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
		rbio = rbio_init(bio_alloc_clone(NULL, &orig->bio, GFP_NOIO,
						 &c->bio_read_split),
				 orig->opts);
		rbio->bio.bi_iter = iter;
		rbio->split	= true;
	} else {
noclone:
		rbio = orig;
		rbio->bio.bi_iter = iter;
		BUG_ON(bio_flagged(&rbio->bio, BIO_CHAIN));
	}

	BUG_ON(bio_sectors(&rbio->bio) != pick.crc.compressed_size);

	rbio->c			= c;
	rbio->submit_time	= local_clock();
	if (rbio->split)
		rbio->parent	= orig;
	else
		rbio->end_io	= orig->bio.bi_end_io;
	rbio->bvec_iter		= iter;
	rbio->flags		= flags;
	rbio->have_ioref	= pick_ret > 0 && bch2_dev_get_ioref(ca, READ);
	rbio->narrow_crcs	= narrow_crcs;
	rbio->hole		= 0;
	rbio->retry		= 0;
	rbio->context		= 0;
	rbio->devs_have		= bch2_bkey_devs(k);
	rbio->pick		= pick;
	rbio->pos		= pos;
	rbio->version		= k.k->version;
	rbio->promote		= promote;
	INIT_WORK(&rbio->work, NULL);

	rbio->bio.bi_opf	= orig->bio.bi_opf;
	rbio->bio.bi_iter.bi_sector = pick.ptr.offset;
	rbio->bio.bi_end_io	= bch2_read_endio;

	if (rbio->bounce)
		trace_read_bounce(&rbio->bio);

	bch2_increment_clock(c, bio_sectors(&rbio->bio), READ);

	percpu_down_read(&c->mark_lock);
	bucket_io_clock_reset(c, ca, PTR_BUCKET_NR(ca, &pick.ptr), READ);
	percpu_up_read(&c->mark_lock);

	if (likely(!(flags & (BCH_READ_IN_RETRY|BCH_READ_LAST_FRAGMENT)))) {
		bio_inc_remaining(&orig->bio);
		trace_read_split(&orig->bio);
	}

	if (!rbio->pick.idx) {
		if (!rbio->have_ioref) {
			__bcache_io_error(c, "no device to read from");
			bch2_rbio_error(rbio, READ_RETRY_AVOID, BLK_STS_IOERR);
			goto out;
		}

		this_cpu_add(ca->io_done->sectors[READ][BCH_DATA_USER],
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
	} else {
		/* Attempting reconstruct read: */
		if (bch2_ec_read_extent(c, rbio)) {
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

void bch2_read(struct bch_fs *c, struct bch_read_bio *rbio, u64 inode)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	struct bkey_s_c k;
	unsigned flags = BCH_READ_RETRY_IF_STALE|
		BCH_READ_MAY_PROMOTE|
		BCH_READ_USER_MAPPED;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	BUG_ON(rbio->_state);
	BUG_ON(flags & BCH_READ_NODECODE);
	BUG_ON(flags & BCH_READ_IN_RETRY);

	rbio->c = c;
	rbio->start_time = local_clock();

	for_each_btree_key(&trans, iter, BTREE_ID_EXTENTS,
			   POS(inode, rbio->bio.bi_iter.bi_sector),
			   BTREE_ITER_SLOTS, k, ret) {
		BKEY_PADDED(k) tmp;
		unsigned bytes;

		/*
		 * Unlock the iterator while the btree node's lock is still in
		 * cache, before doing the IO:
		 */
		bkey_reassemble(&tmp.k, k);
		k = bkey_i_to_s_c(&tmp.k);
		bch2_trans_unlock(&trans);

		bytes = min_t(unsigned, rbio->bio.bi_iter.bi_size,
			      (k.k->p.offset - rbio->bio.bi_iter.bi_sector) << 9);
		swap(rbio->bio.bi_iter.bi_size, bytes);

		if (rbio->bio.bi_iter.bi_size == bytes)
			flags |= BCH_READ_LAST_FRAGMENT;

		bch2_read_extent(c, rbio, k, flags);

		if (flags & BCH_READ_LAST_FRAGMENT)
			return;

		swap(rbio->bio.bi_iter.bi_size, bytes);
		bio_advance(&rbio->bio, bytes);
	}

	/*
	 * If we get here, it better have been because there was an error
	 * reading a btree node
	 */
	BUG_ON(!ret);
	bcache_io_error(c, &rbio->bio, "btree IO error: %i", ret);

	bch2_trans_exit(&trans);
	bch2_rbio_done(rbio);
}

void bch2_fs_io_exit(struct bch_fs *c)
{
	if (c->promote_table.tbl)
		rhashtable_destroy(&c->promote_table);
	mempool_exit(&c->bio_bounce_pages);
	bioset_exit(&c->bio_write);
	bioset_exit(&c->bio_read_split);
	bioset_exit(&c->bio_read);
}

int bch2_fs_io_init(struct bch_fs *c)
{
	if (bioset_init(&c->bio_read, 1, offsetof(struct bch_read_bio, bio),
			BIOSET_NEED_BVECS) ||
	    bioset_init(&c->bio_read_split, 1, offsetof(struct bch_read_bio, bio),
			BIOSET_NEED_BVECS) ||
	    bioset_init(&c->bio_write, 1, offsetof(struct bch_write_bio, bio),
			BIOSET_NEED_BVECS) ||
	    mempool_init_page_pool(&c->bio_bounce_pages,
				   max_t(unsigned,
					 c->opts.btree_node_size,
					 c->sb.encoded_extent_max) /
				   PAGE_SECTORS, 0) ||
	    rhashtable_init(&c->promote_table, &bch_promote_params))
		return -ENOMEM;

	return 0;
}
