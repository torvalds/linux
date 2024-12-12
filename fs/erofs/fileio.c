// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Alibaba Cloud
 */
#include "internal.h"
#include <trace/events/erofs.h>

struct erofs_fileio_rq {
	struct bio_vec bvecs[BIO_MAX_VECS];
	struct bio bio;
	struct kiocb iocb;
	struct super_block *sb;
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

	if (ret > 0) {
		if (ret != rq->bio.bi_iter.bi_size) {
			bio_advance(&rq->bio, ret);
			zero_fill_bio(&rq->bio);
		}
		ret = 0;
	}
	if (rq->bio.bi_end_io) {
		rq->bio.bi_end_io(&rq->bio);
	} else {
		bio_for_each_folio_all(fi, &rq->bio) {
			DBG_BUGON(folio_test_uptodate(fi.folio));
			erofs_onlinefolio_end(fi.folio, ret);
		}
	}
	bio_uninit(&rq->bio);
	kfree(rq);
}

static void erofs_fileio_rq_submit(struct erofs_fileio_rq *rq)
{
	struct iov_iter iter;
	int ret;

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
	ret = vfs_iocb_iter_read(rq->iocb.ki_filp, &rq->iocb, &iter);
	if (ret != -EIOCBQUEUED)
		erofs_fileio_ki_complete(&rq->iocb, ret);
}

static struct erofs_fileio_rq *erofs_fileio_rq_alloc(struct erofs_map_dev *mdev)
{
	struct erofs_fileio_rq *rq = kzalloc(sizeof(*rq),
					     GFP_KERNEL | __GFP_NOFAIL);

	bio_init(&rq->bio, NULL, rq->bvecs, BIO_MAX_VECS, REQ_OP_READ);
	rq->iocb.ki_filp = mdev->m_dif->file;
	rq->sb = mdev->m_sb;
	return rq;
}

struct bio *erofs_fileio_bio_alloc(struct erofs_map_dev *mdev)
{
	return &erofs_fileio_rq_alloc(mdev)->bio;
}

void erofs_fileio_submit_bio(struct bio *bio)
{
	return erofs_fileio_rq_submit(container_of(bio, struct erofs_fileio_rq,
						   bio));
}

static int erofs_fileio_scan_folio(struct erofs_fileio *io, struct folio *folio)
{
	struct inode *inode = folio_inode(folio);
	struct erofs_map_blocks *map = &io->map;
	unsigned int cur = 0, end = folio_size(folio), len, attached = 0;
	loff_t pos = folio_pos(folio), ofs;
	struct iov_iter iter;
	struct bio_vec bv;
	int err = 0;

	erofs_onlinefolio_init(folio);
	while (cur < end) {
		if (!in_range(pos + cur, map->m_la, map->m_llen)) {
			map->m_la = pos + cur;
			map->m_llen = end - cur;
			err = erofs_map_blocks(inode, map);
			if (err)
				break;
		}

		ofs = folio_pos(folio) + cur - map->m_la;
		len = min_t(loff_t, map->m_llen - ofs, end - cur);
		if (map->m_flags & EROFS_MAP_META) {
			struct erofs_buf buf = __EROFS_BUF_INITIALIZER;
			void *src;

			src = erofs_read_metabuf(&buf, inode->i_sb,
						 map->m_pa + ofs, EROFS_KMAP);
			if (IS_ERR(src)) {
				err = PTR_ERR(src);
				break;
			}
			bvec_set_folio(&bv, folio, len, cur);
			iov_iter_bvec(&iter, ITER_DEST, &bv, 1, len);
			if (copy_to_iter(src, len, &iter) != len) {
				erofs_put_metabuf(&buf);
				err = -EIO;
				break;
			}
			erofs_put_metabuf(&buf);
		} else if (!(map->m_flags & EROFS_MAP_MAPPED)) {
			folio_zero_segment(folio, cur, cur + len);
			attached = 0;
		} else {
			if (io->rq && (map->m_pa + ofs != io->dev.m_pa ||
				       map->m_deviceid != io->dev.m_deviceid)) {
io_retry:
				erofs_fileio_rq_submit(io->rq);
				io->rq = NULL;
			}

			if (!io->rq) {
				io->dev = (struct erofs_map_dev) {
					.m_pa = io->map.m_pa + ofs,
					.m_deviceid = io->map.m_deviceid,
				};
				err = erofs_map_dev(inode->i_sb, &io->dev);
				if (err)
					break;
				io->rq = erofs_fileio_rq_alloc(&io->dev);
				io->rq->bio.bi_iter.bi_sector = io->dev.m_pa >> 9;
				attached = 0;
			}
			if (!attached++)
				erofs_onlinefolio_split(folio);
			if (!bio_add_folio(&io->rq->bio, folio, len, cur))
				goto io_retry;
			io->dev.m_pa += len;
		}
		cur += len;
	}
	erofs_onlinefolio_end(folio, err);
	return err;
}

static int erofs_fileio_read_folio(struct file *file, struct folio *folio)
{
	struct erofs_fileio io = {};
	int err;

	trace_erofs_read_folio(folio, true);
	err = erofs_fileio_scan_folio(&io, folio);
	erofs_fileio_rq_submit(io.rq);
	return err;
}

static void erofs_fileio_readahead(struct readahead_control *rac)
{
	struct inode *inode = rac->mapping->host;
	struct erofs_fileio io = {};
	struct folio *folio;
	int err;

	trace_erofs_readpages(inode, readahead_index(rac),
			      readahead_count(rac), true);
	while ((folio = readahead_folio(rac))) {
		err = erofs_fileio_scan_folio(&io, folio);
		if (err && err != -EINTR)
			erofs_err(inode->i_sb, "readahead error at folio %lu @ nid %llu",
				  folio->index, EROFS_I(inode)->nid);
	}
	erofs_fileio_rq_submit(io.rq);
}

const struct address_space_operations erofs_fileio_aops = {
	.read_folio = erofs_fileio_read_folio,
	.readahead = erofs_fileio_readahead,
};
