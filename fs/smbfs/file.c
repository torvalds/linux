/*
 *  file.c
 *
 *  Copyright (C) 1995, 1996, 1997 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 *  Please add a note about your changes to smbfs in the ChangeLog file.
 */

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/net.h>
#include <linux/aio.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/smbno.h>
#include <linux/smb_fs.h>

#include "smb_debug.h"
#include "proto.h"

static int
smb_fsync(struct file *file, struct dentry * dentry, int datasync)
{
	struct smb_sb_info *server = server_from_dentry(dentry);
	int result;

	VERBOSE("sync file %s/%s\n", DENTRY_PATH(dentry));

	/*
	 * The VFS will writepage() all dirty pages for us, but we
	 * should send a SMBflush to the server, letting it know that
	 * we want things synchronized with actual storage.
	 *
	 * Note: this function requires all pages to have been written already
	 *       (should be ok with writepage_sync)
	 */
	result = smb_proc_flush(server, SMB_I(dentry->d_inode)->fileid);
	return result;
}

/*
 * Read a page synchronously.
 */
static int
smb_readpage_sync(struct dentry *dentry, struct page *page)
{
	char *buffer = kmap(page);
	loff_t offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	struct smb_sb_info *server = server_from_dentry(dentry);
	unsigned int rsize = smb_get_rsize(server);
	int count = PAGE_SIZE;
	int result;

	VERBOSE("file %s/%s, count=%d@%Ld, rsize=%d\n",
		DENTRY_PATH(dentry), count, offset, rsize);

	result = smb_open(dentry, SMB_O_RDONLY);
	if (result < 0)
		goto io_error;

	do {
		if (count < rsize)
			rsize = count;

		result = server->ops->read(dentry->d_inode,offset,rsize,buffer);
		if (result < 0)
			goto io_error;

		count -= result;
		offset += result;
		buffer += result;
		dentry->d_inode->i_atime =
			current_fs_time(dentry->d_inode->i_sb);
		if (result < rsize)
			break;
	} while (count);

	memset(buffer, 0, count);
	flush_dcache_page(page);
	SetPageUptodate(page);
	result = 0;

io_error:
	kunmap(page);
	unlock_page(page);
	return result;
}

/*
 * We are called with the page locked and we unlock it when done.
 */
static int
smb_readpage(struct file *file, struct page *page)
{
	int		error;
	struct dentry  *dentry = file->f_path.dentry;

	page_cache_get(page);
	error = smb_readpage_sync(dentry, page);
	page_cache_release(page);
	return error;
}

/*
 * Write a page synchronously.
 * Offset is the data offset within the page.
 */
static int
smb_writepage_sync(struct inode *inode, struct page *page,
		   unsigned long pageoffset, unsigned int count)
{
	loff_t offset;
	char *buffer = kmap(page) + pageoffset;
	struct smb_sb_info *server = server_from_inode(inode);
	unsigned int wsize = smb_get_wsize(server);
	int ret = 0;

	offset = ((loff_t)page->index << PAGE_CACHE_SHIFT) + pageoffset;
	VERBOSE("file ino=%ld, fileid=%d, count=%d@%Ld, wsize=%d\n",
		inode->i_ino, SMB_I(inode)->fileid, count, offset, wsize);

	do {
		int write_ret;

		if (count < wsize)
			wsize = count;

		write_ret = server->ops->write(inode, offset, wsize, buffer);
		if (write_ret < 0) {
			PARANOIA("failed write, wsize=%d, write_ret=%d\n",
				 wsize, write_ret);
			ret = write_ret;
			break;
		}
		/* N.B. what if result < wsize?? */
#ifdef SMBFS_PARANOIA
		if (write_ret < wsize)
			PARANOIA("short write, wsize=%d, write_ret=%d\n",
				 wsize, write_ret);
#endif
		buffer += wsize;
		offset += wsize;
		count -= wsize;
		/*
		 * Update the inode now rather than waiting for a refresh.
		 */
		inode->i_mtime = inode->i_atime = current_fs_time(inode->i_sb);
		SMB_I(inode)->flags |= SMB_F_LOCALWRITE;
		if (offset > inode->i_size)
			inode->i_size = offset;
	} while (count);

	kunmap(page);
	return ret;
}

/*
 * Write a page to the server. This will be used for NFS swapping only
 * (for now), and we currently do this synchronously only.
 *
 * We are called with the page locked and we unlock it when done.
 */
static int
smb_writepage(struct page *page, struct writeback_control *wbc)
{
	struct address_space *mapping = page->mapping;
	struct inode *inode;
	unsigned long end_index;
	unsigned offset = PAGE_CACHE_SIZE;
	int err;

	BUG_ON(!mapping);
	inode = mapping->host;
	BUG_ON(!inode);

	end_index = inode->i_size >> PAGE_CACHE_SHIFT;

	/* easy case */
	if (page->index < end_index)
		goto do_it;
	/* things got complicated... */
	offset = inode->i_size & (PAGE_CACHE_SIZE-1);
	/* OK, are we completely out? */
	if (page->index >= end_index+1 || !offset)
		return 0; /* truncated - don't care */
do_it:
	page_cache_get(page);
	err = smb_writepage_sync(inode, page, 0, offset);
	SetPageUptodate(page);
	unlock_page(page);
	page_cache_release(page);
	return err;
}

static int
smb_updatepage(struct file *file, struct page *page, unsigned long offset,
	       unsigned int count)
{
	struct dentry *dentry = file->f_path.dentry;

	DEBUG1("(%s/%s %d@%lld)\n", DENTRY_PATH(dentry), count,
		((unsigned long long)page->index << PAGE_CACHE_SHIFT) + offset);

	return smb_writepage_sync(dentry->d_inode, page, offset, count);
}

static ssize_t
smb_file_aio_read(struct kiocb *iocb, const struct iovec *iov,
			unsigned long nr_segs, loff_t pos)
{
	struct file * file = iocb->ki_filp;
	struct dentry * dentry = file->f_path.dentry;
	ssize_t	status;

	VERBOSE("file %s/%s, count=%lu@%lu\n", DENTRY_PATH(dentry),
		(unsigned long) iocb->ki_left, (unsigned long) pos);

	status = smb_revalidate_inode(dentry);
	if (status) {
		PARANOIA("%s/%s validation failed, error=%Zd\n",
			 DENTRY_PATH(dentry), status);
		goto out;
	}

	VERBOSE("before read, size=%ld, flags=%x, atime=%ld\n",
		(long)dentry->d_inode->i_size,
		dentry->d_inode->i_flags, dentry->d_inode->i_atime);

	status = generic_file_aio_read(iocb, iov, nr_segs, pos);
out:
	return status;
}

static int
smb_file_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct dentry * dentry = file->f_path.dentry;
	int	status;

	VERBOSE("file %s/%s, address %lu - %lu\n",
		DENTRY_PATH(dentry), vma->vm_start, vma->vm_end);

	status = smb_revalidate_inode(dentry);
	if (status) {
		PARANOIA("%s/%s validation failed, error=%d\n",
			 DENTRY_PATH(dentry), status);
		goto out;
	}
	status = generic_file_mmap(file, vma);
out:
	return status;
}

static ssize_t
smb_file_splice_read(struct file *file, loff_t *ppos,
		     struct pipe_inode_info *pipe, size_t count,
		     unsigned int flags)
{
	struct dentry *dentry = file->f_path.dentry;
	ssize_t status;

	VERBOSE("file %s/%s, pos=%Ld, count=%d\n",
		DENTRY_PATH(dentry), *ppos, count);

	status = smb_revalidate_inode(dentry);
	if (status) {
		PARANOIA("%s/%s validation failed, error=%Zd\n",
			 DENTRY_PATH(dentry), status);
		goto out;
	}
	status = generic_file_splice_read(file, ppos, pipe, count, flags);
out:
	return status;
}

/*
 * This does the "real" work of the write. The generic routine has
 * allocated the page, locked it, done all the page alignment stuff
 * calculations etc. Now we should just copy the data from user
 * space and write it back to the real medium..
 *
 * If the writer ends up delaying the write, the writer needs to
 * increment the page use counts until he is done with the page.
 */
static int smb_prepare_write(struct file *file, struct page *page, 
			     unsigned offset, unsigned to)
{
	return 0;
}

static int smb_commit_write(struct file *file, struct page *page,
			    unsigned offset, unsigned to)
{
	int status;

	status = -EFAULT;
	lock_kernel();
	status = smb_updatepage(file, page, offset, to-offset);
	unlock_kernel();
	return status;
}

const struct address_space_operations smb_file_aops = {
	.readpage = smb_readpage,
	.writepage = smb_writepage,
	.prepare_write = smb_prepare_write,
	.commit_write = smb_commit_write
};

/* 
 * Write to a file (through the page cache).
 */
static ssize_t
smb_file_aio_write(struct kiocb *iocb, const struct iovec *iov,
			       unsigned long nr_segs, loff_t pos)
{
	struct file * file = iocb->ki_filp;
	struct dentry * dentry = file->f_path.dentry;
	ssize_t	result;

	VERBOSE("file %s/%s, count=%lu@%lu\n",
		DENTRY_PATH(dentry),
		(unsigned long) iocb->ki_left, (unsigned long) pos);

	result = smb_revalidate_inode(dentry);
	if (result) {
		PARANOIA("%s/%s validation failed, error=%Zd\n",
			 DENTRY_PATH(dentry), result);
		goto out;
	}

	result = smb_open(dentry, SMB_O_WRONLY);
	if (result)
		goto out;

	if (iocb->ki_left > 0) {
		result = generic_file_aio_write(iocb, iov, nr_segs, pos);
		VERBOSE("pos=%ld, size=%ld, mtime=%ld, atime=%ld\n",
			(long) file->f_pos, (long) dentry->d_inode->i_size,
			dentry->d_inode->i_mtime, dentry->d_inode->i_atime);
	}
out:
	return result;
}

static int
smb_file_open(struct inode *inode, struct file * file)
{
	int result;
	struct dentry *dentry = file->f_path.dentry;
	int smb_mode = (file->f_mode & O_ACCMODE) - 1;

	lock_kernel();
	result = smb_open(dentry, smb_mode);
	if (result)
		goto out;
	SMB_I(inode)->openers++;
out:
	unlock_kernel();
	return result;
}

static int
smb_file_release(struct inode *inode, struct file * file)
{
	lock_kernel();
	if (!--SMB_I(inode)->openers) {
		/* We must flush any dirty pages now as we won't be able to
		   write anything after close. mmap can trigger this.
		   "openers" should perhaps include mmap'ers ... */
		filemap_write_and_wait(inode->i_mapping);
		smb_close(inode);
	}
	unlock_kernel();
	return 0;
}

/*
 * Check whether the required access is compatible with
 * an inode's permission. SMB doesn't recognize superuser
 * privileges, so we need our own check for this.
 */
static int
smb_file_permission(struct inode *inode, int mask, struct nameidata *nd)
{
	int mode = inode->i_mode;
	int error = 0;

	VERBOSE("mode=%x, mask=%x\n", mode, mask);

	/* Look at user permissions */
	mode >>= 6;
	if ((mode & 7 & mask) != mask)
		error = -EACCES;
	return error;
}

const struct file_operations smb_file_operations =
{
	.llseek		= remote_llseek,
	.read		= do_sync_read,
	.aio_read	= smb_file_aio_read,
	.write		= do_sync_write,
	.aio_write	= smb_file_aio_write,
	.ioctl		= smb_ioctl,
	.mmap		= smb_file_mmap,
	.open		= smb_file_open,
	.release	= smb_file_release,
	.fsync		= smb_fsync,
	.splice_read	= smb_file_splice_read,
};

const struct inode_operations smb_file_inode_operations =
{
	.permission	= smb_file_permission,
	.getattr	= smb_getattr,
	.setattr	= smb_notify_change,
};
