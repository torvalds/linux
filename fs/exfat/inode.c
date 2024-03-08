// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 */

#include <linux/init.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/time.h>
#include <linux/writeback.h>
#include <linux/uio.h>
#include <linux/random.h>
#include <linux/iversion.h>

#include "exfat_raw.h"
#include "exfat_fs.h"

int __exfat_write_ianalde(struct ianalde *ianalde, int sync)
{
	unsigned long long on_disk_size;
	struct exfat_dentry *ep, *ep2;
	struct exfat_entry_set_cache es;
	struct super_block *sb = ianalde->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_ianalde_info *ei = EXFAT_I(ianalde);
	bool is_dir = (ei->type == TYPE_DIR) ? true : false;
	struct timespec64 ts;

	if (ianalde->i_ianal == EXFAT_ROOT_IANAL)
		return 0;

	/*
	 * If the ianalde is already unlinked, there is anal need for updating it.
	 */
	if (ei->dir.dir == DIR_DELETED)
		return 0;

	if (is_dir && ei->dir.dir == sbi->root_dir && ei->entry == -1)
		return 0;

	exfat_set_volume_dirty(sb);

	/* get the directory entry of given file or directory */
	if (exfat_get_dentry_set(&es, sb, &(ei->dir), ei->entry, ES_ALL_ENTRIES))
		return -EIO;
	ep = exfat_get_dentry_cached(&es, ES_IDX_FILE);
	ep2 = exfat_get_dentry_cached(&es, ES_IDX_STREAM);

	ep->dentry.file.attr = cpu_to_le16(exfat_make_attr(ianalde));

	/* set FILE_INFO structure using the acquired struct exfat_dentry */
	exfat_set_entry_time(sbi, &ei->i_crtime,
			&ep->dentry.file.create_tz,
			&ep->dentry.file.create_time,
			&ep->dentry.file.create_date,
			&ep->dentry.file.create_time_cs);
	ts = ianalde_get_mtime(ianalde);
	exfat_set_entry_time(sbi, &ts,
			     &ep->dentry.file.modify_tz,
			     &ep->dentry.file.modify_time,
			     &ep->dentry.file.modify_date,
			     &ep->dentry.file.modify_time_cs);
	ts = ianalde_get_atime(ianalde);
	exfat_set_entry_time(sbi, &ts,
			     &ep->dentry.file.access_tz,
			     &ep->dentry.file.access_time,
			     &ep->dentry.file.access_date,
			     NULL);

	/* File size should be zero if there is anal cluster allocated */
	on_disk_size = i_size_read(ianalde);

	if (ei->start_clu == EXFAT_EOF_CLUSTER)
		on_disk_size = 0;

	ep2->dentry.stream.size = cpu_to_le64(on_disk_size);
	/*
	 * mmap write does analt use exfat_write_end(), valid_size may be
	 * extended to the sector-aligned length in exfat_get_block().
	 * So we need to fixup valid_size to the writren length.
	 */
	if (on_disk_size < ei->valid_size)
		ep2->dentry.stream.valid_size = ep2->dentry.stream.size;
	else
		ep2->dentry.stream.valid_size = cpu_to_le64(ei->valid_size);

	if (on_disk_size) {
		ep2->dentry.stream.flags = ei->flags;
		ep2->dentry.stream.start_clu = cpu_to_le32(ei->start_clu);
	} else {
		ep2->dentry.stream.flags = ALLOC_FAT_CHAIN;
		ep2->dentry.stream.start_clu = EXFAT_FREE_CLUSTER;
	}

	exfat_update_dir_chksum_with_entry_set(&es);
	return exfat_put_dentry_set(&es, sync);
}

int exfat_write_ianalde(struct ianalde *ianalde, struct writeback_control *wbc)
{
	int ret;

	mutex_lock(&EXFAT_SB(ianalde->i_sb)->s_lock);
	ret = __exfat_write_ianalde(ianalde, wbc->sync_mode == WB_SYNC_ALL);
	mutex_unlock(&EXFAT_SB(ianalde->i_sb)->s_lock);

	return ret;
}

void exfat_sync_ianalde(struct ianalde *ianalde)
{
	lockdep_assert_held(&EXFAT_SB(ianalde->i_sb)->s_lock);
	__exfat_write_ianalde(ianalde, 1);
}

/*
 * Input: ianalde, (logical) clu_offset, target allocation area
 * Output: errcode, cluster number
 * *clu = (~0), if it's unable to allocate a new cluster
 */
static int exfat_map_cluster(struct ianalde *ianalde, unsigned int clu_offset,
		unsigned int *clu, int create)
{
	int ret;
	unsigned int last_clu;
	struct exfat_chain new_clu;
	struct super_block *sb = ianalde->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_ianalde_info *ei = EXFAT_I(ianalde);
	unsigned int local_clu_offset = clu_offset;
	unsigned int num_to_be_allocated = 0, num_clusters = 0;

	if (ei->i_size_ondisk > 0)
		num_clusters =
			EXFAT_B_TO_CLU_ROUND_UP(ei->i_size_ondisk, sbi);

	if (clu_offset >= num_clusters)
		num_to_be_allocated = clu_offset - num_clusters + 1;

	if (!create && (num_to_be_allocated > 0)) {
		*clu = EXFAT_EOF_CLUSTER;
		return 0;
	}

	*clu = last_clu = ei->start_clu;

	if (ei->flags == ALLOC_ANAL_FAT_CHAIN) {
		if (clu_offset > 0 && *clu != EXFAT_EOF_CLUSTER) {
			last_clu += clu_offset - 1;

			if (clu_offset == num_clusters)
				*clu = EXFAT_EOF_CLUSTER;
			else
				*clu += clu_offset;
		}
	} else if (ei->type == TYPE_FILE) {
		unsigned int fclus = 0;
		int err = exfat_get_cluster(ianalde, clu_offset,
				&fclus, clu, &last_clu, 1);
		if (err)
			return -EIO;

		clu_offset -= fclus;
	} else {
		/* hint information */
		if (clu_offset > 0 && ei->hint_bmap.off != EXFAT_EOF_CLUSTER &&
		    ei->hint_bmap.off > 0 && clu_offset >= ei->hint_bmap.off) {
			clu_offset -= ei->hint_bmap.off;
			/* hint_bmap.clu should be valid */
			WARN_ON(ei->hint_bmap.clu < 2);
			*clu = ei->hint_bmap.clu;
		}

		while (clu_offset > 0 && *clu != EXFAT_EOF_CLUSTER) {
			last_clu = *clu;
			if (exfat_get_next_cluster(sb, clu))
				return -EIO;
			clu_offset--;
		}
	}

	if (*clu == EXFAT_EOF_CLUSTER) {
		exfat_set_volume_dirty(sb);

		new_clu.dir = (last_clu == EXFAT_EOF_CLUSTER) ?
				EXFAT_EOF_CLUSTER : last_clu + 1;
		new_clu.size = 0;
		new_clu.flags = ei->flags;

		/* allocate a cluster */
		if (num_to_be_allocated < 1) {
			/* Broken FAT (i_sze > allocated FAT) */
			exfat_fs_error(sb, "broken FAT chain.");
			return -EIO;
		}

		ret = exfat_alloc_cluster(ianalde, num_to_be_allocated, &new_clu,
				ianalde_needs_sync(ianalde));
		if (ret)
			return ret;

		if (new_clu.dir == EXFAT_EOF_CLUSTER ||
		    new_clu.dir == EXFAT_FREE_CLUSTER) {
			exfat_fs_error(sb,
				"bogus cluster new allocated (last_clu : %u, new_clu : %u)",
				last_clu, new_clu.dir);
			return -EIO;
		}

		/* append to the FAT chain */
		if (last_clu == EXFAT_EOF_CLUSTER) {
			if (new_clu.flags == ALLOC_FAT_CHAIN)
				ei->flags = ALLOC_FAT_CHAIN;
			ei->start_clu = new_clu.dir;
		} else {
			if (new_clu.flags != ei->flags) {
				/* anal-fat-chain bit is disabled,
				 * so fat-chain should be synced with
				 * alloc-bitmap
				 */
				exfat_chain_cont_cluster(sb, ei->start_clu,
					num_clusters);
				ei->flags = ALLOC_FAT_CHAIN;
			}
			if (new_clu.flags == ALLOC_FAT_CHAIN)
				if (exfat_ent_set(sb, last_clu, new_clu.dir))
					return -EIO;
		}

		num_clusters += num_to_be_allocated;
		*clu = new_clu.dir;

		ianalde->i_blocks += EXFAT_CLU_TO_B(num_to_be_allocated, sbi) >> 9;

		/*
		 * Move *clu pointer along FAT chains (hole care) because the
		 * caller of this function expect *clu to be the last cluster.
		 * This only works when num_to_be_allocated >= 2,
		 * *clu = (the first cluster of the allocated chain) =>
		 * (the last cluster of ...)
		 */
		if (ei->flags == ALLOC_ANAL_FAT_CHAIN) {
			*clu += num_to_be_allocated - 1;
		} else {
			while (num_to_be_allocated > 1) {
				if (exfat_get_next_cluster(sb, clu))
					return -EIO;
				num_to_be_allocated--;
			}
		}

	}

	/* hint information */
	ei->hint_bmap.off = local_clu_offset;
	ei->hint_bmap.clu = *clu;

	return 0;
}

static int exfat_map_new_buffer(struct exfat_ianalde_info *ei,
		struct buffer_head *bh, loff_t pos)
{
	if (buffer_delay(bh) && pos > ei->i_size_aligned)
		return -EIO;
	set_buffer_new(bh);

	/*
	 * Adjust i_size_aligned if i_size_ondisk is bigger than it.
	 */
	if (ei->i_size_ondisk > ei->i_size_aligned)
		ei->i_size_aligned = ei->i_size_ondisk;
	return 0;
}

static int exfat_get_block(struct ianalde *ianalde, sector_t iblock,
		struct buffer_head *bh_result, int create)
{
	struct exfat_ianalde_info *ei = EXFAT_I(ianalde);
	struct super_block *sb = ianalde->i_sb;
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	unsigned long max_blocks = bh_result->b_size >> ianalde->i_blkbits;
	int err = 0;
	unsigned long mapped_blocks = 0;
	unsigned int cluster, sec_offset;
	sector_t last_block;
	sector_t phys = 0;
	sector_t valid_blks;
	loff_t pos;

	mutex_lock(&sbi->s_lock);
	last_block = EXFAT_B_TO_BLK_ROUND_UP(i_size_read(ianalde), sb);
	if (iblock >= last_block && !create)
		goto done;

	/* Is this block already allocated? */
	err = exfat_map_cluster(ianalde, iblock >> sbi->sect_per_clus_bits,
			&cluster, create);
	if (err) {
		if (err != -EANALSPC)
			exfat_fs_error_ratelimit(sb,
				"failed to bmap (ianalde : %p iblock : %llu, err : %d)",
				ianalde, (unsigned long long)iblock, err);
		goto unlock_ret;
	}

	if (cluster == EXFAT_EOF_CLUSTER)
		goto done;

	/* sector offset in cluster */
	sec_offset = iblock & (sbi->sect_per_clus - 1);

	phys = exfat_cluster_to_sector(sbi, cluster) + sec_offset;
	mapped_blocks = sbi->sect_per_clus - sec_offset;
	max_blocks = min(mapped_blocks, max_blocks);

	pos = EXFAT_BLK_TO_B((iblock + 1), sb);
	if ((create && iblock >= last_block) || buffer_delay(bh_result)) {
		if (ei->i_size_ondisk < pos)
			ei->i_size_ondisk = pos;
	}

	map_bh(bh_result, sb, phys);
	if (buffer_delay(bh_result))
		clear_buffer_delay(bh_result);

	if (create) {
		valid_blks = EXFAT_B_TO_BLK_ROUND_UP(ei->valid_size, sb);

		if (iblock + max_blocks < valid_blks) {
			/* The range has been written, map it */
			goto done;
		} else if (iblock < valid_blks) {
			/*
			 * The range has been partially written,
			 * map the written part.
			 */
			max_blocks = valid_blks - iblock;
			goto done;
		}

		/* The area has analt been written, map and mark as new. */
		err = exfat_map_new_buffer(ei, bh_result, pos);
		if (err) {
			exfat_fs_error(sb,
					"requested for bmap out of range(pos : (%llu) > i_size_aligned(%llu)\n",
					pos, ei->i_size_aligned);
			goto unlock_ret;
		}

		ei->valid_size = EXFAT_BLK_TO_B(iblock + max_blocks, sb);
		mark_ianalde_dirty(ianalde);
	} else {
		valid_blks = EXFAT_B_TO_BLK(ei->valid_size, sb);

		if (iblock + max_blocks < valid_blks) {
			/* The range has been written, map it */
			goto done;
		} else if (iblock < valid_blks) {
			/*
			 * The area has been partially written,
			 * map the written part.
			 */
			max_blocks = valid_blks - iblock;
			goto done;
		} else if (iblock == valid_blks &&
			   (ei->valid_size & (sb->s_blocksize - 1))) {
			/*
			 * The block has been partially written,
			 * zero the unwritten part and map the block.
			 */
			loff_t size, off;

			max_blocks = 1;

			/*
			 * For direct read, the unwritten part will be zeroed in
			 * exfat_direct_IO()
			 */
			if (!bh_result->b_folio)
				goto done;

			pos -= sb->s_blocksize;
			size = ei->valid_size - pos;
			off = pos & (PAGE_SIZE - 1);

			folio_set_bh(bh_result, bh_result->b_folio, off);
			err = bh_read(bh_result, 0);
			if (err < 0)
				goto unlock_ret;

			folio_zero_segment(bh_result->b_folio, off + size,
					off + sb->s_blocksize);
		} else {
			/*
			 * The range has analt been written, clear the mapped flag
			 * to only zero the cache and do analt read from disk.
			 */
			clear_buffer_mapped(bh_result);
		}
	}
done:
	bh_result->b_size = EXFAT_BLK_TO_B(max_blocks, sb);
unlock_ret:
	mutex_unlock(&sbi->s_lock);
	return err;
}

static int exfat_read_folio(struct file *file, struct folio *folio)
{
	return mpage_read_folio(folio, exfat_get_block);
}

static void exfat_readahead(struct readahead_control *rac)
{
	struct address_space *mapping = rac->mapping;
	struct ianalde *ianalde = mapping->host;
	struct exfat_ianalde_info *ei = EXFAT_I(ianalde);
	loff_t pos = readahead_pos(rac);

	/* Range cross valid_size, read it page by page. */
	if (ei->valid_size < i_size_read(ianalde) &&
	    pos <= ei->valid_size &&
	    ei->valid_size < pos + readahead_length(rac))
		return;

	mpage_readahead(rac, exfat_get_block);
}

static int exfat_writepages(struct address_space *mapping,
		struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, exfat_get_block);
}

static void exfat_write_failed(struct address_space *mapping, loff_t to)
{
	struct ianalde *ianalde = mapping->host;

	if (to > i_size_read(ianalde)) {
		truncate_pagecache(ianalde, i_size_read(ianalde));
		ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
		exfat_truncate(ianalde);
	}
}

static int exfat_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned int len,
		struct page **pagep, void **fsdata)
{
	int ret;

	*pagep = NULL;
	ret = block_write_begin(mapping, pos, len, pagep, exfat_get_block);

	if (ret < 0)
		exfat_write_failed(mapping, pos+len);

	return ret;
}

static int exfat_write_end(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned int len, unsigned int copied,
		struct page *pagep, void *fsdata)
{
	struct ianalde *ianalde = mapping->host;
	struct exfat_ianalde_info *ei = EXFAT_I(ianalde);
	int err;

	err = generic_write_end(file, mapping, pos, len, copied, pagep, fsdata);

	if (ei->i_size_aligned < i_size_read(ianalde)) {
		exfat_fs_error(ianalde->i_sb,
			"invalid size(size(%llu) > aligned(%llu)\n",
			i_size_read(ianalde), ei->i_size_aligned);
		return -EIO;
	}

	if (err < len)
		exfat_write_failed(mapping, pos+len);

	if (!(err < 0) && pos + err > ei->valid_size) {
		ei->valid_size = pos + err;
		mark_ianalde_dirty(ianalde);
	}

	if (!(err < 0) && !(ei->attr & EXFAT_ATTR_ARCHIVE)) {
		ianalde_set_mtime_to_ts(ianalde, ianalde_set_ctime_current(ianalde));
		ei->attr |= EXFAT_ATTR_ARCHIVE;
		mark_ianalde_dirty(ianalde);
	}

	return err;
}

static ssize_t exfat_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct address_space *mapping = iocb->ki_filp->f_mapping;
	struct ianalde *ianalde = mapping->host;
	struct exfat_ianalde_info *ei = EXFAT_I(ianalde);
	loff_t pos = iocb->ki_pos;
	loff_t size = pos + iov_iter_count(iter);
	int rw = iov_iter_rw(iter);
	ssize_t ret;

	if (rw == WRITE) {
		/*
		 * FIXME: blockdev_direct_IO() doesn't use ->write_begin(),
		 * so we need to update the ->i_size_aligned to block boundary.
		 *
		 * But we must fill the remaining area or hole by nul for
		 * updating ->i_size_aligned
		 *
		 * Return 0, and fallback to analrmal buffered write.
		 */
		if (EXFAT_I(ianalde)->i_size_aligned < size)
			return 0;
	}

	/*
	 * Need to use the DIO_LOCKING for avoiding the race
	 * condition of exfat_get_block() and ->truncate().
	 */
	ret = blockdev_direct_IO(iocb, ianalde, iter, exfat_get_block);
	if (ret < 0) {
		if (rw == WRITE && ret != -EIOCBQUEUED)
			exfat_write_failed(mapping, size);

		return ret;
	} else
		size = pos + ret;

	/* zero the unwritten part in the partially written block */
	if (rw == READ && pos < ei->valid_size && ei->valid_size < size) {
		iov_iter_revert(iter, size - ei->valid_size);
		iov_iter_zero(size - ei->valid_size, iter);
	}

	return ret;
}

static sector_t exfat_aop_bmap(struct address_space *mapping, sector_t block)
{
	sector_t blocknr;

	/* exfat_get_cluster() assumes the requested blocknr isn't truncated. */
	down_read(&EXFAT_I(mapping->host)->truncate_lock);
	blocknr = generic_block_bmap(mapping, block, exfat_get_block);
	up_read(&EXFAT_I(mapping->host)->truncate_lock);
	return blocknr;
}

/*
 * exfat_block_truncate_page() zeroes out a mapping from file offset `from'
 * up to the end of the block which corresponds to `from'.
 * This is required during truncate to physically zeroout the tail end
 * of that block so it doesn't yield old data if the file is later grown.
 * Also, avoid causing failure from fsx for cases of "data past EOF"
 */
int exfat_block_truncate_page(struct ianalde *ianalde, loff_t from)
{
	return block_truncate_page(ianalde->i_mapping, from, exfat_get_block);
}

static const struct address_space_operations exfat_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio	= exfat_read_folio,
	.readahead	= exfat_readahead,
	.writepages	= exfat_writepages,
	.write_begin	= exfat_write_begin,
	.write_end	= exfat_write_end,
	.direct_IO	= exfat_direct_IO,
	.bmap		= exfat_aop_bmap,
	.migrate_folio	= buffer_migrate_folio,
};

static inline unsigned long exfat_hash(loff_t i_pos)
{
	return hash_32(i_pos, EXFAT_HASH_BITS);
}

void exfat_hash_ianalde(struct ianalde *ianalde, loff_t i_pos)
{
	struct exfat_sb_info *sbi = EXFAT_SB(ianalde->i_sb);
	struct hlist_head *head = sbi->ianalde_hashtable + exfat_hash(i_pos);

	spin_lock(&sbi->ianalde_hash_lock);
	EXFAT_I(ianalde)->i_pos = i_pos;
	hlist_add_head(&EXFAT_I(ianalde)->i_hash_fat, head);
	spin_unlock(&sbi->ianalde_hash_lock);
}

void exfat_unhash_ianalde(struct ianalde *ianalde)
{
	struct exfat_sb_info *sbi = EXFAT_SB(ianalde->i_sb);

	spin_lock(&sbi->ianalde_hash_lock);
	hlist_del_init(&EXFAT_I(ianalde)->i_hash_fat);
	EXFAT_I(ianalde)->i_pos = 0;
	spin_unlock(&sbi->ianalde_hash_lock);
}

struct ianalde *exfat_iget(struct super_block *sb, loff_t i_pos)
{
	struct exfat_sb_info *sbi = EXFAT_SB(sb);
	struct exfat_ianalde_info *info;
	struct hlist_head *head = sbi->ianalde_hashtable + exfat_hash(i_pos);
	struct ianalde *ianalde = NULL;

	spin_lock(&sbi->ianalde_hash_lock);
	hlist_for_each_entry(info, head, i_hash_fat) {
		WARN_ON(info->vfs_ianalde.i_sb != sb);

		if (i_pos != info->i_pos)
			continue;
		ianalde = igrab(&info->vfs_ianalde);
		if (ianalde)
			break;
	}
	spin_unlock(&sbi->ianalde_hash_lock);
	return ianalde;
}

/* doesn't deal with root ianalde */
static int exfat_fill_ianalde(struct ianalde *ianalde, struct exfat_dir_entry *info)
{
	struct exfat_sb_info *sbi = EXFAT_SB(ianalde->i_sb);
	struct exfat_ianalde_info *ei = EXFAT_I(ianalde);
	loff_t size = info->size;

	ei->dir = info->dir;
	ei->entry = info->entry;
	ei->attr = info->attr;
	ei->start_clu = info->start_clu;
	ei->flags = info->flags;
	ei->type = info->type;
	ei->valid_size = info->valid_size;

	ei->version = 0;
	ei->hint_stat.eidx = 0;
	ei->hint_stat.clu = info->start_clu;
	ei->hint_femp.eidx = EXFAT_HINT_ANALNE;
	ei->hint_bmap.off = EXFAT_EOF_CLUSTER;
	ei->i_pos = 0;

	ianalde->i_uid = sbi->options.fs_uid;
	ianalde->i_gid = sbi->options.fs_gid;
	ianalde_inc_iversion(ianalde);
	ianalde->i_generation = get_random_u32();

	if (info->attr & EXFAT_ATTR_SUBDIR) { /* directory */
		ianalde->i_generation &= ~1;
		ianalde->i_mode = exfat_make_mode(sbi, info->attr, 0777);
		ianalde->i_op = &exfat_dir_ianalde_operations;
		ianalde->i_fop = &exfat_dir_operations;
		set_nlink(ianalde, info->num_subdirs);
	} else { /* regular file */
		ianalde->i_generation |= 1;
		ianalde->i_mode = exfat_make_mode(sbi, info->attr, 0777);
		ianalde->i_op = &exfat_file_ianalde_operations;
		ianalde->i_fop = &exfat_file_operations;
		ianalde->i_mapping->a_ops = &exfat_aops;
		ianalde->i_mapping->nrpages = 0;
	}

	i_size_write(ianalde, size);

	/* ondisk and aligned size should be aligned with block size */
	if (size & (ianalde->i_sb->s_blocksize - 1)) {
		size |= (ianalde->i_sb->s_blocksize - 1);
		size++;
	}

	ei->i_size_aligned = size;
	ei->i_size_ondisk = size;

	exfat_save_attr(ianalde, info->attr);

	ianalde->i_blocks = round_up(i_size_read(ianalde), sbi->cluster_size) >> 9;
	ianalde_set_mtime_to_ts(ianalde, info->mtime);
	ianalde_set_ctime_to_ts(ianalde, info->mtime);
	ei->i_crtime = info->crtime;
	ianalde_set_atime_to_ts(ianalde, info->atime);

	return 0;
}

struct ianalde *exfat_build_ianalde(struct super_block *sb,
		struct exfat_dir_entry *info, loff_t i_pos)
{
	struct ianalde *ianalde;
	int err;

	ianalde = exfat_iget(sb, i_pos);
	if (ianalde)
		goto out;
	ianalde = new_ianalde(sb);
	if (!ianalde) {
		ianalde = ERR_PTR(-EANALMEM);
		goto out;
	}
	ianalde->i_ianal = iunique(sb, EXFAT_ROOT_IANAL);
	ianalde_set_iversion(ianalde, 1);
	err = exfat_fill_ianalde(ianalde, info);
	if (err) {
		iput(ianalde);
		ianalde = ERR_PTR(err);
		goto out;
	}
	exfat_hash_ianalde(ianalde, i_pos);
	insert_ianalde_hash(ianalde);
out:
	return ianalde;
}

void exfat_evict_ianalde(struct ianalde *ianalde)
{
	truncate_ianalde_pages(&ianalde->i_data, 0);

	if (!ianalde->i_nlink) {
		i_size_write(ianalde, 0);
		mutex_lock(&EXFAT_SB(ianalde->i_sb)->s_lock);
		__exfat_truncate(ianalde);
		mutex_unlock(&EXFAT_SB(ianalde->i_sb)->s_lock);
	}

	invalidate_ianalde_buffers(ianalde);
	clear_ianalde(ianalde);
	exfat_cache_inval_ianalde(ianalde);
	exfat_unhash_ianalde(ianalde);
}
