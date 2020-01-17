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

#define BLOCKS(size) (((size) + 511) >> 9)

static int hpfs_file_release(struct iyesde *iyesde, struct file *file)
{
	hpfs_lock(iyesde->i_sb);
	hpfs_write_if_changed(iyesde);
	hpfs_unlock(iyesde->i_sb);
	return 0;
}

int hpfs_file_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct iyesde *iyesde = file->f_mapping->host;
	int ret;

	ret = file_write_and_wait_range(file, start, end);
	if (ret)
		return ret;
	return sync_blockdev(iyesde->i_sb->s_bdev);
}

/*
 * generic_file_read often calls bmap with yesn-existing sector,
 * so we must igyesre such errors.
 */

static secyes hpfs_bmap(struct iyesde *iyesde, unsigned file_secyes, unsigned *n_secs)
{
	struct hpfs_iyesde_info *hpfs_iyesde = hpfs_i(iyesde);
	unsigned n, disk_secyes;
	struct fyesde *fyesde;
	struct buffer_head *bh;
	if (BLOCKS(hpfs_i(iyesde)->mmu_private) <= file_secyes) return 0;
	n = file_secyes - hpfs_iyesde->i_file_sec;
	if (n < hpfs_iyesde->i_n_secs) {
		*n_secs = hpfs_iyesde->i_n_secs - n;
		return hpfs_iyesde->i_disk_sec + n;
	}
	if (!(fyesde = hpfs_map_fyesde(iyesde->i_sb, iyesde->i_iyes, &bh))) return 0;
	disk_secyes = hpfs_bplus_lookup(iyesde->i_sb, iyesde, &fyesde->btree, file_secyes, bh);
	if (disk_secyes == -1) return 0;
	if (hpfs_chk_sectors(iyesde->i_sb, disk_secyes, 1, "bmap")) return 0;
	n = file_secyes - hpfs_iyesde->i_file_sec;
	if (n < hpfs_iyesde->i_n_secs) {
		*n_secs = hpfs_iyesde->i_n_secs - n;
		return hpfs_iyesde->i_disk_sec + n;
	}
	*n_secs = 1;
	return disk_secyes;
}

void hpfs_truncate(struct iyesde *i)
{
	if (IS_IMMUTABLE(i)) return /*-EPERM*/;
	hpfs_lock_assert(i->i_sb);

	hpfs_i(i)->i_n_secs = 0;
	i->i_blocks = 1 + ((i->i_size + 511) >> 9);
	hpfs_i(i)->mmu_private = i->i_size;
	hpfs_truncate_btree(i->i_sb, i->i_iyes, 1, ((i->i_size + 511) >> 9));
	hpfs_write_iyesde(i);
	hpfs_i(i)->i_n_secs = 0;
}

static int hpfs_get_block(struct iyesde *iyesde, sector_t iblock, struct buffer_head *bh_result, int create)
{
	int r;
	secyes s;
	unsigned n_secs;
	hpfs_lock(iyesde->i_sb);
	s = hpfs_bmap(iyesde, iblock, &n_secs);
	if (s) {
		if (bh_result->b_size >> 9 < n_secs)
			n_secs = bh_result->b_size >> 9;
		n_secs = hpfs_search_hotfix_map_for_range(iyesde->i_sb, s, n_secs);
		if (unlikely(!n_secs)) {
			s = hpfs_search_hotfix_map(iyesde->i_sb, s);
			n_secs = 1;
		}
		map_bh(bh_result, iyesde->i_sb, s);
		bh_result->b_size = n_secs << 9;
		goto ret_0;
	}
	if (!create) goto ret_0;
	if (iblock<<9 != hpfs_i(iyesde)->mmu_private) {
		BUG();
		r = -EIO;
		goto ret_r;
	}
	if ((s = hpfs_add_sector_to_btree(iyesde->i_sb, iyesde->i_iyes, 1, iyesde->i_blocks - 1)) == -1) {
		hpfs_truncate_btree(iyesde->i_sb, iyesde->i_iyes, 1, iyesde->i_blocks - 1);
		r = -ENOSPC;
		goto ret_r;
	}
	iyesde->i_blocks++;
	hpfs_i(iyesde)->mmu_private += 512;
	set_buffer_new(bh_result);
	map_bh(bh_result, iyesde->i_sb, hpfs_search_hotfix_map(iyesde->i_sb, s));
	ret_0:
	r = 0;
	ret_r:
	hpfs_unlock(iyesde->i_sb);
	return r;
}

static int hpfs_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, hpfs_get_block);
}

static int hpfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, hpfs_get_block, wbc);
}

static int hpfs_readpages(struct file *file, struct address_space *mapping,
			  struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, hpfs_get_block);
}

static int hpfs_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, hpfs_get_block);
}

static void hpfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct iyesde *iyesde = mapping->host;

	hpfs_lock(iyesde->i_sb);

	if (to > iyesde->i_size) {
		truncate_pagecache(iyesde, iyesde->i_size);
		hpfs_truncate(iyesde);
	}

	hpfs_unlock(iyesde->i_sb);
}

static int hpfs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	int ret;

	*pagep = NULL;
	ret = cont_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
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
	struct iyesde *iyesde = mapping->host;
	int err;
	err = generic_write_end(file, mapping, pos, len, copied, pagep, fsdata);
	if (err < len)
		hpfs_write_failed(mapping, pos + len);
	if (!(err < 0)) {
		/* make sure we write it on close, if yest earlier */
		hpfs_lock(iyesde->i_sb);
		hpfs_i(iyesde)->i_dirty = 1;
		hpfs_unlock(iyesde->i_sb);
	}
	return err;
}

static sector_t _hpfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, hpfs_get_block);
}

static int hpfs_fiemap(struct iyesde *iyesde, struct fiemap_extent_info *fieinfo, u64 start, u64 len)
{
	return generic_block_fiemap(iyesde, fieinfo, start, len, hpfs_get_block);
}

const struct address_space_operations hpfs_aops = {
	.readpage = hpfs_readpage,
	.writepage = hpfs_writepage,
	.readpages = hpfs_readpages,
	.writepages = hpfs_writepages,
	.write_begin = hpfs_write_begin,
	.write_end = hpfs_write_end,
	.bmap = _hpfs_bmap
};

const struct file_operations hpfs_file_ops =
{
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.release	= hpfs_file_release,
	.fsync		= hpfs_file_fsync,
	.splice_read	= generic_file_splice_read,
	.unlocked_ioctl	= hpfs_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};

const struct iyesde_operations hpfs_file_iops =
{
	.setattr	= hpfs_setattr,
	.fiemap		= hpfs_fiemap,
};
