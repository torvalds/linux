/*
 *  file.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Modified 1997 Peter Waltenberg, Bill Hawes, David Woodhouse for 2.1 dcache
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/uaccess.h>

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>

#include "ncp_fs.h"

static int ncp_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	return filemap_write_and_wait_range(file->f_mapping, start, end);
}

/*
 * Open a file with the specified read/write mode.
 */
int ncp_make_open(struct inode *inode, int right)
{
	int error;
	int access;

	error = -EINVAL;
	if (!inode) {
		pr_err("%s: got NULL inode\n", __func__);
		goto out;
	}

	ncp_dbg(1, "opened=%d, volume # %u, dir entry # %u\n",
		atomic_read(&NCP_FINFO(inode)->opened), 
		NCP_FINFO(inode)->volNumber, 
		NCP_FINFO(inode)->dirEntNum);
	error = -EACCES;
	mutex_lock(&NCP_FINFO(inode)->open_mutex);
	if (!atomic_read(&NCP_FINFO(inode)->opened)) {
		struct ncp_entry_info finfo;
		int result;

		/* tries max. rights */
		finfo.access = O_RDWR;
		result = ncp_open_create_file_or_subdir(NCP_SERVER(inode),
					inode, NULL, OC_MODE_OPEN,
					0, AR_READ | AR_WRITE, &finfo);
		if (!result)
			goto update;
		/* RDWR did not succeeded, try readonly or writeonly as requested */
		switch (right) {
			case O_RDONLY:
				finfo.access = O_RDONLY;
				result = ncp_open_create_file_or_subdir(NCP_SERVER(inode),
					inode, NULL, OC_MODE_OPEN,
					0, AR_READ, &finfo);
				break;
			case O_WRONLY:
				finfo.access = O_WRONLY;
				result = ncp_open_create_file_or_subdir(NCP_SERVER(inode),
					inode, NULL, OC_MODE_OPEN,
					0, AR_WRITE, &finfo);
				break;
		}
		if (result) {
			ncp_vdbg("failed, result=%d\n", result);
			goto out_unlock;
		}
		/*
		 * Update the inode information.
		 */
	update:
		ncp_update_inode(inode, &finfo);
		atomic_set(&NCP_FINFO(inode)->opened, 1);
	}

	access = NCP_FINFO(inode)->access;
	ncp_vdbg("file open, access=%x\n", access);
	if (access == right || access == O_RDWR) {
		atomic_inc(&NCP_FINFO(inode)->opened);
		error = 0;
	}

out_unlock:
	mutex_unlock(&NCP_FINFO(inode)->open_mutex);
out:
	return error;
}

static ssize_t
ncp_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	size_t already_read = 0;
	off_t pos = iocb->ki_pos;
	size_t bufsize;
	int error;
	void *freepage;
	size_t freelen;

	ncp_dbg(1, "enter %pD2\n", file);

	if (!iov_iter_count(to))
		return 0;
	if (pos > inode->i_sb->s_maxbytes)
		return 0;
	iov_iter_truncate(to, inode->i_sb->s_maxbytes - pos);

	error = ncp_make_open(inode, O_RDONLY);
	if (error) {
		ncp_dbg(1, "open failed, error=%d\n", error);
		return error;
	}

	bufsize = NCP_SERVER(inode)->buffer_size;

	error = -EIO;
	freelen = ncp_read_bounce_size(bufsize);
	freepage = vmalloc(freelen);
	if (!freepage)
		goto outrel;
	error = 0;
	/* First read in as much as possible for each bufsize. */
	while (iov_iter_count(to)) {
		int read_this_time;
		size_t to_read = min_t(size_t,
				     bufsize - (pos % bufsize),
				     iov_iter_count(to));

		error = ncp_read_bounce(NCP_SERVER(inode),
			 	NCP_FINFO(inode)->file_handle,
				pos, to_read, to, &read_this_time, 
				freepage, freelen);
		if (error) {
			error = -EIO;	/* NW errno -> Linux errno */
			break;
		}
		pos += read_this_time;
		already_read += read_this_time;

		if (read_this_time != to_read)
			break;
	}
	vfree(freepage);

	iocb->ki_pos = pos;

	file_accessed(file);

	ncp_dbg(1, "exit %pD2\n", file);
outrel:
	ncp_inode_close(inode);		
	return already_read ? already_read : error;
}

static ssize_t
ncp_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	size_t already_written = 0;
	size_t bufsize;
	int errno;
	void *bouncebuffer;
	off_t pos;

	ncp_dbg(1, "enter %pD2\n", file);
	errno = generic_write_checks(iocb, from);
	if (errno <= 0)
		return errno;

	errno = ncp_make_open(inode, O_WRONLY);
	if (errno) {
		ncp_dbg(1, "open failed, error=%d\n", errno);
		return errno;
	}
	bufsize = NCP_SERVER(inode)->buffer_size;

	errno = file_update_time(file);
	if (errno)
		goto outrel;

	bouncebuffer = vmalloc(bufsize);
	if (!bouncebuffer) {
		errno = -EIO;	/* -ENOMEM */
		goto outrel;
	}
	pos = iocb->ki_pos;
	while (iov_iter_count(from)) {
		int written_this_time;
		size_t to_write = min_t(size_t,
				      bufsize - (pos % bufsize),
				      iov_iter_count(from));

		if (copy_from_iter(bouncebuffer, to_write, from) != to_write) {
			errno = -EFAULT;
			break;
		}
		if (ncp_write_kernel(NCP_SERVER(inode), 
		    NCP_FINFO(inode)->file_handle,
		    pos, to_write, bouncebuffer, &written_this_time) != 0) {
			errno = -EIO;
			break;
		}
		pos += written_this_time;
		already_written += written_this_time;

		if (written_this_time != to_write)
			break;
	}
	vfree(bouncebuffer);

	iocb->ki_pos = pos;

	if (pos > i_size_read(inode)) {
		mutex_lock(&inode->i_mutex);
		if (pos > i_size_read(inode))
			i_size_write(inode, pos);
		mutex_unlock(&inode->i_mutex);
	}
	ncp_dbg(1, "exit %pD2\n", file);
outrel:
	ncp_inode_close(inode);		
	return already_written ? already_written : errno;
}

static int ncp_release(struct inode *inode, struct file *file) {
	if (ncp_make_closed(inode)) {
		ncp_dbg(1, "failed to close\n");
	}
	return 0;
}

const struct file_operations ncp_file_operations =
{
	.llseek		= generic_file_llseek,
	.read_iter	= ncp_file_read_iter,
	.write_iter	= ncp_file_write_iter,
	.unlocked_ioctl	= ncp_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ncp_compat_ioctl,
#endif
	.mmap		= ncp_mmap,
	.release	= ncp_release,
	.fsync		= ncp_fsync,
};

const struct inode_operations ncp_file_inode_operations =
{
	.setattr	= ncp_notify_change,
};
