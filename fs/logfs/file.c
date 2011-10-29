/*
 * fs/logfs/file.c	- prepare_write, commit_write and friends
 *
 * As should be obvious for Linux kernel code, license is GPLv2
 *
 * Copyright (c) 2005-2008 Joern Engel <joern@logfs.org>
 */
#include "logfs.h"
#include <linux/sched.h>
#include <linux/writeback.h>

static int logfs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata)
{
	struct inode *inode = mapping->host;
	struct page *page;
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;

	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page)
		return -ENOMEM;
	*pagep = page;

	if ((len == PAGE_CACHE_SIZE) || PageUptodate(page))
		return 0;
	if ((pos & PAGE_CACHE_MASK) >= i_size_read(inode)) {
		unsigned start = pos & (PAGE_CACHE_SIZE - 1);
		unsigned end = start + len;

		/* Reading beyond i_size is simple: memset to zero */
		zero_user_segments(page, 0, start, end, PAGE_CACHE_SIZE);
		return 0;
	}
	return logfs_readpage_nolock(page);
}

static int logfs_write_end(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned copied, struct page *page,
		void *fsdata)
{
	struct inode *inode = mapping->host;
	pgoff_t index = page->index;
	unsigned start = pos & (PAGE_CACHE_SIZE - 1);
	unsigned end = start + copied;
	int ret = 0;

	BUG_ON(PAGE_CACHE_SIZE != inode->i_sb->s_blocksize);
	BUG_ON(page->index > I3_BLOCKS);

	if (copied < len) {
		/*
		 * Short write of a non-initialized paged.  Just tell userspace
		 * to retry the entire page.
		 */
		if (!PageUptodate(page)) {
			copied = 0;
			goto out;
		}
	}
	if (copied == 0)
		goto out; /* FIXME: do we need to update inode? */

	if (i_size_read(inode) < (index << PAGE_CACHE_SHIFT) + end) {
		i_size_write(inode, (index << PAGE_CACHE_SHIFT) + end);
		mark_inode_dirty_sync(inode);
	}

	SetPageUptodate(page);
	if (!PageDirty(page)) {
		if (!get_page_reserve(inode, page))
			__set_page_dirty_nobuffers(page);
		else
			ret = logfs_write_buf(inode, page, WF_LOCK);
	}
out:
	unlock_page(page);
	page_cache_release(page);
	return ret ? ret : copied;
}

int logfs_readpage(struct file *file, struct page *page)
{
	int ret;

	ret = logfs_readpage_nolock(page);
	unlock_page(page);
	return ret;
}

/* Clear the page's dirty flag in the radix tree. */
/* TODO: mucking with PageWriteback is silly.  Add a generic function to clear
 * the dirty bit from the radix tree for filesystems that don't have to wait
 * for page writeback to finish (i.e. any compressing filesystem).
 */
static void clear_radix_tree_dirty(struct page *page)
{
	BUG_ON(PagePrivate(page) || page->private);
	set_page_writeback(page);
	end_page_writeback(page);
}

static int __logfs_writepage(struct page *page)
{
	struct inode *inode = page->mapping->host;
	int err;

	err = logfs_write_buf(inode, page, WF_LOCK);
	if (err)
		set_page_dirty(page);
	else
		clear_radix_tree_dirty(page);
	unlock_page(page);
	return err;
}

static int logfs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	loff_t i_size = i_size_read(inode);
	pgoff_t end_index = i_size >> PAGE_CACHE_SHIFT;
	unsigned offset;
	u64 bix;
	level_t level;

	log_file("logfs_writepage(%lx, %lx, %p)\n", inode->i_ino, page->index,
			page);

	logfs_unpack_index(page->index, &bix, &level);

	/* Indirect blocks are never truncated */
	if (level != 0)
		return __logfs_writepage(page);

	/*
	 * TODO: everything below is a near-verbatim copy of nobh_writepage().
	 * The relevant bits should be factored out after logfs is merged.
	 */

	/* Is the page fully inside i_size? */
	if (bix < end_index)
		return __logfs_writepage(page);

	 /* Is the page fully outside i_size? (truncate in progress) */
	offset = i_size & (PAGE_CACHE_SIZE-1);
	if (bix > end_index || offset == 0) {
		unlock_page(page);
		return 0; /* don't care */
	}

	/*
	 * The page straddles i_size.  It must be zeroed out on each and every
	 * writepage invokation because it may be mmapped.  "A file is mapped
	 * in multiples of the page size.  For a file that is not a multiple of
	 * the  page size, the remaining memory is zeroed when mapped, and
	 * writes to that region are not written out to the file."
	 */
	zero_user_segment(page, offset, PAGE_CACHE_SIZE);
	return __logfs_writepage(page);
}

static void logfs_invalidatepage(struct page *page, unsigned long offset)
{
	struct logfs_block *block = logfs_block(page);

	if (block->reserved_bytes) {
		struct super_block *sb = page->mapping->host->i_sb;
		struct logfs_super *super = logfs_super(sb);

		super->s_dirty_pages -= block->reserved_bytes;
		block->ops->free_block(sb, block);
		BUG_ON(bitmap_weight(block->alias_map, LOGFS_BLOCK_FACTOR));
	} else
		move_page_to_btree(page);
	BUG_ON(PagePrivate(page) || page->private);
}

static int logfs_releasepage(struct page *page, gfp_t only_xfs_uses_this)
{
	return 0; /* None of these are easy to release */
}


long logfs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct logfs_inode *li = logfs_inode(inode);
	unsigned int oldflags, flags;
	int err;

	switch (cmd) {
	case FS_IOC_GETFLAGS:
		flags = li->li_flags & LOGFS_FL_USER_VISIBLE;
		return put_user(flags, (int __user *)arg);
	case FS_IOC_SETFLAGS:
		if (IS_RDONLY(inode))
			return -EROFS;

		if (!inode_owner_or_capable(inode))
			return -EACCES;

		err = get_user(flags, (int __user *)arg);
		if (err)
			return err;

		mutex_lock(&inode->i_mutex);
		oldflags = li->li_flags;
		flags &= LOGFS_FL_USER_MODIFIABLE;
		flags |= oldflags & ~LOGFS_FL_USER_MODIFIABLE;
		li->li_flags = flags;
		mutex_unlock(&inode->i_mutex);

		inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty_sync(inode);
		return 0;

	default:
		return -ENOTTY;
	}
}

int logfs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct super_block *sb = file->f_mapping->host->i_sb;
	struct inode *inode = file->f_mapping->host;
	int ret;

	ret = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (ret)
		return ret;

	mutex_lock(&inode->i_mutex);
	logfs_write_anchor(sb);
	mutex_unlock(&inode->i_mutex);

	return 0;
}

static int logfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int err = 0;

	err = inode_change_ok(inode, attr);
	if (err)
		return err;

	if (attr->ia_valid & ATTR_SIZE) {
		err = logfs_truncate(inode, attr->ia_size);
		if (err)
			return err;
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

const struct inode_operations logfs_reg_iops = {
	.setattr	= logfs_setattr,
};

const struct file_operations logfs_reg_fops = {
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.fsync		= logfs_fsync,
	.unlocked_ioctl	= logfs_ioctl,
	.llseek		= generic_file_llseek,
	.mmap		= generic_file_readonly_mmap,
	.open		= generic_file_open,
	.read		= do_sync_read,
	.write		= do_sync_write,
};

const struct address_space_operations logfs_reg_aops = {
	.invalidatepage	= logfs_invalidatepage,
	.readpage	= logfs_readpage,
	.releasepage	= logfs_releasepage,
	.set_page_dirty	= __set_page_dirty_nobuffers,
	.writepage	= logfs_writepage,
	.writepages	= generic_writepages,
	.write_begin	= logfs_write_begin,
	.write_end	= logfs_write_end,
};
