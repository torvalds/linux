// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Alibaba Cloud
 * Copyright 2026 Docker, Inc.
 */
#include "internal.h"
#include <trace/events/erofs.h>

struct erofs_fileio_rq {
	struct bio_vec bvecs[16];
	struct bio bio;
	struct kiocb iocb;
	struct super_block *sb;
	refcount_t ref;
};

struct erofs_fileio {
	struct erofs_map_blocks map;
	struct erofs_map_dev dev;
	struct erofs_fileio_rq *rq;
};

static void erofs_fileio_ki_complete(struct kiocb *iocb, long ret)
{
	struct erofs_fileio_rq *rq =
			container_of(iocb, struct erofs_fileio_rq, iocb);
	struct folio_iter fi;

	if (ret >= 0 && ret != rq->bio.bi_iter.bi_size)
		ret = -EIO;
	if (!rq->bio.bi_end_io) {
		bio_for_each_folio_all(fi, &rq->bio) {
			DBG_BUGON(folio_test_uptodate(fi.folio));
			iomap_finish_folio_read(fi.folio, fi.offset, fi.length,
						ret < 0 ? ret : 0);
		}
	} else if (ret < 0 && !rq->bio.bi_status) {
		rq->bio.bi_status = errno_to_blk_status(ret);
	}
	bio_endio(&rq->bio);
	bio_uninit(&rq->bio);
	if (refcount_dec_and_test(&rq->ref))
		kfree(rq);
}

static void erofs_fileio_rq_submit(struct erofs_fileio_rq *rq)
{
	struct iov_iter iter;
	ssize_t ret;

	if (!rq)
		return;
	rq->iocb.ki_pos = rq->bio.bi_iter.bi_sector << SECTOR_SHIFT;
	rq->iocb.ki_ioprio = get_current_ioprio();
	rq->iocb.ki_complete = erofs_fileio_ki_complete;
	if (test_opt(&EROFS_SB(rq->sb)->opt, DIRECT_IO) &&
	    rq->iocb.ki_filp->f_mode & FMODE_CAN_ODIRECT)
		rq->iocb.ki_flags = IOCB_DIRECT;
	iov_iter_bvec(&iter, ITER_DEST, rq->bvecs, rq->bio.bi_vcnt,
		      rq->bio.bi_iter.bi_size);
	scoped_with_creds(rq->iocb.ki_filp->f_cred)
		ret = vfs_iocb_iter_read(rq->iocb.ki_filp, &rq->iocb, &iter);
	if (ret != -EIOCBQUEUED)
		erofs_fileio_ki_complete(&rq->iocb, ret);
	if (refcount_dec_and_test(&rq->ref))
		kfree(rq);
}

static struct erofs_fileio_rq *erofs_fileio_rq_alloc(struct super_block *sb,
						     struct file *file)
{
	struct erofs_fileio_rq *rq = kzalloc_obj(*rq, GFP_KERNEL | __GFP_NOFAIL);

	bio_init(&rq->bio, NULL, rq->bvecs, ARRAY_SIZE(rq->bvecs), REQ_OP_READ);
	rq->iocb.ki_filp = file;
	rq->sb = sb;
	refcount_set(&rq->ref, 2);
	return rq;
}

struct bio *erofs_fileio_bio_alloc(struct erofs_map_dev *mdev)
{
	return &erofs_fileio_rq_alloc(mdev->m_sb, mdev->m_dif->file)->bio;
}

void erofs_fileio_submit_bio(struct bio *bio)
{
	erofs_fileio_rq_submit(container_of(bio, struct erofs_fileio_rq, bio));
}

static void erofs_fileio_iomap_submit_read(const struct iomap_iter *iter,
		struct iomap_read_folio_ctx *ctx)
{
	erofs_fileio_rq_submit((struct erofs_fileio_rq *)ctx->read_ctx);
	ctx->read_ctx = NULL;
}

static int erofs_fileio_iomap_read_folio_range(const struct iomap_iter *iter,
		struct iomap_read_folio_ctx *ctx, size_t plen)
{
	struct erofs_iomap_iter_ctx *iter_ctx = iter->private;
	struct folio *folio = ctx->cur_folio;
	struct erofs_fileio_rq *rq = ctx->read_ctx;
	sector_t sector = iomap_sector(&iter->iomap, iter->pos);

	if (!rq || bio_end_sector(&rq->bio) != sector ||
	    !bio_add_folio(&rq->bio, folio, plen,
			   offset_in_folio(folio, iter->pos))) {
		struct super_block *sb = iter_ctx->realinode->i_sb;
		struct erofs_device_info *dif = iter->iomap.private;

		/* submit the existing range if there was one. */
		if (ctx->read_ctx)
			erofs_fileio_iomap_submit_read(iter, ctx);
		rq = erofs_fileio_rq_alloc(sb, dif->file);
		rq->bio.bi_iter.bi_sector = sector;
		bio_add_folio_nofail(&rq->bio, folio, plen,
				offset_in_folio(folio, iter->pos));
		ctx->read_ctx = rq;
		ctx->read_ctx_file_offset = iter->pos;
	}
	return 0;
}

const struct iomap_read_ops erofs_fileio_iomap_read_ops = {
	.read_folio_range	= erofs_fileio_iomap_read_folio_range,
	.submit_read		= erofs_fileio_iomap_submit_read,
};

static void erofs_fileio_meta_endio(struct bio *bio)
{
	bool uptodate = !bio->bi_status;
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio)
		folio_end_read(fi.folio, uptodate);
}

int erofs_read_meta_folio(struct file *file, struct folio *folio)
{
	struct erofs_fileio io = {
		.dev = { .m_pa = folio_pos(folio), },
	};
	struct inode *inode = folio_inode(folio);
	int err;

	err = erofs_map_dev(inode->i_sb, &io.dev);
	if (err)
		return err;

	io.rq = erofs_fileio_rq_alloc(inode->i_sb, io.dev.m_dif->file);
	io.rq->bio.bi_end_io = erofs_fileio_meta_endio;
	io.rq->bio.bi_iter.bi_sector =
		(io.dev.m_dif->fsoff + io.dev.m_pa) >> 9;
	bio_add_folio_nofail(&io.rq->bio, folio, folio_size(folio), 0);
	erofs_fileio_rq_submit(io.rq);
	return 0;
}
