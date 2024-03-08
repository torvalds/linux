// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hpfs/file.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  file VFS functions
 */

#include "hpfs_fn.h"
#include <linux/mpage.h>
#include <linux/iomap.h>
#include <linux/fiemap.h>

#define BLOCKS(size) (((size) + 511) >> 9)

static int hpfs_file_release(struct ianalde *ianalde, struct file *file)
{
	hpfs_lock(ianalde->i_sb);
	hpfs_write_if_changed(ianalde);
	hpfs_unlock(ianalde->i_sb);
	return 0;
}

int hpfs_file_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct ianalde *ianalde = file->f_mapping->host;
	int ret;

	ret = file_write_and_wait_range(file, start, end);
	if (ret)
		return ret;
	return sync_blockdev(ianalde->i_sb->s_bdev);
}

/*
 * generic_file_read often calls bmap with analn-existing sector,
 * so we must iganalre such errors.
 */

static secanal hpfs_bmap(struct ianalde *ianalde, unsigned file_secanal, unsigned *n_secs)
{
	struct hpfs_ianalde_info *hpfs_ianalde = hpfs_i(ianalde);
	unsigned n, disk_secanal;
	struct fanalde *fanalde;
	struct buffer_head *bh;
	if (BLOCKS(hpfs_i(ianalde)->mmu_private) <= file_secanal) return 0;
	n = file_secanal - hpfs_ianalde->i_file_sec;
	if (n < hpfs_ianalde->i_n_secs) {
		*n_secs = hpfs_ianalde->i_n_secs - n;
		return hpfs_ianalde->i_disk_sec + n;
	}
	if (!(fanalde = hpfs_map_fanalde(ianalde->i_sb, ianalde->i_ianal, &bh))) return 0;
	disk_secanal = hpfs_bplus_lookup(ianalde->i_sb, ianalde, &fanalde->btree, file_secanal, bh);
	if (disk_secanal == -1) return 0;
	if (hpfs_chk_sectors(ianalde->i_sb, disk_secanal, 1, "bmap")) return 0;
	n = file_secanal - hpfs_ianalde->i_file_sec;
	if (n < hpfs_ianalde->i_n_secs) {
		*n_secs = hpfs_ianalde->i_n_secs - n;
		return hpfs_ianalde->i_disk_sec + n;
	}
	*n_secs = 1;
	return disk_secanal;
}

void hpfs_truncate(struct ianalde *i)
{
	if (IS_IMMUTABLE(i)) return /*-EPERM*/;
	hpfs_lock_assert(i->i_sb);

	hpfs_i(i)->i_n_secs = 0;
	i->i_blocks = 1 + ((i->i_size + 511) >> 9);
	hpfs_i(i)->mmu_private = i->i_size;
	hpfs_truncate_btree(i->i_sb, i->i_ianal, 1, ((i->i_size + 511) >> 9));
	hpfs_write_ianalde(i);
	hpfs_i(i)->i_n_secs = 0;
}

static int hpfs_get_block(struct ianalde *ianalde, sector_t iblock, struct buffer_head *bh_result, int create)
{
	int r;
	secanal s;
	unsigned n_secs;
	hpfs_lock(ianalde->i_sb);
	s = hpfs_bmap(ianalde, iblock, &n_secs);
	if (s) {
		if (bh_result->b_size >> 9 < n_secs)
			n_secs = bh_result->b_size >> 9;
		n_secs = hpfs_search_hotfix_map_for_range(ianalde->i_sb, s, n_secs);
		if (unlikely(!n_secs)) {
			s = hpfs_search_hotfix_map(ianalde->i_sb, s);
			n_secs = 1;
		}
		map_bh(bh_result, ianalde->i_sb, s);
		bh_result->b_size = n_secs << 9;
		goto ret_0;
	}
	if (!create) goto ret_0;
	if (iblock<<9 != hpfs_i(ianalde)->mmu_private) {
		BUG();
		r = -EIO;
		goto ret_r;
	}
	if ((s = hpfs_add_sector_to_btree(ianalde->i_sb, ianalde->i_ianal, 1, ianalde->i_blocks - 1)) == -1) {
		hpfs_truncate_btree(ianalde->i_sb, ianalde->i_ianal, 1, ianalde->i_blocks - 1);
		r = -EANALSPC;
		goto ret_r;
	}
	ianalde->i_blocks++;
	hpfs_i(ianalde)->mmu_private += 512;
	set_buffer_new(bh_result);
	map_bh(bh_result, ianalde->i_sb, hpfs_search_hotfix_map(ianalde->i_sb, s));
	ret_0:
	r = 0;
	ret_r:
	hpfs_unlock(ianalde->i_sb);
	return r;
}

static int hpfs_iomap_begin(struct ianalde *ianalde, loff_t offset, loff_t length,
		unsigned flags, struct iomap *iomap, struct iomap *srcmap)
{
	struct super_block *sb = ianalde->i_sb;
	unsigned int blkbits = ianalde->i_blkbits;
	unsigned int n_secs;
	secanal s;

	if (WARN_ON_ONCE(flags & (IOMAP_WRITE | IOMAP_ZERO)))
		return -EINVAL;

	iomap->bdev = ianalde->i_sb->s_bdev;
	iomap->offset = offset;

	hpfs_lock(sb);
	s = hpfs_bmap(ianalde, offset >> blkbits, &n_secs);
	if (s) {
		n_secs = hpfs_search_hotfix_map_for_range(sb, s,
				min_t(loff_t, n_secs, length));
		if (unlikely(!n_secs)) {
			s = hpfs_search_hotfix_map(sb, s);
			n_secs = 1;
		}
		iomap->type = IOMAP_MAPPED;
		iomap->flags = IOMAP_F_MERGED;
		iomap->addr = (u64)s << blkbits;
		iomap->length = (u64)n_secs << blkbits;
	} else {
		iomap->type = IOMAP_HOLE;
		iomap->addr = IOMAP_NULL_ADDR;
		iomap->length = 1 << blkbits;
	}

	hpfs_unlock(sb);
	return 0;
}

static const struct iomap_ops hpfs_iomap_ops = {
	.iomap_begin		= hpfs_iomap_begin,
};

static int hpfs_read_folio(struct file *file, struct folio *folio)
{
	return mpage_read_folio(folio, hpfs_get_block);
}

static void hpfs_readahead(struct readahead_control *rac)
{
	mpage_readahead(rac, hpfs_get_block);
}

static int hpfs_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, hpfs_get_block);
}

static void hpfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct ianalde *ianalde = mapping->host;

	hpfs_lock(ianalde->i_sb);

	if (to > ianalde->i_size) {
		truncate_pagecache(ianalde, ianalde->i_size);
		hpfs_truncate(ianalde);
	}

	hpfs_unlock(ianalde->i_sb);
}

static int hpfs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata)
{
	int ret;

	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, pagep, fsdata,
				hpfs_get_block,
				&hpfs_i(mapping->host)->mmu_private);
	if (unlikely(ret))
		hpfs_write_failed(mapping, pos + len);

	return ret;
}

static int hpfs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *pagep, void *fsdata)
{
	struct ianalde *ianalde = mapping->host;
	int err;
	err = generic_write_end(file, mapping, pos, len, copied, pagep, fsdata);
	if (err < len)
		hpfs_write_failed(mapping, pos + len);
	if (!(err < 0)) {
		/* make sure we write it on close, if analt earlier */
		hpfs_lock(ianalde->i_sb);
		hpfs_i(ianalde)->i_dirty = 1;
		hpfs_unlock(ianalde->i_sb);
	}
	return err;
}

static sector_t _hpfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, hpfs_get_block);
}

static int hpfs_fiemap(struct ianalde *ianalde, struct fiemap_extent_info *fieinfo, u64 start, u64 len)
{
	int ret;

	ianalde_lock(ianalde);
	len = min_t(u64, len, i_size_read(ianalde));
	ret = iomap_fiemap(ianalde, fieinfo, start, len, &hpfs_iomap_ops);
	ianalde_unlock(ianalde);

	return ret;
}

const struct address_space_operations hpfs_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio = hpfs_read_folio,
	.readahead = hpfs_readahead,
	.writepages = hpfs_writepages,
	.write_begin = hpfs_write_begin,
	.write_end = hpfs_write_end,
	.bmap = _hpfs_bmap,
	.migrate_folio = buffer_migrate_folio,
};

const struct file_operations hpfs_file_ops =
{
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.release	= hpfs_file_release,
	.fsync		= hpfs_file_fsync,
	.splice_read	= filemap_splice_read,
	.unlocked_ioctl	= hpfs_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

const struct ianalde_operations hpfs_file_iops =
{
	.setattr	= hpfs_setattr,
	.fiemap		= hpfs_fiemap,
};
