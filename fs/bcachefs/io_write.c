// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2010, 2011 Kent Overstreet <kent.overstreet@gmail.com>
 * Copyright 2012 Google, Inc.
 */

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "bkey_buf.h"
#include "bset.h"
#include "btree_update.h"
#include "buckets.h"
#include "checksum.h"
#include "clock.h"
#include "compress.h"
#include "debug.h"
#include "ec.h"
#include "error.h"
#include "extent_update.h"
#include "inode.h"
#include "io_write.h"
#include "journal.h"
#include "keylist.h"
#include "move.h"
#include "nocow_locking.h"
#include "rebalance.h"
#include "subvolume.h"
#include "super.h"
#include "super-io.h"
#include "trace.h"

#include <linux/blkdev.h>
#include <linux/prefetch.h>
#include <linux/random.h>
#include <linux/sched/mm.h>

#ifndef CONFIG_BCACHEFS_NO_LATENCY_ACCT

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
		    now & ~(~0U << 5))
			break;

		new = ewma_add(old, io_latency, 5);
	} while ((v = atomic64_cmpxchg(latency, old, new)) != old);

	bch2_congested_acct(ca, io_latency, now, rw);

	__bch2_time_stats_update(&ca->io_latency[rw], submit_time, now);
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
		page = alloc_page(GFP_NOFS);
		if (unlikely(!page)) {
			mutex_lock(&c->bio_bounce_pages_lock);
			*using_mempool = true;
			goto pool_alloc;

		}
	} else {
pool_alloc:
		page = mempool_alloc(&c->bio_bounce_pages, GFP_NOFS);
	}

	return page;
}

void bch2_bio_alloc_pages_pool(struct bch_fs *c, struct bio *bio,
			       size_t size)
{
	bool using_mempool = false;

	while (size) {
		struct page *page = __bio_alloc_page_pool(c, &using_mempool);
		unsigned len = min_t(size_t, PAGE_SIZE, size);

		BUG_ON(!bio_add_page(bio, page, len, 0));
		size -= len;
	}

	if (using_mempool)
		mutex_unlock(&c->bio_bounce_pages_lock);
}

/* Extent update path: */

int bch2_sum_sector_overwrites(struct btree_trans *trans,
			       struct btree_iter *extent_iter,
			       struct bkey_i *new,
			       bool *usage_increasing,
			       s64 *i_sectors_delta,
			       s64 *disk_sectors_delta)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c old;
	unsigned new_replicas = bch2_bkey_replicas(c, bkey_i_to_s_c(new));
	bool new_compressed = bch2_bkey_sectors_compressed(bkey_i_to_s_c(new));
	int ret = 0;

	*usage_increasing	= false;
	*i_sectors_delta	= 0;
	*disk_sectors_delta	= 0;

	bch2_trans_copy_iter(&iter, extent_iter);

	for_each_btree_key_upto_continue_norestart(iter,
				new->k.p, BTREE_ITER_SLOTS, old, ret) {
		s64 sectors = min(new->k.p.offset, old.k->p.offset) -
			max(bkey_start_offset(&new->k),
			    bkey_start_offset(old.k));

		*i_sectors_delta += sectors *
			(bkey_extent_is_allocation(&new->k) -
			 bkey_extent_is_allocation(old.k));

		*disk_sectors_delta += sectors * bch2_bkey_nr_ptrs_allocated(bkey_i_to_s_c(new));
		*disk_sectors_delta -= new->k.p.snapshot == old.k->p.snapshot
			? sectors * bch2_bkey_nr_ptrs_fully_allocated(old)
			: 0;

		if (!*usage_increasing &&
		    (new->k.p.snapshot != old.k->p.snapshot ||
		     new_replicas > bch2_bkey_replicas(c, old) ||
		     (!new_compressed && bch2_bkey_sectors_compressed(old))))
			*usage_increasing = true;

		if (bkey_ge(old.k->p, new->k.p))
			break;
	}

	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static inline int bch2_extent_update_i_size_sectors(struct btree_trans *trans,
						    struct btree_iter *extent_iter,
						    u64 new_i_size,
						    s64 i_sectors_delta)
{
	struct btree_iter iter;
	struct bkey_i *k;
	struct bkey_i_inode_v3 *inode;
	/*
	 * Crazy performance optimization:
	 * Every extent update needs to also update the inode: the inode trigger
	 * will set bi->journal_seq to the journal sequence number of this
	 * transaction - for fsync.
	 *
	 * But if that's the only reason we're updating the inode (we're not
	 * updating bi_size or bi_sectors), then we don't need the inode update
	 * to be journalled - if we crash, the bi_journal_seq update will be
	 * lost, but that's fine.
	 */
	unsigned inode_update_flags = BTREE_UPDATE_NOJOURNAL;
	int ret;

	k = bch2_bkey_get_mut_noupdate(trans, &iter, BTREE_ID_inodes,
			      SPOS(0,
				   extent_iter->pos.inode,
				   extent_iter->snapshot),
			      BTREE_ITER_CACHED);
	ret = PTR_ERR_OR_ZERO(k);
	if (unlikely(ret))
		return ret;

	if (unlikely(k->k.type != KEY_TYPE_inode_v3)) {
		k = bch2_inode_to_v3(trans, k);
		ret = PTR_ERR_OR_ZERO(k);
		if (unlikely(ret))
			goto err;
	}

	inode = bkey_i_to_inode_v3(k);

	if (!(le64_to_cpu(inode->v.bi_flags) & BCH_INODE_i_size_dirty) &&
	    new_i_size > le64_to_cpu(inode->v.bi_size)) {
		inode->v.bi_size = cpu_to_le64(new_i_size);
		inode_update_flags = 0;
	}

	if (i_sectors_delta) {
		le64_add_cpu(&inode->v.bi_sectors, i_sectors_delta);
		inode_update_flags = 0;
	}

	if (inode->k.p.snapshot != iter.snapshot) {
		inode->k.p.snapshot = iter.snapshot;
		inode_update_flags = 0;
	}

	ret = bch2_trans_update(trans, &iter, &inode->k_i,
				BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE|
				inode_update_flags);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_extent_update(struct btree_trans *trans,
		       subvol_inum inum,
		       struct btree_iter *iter,
		       struct bkey_i *k,
		       struct disk_reservation *disk_res,
		       u64 new_i_size,
		       s64 *i_sectors_delta_total,
		       bool check_enospc)
{
	struct bpos next_pos;
	bool usage_increasing;
	s64 i_sectors_delta = 0, disk_sectors_delta = 0;
	int ret;

	/*
	 * This traverses us the iterator without changing iter->path->pos to
	 * search_key() (which is pos + 1 for extents): we want there to be a
	 * path already traversed at iter->pos because
	 * bch2_trans_extent_update() will use it to attempt extent merging
	 */
	ret = __bch2_btree_iter_traverse(iter);
	if (ret)
		return ret;

	ret = bch2_extent_trim_atomic(trans, iter, k);
	if (ret)
		return ret;

	next_pos = k->k.p;

	ret = bch2_sum_sector_overwrites(trans, iter, k,
			&usage_increasing,
			&i_sectors_delta,
			&disk_sectors_delta);
	if (ret)
		return ret;

	if (disk_res &&
	    disk_sectors_delta > (s64) disk_res->sectors) {
		ret = bch2_disk_reservation_add(trans->c, disk_res,
					disk_sectors_delta - disk_res->sectors,
					!check_enospc || !usage_increasing
					? BCH_DISK_RESERVATION_NOFAIL : 0);
		if (ret)
			return ret;
	}

	/*
	 * Note:
	 * We always have to do an inode update - even when i_size/i_sectors
	 * aren't changing - for fsync to work properly; fsync relies on
	 * inode->bi_journal_seq which is updated by the trigger code:
	 */
	ret =   bch2_extent_update_i_size_sectors(trans, iter,
						  min(k->k.p.offset << 9, new_i_size),
						  i_sectors_delta) ?:
		bch2_trans_update(trans, iter, k, 0) ?:
		bch2_trans_commit(trans, disk_res, NULL,
				BCH_TRANS_COMMIT_no_check_rw|
				BCH_TRANS_COMMIT_no_enospc);
	if (unlikely(ret))
		return ret;

	if (i_sectors_delta_total)
		*i_sectors_delta_total += i_sectors_delta;
	bch2_btree_iter_set_pos(iter, next_pos);
	return 0;
}

static int bch2_write_index_default(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct bkey_buf sk;
	struct keylist *keys = &op->insert_keys;
	struct bkey_i *k = bch2_keylist_front(keys);
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	subvol_inum inum = {
		.subvol = op->subvol,
		.inum	= k->k.p.inode,
	};
	int ret;

	BUG_ON(!inum.subvol);

	bch2_bkey_buf_init(&sk);

	do {
		bch2_trans_begin(trans);

		k = bch2_keylist_front(keys);
		bch2_bkey_buf_copy(&sk, c, k);

		ret = bch2_subvolume_get_snapshot(trans, inum.subvol,
						  &sk.k->k.p.snapshot);
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			continue;
		if (ret)
			break;

		bch2_trans_iter_init(trans, &iter, BTREE_ID_extents,
				     bkey_start_pos(&sk.k->k),
				     BTREE_ITER_SLOTS|BTREE_ITER_INTENT);

		ret =   bch2_bkey_set_needs_rebalance(c, sk.k,
					op->opts.background_target,
					op->opts.background_compression) ?:
			bch2_extent_update(trans, inum, &iter, sk.k,
					&op->res,
					op->new_i_size, &op->i_sectors_delta,
					op->flags & BCH_WRITE_CHECK_ENOSPC);
		bch2_trans_iter_exit(trans, &iter);

		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			continue;
		if (ret)
			break;

		if (bkey_ge(iter.pos, k->k.p))
			bch2_keylist_pop_front(&op->insert_keys);
		else
			bch2_cut_front(iter.pos, k);
	} while (!bch2_keylist_empty(keys));

	bch2_trans_put(trans);
	bch2_bkey_buf_exit(&sk, c);

	return ret;
}

/* Writes */

void bch2_submit_wbio_replicas(struct bch_write_bio *wbio, struct bch_fs *c,
			       enum bch_data_type type,
			       const struct bkey_i *k,
			       bool nocow)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(bkey_i_to_s_c(k));
	struct bch_write_bio *n;

	BUG_ON(c->opts.nochanges);

	bkey_for_each_ptr(ptrs, ptr) {
		BUG_ON(!bch2_dev_exists2(c, ptr->dev));

		struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);

		if (to_entry(ptr + 1) < ptrs.end) {
			n = to_wbio(bio_alloc_clone(NULL, &wbio->bio,
						GFP_NOFS, &ca->replica_set));

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
		n->have_ioref		= nocow || bch2_dev_get_ioref(ca,
					type == BCH_DATA_btree ? READ : WRITE);
		n->nocow		= nocow;
		n->submit_time		= local_clock();
		n->inode_offset		= bkey_start_offset(&k->k);
		n->bio.bi_iter.bi_sector = ptr->offset;

		if (likely(n->have_ioref)) {
			this_cpu_add(ca->io_done->sectors[WRITE][type],
				     bio_sectors(&n->bio));

			bio_set_dev(&n->bio, ca->disk_sb.bdev);

			if (type != BCH_DATA_btree && unlikely(c->opts.no_data_io)) {
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

static void __bch2_write(struct bch_write_op *);

static void bch2_write_done(struct closure *cl)
{
	struct bch_write_op *op = container_of(cl, struct bch_write_op, cl);
	struct bch_fs *c = op->c;

	EBUG_ON(op->open_buckets.nr);

	bch2_time_stats_update(&c->times[BCH_TIME_data_write], op->start_time);
	bch2_disk_reservation_put(c, &op->res);

	if (!(op->flags & BCH_WRITE_MOVE))
		bch2_write_ref_put(c, BCH_WRITE_REF_write);
	bch2_keylist_free(&op->insert_keys, op->inline_keys);

	EBUG_ON(cl->parent);
	closure_debug_destroy(cl);
	if (op->end_io)
		op->end_io(op);
}

static noinline int bch2_write_drop_io_error_ptrs(struct bch_write_op *op)
{
	struct keylist *keys = &op->insert_keys;
	struct bch_extent_ptr *ptr;
	struct bkey_i *src, *dst = keys->keys, *n;

	for (src = keys->keys; src != keys->top; src = n) {
		n = bkey_next(src);

		if (bkey_extent_is_direct_data(&src->k)) {
			bch2_bkey_drop_ptrs(bkey_i_to_s(src), ptr,
					    test_bit(ptr->dev, op->failed.d));

			if (!bch2_bkey_nr_ptrs(bkey_i_to_s_c(src)))
				return -EIO;
		}

		if (dst != src)
			memmove_u64s_down(dst, src, src->k.u64s);
		dst = bkey_next(dst);
	}

	keys->top = dst;
	return 0;
}

/**
 * __bch2_write_index - after a write, update index to point to new data
 * @op:		bch_write_op to process
 */
static void __bch2_write_index(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct keylist *keys = &op->insert_keys;
	unsigned dev;
	int ret = 0;

	if (unlikely(op->flags & BCH_WRITE_IO_ERROR)) {
		ret = bch2_write_drop_io_error_ptrs(op);
		if (ret)
			goto err;
	}

	if (!bch2_keylist_empty(keys)) {
		u64 sectors_start = keylist_sectors(keys);

		ret = !(op->flags & BCH_WRITE_MOVE)
			? bch2_write_index_default(op)
			: bch2_data_update_index_update(op);

		BUG_ON(bch2_err_matches(ret, BCH_ERR_transaction_restart));
		BUG_ON(keylist_sectors(keys) && !ret);

		op->written += sectors_start - keylist_sectors(keys);

		if (ret && !bch2_err_matches(ret, EROFS)) {
			struct bkey_i *insert = bch2_keylist_front(&op->insert_keys);

			bch_err_inum_offset_ratelimited(c,
				insert->k.p.inode, insert->k.p.offset << 9,
				"write error while doing btree update: %s",
				bch2_err_str(ret));
		}

		if (ret)
			goto err;
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
	op->flags |= BCH_WRITE_DONE;
	goto out;
}

static inline void __wp_update_state(struct write_point *wp, enum write_point_state state)
{
	if (state != wp->state) {
		u64 now = ktime_get_ns();

		if (wp->last_state_change &&
		    time_after64(now, wp->last_state_change))
			wp->time[wp->state] += now - wp->last_state_change;
		wp->state = state;
		wp->last_state_change = now;
	}
}

static inline void wp_update_state(struct write_point *wp, bool running)
{
	enum write_point_state state;

	state = running			 ? WRITE_POINT_running :
		!list_empty(&wp->writes) ? WRITE_POINT_waiting_io
					 : WRITE_POINT_stopped;

	__wp_update_state(wp, state);
}

static CLOSURE_CALLBACK(bch2_write_index)
{
	closure_type(op, struct bch_write_op, cl);
	struct write_point *wp = op->wp;
	struct workqueue_struct *wq = index_update_wq(op);
	unsigned long flags;

	if ((op->flags & BCH_WRITE_DONE) &&
	    (op->flags & BCH_WRITE_MOVE))
		bch2_bio_free_pages_pool(op->c, &op->wbio.bio);

	spin_lock_irqsave(&wp->writes_lock, flags);
	if (wp->state == WRITE_POINT_waiting_io)
		__wp_update_state(wp, WRITE_POINT_waiting_work);
	list_add_tail(&op->wp_list, &wp->writes);
	spin_unlock_irqrestore (&wp->writes_lock, flags);

	queue_work(wq, &wp->index_update_work);
}

static inline void bch2_write_queue(struct bch_write_op *op, struct write_point *wp)
{
	op->wp = wp;

	if (wp->state == WRITE_POINT_stopped) {
		spin_lock_irq(&wp->writes_lock);
		__wp_update_state(wp, WRITE_POINT_waiting_io);
		spin_unlock_irq(&wp->writes_lock);
	}
}

void bch2_write_point_do_index_updates(struct work_struct *work)
{
	struct write_point *wp =
		container_of(work, struct write_point, index_update_work);
	struct bch_write_op *op;

	while (1) {
		spin_lock_irq(&wp->writes_lock);
		op = list_first_entry_or_null(&wp->writes, struct bch_write_op, wp_list);
		if (op)
			list_del(&op->wp_list);
		wp_update_state(wp, op != NULL);
		spin_unlock_irq(&wp->writes_lock);

		if (!op)
			break;

		op->flags |= BCH_WRITE_IN_WORKER;

		__bch2_write_index(op);

		if (!(op->flags & BCH_WRITE_DONE))
			__bch2_write(op);
		else
			bch2_write_done(&op->cl);
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

	if (bch2_dev_inum_io_err_on(bio->bi_status, ca, BCH_MEMBER_ERROR_write,
				    op->pos.inode,
				    wbio->inode_offset << 9,
				    "data write error: %s",
				    bch2_blk_status_to_str(bio->bi_status))) {
		set_bit(wbio->dev, op->failed.d);
		op->flags |= BCH_WRITE_IO_ERROR;
	}

	if (wbio->nocow)
		set_bit(wbio->dev, op->devs_need_flush->d);

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
	struct bkey_i_extent *e;

	op->pos.offset += crc.uncompressed_size;

	e = bkey_extent_init(op->insert_keys.top);
	e->k.p		= op->pos;
	e->k.size	= crc.uncompressed_size;
	e->k.version	= version;

	if (crc.csum_type ||
	    crc.compression_type ||
	    crc.nonce)
		bch2_extent_crc_append(&e->k_i, crc);

	bch2_alloc_sectors_append_ptrs_inlined(op->c, wp, &e->k_i, crc.compressed_size,
				       op->flags & BCH_WRITE_CACHED);

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
	unsigned pages = DIV_ROUND_UP(output_available +
				      (buf
				       ? ((unsigned long) buf & (PAGE_SIZE - 1))
				       : 0), PAGE_SIZE);

	pages = min(pages, BIO_MAX_VECS);

	bio = bio_alloc_bioset(NULL, pages, 0,
			       GFP_NOFS, &c->bio_write);
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
					c->opts.encoded_extent_max));

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
	int ret;

	if (!bch2_csum_type_is_encryption(op->crc.csum_type))
		return 0;

	/*
	 * If we need to decrypt data in the write path, we'll no longer be able
	 * to verify the existing checksum (poly1305 mac, in this case) after
	 * it's decrypted - this is the last point we'll be able to reverify the
	 * checksum:
	 */
	csum = bch2_checksum_bio(c, op->crc.csum_type, nonce, &op->wbio.bio);
	if (bch2_crc_cmp(op->crc.csum, csum) && !c->opts.no_data_io)
		return -EIO;

	ret = bch2_encrypt_bio(c, op->crc.csum_type, nonce, &op->wbio.bio);
	op->crc.csum_type = 0;
	op->crc.csum = (struct bch_csum) { 0, 0 };
	return ret;
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
	    op->crc.uncompressed_size <= c->opts.encoded_extent_max >> 9 &&
	    op->crc.compressed_size <= wp->sectors_free &&
	    (op->crc.compression_type == bch2_compression_opt_to_type(op->compression_opt) ||
	     op->incompressible)) {
		if (!crc_is_compressed(op->crc) &&
		    op->csum_type != op->crc.csum_type &&
		    bch2_write_rechecksum(c, op, op->csum_type) &&
		    !c->opts.no_data_io)
			return PREP_ENCODED_CHECKSUM_ERR;

		return PREP_ENCODED_DO_WRITE;
	}

	/*
	 * If the data is compressed and we couldn't write the entire extent as
	 * is, we have to decompress it:
	 */
	if (crc_is_compressed(op->crc)) {
		struct bch_csum csum;

		if (bch2_write_decrypt(op))
			return PREP_ENCODED_CHECKSUM_ERR;

		/* Last point we can still verify checksum: */
		csum = bch2_checksum_bio(c, op->crc.csum_type,
					 extent_nonce(op->version, op->crc),
					 bio);
		if (bch2_crc_cmp(op->crc.csum, csum) && !c->opts.no_data_io)
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
	    bch2_write_rechecksum(c, op, op->csum_type) &&
	    !c->opts.no_data_io)
		return PREP_ENCODED_CHECKSUM_ERR;

	/*
	 * If we want to compress the data, it has to be decrypted:
	 */
	if ((op->compression_opt ||
	     bch2_csum_type_is_encryption(op->crc.csum_type) !=
	     bch2_csum_type_is_encryption(op->csum_type)) &&
	    bch2_write_decrypt(op))
		return PREP_ENCODED_CHECKSUM_ERR;

	return PREP_ENCODED_OK;
}

static int bch2_write_extent(struct bch_write_op *op, struct write_point *wp,
			     struct bio **_dst)
{
	struct bch_fs *c = op->c;
	struct bio *src = &op->wbio.bio, *dst = src;
	struct bvec_iter saved_iter;
	void *ec_buf;
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
		/* XXX look for bug here */
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
	    op->compression_opt ||
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
		struct bch_extent_crc_unpacked crc = { 0 };
		struct bversion version = op->version;
		size_t dst_len = 0, src_len = 0;

		if (page_alloc_failed &&
		    dst->bi_iter.bi_size  < (wp->sectors_free << 9) &&
		    dst->bi_iter.bi_size < c->opts.encoded_extent_max)
			break;

		BUG_ON(op->compression_opt &&
		       (op->flags & BCH_WRITE_DATA_ENCODED) &&
		       bch2_csum_type_is_encryption(op->crc.csum_type));
		BUG_ON(op->compression_opt && !bounce);

		crc.compression_type = op->incompressible
			? BCH_COMPRESSION_TYPE_incompressible
			: op->compression_opt
			? bch2_bio_compress(c, dst, &dst_len, src, &src_len,
					    op->compression_opt)
			: 0;
		if (!crc_is_compressed(crc)) {
			dst_len = min(dst->bi_iter.bi_size, src->bi_iter.bi_size);
			dst_len = min_t(unsigned, dst_len, wp->sectors_free << 9);

			if (op->csum_type)
				dst_len = min_t(unsigned, dst_len,
						c->opts.encoded_extent_max);

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
				version.lo = atomic64_inc_return(&c->key_version);
			} else {
				crc.nonce = op->nonce;
				op->nonce += src_len >> 9;
			}
		}

		if ((op->flags & BCH_WRITE_DATA_ENCODED) &&
		    !crc_is_compressed(crc) &&
		    bch2_csum_type_is_encryption(op->crc.csum_type) ==
		    bch2_csum_type_is_encryption(op->csum_type)) {
			u8 compression_type = crc.compression_type;
			u16 nonce = crc.nonce;
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
			/*
			 * rchecksum_bio sets compression_type on crc from op->crc,
			 * this isn't always correct as sometimes we're changing
			 * an extent from uncompressed to incompressible.
			 */
			crc.compression_type = compression_type;
			crc.nonce = nonce;
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
			ret = bch2_encrypt_bio(c, op->csum_type,
					       extent_nonce(version, crc), dst);
			if (ret)
				goto err;

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
				GFP_NOFS, &c->bio_write);
		wbio_init(dst)->put_bio	= true;
		/* copy WRITE_SYNC flag */
		dst->bi_opf		= src->bi_opf;
	}

	dst->bi_iter.bi_size = total_output;
do_write:
	*_dst = dst;
	return more;
csum_err:
	bch_err(c, "error verifying existing checksum while rewriting existing data (memory corruption?)");
	ret = -EIO;
err:
	if (to_wbio(dst)->bounce)
		bch2_bio_free_pages_pool(c, dst);
	if (to_wbio(dst)->put_bio)
		bio_put(dst);

	return ret;
}

static bool bch2_extent_is_writeable(struct bch_write_op *op,
				     struct bkey_s_c k)
{
	struct bch_fs *c = op->c;
	struct bkey_s_c_extent e;
	struct extent_ptr_decoded p;
	const union bch_extent_entry *entry;
	unsigned replicas = 0;

	if (k.k->type != KEY_TYPE_extent)
		return false;

	e = bkey_s_c_to_extent(k);
	extent_for_each_ptr_decode(e, p, entry) {
		if (crc_is_encoded(p.crc) || p.has_ec)
			return false;

		replicas += bch2_extent_ptr_durability(c, &p);
	}

	return replicas >= op->opts.data_replicas;
}

static inline void bch2_nocow_write_unlock(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;

	for_each_keylist_key(&op->insert_keys, k) {
		struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(bkey_i_to_s_c(k));

		bkey_for_each_ptr(ptrs, ptr)
			bch2_bucket_nocow_unlock(&c->nocow_locks,
						 PTR_BUCKET_POS(c, ptr),
						 BUCKET_NOCOW_LOCK_UPDATE);
	}
}

static int bch2_nocow_write_convert_one_unwritten(struct btree_trans *trans,
						  struct btree_iter *iter,
						  struct bkey_i *orig,
						  struct bkey_s_c k,
						  u64 new_i_size)
{
	if (!bch2_extents_match(bkey_i_to_s_c(orig), k)) {
		/* trace this */
		return 0;
	}

	struct bkey_i *new = bch2_bkey_make_mut_noupdate(trans, k);
	int ret = PTR_ERR_OR_ZERO(new);
	if (ret)
		return ret;

	bch2_cut_front(bkey_start_pos(&orig->k), new);
	bch2_cut_back(orig->k.p, new);

	struct bkey_ptrs ptrs = bch2_bkey_ptrs(bkey_i_to_s(new));
	bkey_for_each_ptr(ptrs, ptr)
		ptr->unwritten = 0;

	/*
	 * Note that we're not calling bch2_subvol_get_snapshot() in this path -
	 * that was done when we kicked off the write, and here it's important
	 * that we update the extent that we wrote to - even if a snapshot has
	 * since been created. The write is still outstanding, so we're ok
	 * w.r.t. snapshot atomicity:
	 */
	return  bch2_extent_update_i_size_sectors(trans, iter,
					min(new->k.p.offset << 9, new_i_size), 0) ?:
		bch2_trans_update(trans, iter, new,
				  BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE);
}

static void bch2_nocow_write_convert_unwritten(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct btree_trans *trans = bch2_trans_get(c);

	for_each_keylist_key(&op->insert_keys, orig) {
		int ret = for_each_btree_key_upto_commit(trans, iter, BTREE_ID_extents,
				     bkey_start_pos(&orig->k), orig->k.p,
				     BTREE_ITER_INTENT, k,
				     NULL, NULL, BCH_TRANS_COMMIT_no_enospc, ({
			bch2_nocow_write_convert_one_unwritten(trans, &iter, orig, k, op->new_i_size);
		}));

		if (ret && !bch2_err_matches(ret, EROFS)) {
			struct bkey_i *insert = bch2_keylist_front(&op->insert_keys);

			bch_err_inum_offset_ratelimited(c,
				insert->k.p.inode, insert->k.p.offset << 9,
				"write error while doing btree update: %s",
				bch2_err_str(ret));
		}

		if (ret) {
			op->error = ret;
			break;
		}
	}

	bch2_trans_put(trans);
}

static void __bch2_nocow_write_done(struct bch_write_op *op)
{
	bch2_nocow_write_unlock(op);

	if (unlikely(op->flags & BCH_WRITE_IO_ERROR)) {
		op->error = -EIO;
	} else if (unlikely(op->flags & BCH_WRITE_CONVERT_UNWRITTEN))
		bch2_nocow_write_convert_unwritten(op);
}

static CLOSURE_CALLBACK(bch2_nocow_write_done)
{
	closure_type(op, struct bch_write_op, cl);

	__bch2_nocow_write_done(op);
	bch2_write_done(cl);
}

struct bucket_to_lock {
	struct bpos		b;
	unsigned		gen;
	struct nocow_lock_bucket *l;
};

static void bch2_nocow_write(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct btree_trans *trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	DARRAY_PREALLOCATED(struct bucket_to_lock, 3) buckets;
	u32 snapshot;
	struct bucket_to_lock *stale_at;
	int ret;

	if (op->flags & BCH_WRITE_MOVE)
		return;

	darray_init(&buckets);
	trans = bch2_trans_get(c);
retry:
	bch2_trans_begin(trans);

	ret = bch2_subvolume_get_snapshot(trans, op->subvol, &snapshot);
	if (unlikely(ret))
		goto err;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_extents,
			     SPOS(op->pos.inode, op->pos.offset, snapshot),
			     BTREE_ITER_SLOTS);
	while (1) {
		struct bio *bio = &op->wbio.bio;

		buckets.nr = 0;

		k = bch2_btree_iter_peek_slot(&iter);
		ret = bkey_err(k);
		if (ret)
			break;

		/* fall back to normal cow write path? */
		if (unlikely(k.k->p.snapshot != snapshot ||
			     !bch2_extent_is_writeable(op, k)))
			break;

		if (bch2_keylist_realloc(&op->insert_keys,
					 op->inline_keys,
					 ARRAY_SIZE(op->inline_keys),
					 k.k->u64s))
			break;

		/* Get iorefs before dropping btree locks: */
		struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
		bkey_for_each_ptr(ptrs, ptr) {
			struct bpos b = PTR_BUCKET_POS(c, ptr);
			struct nocow_lock_bucket *l =
				bucket_nocow_lock(&c->nocow_locks, bucket_to_u64(b));
			prefetch(l);

			if (unlikely(!bch2_dev_get_ioref(bch_dev_bkey_exists(c, ptr->dev), WRITE)))
				goto err_get_ioref;

			/* XXX allocating memory with btree locks held - rare */
			darray_push_gfp(&buckets, ((struct bucket_to_lock) {
						   .b = b, .gen = ptr->gen, .l = l,
						   }), GFP_KERNEL|__GFP_NOFAIL);

			if (ptr->unwritten)
				op->flags |= BCH_WRITE_CONVERT_UNWRITTEN;
		}

		/* Unlock before taking nocow locks, doing IO: */
		bkey_reassemble(op->insert_keys.top, k);
		bch2_trans_unlock(trans);

		bch2_cut_front(op->pos, op->insert_keys.top);
		if (op->flags & BCH_WRITE_CONVERT_UNWRITTEN)
			bch2_cut_back(POS(op->pos.inode, op->pos.offset + bio_sectors(bio)), op->insert_keys.top);

		darray_for_each(buckets, i) {
			struct bch_dev *ca = bch_dev_bkey_exists(c, i->b.inode);

			__bch2_bucket_nocow_lock(&c->nocow_locks, i->l,
						 bucket_to_u64(i->b),
						 BUCKET_NOCOW_LOCK_UPDATE);

			rcu_read_lock();
			bool stale = gen_after(*bucket_gen(ca, i->b.offset), i->gen);
			rcu_read_unlock();

			if (unlikely(stale)) {
				stale_at = i;
				goto err_bucket_stale;
			}
		}

		bio = &op->wbio.bio;
		if (k.k->p.offset < op->pos.offset + bio_sectors(bio)) {
			bio = bio_split(bio, k.k->p.offset - op->pos.offset,
					GFP_KERNEL, &c->bio_write);
			wbio_init(bio)->put_bio = true;
			bio->bi_opf = op->wbio.bio.bi_opf;
		} else {
			op->flags |= BCH_WRITE_DONE;
		}

		op->pos.offset += bio_sectors(bio);
		op->written += bio_sectors(bio);

		bio->bi_end_io	= bch2_write_endio;
		bio->bi_private	= &op->cl;
		bio->bi_opf |= REQ_OP_WRITE;
		closure_get(&op->cl);
		bch2_submit_wbio_replicas(to_wbio(bio), c, BCH_DATA_user,
					  op->insert_keys.top, true);

		bch2_keylist_push(&op->insert_keys);
		if (op->flags & BCH_WRITE_DONE)
			break;
		bch2_btree_iter_advance(&iter);
	}
out:
	bch2_trans_iter_exit(trans, &iter);
err:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	if (ret) {
		bch_err_inum_offset_ratelimited(c,
			op->pos.inode, op->pos.offset << 9,
			"%s: btree lookup error %s", __func__, bch2_err_str(ret));
		op->error = ret;
		op->flags |= BCH_WRITE_DONE;
	}

	bch2_trans_put(trans);
	darray_exit(&buckets);

	/* fallback to cow write path? */
	if (!(op->flags & BCH_WRITE_DONE)) {
		closure_sync(&op->cl);
		__bch2_nocow_write_done(op);
		op->insert_keys.top = op->insert_keys.keys;
	} else if (op->flags & BCH_WRITE_SYNC) {
		closure_sync(&op->cl);
		bch2_nocow_write_done(&op->cl.work);
	} else {
		/*
		 * XXX
		 * needs to run out of process context because ei_quota_lock is
		 * a mutex
		 */
		continue_at(&op->cl, bch2_nocow_write_done, index_update_wq(op));
	}
	return;
err_get_ioref:
	darray_for_each(buckets, i)
		percpu_ref_put(&bch_dev_bkey_exists(c, i->b.inode)->io_ref);

	/* Fall back to COW path: */
	goto out;
err_bucket_stale:
	darray_for_each(buckets, i) {
		bch2_bucket_nocow_unlock(&c->nocow_locks, i->b, BUCKET_NOCOW_LOCK_UPDATE);
		if (i == stale_at)
			break;
	}

	/* We can retry this: */
	ret = -BCH_ERR_transaction_restart;
	goto err_get_ioref;
}

static void __bch2_write(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct write_point *wp = NULL;
	struct bio *bio = NULL;
	unsigned nofs_flags;
	int ret;

	nofs_flags = memalloc_nofs_save();

	if (unlikely(op->opts.nocow && c->opts.nocow_enabled)) {
		bch2_nocow_write(op);
		if (op->flags & BCH_WRITE_DONE)
			goto out_nofs_restore;
	}
again:
	memset(&op->failed, 0, sizeof(op->failed));

	do {
		struct bkey_i *key_to_write;
		unsigned key_to_write_offset = op->insert_keys.top_p -
			op->insert_keys.keys_p;

		/* +1 for possible cache device: */
		if (op->open_buckets.nr + op->nr_replicas + 1 >
		    ARRAY_SIZE(op->open_buckets.v))
			break;

		if (bch2_keylist_realloc(&op->insert_keys,
					op->inline_keys,
					ARRAY_SIZE(op->inline_keys),
					BKEY_EXTENT_U64s_MAX))
			break;

		/*
		 * The copygc thread is now global, which means it's no longer
		 * freeing up space on specific disks, which means that
		 * allocations for specific disks may hang arbitrarily long:
		 */
		ret = bch2_trans_do(c, NULL, NULL, 0,
			bch2_alloc_sectors_start_trans(trans,
				op->target,
				op->opts.erasure_code && !(op->flags & BCH_WRITE_CACHED),
				op->write_point,
				&op->devs_have,
				op->nr_replicas,
				op->nr_replicas_required,
				op->watermark,
				op->flags,
				(op->flags & (BCH_WRITE_ALLOC_NOWAIT|
					      BCH_WRITE_ONLY_SPECIFIED_DEVS))
				? NULL : &op->cl, &wp));
		if (unlikely(ret)) {
			if (bch2_err_matches(ret, BCH_ERR_operation_blocked))
				break;

			goto err;
		}

		EBUG_ON(!wp);

		bch2_open_bucket_get(c, wp, &op->open_buckets);
		ret = bch2_write_extent(op, wp, &bio);

		bch2_alloc_sectors_done_inlined(c, wp);
err:
		if (ret <= 0) {
			op->flags |= BCH_WRITE_DONE;

			if (ret < 0) {
				bch_err_inum_offset_ratelimited(c,
					op->pos.inode,
					op->pos.offset << 9,
					"%s(): error: %s", __func__, bch2_err_str(ret));
				op->error = ret;
				break;
			}
		}

		bio->bi_end_io	= bch2_write_endio;
		bio->bi_private	= &op->cl;
		bio->bi_opf |= REQ_OP_WRITE;

		closure_get(bio->bi_private);

		key_to_write = (void *) (op->insert_keys.keys_p +
					 key_to_write_offset);

		bch2_submit_wbio_replicas(to_wbio(bio), c, BCH_DATA_user,
					  key_to_write, false);
	} while (ret);

	/*
	 * Sync or no?
	 *
	 * If we're running asynchronously, wne may still want to block
	 * synchronously here if we weren't able to submit all of the IO at
	 * once, as that signals backpressure to the caller.
	 */
	if ((op->flags & BCH_WRITE_SYNC) ||
	    (!(op->flags & BCH_WRITE_DONE) &&
	     !(op->flags & BCH_WRITE_IN_WORKER))) {
		closure_sync(&op->cl);
		__bch2_write_index(op);

		if (!(op->flags & BCH_WRITE_DONE))
			goto again;
		bch2_write_done(&op->cl);
	} else {
		bch2_write_queue(op, wp);
		continue_at(&op->cl, bch2_write_index, NULL);
	}
out_nofs_restore:
	memalloc_nofs_restore(nofs_flags);
}

static void bch2_write_data_inline(struct bch_write_op *op, unsigned data_len)
{
	struct bio *bio = &op->wbio.bio;
	struct bvec_iter iter;
	struct bkey_i_inline_data *id;
	unsigned sectors;
	int ret;

	op->flags |= BCH_WRITE_WROTE_DATA_INLINE;
	op->flags |= BCH_WRITE_DONE;

	bch2_check_set_feature(op->c, BCH_FEATURE_inline_data);

	ret = bch2_keylist_realloc(&op->insert_keys, op->inline_keys,
				   ARRAY_SIZE(op->inline_keys),
				   BKEY_U64s + DIV_ROUND_UP(data_len, 8));
	if (ret) {
		op->error = ret;
		goto err;
	}

	sectors = bio_sectors(bio);
	op->pos.offset += sectors;

	id = bkey_inline_data_init(op->insert_keys.top);
	id->k.p		= op->pos;
	id->k.version	= op->version;
	id->k.size	= sectors;

	iter = bio->bi_iter;
	iter.bi_size = data_len;
	memcpy_from_bio(id->v.data, bio, iter);

	while (data_len & 7)
		id->v.data[data_len++] = '\0';
	set_bkey_val_bytes(&id->k, data_len);
	bch2_keylist_push(&op->insert_keys);

	__bch2_write_index(op);
err:
	bch2_write_done(&op->cl);
}

/**
 * bch2_write() - handle a write to a cache device or flash only volume
 * @cl:		&bch_write_op->cl
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
CLOSURE_CALLBACK(bch2_write)
{
	closure_type(op, struct bch_write_op, cl);
	struct bio *bio = &op->wbio.bio;
	struct bch_fs *c = op->c;
	unsigned data_len;

	EBUG_ON(op->cl.parent);
	BUG_ON(!op->nr_replicas);
	BUG_ON(!op->write_point.v);
	BUG_ON(bkey_eq(op->pos, POS_MAX));

	op->start_time = local_clock();
	bch2_keylist_init(&op->insert_keys, op->inline_keys);
	wbio_init(bio)->put_bio = false;

	if (bio->bi_iter.bi_size & (c->opts.block_size - 1)) {
		bch_err_inum_offset_ratelimited(c,
			op->pos.inode,
			op->pos.offset << 9,
			"misaligned write");
		op->error = -EIO;
		goto err;
	}

	if (c->opts.nochanges) {
		op->error = -BCH_ERR_erofs_no_writes;
		goto err;
	}

	if (!(op->flags & BCH_WRITE_MOVE) &&
	    !bch2_write_ref_tryget(c, BCH_WRITE_REF_write)) {
		op->error = -BCH_ERR_erofs_no_writes;
		goto err;
	}

	this_cpu_add(c->counters[BCH_COUNTER_io_write], bio_sectors(bio));
	bch2_increment_clock(c, bio_sectors(bio), WRITE);

	data_len = min_t(u64, bio->bi_iter.bi_size,
			 op->new_i_size - (op->pos.offset << 9));

	if (c->opts.inline_data &&
	    data_len <= min(block_bytes(c) / 2, 1024U)) {
		bch2_write_data_inline(op, data_len);
		return;
	}

	__bch2_write(op);
	return;
err:
	bch2_disk_reservation_put(c, &op->res);

	closure_debug_destroy(&op->cl);
	if (op->end_io)
		op->end_io(op);
}

static const char * const bch2_write_flags[] = {
#define x(f)	#f,
	BCH_WRITE_FLAGS()
#undef x
	NULL
};

void bch2_write_op_to_text(struct printbuf *out, struct bch_write_op *op)
{
	prt_str(out, "pos: ");
	bch2_bpos_to_text(out, op->pos);
	prt_newline(out);
	printbuf_indent_add(out, 2);

	prt_str(out, "started: ");
	bch2_pr_time_units(out, local_clock() - op->start_time);
	prt_newline(out);

	prt_str(out, "flags: ");
	prt_bitflags(out, bch2_write_flags, op->flags);
	prt_newline(out);

	prt_printf(out, "ref: %u", closure_nr_remaining(&op->cl));
	prt_newline(out);

	printbuf_indent_sub(out, 2);
}

void bch2_fs_io_write_exit(struct bch_fs *c)
{
	mempool_exit(&c->bio_bounce_pages);
	bioset_exit(&c->bio_write);
}

int bch2_fs_io_write_init(struct bch_fs *c)
{
	if (bioset_init(&c->bio_write, 1, offsetof(struct bch_write_bio, bio),
			BIOSET_NEED_BVECS))
		return -BCH_ERR_ENOMEM_bio_write_init;

	if (mempool_init_page_pool(&c->bio_bounce_pages,
				   max_t(unsigned,
					 c->opts.btree_node_size,
					 c->opts.encoded_extent_max) /
				   PAGE_SIZE, 0))
		return -BCH_ERR_ENOMEM_bio_bounce_pages_init;

	return 0;
}
