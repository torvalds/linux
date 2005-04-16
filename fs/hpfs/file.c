/*
 *  linux/fs/hpfs/file.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  file VFS functions
 */

#include "hpfs_fn.h"

#define BLOCKS(size) (((size) + 511) >> 9)

static int hpfs_file_release(struct inode *inode, struct file *file)
{
	lock_kernel();
	hpfs_write_if_changed(inode);
	unlock_kernel();
	return 0;
}

int hpfs_file_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	/*return file_fsync(file, dentry);*/
	return 0; /* Don't fsync :-) */
}

/*
 * generic_file_read often calls bmap with non-existing sector,
 * so we must ignore such errors.
 */

static secno hpfs_bmap(struct inode *inode, unsigned file_secno)
{
	struct hpfs_inode_info *hpfs_inode = hpfs_i(inode);
	unsigned n, disk_secno;
	struct fnode *fnode;
	struct buffer_head *bh;
	if (BLOCKS(hpfs_i(inode)->mmu_private) <= file_secno) return 0;
	n = file_secno - hpfs_inode->i_file_sec;
	if (n < hpfs_inode->i_n_secs) return hpfs_inode->i_disk_sec + n;
	if (!(fnode = hpfs_map_fnode(inode->i_sb, inode->i_ino, &bh))) return 0;
	disk_secno = hpfs_bplus_lookup(inode->i_sb, inode, &fnode->btree, file_secno, bh);
	if (disk_secno == -1) return 0;
	if (hpfs_chk_sectors(inode->i_sb, disk_secno, 1, "bmap")) return 0;
	return disk_secno;
}

static void hpfs_truncate(struct inode *i)
{
	if (IS_IMMUTABLE(i)) return /*-EPERM*/;
	lock_kernel();
	hpfs_i(i)->i_n_secs = 0;
	i->i_blocks = 1 + ((i->i_size + 511) >> 9);
	hpfs_i(i)->mmu_private = i->i_size;
	hpfs_truncate_btree(i->i_sb, i->i_ino, 1, ((i->i_size + 511) >> 9));
	hpfs_write_inode(i);
	hpfs_i(i)->i_n_secs = 0;
	unlock_kernel();
}

static int hpfs_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create)
{
	secno s;
	s = hpfs_bmap(inode, iblock);
	if (s) {
		map_bh(bh_result, inode->i_sb, s);
		return 0;
	}
	if (!create) return 0;
	if (iblock<<9 != hpfs_i(inode)->mmu_private) {
		BUG();
		return -EIO;
	}
	if ((s = hpfs_add_sector_to_btree(inode->i_sb, inode->i_ino, 1, inode->i_blocks - 1)) == -1) {
		hpfs_truncate_btree(inode->i_sb, inode->i_ino, 1, inode->i_blocks - 1);
		return -ENOSPC;
	}
	inode->i_blocks++;
	hpfs_i(inode)->mmu_private += 512;
	set_buffer_new(bh_result);
	map_bh(bh_result, inode->i_sb, s);
	return 0;
}

static int hpfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page,hpfs_get_block, wbc);
}
static int hpfs_readpage(struct file *file, struct page *page)
{
	return block_read_full_page(page,hpfs_get_block);
}
static int hpfs_prepare_write(struct file *file, struct page *page, unsigned from, unsigned to)
{
	return cont_prepare_write(page,from,to,hpfs_get_block,
		&hpfs_i(page->mapping->host)->mmu_private);
}
static sector_t _hpfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping,block,hpfs_get_block);
}
struct address_space_operations hpfs_aops = {
	.readpage = hpfs_readpage,
	.writepage = hpfs_writepage,
	.sync_page = block_sync_page,
	.prepare_write = hpfs_prepare_write,
	.commit_write = generic_commit_write,
	.bmap = _hpfs_bmap
};

static ssize_t hpfs_file_write(struct file *file, const char __user *buf,
			size_t count, loff_t *ppos)
{
	ssize_t retval;

	retval = generic_file_write(file, buf, count, ppos);
	if (retval > 0) {
		struct inode *inode = file->f_dentry->d_inode;
		inode->i_mtime = CURRENT_TIME_SEC;
		hpfs_i(inode)->i_dirty = 1;
	}
	return retval;
}

struct file_operations hpfs_file_ops =
{
	.llseek		= generic_file_llseek,
	.read		= generic_file_read,
	.write		= hpfs_file_write,
	.mmap		= generic_file_mmap,
	.release	= hpfs_file_release,
	.fsync		= hpfs_file_fsync,
	.sendfile	= generic_file_sendfile,
};

struct inode_operations hpfs_file_iops =
{
	.truncate	= hpfs_truncate,
	.setattr	= hpfs_notify_change,
};
