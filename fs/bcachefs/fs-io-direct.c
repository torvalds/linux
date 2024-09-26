// SPDX-License-Identifier: GPL-2.0
#ifndef NO_BCACHEFS_FS

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "fs.h"
#include "fs-io.h"
#include "fs-io-direct.h"
#include "fs-io-pagecache.h"
#include "io_read.h"
#include "io_write.h"

#include <linux/kthread.h>
#include <linux/pagemap.h>
#include <linux/prefetch.h>
#include <linux/task_io_accounting_ops.h>

/* O_DIRECT reads */

struct dio_read {
	struct closure			cl;
	struct kiocb			*req;
	long				ret;
	bool				should_dirty;
	struct bch_read_bio		rbio;
};

static void bio_check_or_release(struct bio *bio, bool check_dirty)
{
	if (check_dirty) {
		bio_check_pages_dirty(bio);
	} else {
		bio_release_pages(bio, false);
		bio_put(bio);
	}
}

static CLOSURE_CALLBACK(bch2_dio_read_complete)
{
	closure_type(dio, struct dio_read, cl);

	dio->req->ki_complete(dio->req, dio->ret);
	bio_check_or_release(&dio->rbio.bio, dio->should_dirty);
}

static void bch2_direct_IO_read_endio(struct bio *bio)
{
	struct dio_read *dio = bio->bi_private;

	if (bio->bi_status)
		dio->ret = blk_status_to_errno(bio->bi_status);

	closure_put(&dio->cl);
}

static void bch2_direct_IO_read_split_endio(struct bio *bio)
{
	struct dio_read *dio = bio->bi_private;
	bool should_dirty = dio->should_dirty;

	bch2_direct_IO_read_endio(bio);
	bio_check_or_release(bio, should_dirty);
}

static int bch2_direct_IO_read(struct kiocb *req, struct iov_iter *iter)
{
	struct file *file = req->ki_filp;
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct bch_io_opts opts;
	struct dio_read *dio;
	struct bio *bio;
	loff_t offset = req->ki_pos;
	bool sync = is_sync_kiocb(req);
	size_t shorten;
	ssize_t ret;

	bch2_inode_opts_get(&opts, c, &inode->ei_inode);

	/* bios must be 512 byte aligned: */
	if ((offset|iter->count) & (SECTOR_SIZE - 1))
		return -EINVAL;

	ret = min_t(loff_t, iter->count,
		    max_t(loff_t, 0, i_size_read(&inode->v) - offset));

	if (!ret)
		return ret;

	shorten = iov_iter_count(iter) - round_up(ret, block_bytes(c));
	if (shorten >= iter->count)
		shorten = 0;
	iter->count -= shorten;

	bio = bio_alloc_bioset(NULL,
			       bio_iov_vecs_to_alloc(iter, BIO_MAX_VECS),
			       REQ_OP_READ,
			       GFP_KERNEL,
			       &c->dio_read_bioset);

	bio->bi_end_io = bch2_direct_IO_read_endio;

	dio = container_of(bio, struct dio_read, rbio.bio);
	closure_init(&dio->cl, NULL);

	/*
	 * this is a _really_ horrible hack just to avoid an atomic sub at the
	 * end:
	 */
	if (!sync) {
		set_closure_fn(&dio->cl, bch2_dio_read_complete, NULL);
		atomic_set(&dio->cl.remaining,
			   CLOSURE_REMAINING_INITIALIZER -
			   CLOSURE_RUNNING +
			   CLOSURE_DESTRUCTOR);
	} else {
		atomic_set(&dio->cl.remaining,
			   CLOSURE_REMAINING_INITIALIZER + 1);
		dio->cl.closure_get_happened = true;
	}

	dio->req	= req;
	dio->ret	= ret;
	/*
	 * This is one of the sketchier things I've encountered: we have to skip
	 * the dirtying of requests that are internal from the kernel (i.e. from
	 * loopback), because we'll deadlock on page_lock.
	 */
	dio->should_dirty = iter_is_iovec(iter);

	goto start;
	while (iter->count) {
		bio = bio_alloc_bioset(NULL,
				       bio_iov_vecs_to_alloc(iter, BIO_MAX_VECS),
				       REQ_OP_READ,
				       GFP_KERNEL,
				       &c->bio_read);
		bio->bi_end_io		= bch2_direct_IO_read_split_endio;
start:
		bio->bi_opf		= REQ_OP_READ|REQ_SYNC;
		bio->bi_iter.bi_sector	= offset >> 9;
		bio->bi_private		= dio;

		ret = bio_iov_iter_get_pages(bio, iter);
		if (ret < 0) {
			/* XXX: fault inject this path */
			bio->bi_status = BLK_STS_RESOURCE;
			bio_endio(bio);
			break;
		}

		offset += bio->bi_iter.bi_size;

		if (dio->should_dirty)
			bio_set_pages_dirty(bio);

		if (iter->count)
			closure_get(&dio->cl);

		bch2_read(c, rbio_init(bio, opts), inode_inum(inode));
	}

	iter->count += shorten;

	if (sync) {
		closure_sync(&dio->cl);
		closure_debug_destroy(&dio->cl);
		ret = dio->ret;
		bio_check_or_release(&dio->rbio.bio, dio->should_dirty);
		return ret;
	} else {
		return -EIOCBQUEUED;
	}
}

ssize_t bch2_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct bch_inode_info *inode = file_bch_inode(file);
	struct address_space *mapping = file->f_mapping;
	size_t count = iov_iter_count(iter);
	ssize_t ret = 0;

	if (!count)
		return 0; /* skip atime */

	if (iocb->ki_flags & IOCB_DIRECT) {
		struct blk_plug plug;

		if (unlikely(mapping->nrpages)) {
			ret = filemap_write_and_wait_range(mapping,
						iocb->ki_pos,
						iocb->ki_pos + count - 1);
			if (ret < 0)
				goto out;
		}

		file_accessed(file);

		blk_start_plug(&plug);
		ret = bch2_direct_IO_read(iocb, iter);
		blk_finish_plug(&plug);

		if (ret >= 0)
			iocb->ki_pos += ret;
	} else {
		bch2_pagecache_add_get(inode);
		ret = filemap_read(iocb, iter, ret);
		bch2_pagecache_add_put(inode);
	}
out:
	return bch2_err_class(ret);
}

/* O_DIRECT writes */

struct dio_write {
	struct kiocb			*req;
	struct address_space		*mapping;
	struct bch_inode_info		*inode;
	struct mm_struct		*mm;
	const struct iovec		*iov;
	unsigned			loop:1,
					extending:1,
					sync:1,
					flush:1;
	struct quota_res		quota_res;
	u64				written;

	struct iov_iter			iter;
	struct iovec			inline_vecs[2];

	/* must be last: */
	struct bch_write_op		op;
};

static bool bch2_check_range_allocated(struct bch_fs *c, subvol_inum inum,
				       u64 offset, u64 size,
				       unsigned nr_replicas, bool compressed)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	struct bkey_s_c k;
	u64 end = offset + size;
	u32 snapshot;
	bool ret = true;
	int err;
retry:
	bch2_trans_begin(trans);

	err = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
	if (err)
		goto err;

	for_each_btree_key_norestart(trans, iter, BTREE_ID_extents,
			   SPOS(inum.inum, offset, snapshot),
			   BTREE_ITER_slots, k, err) {
		if (bkey_ge(bkey_start_pos(k.k), POS(inum.inum, end)))
			break;

		if (k.k->p.snapshot != snapshot ||
		    nr_replicas > bch2_bkey_replicas(c, k) ||
		    (!compressed && bch2_bkey_sectors_compressed(k))) {
			ret = false;
			break;
		}
	}

	offset = iter.pos.offset;
	bch2_trans_iter_exit(trans, &iter);
err:
	if (bch2_err_matches(err, BCH_ERR_transaction_restart))
		goto retry;
	bch2_trans_put(trans);

	return err ? false : ret;
}

static noinline bool bch2_dio_write_check_allocated(struct dio_write *dio)
{
	struct bch_fs *c = dio->op.c;
	struct bch_inode_info *inode = dio->inode;
	struct bio *bio = &dio->op.wbio.bio;

	return bch2_check_range_allocated(c, inode_inum(inode),
				dio->op.pos.offset, bio_sectors(bio),
				dio->op.opts.data_replicas,
				dio->op.opts.compression != 0);
}

static void bch2_dio_write_loop_async(struct bch_write_op *);
static __always_inline long bch2_dio_write_done(struct dio_write *dio);

/*
 * We're going to return -EIOCBQUEUED, but we haven't finished consuming the
 * iov_iter yet, so we need to stash a copy of the iovec: it might be on the
 * caller's stack, we're not guaranteed that it will live for the duration of
 * the IO:
 */
static noinline int bch2_dio_write_copy_iov(struct dio_write *dio)
{
	struct iovec *iov = dio->inline_vecs;

	/*
	 * iov_iter has a single embedded iovec - nothing to do:
	 */
	if (iter_is_ubuf(&dio->iter))
		return 0;

	/*
	 * We don't currently handle non-iovec iov_iters here - return an error,
	 * and we'll fall back to doing the IO synchronously:
	 */
	if (!iter_is_iovec(&dio->iter))
		return -1;

	if (dio->iter.nr_segs > ARRAY_SIZE(dio->inline_vecs)) {
		dio->iov = iov = kmalloc_array(dio->iter.nr_segs, sizeof(*iov),
				    GFP_KERNEL);
		if (unlikely(!iov))
			return -ENOMEM;
	}

	memcpy(iov, dio->iter.__iov, dio->iter.nr_segs * sizeof(*iov));
	dio->iter.__iov = iov;
	return 0;
}

static CLOSURE_CALLBACK(bch2_dio_write_flush_done)
{
	closure_type(dio, struct dio_write, op.cl);
	struct bch_fs *c = dio->op.c;

	closure_debug_destroy(cl);

	dio->op.error = bch2_journal_error(&c->journal);

	bch2_dio_write_done(dio);
}

static noinline void bch2_dio_write_flush(struct dio_write *dio)
{
	struct bch_fs *c = dio->op.c;
	struct bch_inode_unpacked inode;
	int ret;

	dio->flush = 0;

	closure_init(&dio->op.cl, NULL);

	if (!dio->op.error) {
		ret = bch2_inode_find_by_inum(c, inode_inum(dio->inode), &inode);
		if (ret) {
			dio->op.error = ret;
		} else {
			bch2_journal_flush_seq_async(&c->journal, inode.bi_journal_seq,
						     &dio->op.cl);
			bch2_inode_flush_nocow_writes_async(c, dio->inode, &dio->op.cl);
		}
	}

	if (dio->sync) {
		closure_sync(&dio->op.cl);
		closure_debug_destroy(&dio->op.cl);
	} else {
		continue_at(&dio->op.cl, bch2_dio_write_flush_done, NULL);
	}
}

static __always_inline long bch2_dio_write_done(struct dio_write *dio)
{
	struct kiocb *req = dio->req;
	struct bch_inode_info *inode = dio->inode;
	bool sync = dio->sync;
	long ret;

	if (unlikely(dio->flush)) {
		bch2_dio_write_flush(dio);
		if (!sync)
			return -EIOCBQUEUED;
	}

	bch2_pagecache_block_put(inode);

	kfree(dio->iov);

	ret = dio->op.error ?: ((long) dio->written << 9);
	bio_put(&dio->op.wbio.bio);

	bch2_write_ref_put(dio->op.c, BCH_WRITE_REF_dio_write);

	/* inode->i_dio_count is our ref on inode and thus bch_fs */
	inode_dio_end(&inode->v);

	if (ret < 0)
		ret = bch2_err_class(ret);

	if (!sync) {
		req->ki_complete(req, ret);
		ret = -EIOCBQUEUED;
	}
	return ret;
}

static __always_inline void bch2_dio_write_end(struct dio_write *dio)
{
	struct bch_fs *c = dio->op.c;
	struct kiocb *req = dio->req;
	struct bch_inode_info *inode = dio->inode;
	struct bio *bio = &dio->op.wbio.bio;

	req->ki_pos	+= (u64) dio->op.written << 9;
	dio->written	+= dio->op.written;

	if (dio->extending) {
		spin_lock(&inode->v.i_lock);
		if (req->ki_pos > inode->v.i_size)
			i_size_write(&inode->v, req->ki_pos);
		spin_unlock(&inode->v.i_lock);
	}

	if (dio->op.i_sectors_delta || dio->quota_res.sectors) {
		mutex_lock(&inode->ei_quota_lock);
		__bch2_i_sectors_acct(c, inode, &dio->quota_res, dio->op.i_sectors_delta);
		__bch2_quota_reservation_put(c, inode, &dio->quota_res);
		mutex_unlock(&inode->ei_quota_lock);
	}

	bio_release_pages(bio, false);

	if (unlikely(dio->op.error))
		set_bit(EI_INODE_ERROR, &inode->ei_flags);
}

static __always_inline long bch2_dio_write_loop(struct dio_write *dio)
{
	struct bch_fs *c = dio->op.c;
	struct kiocb *req = dio->req;
	struct address_space *mapping = dio->mapping;
	struct bch_inode_info *inode = dio->inode;
	struct bch_io_opts opts;
	struct bio *bio = &dio->op.wbio.bio;
	unsigned unaligned, iter_count;
	bool sync = dio->sync, dropped_locks;
	long ret;

	bch2_inode_opts_get(&opts, c, &inode->ei_inode);

	while (1) {
		iter_count = dio->iter.count;

		EBUG_ON(current->faults_disabled_mapping);
		current->faults_disabled_mapping = mapping;

		ret = bio_iov_iter_get_pages(bio, &dio->iter);

		dropped_locks = fdm_dropped_locks();

		current->faults_disabled_mapping = NULL;

		/*
		 * If the fault handler returned an error but also signalled
		 * that it dropped & retook ei_pagecache_lock, we just need to
		 * re-shoot down the page cache and retry:
		 */
		if (dropped_locks && ret)
			ret = 0;

		if (unlikely(ret < 0))
			goto err;

		if (unlikely(dropped_locks)) {
			ret = bch2_write_invalidate_inode_pages_range(mapping,
					req->ki_pos,
					req->ki_pos + iter_count - 1);
			if (unlikely(ret))
				goto err;

			if (!bio->bi_iter.bi_size)
				continue;
		}

		unaligned = bio->bi_iter.bi_size & (block_bytes(c) - 1);
		bio->bi_iter.bi_size -= unaligned;
		iov_iter_revert(&dio->iter, unaligned);

		if (!bio->bi_iter.bi_size) {
			/*
			 * bio_iov_iter_get_pages was only able to get <
			 * blocksize worth of pages:
			 */
			ret = -EFAULT;
			goto err;
		}

		bch2_write_op_init(&dio->op, c, opts);
		dio->op.end_io		= sync
			? NULL
			: bch2_dio_write_loop_async;
		dio->op.target		= dio->op.opts.foreground_target;
		dio->op.write_point	= writepoint_hashed((unsigned long) current);
		dio->op.nr_replicas	= dio->op.opts.data_replicas;
		dio->op.subvol		= inode->ei_inum.subvol;
		dio->op.pos		= POS(inode->v.i_ino, (u64) req->ki_pos >> 9);
		dio->op.devs_need_flush	= &inode->ei_devs_need_flush;

		if (sync)
			dio->op.flags |= BCH_WRITE_SYNC;
		dio->op.flags |= BCH_WRITE_CHECK_ENOSPC;

		ret = bch2_quota_reservation_add(c, inode, &dio->quota_res,
						 bio_sectors(bio), true);
		if (unlikely(ret))
			goto err;

		ret = bch2_disk_reservation_get(c, &dio->op.res, bio_sectors(bio),
						dio->op.opts.data_replicas, 0);
		if (unlikely(ret) &&
		    !bch2_dio_write_check_allocated(dio))
			goto err;

		task_io_account_write(bio->bi_iter.bi_size);

		if (unlikely(dio->iter.count) &&
		    !dio->sync &&
		    !dio->loop &&
		    bch2_dio_write_copy_iov(dio))
			dio->sync = sync = true;

		dio->loop = true;
		closure_call(&dio->op.cl, bch2_write, NULL, NULL);

		if (!sync)
			return -EIOCBQUEUED;

		bch2_dio_write_end(dio);

		if (likely(!dio->iter.count) || dio->op.error)
			break;

		bio_reset(bio, NULL, REQ_OP_WRITE | REQ_SYNC | REQ_IDLE);
	}
out:
	return bch2_dio_write_done(dio);
err:
	dio->op.error = ret;

	bio_release_pages(bio, false);

	bch2_quota_reservation_put(c, inode, &dio->quota_res);
	goto out;
}

static noinline __cold void bch2_dio_write_continue(struct dio_write *dio)
{
	struct mm_struct *mm = dio->mm;

	bio_reset(&dio->op.wbio.bio, NULL, REQ_OP_WRITE);

	if (mm)
		kthread_use_mm(mm);
	bch2_dio_write_loop(dio);
	if (mm)
		kthread_unuse_mm(mm);
}

static void bch2_dio_write_loop_async(struct bch_write_op *op)
{
	struct dio_write *dio = container_of(op, struct dio_write, op);

	bch2_dio_write_end(dio);

	if (likely(!dio->iter.count) || dio->op.error)
		bch2_dio_write_done(dio);
	else
		bch2_dio_write_continue(dio);
}

ssize_t bch2_direct_write(struct kiocb *req, struct iov_iter *iter)
{
	struct file *file = req->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct bch_inode_info *inode = file_bch_inode(file);
	struct bch_fs *c = inode->v.i_sb->s_fs_info;
	struct dio_write *dio;
	struct bio *bio;
	bool locked = true, extending;
	ssize_t ret;

	prefetch(&c->opts);
	prefetch((void *) &c->opts + 64);
	prefetch(&inode->ei_inode);
	prefetch((void *) &inode->ei_inode + 64);

	if (!bch2_write_ref_tryget(c, BCH_WRITE_REF_dio_write))
		return -EROFS;

	inode_lock(&inode->v);

	ret = generic_write_checks(req, iter);
	if (unlikely(ret <= 0))
		goto err_put_write_ref;

	ret = file_remove_privs(file);
	if (unlikely(ret))
		goto err_put_write_ref;

	ret = file_update_time(file);
	if (unlikely(ret))
		goto err_put_write_ref;

	if (unlikely((req->ki_pos|iter->count) & (block_bytes(c) - 1))) {
		ret = -EINVAL;
		goto err_put_write_ref;
	}

	inode_dio_begin(&inode->v);
	bch2_pagecache_block_get(inode);

	extending = req->ki_pos + iter->count > inode->v.i_size;
	if (!extending) {
		inode_unlock(&inode->v);
		locked = false;
	}

	bio = bio_alloc_bioset(NULL,
			       bio_iov_vecs_to_alloc(iter, BIO_MAX_VECS),
			       REQ_OP_WRITE | REQ_SYNC | REQ_IDLE,
			       GFP_KERNEL,
			       &c->dio_write_bioset);
	dio = container_of(bio, struct dio_write, op.wbio.bio);
	dio->req		= req;
	dio->mapping		= mapping;
	dio->inode		= inode;
	dio->mm			= current->mm;
	dio->iov		= NULL;
	dio->loop		= false;
	dio->extending		= extending;
	dio->sync		= is_sync_kiocb(req) || extending;
	dio->flush		= iocb_is_dsync(req) && !c->opts.journal_flush_disabled;
	dio->quota_res.sectors	= 0;
	dio->written		= 0;
	dio->iter		= *iter;
	dio->op.c		= c;

	if (unlikely(mapping->nrpages)) {
		ret = bch2_write_invalidate_inode_pages_range(mapping,
						req->ki_pos,
						req->ki_pos + iter->count - 1);
		if (unlikely(ret))
			goto err_put_bio;
	}

	ret = bch2_dio_write_loop(dio);
out:
	if (locked)
		inode_unlock(&inode->v);
	return ret;
err_put_bio:
	bch2_pagecache_block_put(inode);
	bio_put(bio);
	inode_dio_end(&inode->v);
err_put_write_ref:
	bch2_write_ref_put(c, BCH_WRITE_REF_dio_write);
	goto out;
}

void bch2_fs_fs_io_direct_exit(struct bch_fs *c)
{
	bioset_exit(&c->dio_write_bioset);
	bioset_exit(&c->dio_read_bioset);
}

int bch2_fs_fs_io_direct_init(struct bch_fs *c)
{
	if (bioset_init(&c->dio_read_bioset,
			4, offsetof(struct dio_read, rbio.bio),
			BIOSET_NEED_BVECS))
		return -BCH_ERR_ENOMEM_dio_read_bioset_init;

	if (bioset_init(&c->dio_write_bioset,
			4, offsetof(struct dio_write, op.wbio.bio),
			BIOSET_NEED_BVECS))
		return -BCH_ERR_ENOMEM_dio_write_bioset_init;

	return 0;
}

#endif /* NO_BCACHEFS_FS */
