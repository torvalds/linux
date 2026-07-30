// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iomap callack functions
 *
 * Copyright (C) 2026 Namjae Jeon <linkinjeon@kernel.org>
 */

#include <linux/iomap.h>
#include <linux/pagemap.h>

#include "exfat_raw.h"
#include "exfat_fs.h"
#include "iomap.h"

/*
 * exfat_file_write_dio_end_io - Direct I/O write completion handler
 *
 * Updates i_size if the write extended the file. Called from the dio layer
 * after I/O completion.
 */
static int exfat_file_write_dio_end_io(struct kiocb *iocb, ssize_t size,
		int error, unsigned int flags)
{
	struct inode *inode = file_inode(iocb->ki_filp);

	if (error)
		return error;

	if (size && i_size_read(inode) < iocb->ki_pos + size) {
		i_size_write(inode, iocb->ki_pos + size);
		mark_inode_dirty(inode);
	}

	return 0;
}

const struct iomap_dio_ops exfat_write_dio_ops = {
	.end_io		= exfat_file_write_dio_end_io,
};

static int __exfat_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
		unsigned int flags, struct iomap *iomap, bool may_alloc)
{
	struct super_block *sb = inode->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_inode_info *ei = EXFAT_I(inode);
	unsigned int cluster, num_clusters;
	loff_t cluster_offset, cluster_length;
	int err;
	bool balloc = false;

	if (!may_alloc) {
		/* Completely beyond EOF. Treat as hole */
		if (i_size_read(inode) <= offset) {
			iomap->type = IOMAP_HOLE;
			iomap->addr = IOMAP_NULL_ADDR;
			iomap->offset = offset;
			iomap->length = length;
			return 0;
		}

		/* Clamp length if the requested range goes beyond i_size */
		if (offset + length > i_size_read(inode))
			length = round_up(i_size_read(inode),
					  i_blocksize(inode)) - offset;
	}

	num_clusters = exfat_bytes_to_cluster_round_up(sbi,
			offset + length) - exfat_bytes_to_cluster(sbi, offset);

	mutex_lock(&sbi->s_lock);
	iomap->bdev = inode->i_sb->s_bdev;
	iomap->offset = offset;

	err = exfat_map_cluster(inode, exfat_bytes_to_cluster(sbi, offset),
			&cluster, &num_clusters, may_alloc, &balloc);
	if (err)
		goto out;

	cluster_offset = exfat_cluster_offset(sbi, offset);
	cluster_length = exfat_cluster_to_bytes(sbi, num_clusters);

	iomap->length = min_t(loff_t, length, cluster_length - cluster_offset);
	iomap->addr = exfat_cluster_to_phys_bytes(sbi, cluster) + cluster_offset;
	iomap->type = IOMAP_MAPPED;
	iomap->flags = IOMAP_F_MERGED;

	if (may_alloc || flags & IOMAP_ZERO) {
		if (balloc)
			iomap->flags |= IOMAP_F_NEW;
		else if (iomap->offset + iomap->length >= ei->valid_size) {
			/*
			 * This is a write that starts at or extends beyond
			 * the current valid_size. The region between the old
			 * valid_size and the end of this write needs to be
			 * zeroed in the page cache to prevent stale data
			 * exposure (see IOMAP_F_ZERO_TAIL handling in
			 * __iomap_write_begin()).
			 */
			iomap->flags |= IOMAP_F_ZERO_TAIL;
		}
	} else {
		/*
		 * valid_size is tracked in byte granularity and
		 * marks the exact boundary between valid data and
		 * holes (or unwritten space).
		 *
		 * When IOMAP_REPORT is set (used by lseek(SEEK_HOLE)
		 * and SEEK_DATA), we return IOMAP_HOLE. This allows
		 * iomap_seek_hole_iter() to directly return the
		 * precise byte position.
		 *
		 * For normal I/O paths (without IOMAP_REPORT) we
		 * return IOMAP_UNWRITTEN so the write path can
		 * distinguish it from a real hole.
		 */
		if (offset >= ei->valid_size) {
			iomap->type = flags & IOMAP_REPORT ?
				IOMAP_HOLE : IOMAP_UNWRITTEN;
		} else if (offset + iomap->length > ei->valid_size) {
			if (flags & IOMAP_REPORT) {
				/*
				 * For SEEK_HOLE/SEEK_DATA, clip the length
				 * to the exact byte boundary (valid_size).
				 * This ensures the caller gets the precise
				 * hole position in byte units.
				 */
				iomap->length = ei->valid_size - iomap->offset;
			} else
				iomap->length = round_up(ei->valid_size,
							 i_blocksize(inode)) -
								iomap->offset;
		}
	}

	iomap->flags |= IOMAP_F_MERGED;
out:
	mutex_unlock(&sbi->s_lock);
	return err;
}

static int exfat_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
		unsigned int flags, struct iomap *iomap, struct iomap *srcmap)
{
	return __exfat_iomap_begin(inode, offset, length, flags, iomap, false);
}

static int exfat_write_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
		unsigned int flags, struct iomap *iomap, struct iomap *srcmap)
{
	return __exfat_iomap_begin(inode, offset, length, flags, iomap, true);
}

const struct iomap_ops exfat_iomap_ops = {
	.iomap_begin = exfat_iomap_begin,
};

/*
 * exfat_write_iomap_end - Update the state after write
 *
 * Extends ->valid_size to cover the newly written range.
 * Marks the inode dirty if metadata was changed.
 */
static int exfat_write_iomap_end(struct inode *inode, loff_t pos, loff_t length,
		ssize_t written, unsigned int flags, struct iomap *iomap)
{
	struct exfat_inode_info *ei = EXFAT_I(inode);
	bool dirtied = false;
	loff_t end;

	if (!written)
		return 0;

	end = pos + written;

	if (ei->valid_size < end) {
		ei->valid_size = end;
		if (ei->zeroed_size < end)
			ei->zeroed_size = end;
		dirtied = true;
	}

	if (dirtied || iomap->flags & IOMAP_F_SIZE_CHANGED)
		mark_inode_dirty(inode);

	return written;
}

const struct iomap_ops exfat_write_iomap_ops = {
	.iomap_begin	= exfat_write_iomap_begin,
	.iomap_end	= exfat_write_iomap_end,
};

/*
 * exfat_writeback_range - Map folio during writeback
 *
 * Called for each folio during writeback. If the folio falls outside the
 * current iomap, remaps by calling read_iomap_begin.
 */
static ssize_t exfat_writeback_range(struct iomap_writepage_ctx *wpc,
		struct folio *folio, u64 offset, unsigned int len, u64 end_pos)
{
	if (offset < wpc->iomap.offset ||
	    offset >= wpc->iomap.offset + wpc->iomap.length) {
		int error;

		error = __exfat_iomap_begin(wpc->inode, offset, len,
				0, &wpc->iomap, false);
		if (error)
			return error;
	}

	return iomap_add_to_ioend(wpc, folio, offset, end_pos, len);
}

const struct iomap_writeback_ops exfat_writeback_ops = {
	.writeback_range	= exfat_writeback_range,
	.writeback_submit	= iomap_ioend_writeback_submit,
};

/**
 * exfat_iomap_read_end_io - iomap read bio completion handler for exFAT
 * @bio: bio that has completed reading
 *
 * exfat_iomap_begin() rounds up MAPPED extents to the block boundary of
 * valid_size. This ensures that any subsequent blocks are treated as
 * IOMAP_UNWRITTEN, but it also causes the "straddle block" containing
 * valid_size to be read from disk. The disk data beyond valid_size in
 * this block is stale and must be zeroed to prevent data leakage.
 */
static void exfat_iomap_read_end_io(struct bio *bio)
{
	int error = blk_status_to_errno(bio->bi_status);
	struct folio_iter iter;

	bio_for_each_folio_all(iter, bio) {
		struct folio *folio = iter.folio;
		struct exfat_inode_info *ei = EXFAT_I(folio->mapping->host);
		s64 valid_size;
		loff_t pos = folio_pos(folio);

		valid_size = ei->valid_size;
		if (pos + iter.offset < valid_size &&
		    pos + iter.offset + iter.length > valid_size)
			folio_zero_segment(folio, offset_in_folio(folio, valid_size),
					   iter.offset + iter.length);

		iomap_finish_folio_read(folio, iter.offset, iter.length, error);
	}
	bio_put(bio);
}

static void exfat_iomap_bio_submit_read(const struct iomap_iter *iter,
		struct iomap_read_folio_ctx *ctx)
{
	iomap_bio_submit_read_endio(iter, ctx, exfat_iomap_read_end_io);
}

const struct iomap_read_ops exfat_iomap_bio_read_ops = {
	.read_folio_range	= iomap_bio_read_folio_range,
	.submit_read		= exfat_iomap_bio_submit_read,
};

int exfat_iomap_swap_activate(struct swap_info_struct *sis,
			       struct file *file, sector_t *span)
{
	return iomap_swapfile_activate(sis, file, span, &exfat_iomap_ops);
}
