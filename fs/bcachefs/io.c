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
#include "bkey_buf.h"
#include "bset.h"
#include "btree_update.h"
#include "buckets.h"
#include "checksum.h"
#include "compress.h"
#include "clock.h"
#include "data_update.h"
#include "debug.h"
#include "disk_groups.h"
#include "ec.h"
#include "error.h"
#include "extent_update.h"
#include "inode.h"
#include "io.h"
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

const char *bch2_blk_status_to_str(blk_status_t status)
{
	if (status == BLK_STS_REMOVED)
		return "device removed";
	return blk_status_to_str(status);
}

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

	for_each_btree_key_continue_norestart(iter, BTREE_ITER_SLOTS, old, ret) {
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
	unsigned inode_update_flags = BTREE_UPDATE_NOJOURNAL;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_inodes,
			     SPOS(0,
				  extent_iter->pos.inode,
				  extent_iter->snapshot),
			     BTREE_ITER_INTENT|BTREE_ITER_CACHED);
	k = bch2_bkey_get_mut(trans, &iter);
	ret = PTR_ERR_OR_ZERO(k);
	if (unlikely(ret))
		goto err;

	if (unlikely(k->k.type != KEY_TYPE_inode_v3)) {
		k = bch2_inode_to_v3(trans, k);
		ret = PTR_ERR_OR_ZERO(k);
		if (unlikely(ret))
			goto err;
	}

	inode = bkey_i_to_inode_v3(k);

	if (!(le64_to_cpu(inode->v.bi_flags) & BCH_INODE_I_SIZE_DIRTY) &&
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
				BTREE_INSERT_NOCHECK_RW|
				BTREE_INSERT_NOFAIL);
	if (unlikely(ret))
		return ret;

	if (i_sectors_delta_total)
		*i_sectors_delta_total += i_sectors_delta;
	bch2_btree_iter_set_pos(iter, next_pos);
	return 0;
}

/* Overwrites whatever was present with zeroes: */
int bch2_extent_fallocate(struct btree_trans *trans,
			  subvol_inum inum,
			  struct btree_iter *iter,
			  unsigned sectors,
			  struct bch_io_opts opts,
			  s64 *i_sectors_delta,
			  struct write_point_specifier write_point)
{
	struct bch_fs *c = trans->c;
	struct disk_reservation disk_res = { 0 };
	struct closure cl;
	struct open_buckets open_buckets;
	struct bkey_s_c k;
	struct bkey_buf old, new;
	bool have_reservation = false;
	bool unwritten = opts.nocow &&
	    c->sb.version >= bcachefs_metadata_version_unwritten_extents;
	int ret;

	bch2_bkey_buf_init(&old);
	bch2_bkey_buf_init(&new);
	closure_init_stack(&cl);
	open_buckets.nr = 0;
retry:
	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret)
		return ret;

	sectors = min_t(u64, sectors, k.k->p.offset - iter->pos.offset);

	if (!have_reservation) {
		unsigned new_replicas =
			max(0, (int) opts.data_replicas -
			    (int) bch2_bkey_nr_ptrs_fully_allocated(k));
		/*
		 * Get a disk reservation before (in the nocow case) calling
		 * into the allocator:
		 */
		ret = bch2_disk_reservation_get(c, &disk_res, sectors, new_replicas, 0);
		if (unlikely(ret))
			goto out;

		bch2_bkey_buf_reassemble(&old, c, k);
	}

	if (have_reservation) {
		if (!bch2_extents_match(k, bkey_i_to_s_c(old.k)))
			goto out;

		bch2_key_resize(&new.k->k, sectors);
	} else if (!unwritten) {
		struct bkey_i_reservation *reservation;

		bch2_bkey_buf_realloc(&new, c, sizeof(*reservation) / sizeof(u64));
		reservation = bkey_reservation_init(new.k);
		reservation->k.p = iter->pos;
		bch2_key_resize(&reservation->k, sectors);
		reservation->v.nr_replicas = opts.data_replicas;
	} else {
		struct bkey_i_extent *e;
		struct bch_devs_list devs_have;
		struct write_point *wp;
		struct bch_extent_ptr *ptr;

		devs_have.nr = 0;

		bch2_bkey_buf_realloc(&new, c, BKEY_EXTENT_U64s_MAX);

		e = bkey_extent_init(new.k);
		e->k.p = iter->pos;

		ret = bch2_alloc_sectors_start_trans(trans,
				opts.foreground_target,
				false,
				write_point,
				&devs_have,
				opts.data_replicas,
				opts.data_replicas,
				RESERVE_none, 0, &cl, &wp);
		if (bch2_err_matches(ret, BCH_ERR_operation_blocked)) {
			bch2_trans_unlock(trans);
			closure_sync(&cl);
			goto retry;
		}
		if (ret)
			return ret;

		sectors = min(sectors, wp->sectors_free);

		bch2_key_resize(&e->k, sectors);

		bch2_open_bucket_get(c, wp, &open_buckets);
		bch2_alloc_sectors_append_ptrs(c, wp, &e->k_i, sectors, false);
		bch2_alloc_sectors_done(c, wp);

		extent_for_each_ptr(extent_i_to_s(e), ptr)
			ptr->unwritten = true;
	}

	have_reservation = true;

	ret = bch2_extent_update(trans, inum, iter, new.k, &disk_res,
				 0, i_sectors_delta, true);
out:
	if ((atomic_read(&cl.remaining) & CLOSURE_REMAINING_MASK) != 1) {
		bch2_trans_unlock(trans);
		closure_sync(&cl);
	}

	if (bch2_err_matches(ret, BCH_ERR_transaction_restart)) {
		bch2_trans_begin(trans);
		goto retry;
	}

	bch2_open_buckets_put(c, &open_buckets);
	bch2_disk_reservation_put(c, &disk_res);
	bch2_bkey_buf_exit(&new, c);
	bch2_bkey_buf_exit(&old, c);

	return ret;
}

/*
 * Returns -BCH_ERR_transacton_restart if we had to drop locks:
 */
int bch2_fpunch_at(struct btree_trans *trans, struct btree_iter *iter,
		   subvol_inum inum, u64 end,
		   s64 *i_sectors_delta)
{
	struct bch_fs *c	= trans->c;
	unsigned max_sectors	= KEY_SIZE_MAX & (~0 << c->block_bits);
	struct bpos end_pos = POS(inum.inum, end);
	struct bkey_s_c k;
	int ret = 0, ret2 = 0;
	u32 snapshot;

	while (!ret ||
	       bch2_err_matches(ret, BCH_ERR_transaction_restart)) {
		struct disk_reservation disk_res =
			bch2_disk_reservation_init(c, 0);
		struct bkey_i delete;

		if (ret)
			ret2 = ret;

		bch2_trans_begin(trans);

		ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
		if (ret)
			continue;

		bch2_btree_iter_set_snapshot(iter, snapshot);

		/*
		 * peek_upto() doesn't have ideal semantics for extents:
		 */
		k = bch2_btree_iter_peek_upto(iter, end_pos);
		if (!k.k)
			break;

		ret = bkey_err(k);
		if (ret)
			continue;

		bkey_init(&delete.k);
		delete.k.p = iter->pos;

		/* create the biggest key we can */
		bch2_key_resize(&delete.k, max_sectors);
		bch2_cut_back(end_pos, &delete);

		ret = bch2_extent_update(trans, inum, iter, &delete,
				&disk_res, 0, i_sectors_delta, false);
		bch2_disk_reservation_put(c, &disk_res);
	}

	return ret ?: ret2;
}

int bch2_fpunch(struct bch_fs *c, subvol_inum inum, u64 start, u64 end,
		s64 *i_sectors_delta)
{
	struct btree_trans trans;
	struct btree_iter iter;
	int ret;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 1024);
	bch2_trans_iter_init(&trans, &iter, BTREE_ID_extents,
			     POS(inum.inum, start),
			     BTREE_ITER_INTENT);

	ret = bch2_fpunch_at(&trans, &iter, inum, end, i_sectors_delta);

	bch2_trans_iter_exit(&trans, &iter);
	bch2_trans_exit(&trans);

	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		ret = 0;

	return ret;
}

static int bch2_write_index_default(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct bkey_buf sk;
	struct keylist *keys = &op->insert_keys;
	struct bkey_i *k = bch2_keylist_front(keys);
	struct btree_trans trans;
	struct btree_iter iter;
	subvol_inum inum = {
		.subvol = op->subvol,
		.inum	= k->k.p.inode,
	};
	int ret;

	BUG_ON(!inum.subvol);

	bch2_bkey_buf_init(&sk);
	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 1024);

	do {
		bch2_trans_begin(&trans);

		k = bch2_keylist_front(keys);
		bch2_bkey_buf_copy(&sk, c, k);

		ret = bch2_subvolume_get_snapshot(&trans, inum.subvol,
						  &sk.k->k.p.snapshot);
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			continue;
		if (ret)
			break;

		bch2_trans_iter_init(&trans, &iter, BTREE_ID_extents,
				     bkey_start_pos(&sk.k->k),
				     BTREE_ITER_SLOTS|BTREE_ITER_INTENT);

		ret = bch2_extent_update(&trans, inum, &iter, sk.k,
					 &op->res,
					 op->new_i_size, &op->i_sectors_delta,
					 op->flags & BCH_WRITE_CHECK_ENOSPC);
		bch2_trans_iter_exit(&trans, &iter);

		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			continue;
		if (ret)
			break;

		if (bkey_ge(iter.pos, k->k.p))
			bch2_keylist_pop_front(&op->insert_keys);
		else
			bch2_cut_front(iter.pos, k);
	} while (!bch2_keylist_empty(keys));

	bch2_trans_exit(&trans);
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

	bch2_disk_reservation_put(c, &op->res);
	bch2_write_ref_put(c, BCH_WRITE_REF_write);
	bch2_keylist_free(&op->insert_keys, op->inline_keys);

	bch2_time_stats_update(&c->times[BCH_TIME_data_write], op->start_time);

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
 * bch_write_index - after a write, update index to point to new data
 */
static void __bch2_write_index(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct keylist *keys = &op->insert_keys;
	struct bkey_i *k;
	unsigned dev;
	int ret = 0;

	if (unlikely(op->flags & BCH_WRITE_IO_ERROR)) {
		ret = bch2_write_drop_io_error_ptrs(op);
		if (ret)
			goto err;
	}

	/*
	 * probably not the ideal place to hook this in, but I don't
	 * particularly want to plumb io_opts all the way through the btree
	 * update stack right now
	 */
	for_each_keylist_key(keys, k)
		bch2_rebalance_add_key(c, bkey_i_to_s_c(k), &op->opts);

	if (!bch2_keylist_empty(keys)) {
		u64 sectors_start = keylist_sectors(keys);

		ret = !(op->flags & BCH_WRITE_MOVE)
			? bch2_write_index_default(op)
			: bch2_data_update_index_update(op);

		BUG_ON(bch2_err_matches(ret, BCH_ERR_transaction_restart));
		BUG_ON(keylist_sectors(keys) && !ret);

		op->written += sectors_start - keylist_sectors(keys);

		if (ret && !bch2_err_matches(ret, EROFS)) {
			struct bkey_i *k = bch2_keylist_front(&op->insert_keys);

			bch_err_inum_offset_ratelimited(c,
				k->k.p.inode, k->k.p.offset << 9,
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

static void bch2_write_index(struct closure *cl)
{
	struct bch_write_op *op = container_of(cl, struct bch_write_op, cl);
	struct write_point *wp = op->wp;
	struct workqueue_struct *wq = index_update_wq(op);

	barrier();

	/*
	 * We're not using wp->writes_lock here, so this is racey: that's ok,
	 * because this is just for diagnostic purposes, and we're running out
	 * of interrupt context here so if we were to take the log we'd have to
	 * switch to spin_lock_irq()/irqsave(), which is not free:
	 */
	if (wp->state == WRITE_POINT_waiting_io)
		__wp_update_state(wp, WRITE_POINT_waiting_work);

	op->btree_update_ready = true;
	queue_work(wq, &wp->index_update_work);
}

static inline void bch2_write_queue(struct bch_write_op *op, struct write_point *wp)
{
	op->btree_update_ready = false;
	op->wp = wp;

	spin_lock(&wp->writes_lock);
	list_add_tail(&op->wp_list, &wp->writes);
	if (wp->state == WRITE_POINT_stopped)
		__wp_update_state(wp, WRITE_POINT_waiting_io);
	spin_unlock(&wp->writes_lock);
}

void bch2_write_point_do_index_updates(struct work_struct *work)
{
	struct write_point *wp =
		container_of(work, struct write_point, index_update_work);
	struct bch_write_op *op;

	while (1) {
		spin_lock(&wp->writes_lock);
		list_for_each_entry(op, &wp->writes, wp_list)
			if (op->btree_update_ready) {
				list_del(&op->wp_list);
				goto unlock;
			}
		op = NULL;
unlock:
		wp_update_state(wp, op != NULL);
		spin_unlock(&wp->writes_lock);

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

	if (bch2_dev_inum_io_err_on(bio->bi_status, ca,
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
	if (bch2_crc_cmp(op->crc.csum, csum))
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
	    op->crc.compressed_size <= wp->sectors_free &&
	    (op->crc.compression_type == op->compression_type ||
	     op->incompressible)) {
		if (!crc_is_compressed(op->crc) &&
		    op->csum_type != op->crc.csum_type &&
		    bch2_write_rechecksum(c, op, op->csum_type))
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
		struct bch_extent_crc_unpacked crc = { 0 };
		struct bversion version = op->version;
		size_t dst_len, src_len;

		if (page_alloc_failed &&
		    dst->bi_iter.bi_size  < (wp->sectors_free << 9) &&
		    dst->bi_iter.bi_size < c->opts.encoded_extent_max)
			break;

		BUG_ON(op->compression_type &&
		       (op->flags & BCH_WRITE_DATA_ENCODED) &&
		       bch2_csum_type_is_encryption(op->crc.csum_type));
		BUG_ON(op->compression_type && !bounce);

		crc.compression_type = op->incompressible
			? BCH_COMPRESSION_TYPE_incompressible
			: op->compression_type
			? bch2_bio_compress(c, dst, &dst_len, src, &src_len,
					    op->compression_type)
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
				GFP_NOIO, &c->bio_write);
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
		if (p.crc.csum_type ||
		    crc_is_compressed(p.crc) ||
		    p.has_ec)
			return false;

		replicas += bch2_extent_ptr_durability(c, &p);
	}

	return replicas >= op->opts.data_replicas;
}

static inline void bch2_nocow_write_unlock(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	const struct bch_extent_ptr *ptr;
	struct bkey_i *k;

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
	struct bkey_i *new;
	struct bkey_ptrs ptrs;
	struct bch_extent_ptr *ptr;
	int ret;

	if (!bch2_extents_match(bkey_i_to_s_c(orig), k)) {
		/* trace this */
		return 0;
	}

	new = bch2_bkey_make_mut(trans, k);
	ret = PTR_ERR_OR_ZERO(new);
	if (ret)
		return ret;

	bch2_cut_front(bkey_start_pos(&orig->k), new);
	bch2_cut_back(orig->k.p, new);

	ptrs = bch2_bkey_ptrs(bkey_i_to_s(new));
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
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_i *orig;
	struct bkey_s_c k;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_keylist_key(&op->insert_keys, orig) {
		ret = for_each_btree_key_upto_commit(&trans, iter, BTREE_ID_extents,
				     bkey_start_pos(&orig->k), orig->k.p,
				     BTREE_ITER_INTENT, k,
				     NULL, NULL, BTREE_INSERT_NOFAIL, ({
			bch2_nocow_write_convert_one_unwritten(&trans, &iter, orig, k, op->new_i_size);
		}));

		if (ret && !bch2_err_matches(ret, EROFS)) {
			struct bkey_i *k = bch2_keylist_front(&op->insert_keys);

			bch_err_inum_offset_ratelimited(c,
				k->k.p.inode, k->k.p.offset << 9,
				"write error while doing btree update: %s",
				bch2_err_str(ret));
		}

		if (ret) {
			op->error = ret;
			break;
		}
	}

	bch2_trans_exit(&trans);
}

static void __bch2_nocow_write_done(struct bch_write_op *op)
{
	bch2_nocow_write_unlock(op);

	if (unlikely(op->flags & BCH_WRITE_IO_ERROR)) {
		op->error = -EIO;
	} else if (unlikely(op->flags & BCH_WRITE_CONVERT_UNWRITTEN))
		bch2_nocow_write_convert_unwritten(op);
}

static void bch2_nocow_write_done(struct closure *cl)
{
	struct bch_write_op *op = container_of(cl, struct bch_write_op, cl);

	__bch2_nocow_write_done(op);
	bch2_write_done(cl);
}

static void bch2_nocow_write(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_ptrs_c ptrs;
	const struct bch_extent_ptr *ptr, *ptr2;
	struct {
		struct bpos	b;
		unsigned	gen;
		struct nocow_lock_bucket *l;
	} buckets[BCH_REPLICAS_MAX];
	unsigned nr_buckets = 0;
	u32 snapshot;
	int ret, i;

	if (op->flags & BCH_WRITE_MOVE)
		return;

	bch2_trans_init(&trans, c, 0, 0);
retry:
	bch2_trans_begin(&trans);

	ret = bch2_subvolume_get_snapshot(&trans, op->subvol, &snapshot);
	if (unlikely(ret))
		goto err;

	bch2_trans_iter_init(&trans, &iter, BTREE_ID_extents,
			     SPOS(op->pos.inode, op->pos.offset, snapshot),
			     BTREE_ITER_SLOTS);
	while (1) {
		struct bio *bio = &op->wbio.bio;

		nr_buckets = 0;

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
		ptrs = bch2_bkey_ptrs_c(k);
		bkey_for_each_ptr(ptrs, ptr) {
			buckets[nr_buckets].b = PTR_BUCKET_POS(c, ptr);
			buckets[nr_buckets].gen = ptr->gen;
			buckets[nr_buckets].l =
				bucket_nocow_lock(&c->nocow_locks,
						  bucket_to_u64(buckets[nr_buckets].b));

			prefetch(buckets[nr_buckets].l);
			nr_buckets++;

			if (unlikely(!bch2_dev_get_ioref(bch_dev_bkey_exists(c, ptr->dev), WRITE)))
				goto err_get_ioref;

			if (ptr->unwritten)
				op->flags |= BCH_WRITE_CONVERT_UNWRITTEN;
		}

		/* Unlock before taking nocow locks, doing IO: */
		bkey_reassemble(op->insert_keys.top, k);
		bch2_trans_unlock(&trans);

		bch2_cut_front(op->pos, op->insert_keys.top);
		if (op->flags & BCH_WRITE_CONVERT_UNWRITTEN)
			bch2_cut_back(POS(op->pos.inode, op->pos.offset + bio_sectors(bio)), op->insert_keys.top);

		for (i = 0; i < nr_buckets; i++) {
			struct bch_dev *ca = bch_dev_bkey_exists(c, buckets[i].b.inode);
			struct nocow_lock_bucket *l = buckets[i].l;
			bool stale;

			__bch2_bucket_nocow_lock(&c->nocow_locks, l,
						 bucket_to_u64(buckets[i].b),
						 BUCKET_NOCOW_LOCK_UPDATE);

			rcu_read_lock();
			stale = gen_after(*bucket_gen(ca, buckets[i].b.offset), buckets[i].gen);
			rcu_read_unlock();

			if (unlikely(stale))
				goto err_bucket_stale;
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
	bch2_trans_iter_exit(&trans, &iter);
err:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto retry;

	if (ret) {
		bch_err_inum_offset_ratelimited(c,
				op->pos.inode,
				op->pos.offset << 9,
				"%s: btree lookup error %s",
				__func__, bch2_err_str(ret));
		op->error = ret;
		op->flags |= BCH_WRITE_DONE;
	}

	bch2_trans_exit(&trans);

	/* fallback to cow write path? */
	if (!(op->flags & BCH_WRITE_DONE)) {
		closure_sync(&op->cl);
		__bch2_nocow_write_done(op);
		op->insert_keys.top = op->insert_keys.keys;
	} else if (op->flags & BCH_WRITE_SYNC) {
		closure_sync(&op->cl);
		bch2_nocow_write_done(&op->cl);
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
	bkey_for_each_ptr(ptrs, ptr2) {
		if (ptr2 == ptr)
			break;

		percpu_ref_put(&bch_dev_bkey_exists(c, ptr2->dev)->io_ref);
	}

	/* Fall back to COW path: */
	goto out;
err_bucket_stale:
	while (--i >= 0)
		bch2_bucket_nocow_unlock(&c->nocow_locks,
					 buckets[i].b,
					 BUCKET_NOCOW_LOCK_UPDATE);

	bkey_for_each_ptr(ptrs, ptr2)
		percpu_ref_put(&bch_dev_bkey_exists(c, ptr2->dev)->io_ref);

	/* We can retry this: */
	ret = BCH_ERR_transaction_restart;
	goto out;
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
	op->btree_update_ready = false;

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
			bch2_alloc_sectors_start_trans(&trans,
				op->target,
				op->opts.erasure_code && !(op->flags & BCH_WRITE_CACHED),
				op->write_point,
				&op->devs_have,
				op->nr_replicas,
				op->nr_replicas_required,
				op->alloc_reserve,
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

	if (c->opts.nochanges ||
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

/* Cache promotion on read */

struct promote_op {
	struct rcu_head		rcu;
	u64			start_time;

	struct rhash_head	hash;
	struct bpos		pos;

	struct data_update	write;
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
	if (!(flags & BCH_READ_MAY_PROMOTE))
		return false;

	if (!opts.promote_target)
		return false;

	if (bch2_bkey_has_target(c, k, opts.promote_target))
		return false;

	if (bkey_extent_is_unwritten(k))
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
					  struct bch_read_bio **rbio)
{
	struct bch_fs *c = trans->c;
	struct promote_op *op = NULL;
	struct bio *bio;
	unsigned pages = DIV_ROUND_UP(sectors, PAGE_SECTORS);
	int ret;

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_promote))
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

	ret = bch2_data_update_init(trans, NULL, &op->write,
			writepoint_hashed((unsigned long) current),
			opts,
			(struct data_update_opts) {
				.target		= opts.promote_target,
				.extra_replicas	= 1,
				.write_flags	= BCH_WRITE_ALLOC_NOWAIT|BCH_WRITE_CACHED,
			},
			btree_id, k);
	if (ret == -BCH_ERR_nocow_lock_blocked) {
		ret = rhashtable_remove_fast(&c->promote_table, &op->hash,
					bch_promote_params);
		BUG_ON(ret);
		goto err;
	}

	BUG_ON(ret);
	op->write.op.end_io = promote_done;

	return op;
err:
	if (*rbio)
		bio_free_pages(&(*rbio)->bio);
	kfree(*rbio);
	*rbio = NULL;
	kfree(op);
	bch2_write_ref_put(c, BCH_WRITE_REF_promote);
	return NULL;
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
					bool *read_full)
{
	struct bch_fs *c = trans->c;
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

	promote = __promote_alloc(trans,
				  k.k->type == KEY_TYPE_reflink_v
				  ? BTREE_ID_reflink
				  : BTREE_ID_extents,
				  k, pos, pick, opts, sectors, rbio);
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
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_buf sk;
	struct bkey_s_c k;
	int ret;

	flags &= ~BCH_READ_LAST_FRAGMENT;
	flags |= BCH_READ_MUST_CLONE;

	bch2_bkey_buf_init(&sk);
	bch2_trans_init(&trans, c, 0, 0);

	bch2_trans_iter_init(&trans, &iter, rbio->data_btree,
			     rbio->read_pos, BTREE_ITER_SLOTS);
retry:
	rbio->bio.bi_status = 0;

	k = bch2_btree_iter_peek_slot(&iter);
	if (bkey_err(k))
		goto err;

	bch2_bkey_buf_reassemble(&sk, c, k);
	k = bkey_i_to_s_c(sk.k);
	bch2_trans_unlock(&trans);

	if (!bch2_bkey_matches_ptr(c, k,
				   rbio->pick.ptr,
				   rbio->data_pos.offset -
				   rbio->pick.crc.offset)) {
		/* extent we wanted to read no longer exists: */
		rbio->hole = true;
		goto out;
	}

	ret = __bch2_read_extent(&trans, rbio, bvec_iter,
				 rbio->read_pos,
				 rbio->data_btree,
				 k, 0, failed, flags);
	if (ret == READ_RETRY)
		goto retry;
	if (ret)
		goto err;
out:
	bch2_rbio_done(rbio);
	bch2_trans_iter_exit(&trans, &iter);
	bch2_trans_exit(&trans);
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

	bch2_trans_iter_init(trans, &iter, rbio->data_btree, rbio->data_pos,
			     BTREE_ITER_SLOTS|BTREE_ITER_INTENT);
	k = bch2_btree_iter_peek_slot(&iter);
	if ((ret = bkey_err(k)))
		goto out;

	if (bversion_cmp(k.k->version, rbio->version) ||
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
				BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE);
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static noinline void bch2_rbio_narrow_crcs(struct bch_read_bio *rbio)
{
	bch2_trans_do(rbio->c, NULL, NULL, BTREE_INSERT_NOFAIL,
		      __bch2_rbio_narrow_crcs(&trans, rbio));
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

		if (bch2_bio_uncompress(c, src, dst, dst_iter, crc))
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

	bch_err_inum_offset_ratelimited(ca,
		rbio->read_pos.inode,
		rbio->read_pos.offset << 9,
		"data checksum error: expected %0llx:%0llx got %0llx:%0llx (type %s)",
		rbio->pick.crc.csum.hi, rbio->pick.crc.csum.lo,
		csum.hi, csum.lo, bch2_csum_types[crc.csum_type]);
	bch2_io_error(ca);
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
	struct bch_dev *ca	= bch_dev_bkey_exists(c, rbio->pick.ptr.dev);
	struct workqueue_struct *wq = NULL;
	enum rbio_context context = RBIO_CONTEXT_NULL;

	if (rbio->have_ioref) {
		bch2_latency_acct(ca, rbio->submit_time, READ);
		percpu_ref_put(&ca->io_ref);
	}

	if (!rbio->split)
		rbio->bio.bi_end_io = rbio->end_io;

	if (bch2_dev_inum_io_err_on(bio->bi_status, ca,
				    rbio->read_pos.inode,
				    rbio->read_pos.offset,
				    "data read error: %s",
			       bch2_blk_status_to_str(bio->bi_status))) {
		bch2_rbio_error(rbio, READ_RETRY_AVOID, bio->bi_status);
		return;
	}

	if (((rbio->flags & BCH_READ_RETRY_IF_STALE) && race_fault()) ||
	    ptr_stale(ca, &rbio->pick.ptr)) {
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

	bch2_trans_iter_init(trans, &iter, BTREE_ID_reflink,
			     POS(0, reflink_offset),
			     BTREE_ITER_SLOTS);
	k = bch2_btree_iter_peek_slot(&iter);
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
		ret = -EIO;
		goto err;
	}

	*offset_into_extent = iter.pos.offset - bkey_start_offset(k.k);
	bch2_bkey_buf_reassemble(orig_k, trans->c, k);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static noinline void read_from_stale_dirty_pointer(struct btree_trans *trans,
						   struct bkey_s_c k,
						   struct bch_extent_ptr ptr)
{
	struct bch_fs *c = trans->c;
	struct bch_dev *ca = bch_dev_bkey_exists(c, ptr.dev);
	struct btree_iter iter;
	struct printbuf buf = PRINTBUF;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_alloc,
			     PTR_BUCKET_POS(c, &ptr),
			     BTREE_ITER_CACHED);

	prt_printf(&buf, "Attempting to read from stale dirty pointer:");
	printbuf_indent_add(&buf, 2);
	prt_newline(&buf);

	bch2_bkey_val_to_text(&buf, c, k);
	prt_newline(&buf);

	prt_printf(&buf, "memory gen: %u", *bucket_gen(ca, iter.pos.offset));

	ret = lockrestart_do(trans, bkey_err(k = bch2_btree_iter_peek_slot(&iter)));
	if (!ret) {
		prt_newline(&buf);
		bch2_bkey_val_to_text(&buf, c, k);
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
	struct bch_dev *ca = NULL;
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
		bch_err_inum_offset_ratelimited(c,
				read_pos.inode, read_pos.offset << 9,
				"no device to read from");
		goto err;
	}

	ca = bch_dev_bkey_exists(c, pick.ptr.dev);

	/*
	 * Stale dirty pointers are treated as IO errors, but @failed isn't
	 * allocated unless we're in the retry path - so if we're not in the
	 * retry path, don't check here, it'll be caught in bch2_read_endio()
	 * and we'll end up in the retry path:
	 */
	if ((flags & BCH_READ_IN_RETRY) &&
	    !pick.ptr.cached &&
	    unlikely(ptr_stale(ca, &pick.ptr))) {
		read_from_stale_dirty_pointer(trans, k, pick.ptr);
		bch2_mark_io_failure(failed, &pick);
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
		if (pick.crc.compressed_size > orig->bio.bi_vcnt * PAGE_SECTORS)
			goto hole;

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

	if (orig->opts.promote_target)
		promote = promote_alloc(trans, iter, k, &pick, orig->opts, flags,
					&rbio, &bounce, &read_full);

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
		offset_into_extent		= 0;
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
	rbio->have_ioref	= pick_ret > 0 && bch2_dev_get_ioref(ca, READ);
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
	rbio->version		= k.k->version;
	rbio->promote		= promote;
	INIT_WORK(&rbio->work, NULL);

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
	if (pick.ptr.cached && !(flags & BCH_READ_NODECODE))
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
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_buf sk;
	struct bkey_s_c k;
	u32 snapshot;
	int ret;

	BUG_ON(flags & BCH_READ_NODECODE);

	bch2_bkey_buf_init(&sk);
	bch2_trans_init(&trans, c, 0, 0);
retry:
	bch2_trans_begin(&trans);
	iter = (struct btree_iter) { NULL };

	ret = bch2_subvolume_get_snapshot(&trans, inum.subvol, &snapshot);
	if (ret)
		goto err;

	bch2_trans_iter_init(&trans, &iter, BTREE_ID_extents,
			     SPOS(inum.inum, bvec_iter.bi_sector, snapshot),
			     BTREE_ITER_SLOTS);
	while (1) {
		unsigned bytes, sectors, offset_into_extent;
		enum btree_id data_btree = BTREE_ID_extents;

		/*
		 * read_extent -> io_time_reset may cause a transaction restart
		 * without returning an error, we need to check for that here:
		 */
		ret = bch2_trans_relock(&trans);
		if (ret)
			break;

		bch2_btree_iter_set_pos(&iter,
				POS(inum.inum, bvec_iter.bi_sector));

		k = bch2_btree_iter_peek_slot(&iter);
		ret = bkey_err(k);
		if (ret)
			break;

		offset_into_extent = iter.pos.offset -
			bkey_start_offset(k.k);
		sectors = k.k->size - offset_into_extent;

		bch2_bkey_buf_reassemble(&sk, c, k);

		ret = bch2_read_indirect_extent(&trans, &data_btree,
					&offset_into_extent, &sk);
		if (ret)
			break;

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

		ret = __bch2_read_extent(&trans, rbio, bvec_iter, iter.pos,
					 data_btree, k,
					 offset_into_extent, failed, flags);
		if (ret)
			break;

		if (flags & BCH_READ_LAST_FRAGMENT)
			break;

		swap(bvec_iter.bi_size, bytes);
		bio_advance_iter(&rbio->bio, &bvec_iter, bytes);

		ret = btree_trans_too_many_iters(&trans);
		if (ret)
			break;
	}
err:
	bch2_trans_iter_exit(&trans, &iter);

	if (bch2_err_matches(ret, BCH_ERR_transaction_restart) ||
	    ret == READ_RETRY ||
	    ret == READ_RETRY_AVOID)
		goto retry;

	bch2_trans_exit(&trans);
	bch2_bkey_buf_exit(&sk, c);

	if (ret) {
		bch_err_inum_offset_ratelimited(c, inum.inum,
						bvec_iter.bi_sector << 9,
						"read error %i from btree lookup", ret);
		rbio->bio.bi_status = BLK_STS_IOERR;
		bch2_rbio_done(rbio);
	}
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
					 c->opts.encoded_extent_max) /
				   PAGE_SIZE, 0) ||
	    rhashtable_init(&c->promote_table, &bch_promote_params))
		return -ENOMEM;

	return 0;
}
