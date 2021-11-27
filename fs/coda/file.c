// SPDX-License-Identifier: GPL-2.0
/*
 * File operations for Coda.
 * Original version: (C) 1996 Peter Braam 
 * Rewritten for Linux 2.1: (C) 1997 Carnegie Mellon University
 *
 * Carnegie Mellon encourages users of this code to contribute improvements
 * to the Coda project. Contact Peter Braam <coda@cs.cmu.edu>.
 */

#include <linux/refcount.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/uio.h>

#include <linux/coda.h>
#include "coda_psdev.h"
#include "coda_linux.h"
#include "coda_int.h"

struct coda_vm_ops {
	refcount_t refcnt;
	struct file *coda_file;
	const struct vm_operations_struct *host_vm_ops;
	struct vm_operations_struct vm_ops;
};

static ssize_t
coda_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *coda_file = iocb->ki_filp;
	struct inode *coda_inode = file_inode(coda_file);
	struct coda_file_info *cfi = coda_ftoc(coda_file);
	loff_t ki_pos = iocb->ki_pos;
	size_t count = iov_iter_count(to);
	ssize_t ret;

	ret = venus_access_intent(coda_inode->i_sb, coda_i2f(coda_inode),
				  &cfi->cfi_access_intent,
				  count, ki_pos, CODA_ACCESS_TYPE_READ);
	if (ret)
		goto finish_read;

	ret = vfs_iter_read(cfi->cfi_container, to, &iocb->ki_pos, 0);

finish_read:
	venus_access_intent(coda_inode->i_sb, coda_i2f(coda_inode),
			    &cfi->cfi_access_intent,
			    count, ki_pos, CODA_ACCESS_TYPE_READ_FINISH);
	return ret;
}

static ssize_t
coda_file_write_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *coda_file = iocb->ki_filp;
	struct inode *coda_inode = file_inode(coda_file);
	struct coda_file_info *cfi = coda_ftoc(coda_file);
	struct file *host_file = cfi->cfi_container;
	loff_t ki_pos = iocb->ki_pos;
	size_t count = iov_iter_count(to);
	ssize_t ret;

	ret = venus_access_intent(coda_inode->i_sb, coda_i2f(coda_inode),
				  &cfi->cfi_access_intent,
				  count, ki_pos, CODA_ACCESS_TYPE_WRITE);
	if (ret)
		goto finish_write;

	file_start_write(host_file);
	inode_lock(coda_inode);
	ret = vfs_iter_write(cfi->cfi_container, to, &iocb->ki_pos, 0);
	coda_inode->i_size = file_inode(host_file)->i_size;
	coda_inode->i_blocks = (coda_inode->i_size + 511) >> 9;
	coda_inode->i_mtime = coda_inode->i_ctime = current_time(coda_inode);
	inode_unlock(coda_inode);
	file_end_write(host_file);

finish_write:
	venus_access_intent(coda_inode->i_sb, coda_i2f(coda_inode),
			    &cfi->cfi_access_intent,
			    count, ki_pos, CODA_ACCESS_TYPE_WRITE_FINISH);
	return ret;
}

static void
coda_vm_open(struct vm_area_struct *vma)
{
	struct coda_vm_ops *cvm_ops =
		container_of(vma->vm_ops, struct coda_vm_ops, vm_ops);

	refcount_inc(&cvm_ops->refcnt);

	if (cvm_ops->host_vm_ops && cvm_ops->host_vm_ops->open)
		cvm_ops->host_vm_ops->open(vma);
}

static void
coda_vm_close(struct vm_area_struct *vma)
{
	struct coda_vm_ops *cvm_ops =
		container_of(vma->vm_ops, struct coda_vm_ops, vm_ops);

	if (cvm_ops->host_vm_ops && cvm_ops->host_vm_ops->close)
		cvm_ops->host_vm_ops->close(vma);

	if (refcount_dec_and_test(&cvm_ops->refcnt)) {
		vma->vm_ops = cvm_ops->host_vm_ops;
		fput(cvm_ops->coda_file);
		kfree(cvm_ops);
	}
}

static int
coda_file_mmap(struct file *coda_file, struct vm_area_struct *vma)
{
	struct inode *coda_inode = file_inode(coda_file);
	struct coda_file_info *cfi = coda_ftoc(coda_file);
	struct file *host_file = cfi->cfi_container;
	struct inode *host_inode = file_inode(host_file);
	struct coda_inode_info *cii;
	struct coda_vm_ops *cvm_ops;
	loff_t ppos;
	size_t count;
	int ret;

	if (!host_file->f_op->mmap)
		return -ENODEV;

	if (WARN_ON(coda_file != vma->vm_file))
		return -EIO;

	count = vma->vm_end - vma->vm_start;
	ppos = vma->vm_pgoff * PAGE_SIZE;

	ret = venus_access_intent(coda_inode->i_sb, coda_i2f(coda_inode),
				  &cfi->cfi_access_intent,
				  count, ppos, CODA_ACCESS_TYPE_MMAP);
	if (ret)
		return ret;

	cvm_ops = kmalloc(sizeof(struct coda_vm_ops), GFP_KERNEL);
	if (!cvm_ops)
		return -ENOMEM;

	cii = ITOC(coda_inode);
	spin_lock(&cii->c_lock);
	coda_file->f_mapping = host_file->f_mapping;
	if (coda_inode->i_mapping == &coda_inode->i_data)
		coda_inode->i_mapping = host_inode->i_mapping;

	/* only allow additional mmaps as long as userspace isn't changing
	 * the container file on us! */
	else if (coda_inode->i_mapping != host_inode->i_mapping) {
		spin_unlock(&cii->c_lock);
		kfree(cvm_ops);
		return -EBUSY;
	}

	/* keep track of how often the coda_inode/host_file has been mmapped */
	cii->c_mapcount++;
	cfi->cfi_mapcount++;
	spin_unlock(&cii->c_lock);

	vma->vm_file = get_file(host_file);
	ret = call_mmap(vma->vm_file, vma);

	if (ret) {
		/* if call_mmap fails, our caller will put host_file so we
		 * should drop the reference to the coda_file that we got.
		 */
		fput(coda_file);
		kfree(cvm_ops);
	} else {
		/* here we add redirects for the open/close vm_operations */
		cvm_ops->host_vm_ops = vma->vm_ops;
		if (vma->vm_ops)
			cvm_ops->vm_ops = *vma->vm_ops;

		cvm_ops->vm_ops.open = coda_vm_open;
		cvm_ops->vm_ops.close = coda_vm_close;
		cvm_ops->coda_file = coda_file;
		refcount_set(&cvm_ops->refcnt, 1);

		vma->vm_ops = &cvm_ops->vm_ops;
	}
	return ret;
}

int coda_open(struct inode *coda_inode, struct file *coda_file)
{
	struct file *host_file = NULL;
	int error;
	unsigned short flags = coda_file->f_flags & (~O_EXCL);
	unsigned short coda_flags = coda_flags_to_cflags(flags);
	struct coda_file_info *cfi;

	cfi = kmalloc(sizeof(struct coda_file_info), GFP_KERNEL);
	if (!cfi)
		return -ENOMEM;

	error = venus_open(coda_inode->i_sb, coda_i2f(coda_inode), coda_flags,
			   &host_file);
	if (!host_file)
		error = -EIO;

	if (error) {
		kfree(cfi);
		return error;
	}

	host_file->f_flags |= coda_file->f_flags & (O_APPEND | O_SYNC);

	cfi->cfi_magic = CODA_MAGIC;
	cfi->cfi_mapcount = 0;
	cfi->cfi_container = host_file;
	/* assume access intents are supported unless we hear otherwise */
	cfi->cfi_access_intent = true;

	BUG_ON(coda_file->private_data != NULL);
	coda_file->private_data = cfi;
	return 0;
}

int coda_release(struct inode *coda_inode, struct file *coda_file)
{
	unsigned short flags = (coda_file->f_flags) & (~O_EXCL);
	unsigned short coda_flags = coda_flags_to_cflags(flags);
	struct coda_file_info *cfi;
	struct coda_inode_info *cii;
	struct inode *host_inode;

	cfi = coda_ftoc(coda_file);

	venus_close(coda_inode->i_sb, coda_i2f(coda_inode),
			  coda_flags, coda_file->f_cred->fsuid);

	host_inode = file_inode(cfi->cfi_container);
	cii = ITOC(coda_inode);

	/* did we mmap this file? */
	spin_lock(&cii->c_lock);
	if (coda_inode->i_mapping == &host_inode->i_data) {
		cii->c_mapcount -= cfi->cfi_mapcount;
		if (!cii->c_mapcount)
			coda_inode->i_mapping = &coda_inode->i_data;
	}
	spin_unlock(&cii->c_lock);

	fput(cfi->cfi_container);
	kfree(coda_file->private_data);
	coda_file->private_data = NULL;

	/* VFS fput ignores the return value from file_operations->release, so
	 * there is no use returning an error here */
	return 0;
}

int coda_fsync(struct file *coda_file, loff_t start, loff_t end, int datasync)
{
	struct file *host_file;
	struct inode *coda_inode = file_inode(coda_file);
	struct coda_file_info *cfi;
	int err;

	if (!(S_ISREG(coda_inode->i_mode) || S_ISDIR(coda_inode->i_mode) ||
	      S_ISLNK(coda_inode->i_mode)))
		return -EINVAL;

	err = filemap_write_and_wait_range(coda_inode->i_mapping, start, end);
	if (err)
		return err;
	inode_lock(coda_inode);

	cfi = coda_ftoc(coda_file);
	host_file = cfi->cfi_container;

	err = vfs_fsync(host_file, datasync);
	if (!err && !datasync)
		err = venus_fsync(coda_inode->i_sb, coda_i2f(coda_inode));
	inode_unlock(coda_inode);

	return err;
}

const struct file_operations coda_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= coda_file_read_iter,
	.write_iter	= coda_file_write_iter,
	.mmap		= coda_file_mmap,
	.open		= coda_open,
	.release	= coda_release,
	.fsync		= coda_fsync,
	.splice_read	= generic_file_splice_read,
};
